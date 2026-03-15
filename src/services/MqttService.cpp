#include "MqttService.h"
#include <ArduinoJson.h>
#include "../config.h"
#include "../helpers/Logger.h"
#include "WiFiService.h"
#include "LampService.h"
#include "StateService.h"
#include "PeripheralManager.h"

#if USE_TLS
  #include <WiFiClientSecure.h>
  static WiFiClientSecure _net;
#else
  #include <WiFiClient.h>
  static WiFiClient _net;
#endif

// ====== statics ======
PubSubClient MqttService::client(_net);
MqttMessageHandler MqttService::onMessage = nullptr;

const uint32_t MqttService::RECONNECT_MS  = 3000;
uint32_t       MqttService::lastReconnectAttempt = 0;

const uint32_t MqttService::HEARTBEAT_MS  = 30000;
uint32_t       MqttService::lastHeartbeat = 0;

// ====== topics util ======
String MqttService::tBase()        { return "esp32/" + String(DEVICE_ID) + "/"; }
String MqttService::tState()       { return tBase() + "status/online"; }
String MqttService::tHeartbeat()   { return tBase() + "heartbeat"; }
String MqttService::tCmdGet()      { return tBase() + "cmd/get"; }
String MqttService::tDesiredNS()   { return tBase() + "desired/"; }
String MqttService::tReportedNS()  { return tBase() + "reported/"; }

// ====== begin ======
void MqttService::begin(MqttMessageHandler handler) {
    onMessage = handler;

#if USE_TLS
    _net.setInsecure();
#endif

    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setCallback(onMqttMessage);
    client.setSocketTimeout(5);

    lastReconnectAttempt = 0;
    lastHeartbeat = 0;

    if (WiFiService::isConnected()) {
        reconnect();
    }
}

// ====== loop ======
void MqttService::loop() {
    if (client.connected()) {
        client.loop();
        uint32_t now = millis();
        if (now - lastHeartbeat >= HEARTBEAT_MS) {
            lastHeartbeat = now;
            publishHeartbeat();
        }
    } else {
        uint32_t now = millis();
        if (WiFiService::isConnected() && (now - lastReconnectAttempt >= RECONNECT_MS)) {
            lastReconnectAttempt = now;
            reconnect();
        }
    }
}

bool MqttService::connected() {
    return client.connected();
}

// ====== reconnect ======
void MqttService::reconnect() {
    if (!WiFiService::isConnected()) return;

    StaticJsonDocument<64> will;
    will["state"] = "offline";
    String willStr; serializeJson(will, willStr);

    Logger::info("MQTT reconnect to %s:%d ...", MQTT_HOST, MQTT_PORT);

    bool ok;
    if (strlen(MQTT_USER)) {
        ok = client.connect(
            DEVICE_ID,
            MQTT_USER, MQTT_PASS,
            tState().c_str(),
            1, true,
            willStr.c_str()
        );
    } else {
        ok = client.connect(
            DEVICE_ID,
            tState().c_str(),
            1, true,
            willStr.c_str()
        );
    }

    if (ok) {
        Logger::info("MQTT connected");

        subscribeTopic(tDesiredNS() + "#");
        subscribeTopic(tBase() + "cmd/#");

        StaticJsonDocument<64> birth;
        birth["state"] = "online";
        String s; serializeJson(birth, s);
        publish(tState(), s, true);

        publish(tCmdGet(), "", false);

        lastHeartbeat = millis() - HEARTBEAT_MS;
    } else {
        Logger::warn("MQTT connect failed, state=%d", client.state());
    }
}

// ====== message callback ======
void MqttService::onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String t(topic);
    String pl; pl.reserve(length);
    for (unsigned int i = 0; i < length; i++) pl += (char)payload[i];

    Logger::info("MQTT msg %s: %s", t.c_str(), pl.c_str());
    if (onMessage) onMessage(t, pl);
}

// ====== publish / subscribe ======
void MqttService::publish(const String& topic, const String& payload, bool retain) {
    if (!client.connected()) return;
    if (!client.publish(topic.c_str(), payload.c_str(), retain)) {
        Logger::warn("MQTT publish failed: %s (len=%u)", topic.c_str(), payload.length());
    }
}

void MqttService::subscribeTopic(const String& topic) {
    if (!client.connected()) return;
    if (!client.subscribe(topic.c_str())) {
        Logger::warn("MQTT subscribe failed: %s", topic.c_str());
    } else {
        Logger::info("MQTT subscribed: %s", topic.c_str());
    }
}

