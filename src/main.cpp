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

  DynamicJsonDocument doc(384);
  DeserializationError err = deserializeJson(doc, payload);

  if (err && payload != "toggle") {
    Logger::warn("JSON parse error (%s): %s", err.c_str(), payload.c_str());
    return;
  }

  // ====== Lamp 1 ======
  if (topic == tCmdLamp1()) {
    bool toggle = false;
    float p = NAN;

    if (payload == "toggle") {
      toggle = true;
    } else if (doc.is<JsonObject>()) {
      if (!doc["toggle"].isNull() && doc["toggle"].as<bool>()) {
        toggle = true;
      } else if (!doc["power"].isNull()) {
        p = doc["power"].as<float>();
      }
    } else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) {
      p = doc.as<float>();
    }

    if (toggle) {
      float current = LampService::getLamp1().power;
      float newPower = (current >= 0.5f) ? 0.0f : 1.0f;
      LampService::setLamp1(newPower);
      Logger::info("Lamp1 toggled to %.2f", newPower);
    } else if (!isnan(p)) {
      p = constrain(p, 0.0f, 1.0f);
      LampService::setLamp1(p);
      Logger::info("Lamp1 set to %.2f", p);
    }
  }

  // ====== Lamp 2 ======
  else if (topic == tCmdLamp2()) {
    bool toggle = false;
    float p = NAN;

    if (payload == "toggle") {
      toggle = true;
    } else if (doc.is<JsonObject>()) {
      if (!doc["toggle"].isNull() && doc["toggle"].as<bool>()) {
        toggle = true;
      } else if (!doc["power"].isNull()) {
        p = doc["power"].as<float>();
      }
    } else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) {
      p = doc.as<float>();
    }

    if (toggle) {
      float current = LampService::getLamp2().power;
      float newPower = (current >= 0.5f) ? 0.0f : 1.0f;
      LampService::setLamp2(newPower);
      Logger::info("Lamp2 toggled to %.2f", newPower);
    } else if (!isnan(p)) {
      p = constrain(p, 0.0f, 1.0f);
      LampService::setLamp2(p);
      Logger::info("Lamp2 set to %.2f", p);
    }
  }

  // ====== Master Power ======
  else if (topic == tCmdPowerMaster()) {
    bool on = false;
    bool valid = false;
    bool toggle = false;

    if (payload == "toggle") {
      toggle = true;
    } else if (doc.is<JsonObject>()) {
      if (!doc["on"].isNull()) {
        on = doc["on"].as<bool>();
        valid = true;
      } else if (!doc["toggle"].isNull() && doc["toggle"].as<bool>()) {
        toggle = true;
      }
    } else if (doc.is<bool>()) {
      on = doc.as<bool>();
      valid = true;
    } else if (doc.is<int>()) {
      on = doc.as<int>() != 0;
      valid = true;
    }

    if (toggle) {
      bool current = LampService::getMaster();
      LampService::setMaster(!current);
      Logger::info("Master toggled to %s", !current ? "OFF" : "ON");
    } else if (valid) {
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

  // Sensor DHT array
  JsonArray sensors = doc.createNestedArray("sensors");

  JsonObject s1 = sensors.createNestedObject();
  s1["id"] = "dht1";
  s1["t"] = g_temp1;
  s1["h"] = g_hum1;

  JsonObject s2 = sensors.createNestedObject();
  s2["id"] = "dht2";
  s2["t"] = g_temp2;
  s2["h"] = g_hum2;

  // Lamps array
  JsonArray lamps = doc.createNestedArray("lamps");

  JsonObject l1 = lamps.createNestedObject();
  l1["id"] = "lamp1";
  l1["power"] = LampService::getLamp1().power;

  JsonObject l2 = lamps.createNestedObject();
  l2["id"] = "lamp2";
  l2["power"] = LampService::getLamp2().power;

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
  pinMode(WIFI_STATE, OUTPUT);
  digitalWrite(WIFI_STATE, LOW); // indikator WiFi mati

  // Urutan init:

  // 2) State/sensor/actuator
  StateService::begin();
  SensorService::begin();
  LampService::begin();
  
  // 1) WiFi (akan coba kredensial tersimpan, kalau gagal -> provisioning AP)
  WiFiService::begin();

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

  if (WiFiService::isConnected()) {
    digitalWrite(WIFI_STATE, HIGH); // indikator WiFi connected
  } else {
    digitalWrite(WIFI_STATE, LOW); // indikator WiFi mati
  }

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

    if (WiFiService::hasInternetPing()) {
      Logger::info("Internet access OK");
    } else {
      Logger::warn("No internet access");
    }

    publishTelemetry();
  }
}
