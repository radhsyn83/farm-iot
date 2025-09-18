#pragma once

#include <Arduino.h>

// ========= WiFi =========
static const char* WIFI_SSID     = "Wokwi-GUEST";
static const char* WIFI_PASSWORD = "";

// ========= MQTT =========
static const char* MQTT_HOST = "192.168.1.10"; // broker
static const uint16_t MQTT_PORT = 1883;
static const char* MQTT_USER = "";   // optional
static const char* MQTT_PASS = "";   // optional
static const char* DEVICE_ID = "doc-kandang-01";
static const char* SITE_NS   = "farm/doc1";

// ========= Telemetry Interval =========
static const uint32_t TELEMETRY_MS = 5000;

// ========= Sensor =========
#define DHTTYPE DHT22

// ========= Lamp Mode =========
// Tambahkan ROBOTDYN_AC di enum
enum class LampMode { RELAY, PWM_DC, ROBOTDYN_AC };

// Pilih mode di sini (ganti sesuai kebutuhan)
static const LampMode LAMP_MODE = LampMode::ROBOTDYN_AC;

// ========= API =========
static const char* API_BASE_URL   = "http://192.168.1.20:8000";
static const char* API_NOTIF_PATH = "/api/notify";
static const uint32_t API_TIMEOUT_MS = 4000;
