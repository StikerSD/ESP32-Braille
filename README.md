# ESP32-Braille

A braille keyboard built with an ESP32 and a 74HC147 priority encoder. Each button is one dot of a braille cell: you press a chord, release it, and the matching letter (a–z) shows up on an OLED display.

Built for a Digital Systems course that required the use of an encoder IC.

## Parts

| Qty | Part |
|---|---|
| 1 | ESP32 DevKit (CP2102) |
| 1 | 74HC147 (9-to-4 priority encoder) |
| 1 | 0.96" SSD1306 OLED, I2C (0x3C) |
| 6 | 6×6 mm push buttons |
| 6 | 10 kΩ resistors |
| 1 | 100 nF ceramic capacitor |
| 1 | 830-point breadboard and jumper wires |

Everything runs at 3.3 V. No OR gates are used: reading several keys at once is done in software (see [Masked scanning](#masked-scanning)).

## Wiring

### 74HC147

| Pin | Function | Connection |
|---|---|---|
| 16 | VCC | +3V3 |
| 8 | GND | GND |
| 15 | NC | not connected |
| 4, 5, 10 | I7, I8, I9 | +3V3 (unused) |
| 9 | Output A | GPIO32 |
| 7 | Output B | GPIO33 |
| 6 | Output C | GPIO34 |
| 14 | Output D | GPIO35 |

Place the 100 nF capacitor between +3V3 and GND, as close to the IC as possible.

### Buttons

| Dot | 147 input | Mask GPIO |
|---|---|---|
| 1 | pin 11 (I1) | GPIO13 |
| 2 | pin 12 (I2) | GPIO14 |
| 3 | pin 13 (I3) | GPIO27 |
| 4 | pin 1 (I4) | GPIO26 |
| 5 | pin 2 (I5) | GPIO25 |
| 6 | pin 3 (I6) | GPIO23 |

Each button is wired like this:

```
            +3V3
             │
            10k
             │
 147 ────────┴──── button leg A
 input
                   [ button ]
 mask ──────────── button leg B
 GPIO
```

Legs A and B must belong to different internal pairs of the button, otherwise it conducts even when released. Using two diagonally opposite legs guarantees this.

### OLED

| OLED | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

## How it works

### The 74HC147

The 74HC147 has 9 active-low inputs and 4 outputs. The output is the number of the **highest-priority** active input, in inverted BCD:

| Active input | Binary (DCBA) | IC output (DCBA) |
|---|---|---|
| none | 0000 | 1111 |
| 1 | 0001 | 1110 |
| 2 | 0010 | 1101 |
| 3 | 0011 | 1100 |
| 4 | 0100 | 1011 |
| 5 | 0101 | 1010 |
| 6 | 0110 | 1001 |

The catch is that a braille letter can have several dots pressed at once, and the IC only reports one of them.

### The mask

Instead of going to GND, leg B of each button goes to an ESP32 GPIO that works as a programmable ground:

- **Mask LOW:** the button works normally. When pressed, it pulls the 147 input to 0 V.
- **Mask HIGH:** even when pressed, both sides of the button sit at a high level, so the 147 sees the dot as released.

This lets the ESP32 turn any button off in software.

### Masked scanning

To read the whole chord, the `varrer()` function:

1. sets every mask LOW;
2. reads the highest pressed dot from the 147 and stores it;
3. sets that dot's mask HIGH;
4. reads again, and now the 147 reports the next dot;
5. repeats until the 147 returns 0.

Example with the letter **d** (dots 1, 4 and 5):

| Read | 147 reports | Action |
|---|---|---|
| 1 | 5 | store 5, mask it |
| 2 | 4 | store 4, mask it |
| 3 | 1 | store 1, mask it |
| 4 | 0 | done: dots {1, 4, 5} |

The result is a byte with one bit per dot (bit 0 = dot 1, …, bit 5 = dot 6).

### Chords

- **Filter:** each dot has a counter that goes up when the dot shows up in a scan and down when it doesn't. A dot only counts once its counter reaches 3, and is only released when the counter drops back to 0. This rejects noise and tolerates flaky contacts.
- **Chord:** while any button is held, dots are accumulated with OR. Once everything has been released for 120 ms, the chord closes.
- **Letter:** the chord byte is looked up in a braille alphabet table. If it matches no letter, a `?` is shown and nothing is added to the text.

### Display

The typed text (6 lines of 16 characters) is on the left. On the right are the braille cell, with pressed dots filled in, and the last recognized letter.

### BOOT button

| Press | Action |
|---|---|
| short | space |
| 0.7 s | backspace |
| 2 s | clear |

## Building

This is a [PlatformIO](https://platformio.org/) project. Example `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 460800
lib_deps =
    adafruit/Adafruit SSD1306
    adafruit/Adafruit GFX Library
```

The code lives in `src/main.cpp`. To flash and open the monitor:

```
pio run -t upload
pio device monitor
```

If the upload drops halfway, use `upload_speed = 115200` and a short USB cable.

## Serial warnings

The firmware messages are in Portuguese:

| Message | Meaning | Likely cause |
|---|---|---|
| `ponto(s) ... ativo(s) ha mais de 3 s` | dot(s) held for over 3 s | stuck button or node shorted to GND |
| `o ponto X continuou ativo mesmo mascarado` | dot X stayed active while masked | leg B on the wrong GPIO, or both connections on the same leg pair |
| `ponto(s) ... apareceram mas nao foram confirmados` | dot(s) seen but not confirmed | bad contact in the button or breadboard |
| `I2C: nenhum dispositivo encontrado` | no I2C device found | check the OLED's SDA, SCL, VCC and GND |

## Troubleshooting

- **All dots lit and buttons unresponsive:** check that I7, I8 and I9 are tied to +3V3, and that no button has both connections on the same leg pair.
- **Presses that sometimes don't register:** reseat the button in the breadboard (the short legs often only touch the contacts), or move it to other columns.
- **Readings change when you touch the board:** the nodes are high impedance. Swapping the 10 kΩ pull-ups for 4.7 kΩ makes the circuit less sensitive.
- Always connect and disconnect wires with USB unplugged.

## License

[MIT](LICENSE)
