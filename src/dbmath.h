#pragma once
#include <cmath>
#include <cstdint>

// dB SPL non pondéré d'un bloc INMP441 (24 bits utiles en haut de mots 32 bits), composante continue retirée.
// offset = 94 dB SPL - sensibilité en dBFS (-26 pour l'INMP441) = 120.
inline float blockDb(const int32_t *s, int n, float offset) {
  double mean = 0, sq = 0;
  for (int i = 0; i < n; i++) mean += s[i] >> 8;
  mean /= n;
  for (int i = 0; i < n; i++) { const double v = (s[i] >> 8) - mean; sq += v * v; }
  const double rms = std::sqrt(sq / n) / 8388608.0;
  return static_cast<float>(20 * std::log10(rms + 1e-9) + offset);
}
