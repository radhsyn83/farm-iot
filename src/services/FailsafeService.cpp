#include "FailsafeService.h"
#include <math.h>
#include "LampService.h"          // harus menyediakan setLamp1/2(float 0..1) & setMaster(bool)
#include "../helpers/Logger.h"

namespace Failsafe {

static Config    CFG;
static Mode      g_mode       = Mode::IDLE;
static bool      g_active     = false;       // sedang override?
static uint32_t  g_lastNetOk  = 0;           // kapan terakhir jaringan OK
static uint32_t  g_lastSwitch = 0;           // kapan terakhir ganti mode

static inline bool elapsed(uint32_t start, uint32_t dur) {
  return (millis() - start) >= dur;
}

void begin(const Config& cfg) {
  CFG = cfg;
  g_mode = Mode::IDLE;
  g_active = false;
  g_lastNetOk = millis();
  g_lastSwitch = millis();
}

Mode currentMode()            { return g_mode; }
bool isActive()               { return g_active; }
uint32_t lastChangeMs()       { return g_lastSwitch; }

static void applyMode(Mode m) {
  if (g_mode == m) return;

  // tegakkan minimum hold
  const bool wasActive = (g_mode != Mode::IDLE);
  const uint32_t hold = wasActive ? CFG.MIN_ON_MS : CFG.MIN_OFF_MS;
  if (!elapsed(g_lastSwitch, hold)) {
    return; // belum boleh ganti
  }

  g_mode = m;
  g_lastSwitch = millis();

  // mapping aktuator:
  // - COOLING  -> lamp1 ON, lamp2 OFF, master ON
  // - HEATING  -> lamp2 ON, lamp1 OFF, master ON
  // - IDLE     -> semua OFF (failsafe release)
  switch (g_mode) {
    case Mode::IDLE:
      LampService::setLamp1(0.0f);
      LampService::setLamp2(0.0f);
      LampService::setMaster(false);
      Logger::info("[FAILSAFE] IDLE");
      break;

    case Mode::COOLING:
      LampService::setMaster(true);
      LampService::setLamp2(0.0f);
      LampService::setLamp1(1.0f);
      Logger::warn("[FAILSAFE] COOLING (fan ON)");
      break;

    case Mode::HEATING:
      LampService::setMaster(true);
      LampService::setLamp1(0.0f);
      LampService::setLamp2(1.0f);
      Logger::warn("[FAILSAFE] HEATING (heater ON)");
      break;
  }
}

void tick(float avgTemp, bool mqttOk, bool wifiOk) {
  // catat kapan jaringan sehat
  if (wifiOk && mqttOk) {
    g_lastNetOk = millis();
  }

  // aktifkan failsafe jika jaringan mati cukup lama
  const bool netDead  = (!wifiOk || !mqttOk);
  const bool enableFS = netDead && elapsed(g_lastNetOk, CFG.NET_DEAD_MS);

  if (!enableFS) {
    // release override bila sebelumnya aktif
    if (g_active) {
      Logger::info("[FAILSAFE] released (network OK)");
      g_active = false;
      // tidak paksa ubah lamp di sini; biarkan state server yang ambil alih
      // kalau mau hard reset hardware saat recover: applyMode(Mode::IDLE);
    }
    return;
  }

  // dari sini: failsafe aktif
  g_active = true;

  // butuh suhu valid; kalau tidak ada, tahan state saat ini (hormati MIN_ON/OFF)
  if (!isfinite(avgTemp)) {
    return;
  }

  // state machine dengan hysteresis
  switch (g_mode) {
    case Mode::IDLE:
      if (avgTemp > (CFG.HARD_MAX + CFG.HYST)) {
        applyMode(Mode::COOLING);
      } else if (avgTemp < (CFG.HARD_MIN - CFG.HYST)) {
        applyMode(Mode::HEATING);
      }
      break;

    case Mode::COOLING:
      // kembali normal bila cukup turun
      if (avgTemp <= (CFG.HARD_MAX - CFG.HYST)) {
        applyMode(Mode::IDLE);
      }
      // kalau terlalu dingin, switch ke heating
      if (avgTemp < (CFG.HARD_MIN - CFG.HYST)) {
        applyMode(Mode::HEATING);
      }
      break;

    case Mode::HEATING:
      // kembali normal bila cukup naik
      if (avgTemp >= (CFG.HARD_MIN + CFG.HYST)) {
        applyMode(Mode::IDLE);
      }
      // kalau terlalu panas, switch ke cooling
      if (avgTemp > (CFG.HARD_MAX + CFG.HYST)) {
        applyMode(Mode::COOLING);
      }
      break;
  }
}

void forceOff() {
  g_active = false;
  // langsung reset aktuator ke aman
  g_mode = Mode::IDLE;
  g_lastSwitch = millis();
  LampService::setLamp1(0.0f);
  LampService::setLamp2(0.0f);
  LampService::setMaster(false);
  Logger::info("[FAILSAFE] forceOff -> IDLE");
}

} // namespace Failsafe