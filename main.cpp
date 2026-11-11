#include <Arduino.h>
#include <string.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define TEMPO_ACORDE_MS 120        // tempo com tudo solto para fechar o acorde (aumente se ainda falhar)
#define CONFIRMA_LEITURAS 3        // contagem minima para um ponto valer (~4 ms com contato bom)
#define TETO_CONTAGEM 8            // limite da contagem: define quanto tempo o ponto "sobrevive" a falhas
#define TEMPO_PRESO_MS 3000        // avisa no Serial se algum ponto ficar apertado mais que isso
#define USAR_BOTAO_BOOT 1          // 1 = botao BOOT como espaco / apagar / limpar
#define PINO_BOOT 0
#define TEMPO_LONGO_MS 700
#define TEMPO_MUITO_LONGO_MS 2000

const int PINOS_SAIDA_147[4] = {32, 33, 34, 35};              // A, B, C, D
const int PINOS_MASCARA[6]   = {13, 14, 27, 26, 25, 23};      // pontos 1..6
const int NUM_PONTOS = 6;

const int PINO_SDA = 21;
const int PINO_SCL = 22;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool temDisplay = false;

// Alfabeto braille: pontos de cada letra de 'a' a 'z'
const char *PONTOS_LETRA[26] = {
  "1",     "12",    "14",    "145",   "15",    "124",   "1245",  "125",   "24",    "245",
  "13",    "123",   "134",   "1345",  "135",   "1234",  "12345", "1235",  "234",   "2345",
  "136",   "1236",  "2456",  "1346",  "13456", "1356"
};
byte tabela[26];   // mascara de bits de cada letra (bit0 = ponto 1 ... bit5 = ponto 6)

const int TAM_MAX = 96;            // 6 linhas de 16 caracteres
char texto[TAM_MAX + 1];
int tamanho = 0;
char ultimaLetra = ' ';

byte mascaraDe(const char *pontos) {
  byte m = 0;
  for (; *pontos; pontos++) m |= (1 << (*pontos - '1'));
  return m;
}

char letra(byte c) {
  for (int i = 0; i < 26; i++) {
    if (tabela[i] == c) return 'a' + i;
  }
  return '?';
}

byte lerBruto() {   // saida do 147 (BCD invertido) numa unica passada
  byte v = 0;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(PINOS_SAIDA_147[i]) == LOW) v |= (1 << i);
  }
  return v;
}

// Leitura estavel: so aceita quando duas leituras seguidas coincidem.
// Devolve: 0 = nenhuma tecla, 1..6 = ponto de maior numero apertado
byte lerPrioridade() {
  byte anterior = lerBruto();
  for (int t = 0; t < 4; t++) {
    delayMicroseconds(5);
    byte atual = lerBruto();
    if (atual == anterior) return atual;
    anterior = atual;
  }
  return anterior;
}

void limparMascaras() {
  for (int i = 0; i < 6; i++) digitalWrite(PINOS_MASCARA[i], LOW);
}

byte falhaMascara = 0;   // ponto que continuou ativo mesmo mascarado (0 = nenhum)

// Varre o acorde atual. Devolve os pontos apertados (bit0 = ponto 1).
byte varrer() {
  byte celula = 0;
  limparMascaras();
  delayMicroseconds(30);
  for (int n = 0; n < NUM_PONTOS; n++) {
    byte v = lerPrioridade();
    if (v == 0 || v > NUM_PONTOS) break;        // nada apertado (ou leitura invalida)
    byte bit = 1 << (v - 1);
    if (celula & bit) { falhaMascara = v; break; }   // mesmo ponto de novo: a mascara nao funcionou
    celula |= bit;
    digitalWrite(PINOS_MASCARA[v - 1], HIGH);   // mascara esse ponto
    delayMicroseconds(30);
  }
  limparMascaras();
  return celula;
}

// ---------------------------------------------------------------- tela

const int CEL_X[2] = {106, 119};   // colunas da celula braille (pontos 1-3 | 4-5-6)
const int CEL_Y[3] = {6, 18, 30};  // linhas da celula
const int CEL_R = 4;

