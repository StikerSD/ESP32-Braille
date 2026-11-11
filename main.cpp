#include <Arduino.h>
#include <string.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define CHORD_TIMEOUT_MS 120       // time with everything released to close the chord (increase if it still fails)
#define CONFIRM_READS 3            // minimum count for a dot to be valid (~4 ms with good contact)
#define COUNT_CEILING 8            // count limit: sets how long the dot "survives" failures
#define STUCK_TIME_MS 3000         // warns on Serial if any dot stays pressed longer than this
#define USE_BOOT_BUTTON 1          // 1 = BOOT button as space / delete / clear
#define BOOT_PIN 0
#define LONG_PRESS_MS 700
#define VERY_LONG_PRESS_MS 2000

const int OUTPUT_PINS_147[4] = {32, 33, 34, 35};              // A, B, C, D
const int MASK_PINS[6]       = {13, 14, 27, 26, 25, 23};      // dots 1..6
const int NUM_DOTS = 6;

const int SDA_PIN = 21;
const int SCL_PIN = 22;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool hasDisplay = false;

// Braille alphabet: dots of each letter from 'a' to 'z'
const char *LETTER_DOTS[26] = {
  "1",     "12",    "14",    "145",   "15",    "124",   "1245",  "125",   "24",    "245",
  "13",    "123",   "134",   "1345",  "135",   "1234",  "12345", "1235",  "234",   "2345",
  "136",   "1236",  "2456",  "1346",  "13456", "1356"
};
byte table[26];   // bit mask of each letter (bit0 = dot 1 ... bit5 = dot 6)

const int MAX_LEN = 96;            // 6 lines of 16 characters
char text[MAX_LEN + 1];
int textLength = 0;
char lastLetter = ' ';

byte maskOf(const char *dots) {
  byte m = 0;
  for (; *dots; dots++) m |= (1 << (*dots - '1'));
  return m;
}

char letter(byte c) {
  for (int i = 0; i < 26; i++) {
    if (table[i] == c) return 'a' + i;
  }
  return '?';
}

byte readRaw() {   // 147 output (inverted BCD) in a single pass
  byte v = 0;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(OUTPUT_PINS_147[i]) == LOW) v |= (1 << i);
  }
  return v;
}

// Stable reading: only accepts when two consecutive readings match.
// Returns: 0 = no key, 1..6 = highest-numbered dot pressed
byte readPriority() {
  byte previous = readRaw();
  for (int t = 0; t < 4; t++) {
    delayMicroseconds(5);
    byte current = readRaw();
    if (current == previous) return current;
    previous = current;
  }
  return previous;
}

void clearMasks() {
  for (int i = 0; i < 6; i++) digitalWrite(MASK_PINS[i], LOW);
}

byte maskFailure = 0;   // dot that stayed active even when masked (0 = none)

// Scans the current chord. Returns the pressed dots (bit0 = dot 1).
byte scan() {
  byte cell = 0;
  clearMasks();
  delayMicroseconds(30);
  for (int n = 0; n < NUM_DOTS; n++) {
    byte v = readPriority();
    if (v == 0 || v > NUM_DOTS) break;          // nothing pressed (or invalid reading)
    byte bit = 1 << (v - 1);
    if (cell & bit) { maskFailure = v; break; }      // same dot again: the mask did not work
    cell |= bit;
    digitalWrite(MASK_PINS[v - 1], HIGH);       // masks this dot
    delayMicroseconds(30);
  }
  clearMasks();
  return cell;
}

// ---------------------------------------------------------------- screen

const int CELL_X[2] = {106, 119};  // braille cell columns (dots 1-3 | 4-5-6)
const int CELL_Y[3] = {6, 18, 30}; // cell rows
const int CELL_R = 4;

