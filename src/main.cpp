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

static float g_temp1 = NAN;
static float g_temp2 = NAN;
static float g_setpoint = 32.0;
static uint32_t lastTelemetry = 0;

// ========== MQTT HANDLER ==========
static void handleMqttMessage(const String& topic, const String& payload) {
  Logger::info("Got MQTT message %s: %s", topic.c_str(), payload.c_str());

  // Lamp1
  if (topic == tCmdLamp1()) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      if (doc["on"].is<bool>()) {
        LampService::switchLamp1(doc["on"]);
      }
      if (doc["power"].is<int>()) {
        LampService::setLamp1(doc["power"]);
      }
    }
  }

  // Lamp2
  else if (topic == tCmdLamp2()) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      if (doc["on"].is<bool>()) {
        LampService::switchLamp2(doc["on"]);
      }
      if (doc["power"].is<int>()) {
        LampService::setLamp2(doc["power"]);
      }
    }
  }

  // Master Power
  else if (topic == tCmdPowerMaster()) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      if (doc["on"].is<bool>()) {
        LampService::setMaster(doc["on"]);
      }
    }
  }

  // Setpoint
  else if (topic == tCmdSetpoint()) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      if (doc["t"].is<float>()) { // Use .is<T>() to check for a float value
        g_setpoint = doc["t"];
        StateService::saveSetpoint(g_setpoint);
        Logger::info("New setpoint=%.2f (saved)", g_setpoint);
      }
    }
  }
}

// ========== PUBLISH TELEMETRY ==========
static void publishTelemetry() {
  if (!MqttService::connected()) return;

  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["temp1"] = g_temp1;
  doc["temp2"] = g_temp2;
  doc["lamp1"]["on"] = LampService::getLamp1().on;
  doc["lamp1"]["power"] = LampService::getLamp1().power;
  doc["lamp2"]["on"] = LampService::getLamp2().on;
  doc["lamp2"]["power"] = LampService::getLamp2().power;
  doc["power_master"] = LampService::getMaster();
  doc["setpoint"] = g_setpoint;
  doc["ts"] = millis();

  String out;
  serializeJson(doc, out);
  MqttService::publish(tTelemetry(), out, false, 0);

  Logger::info("Telemetry published: %s", out.c_str());
}

// ========== SETUP ==========
void setup() {
  delay(1000);
  Logger::info("Booting ESP32 Brooder Controller...");

  WiFiService::begin();
  StateService::begin();
  SensorService::begin();
  LampService::begin();

  g_setpoint = StateService::loadSetpoint();
  Logger::info("Loaded setpoint=%.2f", g_setpoint);

  MqttService::begin(handleMqttMessage);
}

// ========== LOOP ==========
void loop() {
  WiFiService::loop();
  MqttService::loop();

  // Read sensors
  if (SensorService::readTemps(g_temp1, g_temp2)) {
    if (millis() - lastTelemetry > TELEMETRY_MS) {
      lastTelemetry = millis();
      publishTelemetry();
    }
  } else {
    Logger::warn("Failed to read temperature sensors");
  }
}