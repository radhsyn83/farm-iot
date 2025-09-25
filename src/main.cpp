#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "pins.h"
#include "topics.h"
#include "services/WiFiService.h"
#include "services/MqttService.h"
#include "services/SensorService.h"
#include "services/LampService.h"
#include "services/ApiService.h"
#include "services/StateService.h"
#include "helpers/Logger.h"

// ====== Globals ======
static float g_temp1 = NAN;
static float g_temp2 = NAN;
static float g_hum1 = NAN;
static float g_hum2 = NAN;
static float g_setpoint = 32.0;
static uint32_t lastTelemetry = 0;

// (Opsional) tombol reset kredensial WiFi
#define RESET_BTN_PIN   0   // ganti sesuai hardware
static bool btnPressed = false;
static unsigned long btnPressStart = 0;

// ========== MQTT HANDLER ==========
static void handleMqttMessage(const String& topic, const String& payload) {
  Logger::info("Got MQTT message %s: %s", topic.c_str(), payload.c_str());

  // gunakan DynamicJsonDocument biar fleksibel
  DynamicJsonDocument doc(384);
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Logger::warn("JSON parse error (%s): %s", err.c_str(), payload.c_str());
    return;
  }

  // ====== Lamp 1 ======
  if (topic == tCmdLamp1()) {
    float p = NAN;
    if (doc.is<JsonObject>() && !doc["power"].isNull()) {
      p = doc["power"].as<float>();
    } else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) {
      p = doc.as<float>();
    }

    if (!isnan(p)) {
      p = constrain(p, 0.0f, 1.0f);
      LampService::setLamp1(p);
      Logger::info("Lamp1 set to %.2f", p);
    }
  }

  // ====== Lamp 2 ======
  else if (topic == tCmdLamp2()) {
    float p = NAN;
    if (doc.is<JsonObject>() && !doc["power"].isNull()) {
      p = doc["power"].as<float>();
    } else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) {
      p = doc.as<float>();
    }

    if (!isnan(p)) {
      p = constrain(p, 0.0f, 1.0f);
      LampService::setLamp2(p);
      Logger::info("Lamp2 set to %.2f", p);
    }
  }

  // ====== Master Power ======
  else if (topic == tCmdPowerMaster()) {
    bool on = false;
    bool valid = false;

    if (doc.is<JsonObject>() && !doc["on"].isNull()) {
      on = doc["on"].as<bool>();
      valid = true;
    } else if (doc.is<bool>()) {
      on = doc.as<bool>();
      valid = true;
    } else if (doc.is<int>()) {
      on = doc.as<int>() != 0;
      valid = true;
    }

    if (valid) {
      LampService::setMaster(on);
      Logger::info("Master set to %s", on ? "ON" : "OFF");
    }
  }

  // ====== Temperature Setpoint ======
  else if (topic == tCmdSetpoint()) {
    float t = NAN;
    if (doc.is<JsonObject>() && !doc["t"].isNull()) {
      t = doc["t"].as<float>();
    } else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) {
      t = doc.as<float>();
    }

    if (!isnan(t)) {
      g_setpoint = t;
      StateService::saveSetpoint(g_setpoint);
      Logger::info("New setpoint = %.2f (saved)", g_setpoint);
    }
  }
}


// ========== PUBLISH TELEMETRY ==========
static void publishTelemetry() {
  if (!MqttService::connected()) return;

  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["sensors"]["dht1"]["t"] = g_temp1;
  doc["sensors"]["dht1"]["h"] = g_hum1;
  doc["sensors"]["dht2"]["t"] = g_temp2;
  doc["sensors"]["dht2"]["h"] = g_hum2;
  doc["lamps"]["lamp1"]["power"] = LampService::getLamp1().power;
  doc["lamps"]["lamp2"]["power"] = LampService::getLamp2().power;
  doc["power_master"] = LampService::getMaster();
  doc["option"]["dht_setpoint"] = g_setpoint;
  doc["ts"] = millis();

  String out;
  serializeJson(doc, out);
  MqttService::publish(tTelemetry(), out, false);

  Logger::info("Telemetry published: %s", out.c_str());
}

// ========== SETUP ==========
void setup() {
  delay(1000);
  Logger::begin();

  // (Opsional) tombol reset
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  // Urutan init:
  // 1) WiFi (akan coba kredensial tersimpan, kalau gagal -> provisioning AP)
  WiFiService::begin();

  // 2) State/sensor/actuator
  StateService::begin();
  SensorService::begin();
  LampService::begin();

  // 3) Load setpoint tersimpan
  g_setpoint = StateService::loadSetpoint();
  Logger::info("Loaded setpoint=%.2f", g_setpoint);

  // 4) MQTT (boleh dipanggil di awal; di dalamnya pastikan handle reconnect)
  MqttService::begin(handleMqttMessage);
}

// ========== LOOP ==========
void loop() {
  // Penting: supaya HTTP provisioning jalan
  WiFiService::loop();

  // MQTT loop (reconnect otomatis kalau WiFi sudah connected)
  if (WiFiService::isConnected() && !MqttService::connected()) {
    MqttService::reconnect(); // pastikan MqttService ada fungsi ini
  }
  MqttService::loop();

  // (Opsional) Long-press tombol 5 detik untuk reset WiFi
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    if (!btnPressed) { btnPressed = true; btnPressStart = millis(); }
    else if (millis() - btnPressStart > 5000) {
      Logger::warn("Long press detected: resetting WiFi credentials");
      WiFiService::resetCredentials();
      ESP.restart();
    }
  } else {
    btnPressed = false;
  }

  // Baca sensor sekali per loop
  bool ok = SensorService::readTemps(g_temp1, g_temp2, g_hum1, g_hum2);
//   if (!ok) {
//     Logger::warn("Failed to read temperature sensors");
//   }

  // Kirim telemetry periodik
  if (millis() - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = millis();

    // if (WiFiService::hasInternetPing()) {
    //   Logger::info("Internet access OK");
    // } else {
    //   Logger::warn("No internet access");
    // }

    publishTelemetry();
  }
}
