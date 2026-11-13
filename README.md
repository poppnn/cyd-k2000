# CYD K2000 dB

Décibelmètre pour Cheap Yellow Display (ESP32-2432S028R) avec un affichage façon K2000 : un scanner rouge dont la vitesse suit le son, et la « voix » de KITT en barres qui s'ouvrent depuis le centre.

**Web flasher : https://poppnn.github.io/cyd-k2000/** (Chrome ou Edge)

## Matériel

Le CYD n'a pas de micro : il faut un INMP441 (I2S).

| INMP441 | CYD |
|---|---|
| SCK | GPIO22 (CN1) |
| WS | GPIO27 (CN1) |
| SD | GPIO35 (P3) |
| L/R | GND |
| VDD / GND | 3V3 / GND |

## Compiler

```bash
pio run -t upload
```

- Calibrage : `CAL_OFFSET` dans `src/main.cpp`, à ajuster avec un sonomètre de référence.
- Prévu pour le CYD 2,8" à touch résistif (ILI9341 avec inversion). Couleurs fausses sur une autre version : retirer `TFT_INVERSION_ON` dans `platformio.ini`.
- Mesure non pondérée (dB Z, pas dB(A)).