void draw(byte cell) {
  if (!hasDisplay) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // typed text: up to 6 lines of 16 characters
  for (int line = 0; line < 6; line++) {
    int start = line * 16;
    if (start >= textLength) break;
    char buf[17];
    int n = 0;
    for (int i = start; i < textLength && n < 16; i++) buf[n++] = text[i];
    buf[n] = 0;
    display.setCursor(0, line * 10);
    display.print(buf);
  }

  display.drawFastVLine(98, 0, 64, SSD1306_WHITE);

  // braille cell: filled = pressed dot
  for (int k = 1; k <= 6; k++) {
    int x = CELL_X[(k - 1) / 3];
    int y = CELL_Y[(k - 1) % 3];
    if (cell & (1 << (k - 1))) display.fillCircle(x, y, CELL_R, SSD1306_WHITE);
    else display.drawCircle(x, y, CELL_R, SSD1306_WHITE);
  }

  // last recognized letter
  if (lastLetter != ' ') {
    char big[2] = { lastLetter, 0 };
    display.setTextSize(3);
    display.setCursor(106, 40);
    display.print(big);
  }

  display.display();
}

// ---------------------------------------------------------------- text

void addChar(char ch) {
  if (textLength >= MAX_LEN) {                 // full: discards the first line
    memmove(text, text + 16, MAX_LEN - 16);
    textLength -= 16;
  }
  text[textLength++] = ch;
  text[textLength] = 0;
}

void emitLetter(byte c) {
  char l = letter(c);
  lastLetter = l;
  if (l != '?') addChar(l);                    // chord with no letter does not go into the text

  Serial.print(l);
  Serial.print("   <- dots:");
  for (int k = 1; k <= 6; k++) {
    if (c & (1 << (k - 1))) { Serial.print(" "); Serial.print(k); }
  }
  Serial.println();
}

// ---------------------------------------------------------------- bad contact diagnostics

byte rawSeen = 0;                  // dots that showed up in some scan, confirmed or not
unsigned long lastRaw = 0;         // last time any dot showed up in the scan

void warnMissed(byte missed) {
  if (!missed) return;
  Serial.print("WARNING: dot(s)");
  for (int k = 1; k <= 6; k++) {
    if (missed & (1 << (k - 1))) { Serial.print(" "); Serial.print(k); }
  }
  Serial.println(" showed up but were not confirmed (bad contact?)");
}

// ---------------------------------------------------------------- chord and commands

byte chord = 0;
bool active = false;
unsigned long lastSeen = 0;      // last time any confirmed dot was seen

// If there is still a chord waiting to close, emits the letter before the command.
// Without this, a space given right after releasing the keys went in BEFORE the letter.
void closePendingChord() {
  if (active && chord) emitLetter(chord);
  chord = 0;
  active = false;
  rawSeen = 0;
}

void spaceCommand() {
  closePendingChord();
  addChar(' ');
  lastLetter = ' ';
  Serial.println("[space]");
  draw(0);
}

void deleteCommand() {
  closePendingChord();
  if (textLength > 0) { textLength--; text[textLength] = 0; }
  lastLetter = ' ';
  Serial.println("[delete]");
  draw(0);
}

void clearCommand() {
  closePendingChord();
  textLength = 0;
  text[0] = 0;
  lastLetter = ' ';
  Serial.println("[clear]");
  draw(0);
}

// ---------------------------------------------------------------- BOOT button

bool bootPrevious = false;
unsigned long bootSince = 0;
bool longDone = false;
bool veryLongDone = false;

void handleBootButton(unsigned long now) {
#if USE_BOOT_BUTTON
  bool pressed = (digitalRead(BOOT_PIN) == LOW);

  if (pressed && !bootPrevious) {              // just pressed
    bootSince = now;
    longDone = false;
    veryLongDone = false;
  }
  if (pressed) {
    unsigned long t = now - bootSince;
    if (!longDone && t >= LONG_PRESS_MS) { longDone = true; deleteCommand(); }
    if (!veryLongDone && t >= VERY_LONG_PRESS_MS) { veryLongDone = true; clearCommand(); }
  } else if (bootPrevious) {                   // just released
    unsigned long t = now - bootSince;
    if (!longDone && t >= 30) spaceCommand();      // short press (ignores noise below 30 ms)
  }
  bootPrevious = pressed;
#endif
}

