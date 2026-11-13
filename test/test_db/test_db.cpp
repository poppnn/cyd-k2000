#include <unity.h>
#include "../../src/dbmath.h"

constexpr int N = 1600;
int32_t buf[N];

// Sinus à amplitude A (en pleine échelle 24 bits), décalé d'un continu, cadré en haut de mots 32 bits.
void sine(double amp, int32_t dc) {
  for (int i = 0; i < N; i++)
    buf[i] = static_cast<int32_t>(dc + amp * 8388607 * std::sin(2 * M_PI * 1000 * i / 16000.0)) * 256;
}

void setUp() {}
void tearDown() {}

// Pleine échelle : RMS = 1/sqrt(2) -> -3,01 dBFS -> 116,99 dB SPL.
void test_full_scale() { sine(1.0, 0); TEST_ASSERT_FLOAT_WITHIN(0.05f, 116.99f, blockDb(buf, N, 120)); }
// Point de calibration INMP441 : -26 dBFS RMS = 94 dB SPL.
void test_94db() { sine(std::pow(10, -26 / 20.0) * std::sqrt(2.0), 0); TEST_ASSERT_FLOAT_WITHIN(0.05f, 94.f, blockDb(buf, N, 120)); }
// Le continu du micro ne doit pas compter.
void test_dc_removed() { sine(0.01, 300000); TEST_ASSERT_FLOAT_WITHIN(0.05f, 76.99f, blockDb(buf, N, 120)); }
// Silence : valeur finie, très basse (pas -inf ni NaN).
void test_silence() { sine(0, 0); float db = blockDb(buf, N, 120); TEST_ASSERT_TRUE(std::isfinite(db) && db < 0); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_full_scale);
  RUN_TEST(test_94db);
  RUN_TEST(test_dc_removed);
  RUN_TEST(test_silence);
  return UNITY_END();
}
