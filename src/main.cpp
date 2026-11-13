// Décibelmètre K2000 pour CYD.
// Micro INMP441 : SCK->GPIO22 (CN1), WS->GPIO27 (CN1), SD->GPIO35 (P3), L/R->GND, VDD->3V3.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <driver/i2s.h>

// INMP441 : -26 dBFS à 94 dB SPL -> dB SPL = dBFS + 120. Ajuster avec un sonomètre de référence.
constexpr float CAL_OFFSET = 120.0f;
constexpr float DB_MIN = 35, DB_MAX = 100;  // plage affichée par les barres
constexpr int N = 512;                      // échantillons par bloc (32 ms à 16 kHz)

TFT_eSPI tft;
int32_t buf[N];

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

// dB SPL non pondéré (dB Z) d'un bloc ; NAN si lecture impossible.
float readDb() {
  size_t got = 0;
  if (i2s_read(I2S_NUM_0, buf, sizeof buf, &got, portMAX_DELAY) != ESP_OK || !got) return NAN;
  int n = got / 4;
  double mean = 0, sq = 0;
  for (int i = 0; i < n; i++) mean += buf[i] >> 8;  // 24 bits utiles en haut du mot
  mean /= n;
  for (int i = 0; i < n; i++) { double v = (buf[i] >> 8) - mean; sq += v * v; }
  double rms = sqrt(sq / n) / 8388608.0;
  return 20 * log10(rms + 1e-9) + CAL_OFFSET;
}

uint16_t red(float b) {  // b 0..1 -> rouge, avec un cœur un peu orangé quand c'est plein
  b = constrain(b, 0.f, 1.f);
  return tft.color565(30 + 225 * b, b > 0.85f ? 90 * (b - 0.85f) / 0.15f : 0, 0);
}

// --- Scanner K2000 : un point qui fait l'aller-retour avec traînée ---
constexpr int SEG = 12, SX = 10, SY = 12, SW = 23, SH = 18, SGAP = 2;
float trail[SEG];
float scanPos = 0, scanDir = 1;

void drawScanner(float level, float dt) {
  scanPos += scanDir * dt * (6 + 30 * level);  // segments/s : accélère avec le son
  if (scanPos >= SEG - 1) { scanPos = SEG - 1; scanDir = -1; }
  if (scanPos <= 0) { scanPos = 0; scanDir = 1; }
  for (int i = 0; i < SEG; i++) {
    trail[i] *= powf(0.004f, dt);  // décroissance indépendante du framerate
    if (i == lroundf(scanPos)) trail[i] = 1;
    tft.fillRect(SX + i * (SW + SGAP), SY, SW, SH, red(trail[i]));
  }
}

// --- Boîte vocale de KITT : 3 colonnes de barres qui s'ouvrent depuis le centre ---
constexpr int ROWS = 11, VY = 48, VH = 9, VGAP = 3;
const int colX[3] = {50, 125, 200}, colW[3] = {60, 70, 60};
const float colGain[3] = {0.7f, 1.0f, 0.7f};

void drawVoice(float level) {
  for (int c = 0; c < 3; c++) {
    float lit = level * colGain[c] * (ROWS / 2 + 1);  // demi-hauteur allumée
    for (int r = 0; r < ROWS; r++) {
      float d = abs(r - ROWS / 2);
      float b = d < lit ? 1 - 0.5f * d / (ROWS / 2) : 0;
      tft.fillRect(colX[c], VY + r * (VH + VGAP), colW[c], VH, b > 0 ? red(b) : tft.color565(25, 0, 0));
    }
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);  // paysage 320x240
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("dB", 270, 180, 4);
  micInit();
}

void loop() {
  static float shown = DB_MIN, peak = 0;
  static uint32_t last = millis(), lastText = 0, lastPeak = 0;
  float db = readDb();
  if (isnan(db)) return;

  uint32_t now = millis();
  float dt = (now - last) / 1000.f;
  last = now;
  // attaque rapide, retombée lente, comme un vrai VU
  shown += (db - shown) * (db > shown ? 0.6f : 0.15f);
  if (db > peak || now - lastPeak > 2000) { peak = db; lastPeak = now; }
  float level = constrain((shown - DB_MIN) / (DB_MAX - DB_MIN), 0.f, 1.f);

  drawScanner(level, dt);
  drawVoice(level);

  if (now - lastText > 250) {  // texte à 4 Hz sinon illisible
    lastText = now;
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextPadding(tft.textWidth("888.8", 7));
    tft.setTextDatum(BR_DATUM);
    tft.drawFloat(shown, 1, 262, 232, 7);
    tft.setTextPadding(tft.textWidth("PK 888", 2));
    tft.setTextDatum(BL_DATUM);
    tft.drawString("PK " + String(lroundf(peak)), 270, 232, 2);
    tft.setTextPadding(0);
    Serial.printf("%.1f dB\n", shown);
  }
}
