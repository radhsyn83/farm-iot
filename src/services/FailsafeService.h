#pragma once
#include <Arduino.h>

namespace Failsafe {

struct Config {
  float HARD_MIN = 20.0f;   // °C: darurat bawah (heater ON)
  float HARD_MAX = 38.0f;   // °C: darurat atas (cooling ON)
  float HYST     = 1.0f;    // °C: deadband agar tidak flip-flop

  uint32_t NET_DEAD_MS = 30 * 1000UL; // MQTT/WiFi mati berapa lama baru failsafe aktif
  uint32_t MIN_ON_MS   = 20 * 1000UL; // minimal ON sebelum boleh OFF
  uint32_t MIN_OFF_MS  = 20 * 1000UL; // minimal OFF sebelum boleh ON lagi
};

enum class Mode : uint8_t {
  IDLE = 0,       // tidak darurat
  COOLING,        // kipas ON
  HEATING        // pemanas ON
};

void begin(const Config& cfg = Config{});

// panggil tiap loop, kirim:
// - avgTemp: rata-rata suhu yang kamu hitung (NaN kalau gak ada valid)
// - mqttOk : true kalau MQTT connected (boleh pakai MqttService::connected())
// - wifiOk : true kalau WiFi connected (WiFiService::isConnected())
void tick(float avgTemp, bool mqttOk, bool wifiOk);

// untuk debug/telemetry
Mode currentMode();
bool  isActive();    // true kalau failsafe sedang override
uint32_t lastChangeMs();

// optional: paksa off (misalnya saat shutdown normal)
void forceOff();

} // namespace Failsafe