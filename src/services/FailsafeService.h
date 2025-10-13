#pragma once
#include <Arduino.h>

namespace Failsafe {

enum class Mode : uint8_t { IDLE = 0, HEATING, COOLING /*overheat=ALL OFF*/ };

struct Config {
  float HARD_MIN = 20.0f;   // batas bawah (°C)
  float HARD_MAX = 38.0f;   // batas atas (°C)
  float HYST     = 1.0f;    // hysteresis (°C)

  // anti flip-flop
  uint32_t MIN_ON_MS  = 20000;  // minimal waktu bertahan setelah ON
  uint32_t MIN_OFF_MS = 20000;  // minimal waktu bertahan setelah OFF

  // (opsional) indikator network
  uint32_t NET_DEAD_MS = 30000;
};

void begin(const Config& cfg);
void tick(float avgTemp, bool mqttOk, bool wifiOk);
void forceOff();

Mode currentMode();
bool isActive();               // true saat sedang overriding (mode != IDLE)
uint32_t lastChangeMs();

} // namespace Failsafe