// ====== heartbeat ======
static String ipToString(IPAddress ip) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return String(buf);
}

void MqttService::publishHeartbeat() {
    if (!client.connected()) return;
    StaticJsonDocument<256> d;
    d["device"]        = DEVICE_ID;
    d["ts"]            = millis();
    d["uptime_s"]      = millis()/1000;
    d["wifi"]["ssid"]  = WiFi.SSID();
    d["wifi"]["ip"]    = ipToString(WiFi.localIP());
    d["wifi"]["rssi"]  = WiFi.RSSI();
    d["sys"]["heap_free"] = ESP.getFreeHeap();
    d["sys"]["cpu_mhz"]   = ESP.getCpuFreqMHz();
    d["sys"]["temp_c"]    = temperatureRead();
    d["fw"]            = "2.0.0";
    d["claimed"]       = false;

    String out; serializeJson(d, out);
    publish(tHeartbeat(), out, false);
    Logger::info("Heartbeat: %s", out.c_str());
}

// ====== reported helpers (dynamic) ======
void MqttService::publishReportedLamp(const char* lampId) {
    LampState st;
    if (!LampService::getLampById(lampId, st)) return;

    StaticJsonDocument<96> d;
    d["id"]    = lampId;
    d["power"] = st.power;
    String out; serializeJson(d, out);
    publish(tReportedNS() + String(lampId), out, true);
}

void MqttService::publishReportedAllLamps() {
    const auto& cfg = PeripheralManager::config();
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        publishReportedLamp(cfg.lamps[i].id);
    }
}

void MqttService::publishReportedMaster() {
    StaticJsonDocument<48> d;
    d["on"] = LampService::getMaster();
    String out; serializeJson(d, out);
    publish(tReportedNS() + "power_master", out, true);
}

void MqttService::publishReportedSetpoint() {
    StaticJsonDocument<48> d;
    d["t"] = StateService::loadSetpoint();
    String out; serializeJson(d, out);
    publish(tReportedNS() + "setpoint", out, true);
}

void MqttService::publishReportedSnapshot() {
    const auto& cfg = PeripheralManager::config();
    StaticJsonDocument<512> d;

    d["power_master"] = LampService::getMaster();

    JsonArray lamps = d.createNestedArray("lamps");
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        JsonObject l = lamps.createNestedObject();
        l["id"]    = cfg.lamps[i].id;
        l["power"] = LampService::getLamp(i).power;
    }

    d["setpoint"] = StateService::loadSetpoint();

    String out; serializeJson(d, out);
    publish(tReportedNS() + "snapshot", out, true);
}

void MqttService::publishReportedConfig() {
    String configJson = PeripheralManager::configToJson();
    publish(tReportedNS() + "config", configJson, true);
}

void MqttService::publishReportedConfigFull(const FailsafeParams& fs) {
    StaticJsonDocument<1024> d;

    // Peripheral config
    const auto& cfg = PeripheralManager::config();
    JsonArray sensors = d.createNestedArray("sensors");
    for (uint8_t i = 0; i < cfg.sensorCount; i++) {
        JsonObject s = sensors.createNestedObject();
        s["id"]   = cfg.sensors[i].id;
        s["pin"]  = cfg.sensors[i].pin;
        s["type"] = cfg.sensors[i].type;
    }
    JsonArray lamps = d.createNestedArray("lamps");
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        JsonObject l = lamps.createNestedObject();
        l["id"]   = cfg.lamps[i].id;
        l["pin"]  = cfg.lamps[i].pin;
        switch (cfg.lamps[i].mode) {
            case LampMode::RELAY:        l["mode"] = "relay"; break;
            case LampMode::PWM_DC:       l["mode"] = "pwm"; break;
            case LampMode::ROBOTDYN_AC:  l["mode"] = "ac_dimmer"; break;
        }
    }
    d["masterRelayPin"] = cfg.masterRelayPin;
    d["version"]        = cfg.version;

    // Failsafe params (merged)
    d["failsafe_auto"]  = fs.fsAuto;
    d["failsafe_mode"]  = fs.fsMode;
    d["hard_min"]       = fs.hardMin;
    d["hard_max"]       = fs.hardMax;
    d["hyst"]           = fs.hyst;
    d["dht_setpoint"]   = fs.setpoint;

    String out; serializeJson(d, out);
    publish(tReportedNS() + "config", out, true);
}