// ---------------------------------------------------------------- setup / loop

byte counts[6] = {0, 0, 0, 0, 0, 0};     // goes up when the dot shows up, down when it does not
byte confirmed = 0;                      // dots that passed the filter (with hysteresis)
unsigned long chordStart = 0;
bool stuckWarned = false;
unsigned long lastMaskWarning = 0;

void scanI2C() {
  int found = 0;
  for (int addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char buf[40];
      snprintf(buf, sizeof(buf), "I2C: device at 0x%02X", addr);
      Serial.println(buf);
      found++;
    }
  }
  if (found == 0) Serial.println("I2C: no device found (check SDA, SCL, VCC and GND)");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 26; i++) table[i] = maskOf(LETTER_DOTS[i]);

  for (int i = 0; i < 4; i++) pinMode(OUTPUT_PINS_147[i], INPUT);
  for (int i = 0; i < 6; i++) pinMode(MASK_PINS[i], OUTPUT);
  clearMasks();
#if USE_BOOT_BUTTON
  pinMode(BOOT_PIN, INPUT_PULLUP);
#endif

  Wire.begin(SDA_PIN, SCL_PIN);
  scanI2C();

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) hasDisplay = true;        // most common address
  else if (display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) hasDisplay = true;   // some modules
  else Serial.println("OLED not found at 0x3C or 0x3D. Continuing with Serial only.");

  text[0] = 0;
  draw(0);
  Serial.println("Braille keyboard ready. Type a chord and release the keys.");
}

void loop() {
  unsigned long now = millis();
  byte reading = scan();

  if (reading) { rawSeen |= reading; lastRaw = now; }

  // Noise filter with tolerance: the count goes up when the dot shows up and down when
  // it does not. An isolated spike (up 1, down 1) never reaches CONFIRM_READS, but a
  // press with unstable contact (shows up in most scans) ends up confirming.
  // The dot only stops being valid when the count goes back to zero (hysteresis).
  for (int k = 0; k < NUM_DOTS; k++) {
    byte bit = 1 << k;
    if (reading & bit) { if (counts[k] < COUNT_CEILING) counts[k]++; }
    else if (counts[k] > 0) counts[k]--;

    if (counts[k] >= CONFIRM_READS) confirmed |= bit;
    else if (counts[k] == 0) confirmed &= ~bit;
  }
  byte c = confirmed;

  if (c) {
    if (!active) { chordStart = now; stuckWarned = false; }
    byte before = chord;
    chord |= c;                  // accumulates while a key is pressed
    active = true;
    if (chord != before) draw(chord);       // shows the dots live on the cell
    lastSeen = millis();         // measured after drawing (the screen takes a few ms)

    if (!stuckWarned && (now - chordStart) >= STUCK_TIME_MS) {
      stuckWarned = true;
      Serial.print("WARNING: dot(s)");
      for (int k = 1; k <= 6; k++) {
        if (c & (1 << (k - 1))) { Serial.print(" "); Serial.print(k); }
      }
      Serial.println(" active for over 3 s without release (stuck button or bad contact?)");
    }
  } else if (active && (millis() - lastSeen) >= CHORD_TIMEOUT_MS) {
    warnMissed(rawSeen & ~chord);          // dots that showed up but did not go into the letter
    rawSeen = 0;
    emitLetter(chord);           // everything released long enough: chord closed
    draw(chord);                 // text updated; the last cell stays on screen
    chord = 0;
    active = false;
  }

  // Dot that showed up but was never confirmed (no chord ever opened)
  if (!active && rawSeen && (now - lastRaw) >= CHORD_TIMEOUT_MS) {
    warnMissed(rawSeen);
    rawSeen = 0;
  }

  if (maskFailure && (now - lastMaskWarning) >= 2000) {
    lastMaskWarning = now;
    Serial.print("WARNING: dot ");
    Serial.print(maskFailure);
    Serial.println(" stayed active even when masked (is the button's leg B on the right GPIO?)");
  }
  maskFailure = 0;

  handleBootButton(now);
  delay(1);
}
