// Décibelmètre K2000 pour CYD.
// Micro INMP441 : SCK->GPIO22 (CN1), WS->GPIO27 (CN1), SD->GPIO35 (P3), L/R->GND, VDD->3V3.
// Fluidité : l'audio tourne sur le core 0, l'écran sur le core 1 à ~60 i/s,
// et on ne renvoie à l'écran que les rectangles dont la couleur a changé.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>
#include "dbmath.h"

// INMP441 : -26 dBFS à 94 dB SPL -> dB SPL = dBFS + 120. Ajuster avec un sonomètre de référence.
constexpr float CAL_OFFSET = 120.0f;
constexpr float DB_MIN = 35, DB_MAX = 100;  // plage affichée par les barres
constexpr int N = 256;                      // 16 ms à 16 kHz

TFT_eSPI tft;
TFT_eSprite bottom(&tft);  // bandeau chiffres : dessiné hors écran puis envoyé d'un coup, sans scintillement
volatile float g_db = DB_MIN;

void micInit() {
  i2s_config_t cfg = {};
  cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = N;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = 22;
  pins.ws_io_num = 27;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = 35;
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_set_pin(I2S_NUM_0, &pins);
}

void audioTask(void *) {
  static int32_t buf[N];
  for (;;) {
    size_t got = 0;
    if (i2s_read(I2S_NUM_0, buf, sizeof buf, &got, portMAX_DELAY) != ESP_OK || got < 4) continue;
    g_db = blockDb(buf, got / 4, CAL_OFFSET);
  }
}

uint16_t red(float b) {  // b 0..1 -> rouge sombre à rouge vif, cœur légèrement orangé quand c'est plein
  b = constrain(b, 0.f, 1.f);
  return tft.color565(25 + 230 * b, b > 0.85f ? 90 * (b - 0.85f) / 0.15f : 0, 0);
}

void rect(uint16_t &cache, int x, int y, int w, int h, uint16_t c) {
  if (cache == c) return;
  cache = c;
  tft.fillRect(x, y, w, h, c);
}

// --- Scanner K2000 : tête qui glisse entre les segments, avec traînée ---
constexpr int SEG = 12, SX = 10, SY = 12, SW = 23, SH = 18, SGAP = 2;
float trail[SEG];
uint16_t scanCache[SEG];
float scanPos = 0, scanDir = 1;

void drawScanner(float level, float dt) {
  scanPos += scanDir * dt * (7 + 25 * level);  // segments/s : accélère avec le son
  if (scanPos >= SEG - 1) { scanPos = SEG - 1; scanDir = -1; }
  if (scanPos <= 0) { scanPos = 0; scanDir = 1; }
  float decay = powf(0.01f, dt);  // ~1 % restant après 1 s, quel que soit le framerate
  for (int i = 0; i < SEG; i++) {
    float head = 1 - fabsf(i - scanPos);  // tête répartie sur 2 segments : mouvement continu
    trail[i] = max(trail[i] * decay, head);
    rect(scanCache[i], SX + i * (SW + SGAP), SY, SW, SH, red(trail[i]));
  }
}

// --- Boîte vocale de KITT : 3 colonnes qui s'ouvrent depuis le centre ---
constexpr int ROWS = 11, HALF = ROWS / 2, VY = 48, VH = 9, VGAP = 3;
const int colX[3] = {50, 125, 200}, colW[3] = {60, 70, 60};
const float colGain[3] = {0.7f, 1.0f, 0.7f};
uint16_t voiceCache[3][ROWS];

void drawVoice(float level) {
  for (int c = 0; c < 3; c++) {
    float lit = level * colGain[c] * (HALF + 1);  // demi-hauteur allumée, fractionnaire
    for (int r = 0; r < ROWS; r++) {
      float d = abs(r - HALF);
      float on = constrain(lit - d, 0.f, 1.f);  // la barre du bord s'allume progressivement
      rect(voiceCache[c][r], colX[c], VY + r * (VH + VGAP), colW[c], VH, red(on * (1 - 0.45f * d / HALF)));
    }
  }
}

void drawNumbers(float db, float peak) {
  bottom.fillSprite(TFT_BLACK);
  bottom.setTextColor(TFT_RED);
  bottom.setTextDatum(BR_DATUM);
  bottom.drawFloat(db, 1, 262, 52, 7);
  bottom.setTextDatum(TL_DATUM);
  bottom.drawString("dB", 270, 0, 4);
  bottom.setTextDatum(BL_DATUM);
  bottom.drawString("PK " + String(lroundf(peak)), 270, 52, 2);
  bottom.pushSprite(0, 184);
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);  // paysage 320x240
  tft.fillScreen(TFT_BLACK);
  bottom.createSprite(320, 56);  // 36 Ko
  micInit();
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);
}

void loop() {
  static float shown = DB_MIN, peak = 0, bar = 0;
  static uint32_t last = micros(), lastText = 0, lastPeak = 0;
  uint32_t now = micros();
  float dt = (now - last) / 1e6f;
  if (dt < 1 / 60.f) { delay(1); return; }  // ~60 i/s
  last = now;

  float db = g_db;
  // attaque rapide (~30 ms), retombée douce (~300 ms), indépendantes du framerate
  shown += (db - shown) * (1 - expf(-dt / (db > shown ? 0.03f : 0.3f)));
  uint32_t ms = now / 1000;
  if (shown > peak || ms - lastPeak > 2000) { peak = shown; lastPeak = ms; }
  bar = constrain((shown - DB_MIN) / (DB_MAX - DB_MIN), 0.f, 1.f);

  drawScanner(bar, dt);
  drawVoice(bar);
  if (ms - lastText > 100) {  // chiffres à 10 Hz : au-delà ils deviennent illisibles
    lastText = ms;
    drawNumbers(shown, peak);
    Serial.printf("%.1f dB\n", shown);
  }
}
