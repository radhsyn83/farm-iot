#pragma once

#include <Arduino.h>

// ========= WiFi =========
static const char* WIFI_SSID     = "WIFI-SYN";
static const char* WIFI_PASSWORD = "qwerty1234";

// ========= MQTT =========
static const char* MQTT_HOST = "f28a90877b0e429896dd24d452e4cb23.s1.eu.hivemq.cloud"; // broker
static const uint16_t MQTT_PORT = 8883;
static const char* MQTT_USER = "radhsyn83";   // optional
static const char* MQTT_PASS = "Sense324";   // optional
static const char* MQTT_PUB_STATUS = "esp32/status";
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