void desenha(byte celula) {
  if (!temDisplay) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // texto digitado: ate 6 linhas de 16 caracteres
  for (int linha = 0; linha < 6; linha++) {
    int ini = linha * 16;
    if (ini >= tamanho) break;
    char buf[17];
    int n = 0;
    for (int i = ini; i < tamanho && n < 16; i++) buf[n++] = texto[i];
    buf[n] = 0;
    display.setCursor(0, linha * 10);
    display.print(buf);
  }

  display.drawFastVLine(98, 0, 64, SSD1306_WHITE);

  // celula braille: preenchido = ponto apertado
  for (int k = 1; k <= 6; k++) {
    int x = CEL_X[(k - 1) / 3];
    int y = CEL_Y[(k - 1) % 3];
    if (celula & (1 << (k - 1))) display.fillCircle(x, y, CEL_R, SSD1306_WHITE);
    else display.drawCircle(x, y, CEL_R, SSD1306_WHITE);
  }

  // ultima letra reconhecida
  if (ultimaLetra != ' ') {
    char grande[2] = { ultimaLetra, 0 };
    display.setTextSize(3);
    display.setCursor(106, 40);
    display.print(grande);
  }

  display.display();
}

// ---------------------------------------------------------------- texto

void adicionaChar(char ch) {
  if (tamanho >= TAM_MAX) {                    // cheio: descarta a primeira linha
    memmove(texto, texto + 16, TAM_MAX - 16);
    tamanho -= 16;
  }
  texto[tamanho++] = ch;
  texto[tamanho] = 0;
}

void emiteLetra(byte c) {
  char l = letra(c);
  ultimaLetra = l;
  if (l != '?') adicionaChar(l);               // acorde sem letra nao entra no texto

  Serial.print(l);
  Serial.print("   <- pontos:");
  for (int k = 1; k <= 6; k++) {
    if (c & (1 << (k - 1))) { Serial.print(" "); Serial.print(k); }
  }
  Serial.println();
}

// ---------------------------------------------------------------- diagnostico de mau contato

byte vistoBruto = 0;               // pontos que apareceram em alguma varredura, confirmados ou nao
unsigned long ultimoBruto = 0;     // ultima vez que algum ponto apareceu na varredura

void avisaPerdidos(byte perdidos) {
  if (!perdidos) return;
  Serial.print("AVISO: ponto(s)");
  for (int k = 1; k <= 6; k++) {
    if (perdidos & (1 << (k - 1))) { Serial.print(" "); Serial.print(k); }
  }
  Serial.println(" apareceram mas nao foram confirmados (mau contato?)");
}

// ---------------------------------------------------------------- acorde e comandos

byte acorde = 0;
bool ativo = false;
unsigned long ultimoVisto = 0;   // ultima vez que algum ponto confirmado foi visto

// Se ainda houver um acorde esperando fechar, emite a letra antes do comando.
// Sem isso, um espaco dado logo depois de soltar as teclas entrava ANTES da letra.
void fecharAcordePendente() {
  if (ativo && acorde) emiteLetra(acorde);
  acorde = 0;
  ativo = false;
  vistoBruto = 0;
}

void comandoEspaco() {
  fecharAcordePendente();
  adicionaChar(' ');
  ultimaLetra = ' ';
  Serial.println("[espaco]");
  desenha(0);
}

void comandoApagar() {
  fecharAcordePendente();
  if (tamanho > 0) { tamanho--; texto[tamanho] = 0; }
  ultimaLetra = ' ';
  Serial.println("[apagar]");
  desenha(0);
}

void comandoLimpar() {
  fecharAcordePendente();
  tamanho = 0;
  texto[0] = 0;
  ultimaLetra = ' ';
  Serial.println("[limpar]");
  desenha(0);
}

// ---------------------------------------------------------------- botao BOOT

bool bootAnterior = false;
unsigned long bootDesde = 0;
bool longoFeito = false;
bool muitoLongoFeito = false;

void tratarBotaoBoot(unsigned long agora) {
#if USAR_BOTAO_BOOT
  bool apertado = (digitalRead(PINO_BOOT) == LOW);

  if (apertado && !bootAnterior) {             // acabou de apertar
    bootDesde = agora;
    longoFeito = false;
    muitoLongoFeito = false;
  }
  if (apertado) {
    unsigned long t = agora - bootDesde;
    if (!longoFeito && t >= TEMPO_LONGO_MS) { longoFeito = true; comandoApagar(); }
    if (!muitoLongoFeito && t >= TEMPO_MUITO_LONGO_MS) { muitoLongoFeito = true; comandoLimpar(); }
  } else if (bootAnterior) {                   // acabou de soltar
    unsigned long t = agora - bootDesde;
    if (!longoFeito && t >= 30) comandoEspaco();   // toque curto (ignora ruido abaixo de 30 ms)
  }
  bootAnterior = apertado;
#endif
}

// ---------------------------------------------------------------- setup / loop

