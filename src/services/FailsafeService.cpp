#include "FailsafeService.h"
#include <math.h>
#include "LampService.h"
#include "../helpers/Logger.h"

namespace Failsafe {

static Config    CFG;
static Mode      g_mode       = Mode::IDLE;
static uint32_t  g_lastNetOk  = 0;
static uint32_t  g_lastSwitch = 0;

static inline bool elapsed(uint32_t start, uint32_t dur) {
  return (millis() - start) >= dur;
}

void begin(const Config& cfg) {
  CFG = cfg;
  g_mode = Mode::IDLE;
  g_lastNetOk = millis();
  g_lastSwitch = millis();
}

// ======= HEATER-ONLY policy mapping =======
// HEATING  : lampu ON (heater ON), master ON
// COOLING  : OVERHEAT → ALL OFF, master OFF
// IDLE     : ALL OFF, master OFF
static void applyMode(Mode m) {
  if (g_mode == m) return;

  // tahan pindah mode sesuai hold time
  const bool wasActive = (g_mode != Mode::IDLE);
  const uint32_t hold = wasActive ? CFG.MIN_ON_MS : CFG.MIN_OFF_MS;
  if (!elapsed(g_lastSwitch, hold)) return;

  g_mode = m;
  g_lastSwitch = millis();

  switch (g_mode) {
    case Mode::IDLE:
      LampService::setMaster(false);
      LampService::setLamp1(0.0f);
      LampService::setLamp2(0.0f);
      Logger::info("[SAFETY] IDLE (all OFF)");
      break;

    case Mode::HEATING: // dingin → heater ON
      LampService::setMaster(true);
      LampService::setLamp1(1.0f);
      LampService::setLamp2(1.0f);
      Logger::warn("[SAFETY] HEATING (heater ON)");
      break;

    case Mode::COOLING: // panas → semua OFF (tidak pakai kipas)
      LampService::setMaster(false);
      LampService::setLamp1(0.0f);
      LampService::setLamp2(0.0f);
      Logger::warn("[SAFETY] OVERHEAT -> ALL OFF");
      break;
  }
}

Mode currentMode()      { return g_mode; }
bool isActive()         { return g_mode != Mode::IDLE; }
uint32_t lastChangeMs() { return g_lastSwitch; }

// ======= Thermostat SELALU aktif (tanpa tergantung network) =======
void tick(float avgTemp, bool mqttOk, bool wifiOk) {
  // catat network (opsional buat telemetri/diagnostik)
  if (wifiOk && mqttOk) g_lastNetOk = millis();

  if (!isfinite(avgTemp)) return; // butuh suhu valid

  const float lowEnter  = CFG.HARD_MIN - CFG.HYST; // masuk HEATING
  const float lowExit   = CFG.HARD_MIN + CFG.HYST; // keluar HEATING
  const float highEnter = CFG.HARD_MAX + CFG.HYST; // masuk OVERHEAT (COOLING=OFF)
  const float highExit  = CFG.HARD_MAX - CFG.HYST; // keluar OVERHEAT

  switch (g_mode) {
    case Mode::IDLE:
      if (avgTemp < lowEnter) {
        applyMode(Mode::HEATING);
      } else if (avgTemp > highEnter) {
        applyMode(Mode::COOLING);  // overheat → OFF
      }
      break;

    case Mode::HEATING:
      // keluar HEATING saat cukup hangat
      if (avgTemp >= lowExit) {
        applyMode(Mode::IDLE);
      }
      // kalau melonjak panas → OFF
      if (avgTemp > highEnter) {
        applyMode(Mode::COOLING);
      }
      break;

    case Mode::COOLING: // di kebijakan ini = ALL OFF
      // kembali IDLE saat cukup turun
      if (avgTemp <= highExit) {
        applyMode(Mode::IDLE);
      }
      // kalau terlalu dingin, balik HEATING
      if (avgTemp < lowEnter) {
        applyMode(Mode::HEATING);
      }
      break;
  }
}

void forceOff() {
  g_mode = Mode::IDLE;
  g_lastSwitch = millis();
  LampService::setMaster(false);
  LampService::setLamp1(0.0f);
  LampService::setLamp2(0.0f);
  Logger::info("[SAFETY] forceOff -> IDLE");
}

} // namespace Failsafe