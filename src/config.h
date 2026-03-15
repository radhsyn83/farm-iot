#pragma once

#include <Arduino.h>

// ========= WiFi =========
// static const char* WIFI_SSID     = "Annura";
static const char* WIFI_SSID     = "WIFI-SYN";
static const char* WIFI_PASSWORD = "qwerty1234";

// ========= MQTT =========
static const char* MQTT_HOST = "emqx.radhsyn83.dev"; // broker
static const uint16_t MQTT_PORT = 8883;
static const char* MQTT_USER = "radhsyn83";   // optional
static const char* MQTT_PASS = "Sense324";   // optional
static const char* DEVICE_ID = "doc-kandang-01";
static const char* SITE_NS   = "esp32";

// ========= Telemetry Interval =========
static const uint32_t TELEMETRY_MS = 5000;

// ========= NTP / Timezone =========
static const long NTP_OFFSET_SEC = 7 * 3600;  // GMT+7 (WIB Indonesia)
static const int  NIGHT_START_HOUR = 16;       // 4PM — heaters ON (all sensors fail fallback)
static const int  NIGHT_END_HOUR   = 10;       // 10AM — heaters OFF

// ========= Sensor =========
#define DHTTYPE DHT22

// ========= API =========
static const char* API_BASE_URL   = "http://192.168.1.20:8000";
static const char* API_NOTIF_PATH = "/api/notify";
static const uint32_t API_TIMEOUT_MS = 4000;

// ========== Provisioning ==========
static const char* PROVISION_AP_PASS = "sense324";   // password AP sementara (min 8 char). Kosongkan jika ingin open (tidak disarankan).
static const char* PROVISION_TOKEN   = "abcd1234";     // token sederhana utk POST /provision & /reset (opsional, bisa kosong)