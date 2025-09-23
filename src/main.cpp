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

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        Logger::warn("Invalid JSON payload: %s", payload.c_str());
        return;
    }

    if (topic == tCmdLamp1() && doc["power"].is<float>()) {
        float p = constrain(doc["power"].as<float>(), 0.0, 1.0);
        LampService::setLamp1(p);
    }
    else if (topic == tCmdLamp2() && doc["power"].is<float>()) {
        float p = constrain(doc["power"].as<float>(), 0.0, 1.0);
        LampService::setLamp2(p);
    }
    else if (topic == tCmdPowerMaster() && doc["on"].is<bool>()) {
        LampService::setMaster(doc["on"]);
    }
    else if (topic == tCmdSetpoint() && doc["t"].is<float>()) {
        g_setpoint = doc["t"];
        StateService::saveSetpoint(g_setpoint);
        Logger::info("New setpoint=%.2f (saved)", g_setpoint);
    }
}

// ========== PUBLISH TELEMETRY ==========
static void publishTelemetry() {
    if (!MqttService::connected()) return;

    JsonDocument doc;
    doc["device"] = DEVICE_ID;
    doc["temp1"] = g_temp1;
    doc["temp2"] = g_temp2;
    doc["lamp1"]["power"] = LampService::getLamp1().power;
    doc["lamp2"]["power"] = LampService::getLamp2().power;
    doc["power_master"] = LampService::getMaster();
    doc["setpoint"] = g_setpoint;
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
    MqttService::loop();

    SensorService::readTemps(g_temp1, g_temp2);

    // if (SensorService::readTemps(g_temp1, g_temp2)) {
        if (millis() - lastTelemetry > TELEMETRY_MS) {
            lastTelemetry = millis();

            if (WiFiService::hasInternetPing()) {
                Logger::info("Internet access OK");
            } else {
                Logger::warn("No internet access");
            }

            publishTelemetry();
        }
    // } else {
        // Logger::warn("Failed to read temperature sensors");
    // }
}