byte contagem[6] = {0, 0, 0, 0, 0, 0};   // sobe quando o ponto aparece, desce quando nao aparece
byte confirmados = 0;                    // pontos que passaram no filtro (com histerese)
unsigned long inicioAcorde = 0;
bool avisouPreso = false;
unsigned long ultimoAvisoMascara = 0;

void varreduraI2C() {
  int achados = 0;
  for (int addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char buf[40];
      snprintf(buf, sizeof(buf), "I2C: dispositivo em 0x%02X", addr);
      Serial.println(buf);
      achados++;
    }
  }
  if (achados == 0) Serial.println("I2C: nenhum dispositivo encontrado (confira SDA, SCL, VCC e GND)");
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 26; i++) tabela[i] = mascaraDe(PONTOS_LETRA[i]);

  for (int i = 0; i < 4; i++) pinMode(PINOS_SAIDA_147[i], INPUT);
  for (int i = 0; i < 6; i++) pinMode(PINOS_MASCARA[i], OUTPUT);
  limparMascaras();
#if USAR_BOTAO_BOOT
  pinMode(PINO_BOOT, INPUT_PULLUP);
#endif

  Wire.begin(PINO_SDA, PINO_SCL);
  varreduraI2C();

  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) temDisplay = true;        // endereco mais comum
  else if (display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) temDisplay = true;   // alguns modulos
  else Serial.println("OLED nao encontrado em 0x3C nem 0x3D. Continuando so com o Serial.");

  texto[0] = 0;
  desenha(0);
  Serial.println("Teclado braille pronto. Digite um acorde e solte as teclas.");
}

void loop() {
  unsigned long agora = millis();
  byte lido = varrer();

  if (lido) { vistoBruto |= lido; ultimoBruto = agora; }

  // Filtro de ruido com tolerancia: a contagem sobe quando o ponto aparece e desce quando
  // nao aparece. Um pico isolado (sobe 1, desce 1) nunca chega a CONFIRMA_LEITURAS, mas um
  // aperto com contato instavel (aparece na maioria das varreduras) acaba confirmando.
  // O ponto so deixa de valer quando a contagem volta a zero (histerese).
  for (int k = 0; k < NUM_PONTOS; k++) {
    byte bit = 1 << k;
    if (lido & bit) { if (contagem[k] < TETO_CONTAGEM) contagem[k]++; }
    else if (contagem[k] > 0) contagem[k]--;

    if (contagem[k] >= CONFIRMA_LEITURAS) confirmados |= bit;
    else if (contagem[k] == 0) confirmados &= ~bit;
  }
  byte c = confirmados;

  if (c) {
    if (!ativo) { inicioAcorde = agora; avisouPreso = false; }
    byte antes = acorde;
    acorde |= c;                 // acumula enquanto houver tecla pressionada
    ativo = true;
    if (acorde != antes) desenha(acorde);   // mostra os pontos ao vivo na celula
    ultimoVisto = millis();      // medido depois de desenhar (a tela leva alguns ms)

    if (!avisouPreso && (agora - inicioAcorde) >= TEMPO_PRESO_MS) {
      avisouPreso = true;
      Serial.print("AVISO: ponto(s)");
      for (int k = 1; k <= 6; k++) {
        if (c & (1 << (k - 1))) { Serial.print(" "); Serial.print(k); }
      }
      Serial.println(" ativo(s) ha mais de 3 s sem soltar (botao travado ou mau contato?)");
    }
  } else if (ativo && (millis() - ultimoVisto) >= TEMPO_ACORDE_MS) {
    avisaPerdidos(vistoBruto & ~acorde);   // pontos que apareceram mas nao entraram na letra
    vistoBruto = 0;
    emiteLetra(acorde);          // tudo solto por tempo suficiente: acorde fechado
    desenha(acorde);             // texto atualizado; a ultima celula continua na tela
    acorde = 0;
    ativo = false;
  }

  // Ponto que apareceu mas nunca foi confirmado (nenhum acorde chegou a abrir)
  if (!ativo && vistoBruto && (agora - ultimoBruto) >= TEMPO_ACORDE_MS) {
    avisaPerdidos(vistoBruto);
    vistoBruto = 0;
  }

  if (falhaMascara && (agora - ultimoAvisoMascara) >= 2000) {
    ultimoAvisoMascara = agora;
    Serial.print("AVISO: o ponto ");
    Serial.print(falhaMascara);
    Serial.println(" continuou ativo mesmo mascarado (a perna B do botao esta no GPIO certo?)");
  }
  falhaMascara = 0;

  tratarBotaoBoot(agora);
  delay(1);
}