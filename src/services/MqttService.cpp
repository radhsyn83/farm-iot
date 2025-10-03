#include "MqttService.h"
#include <ArduinoJson.h>
#include "../config.h"          // DEVICE_ID, MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS
#include "../helpers/Logger.h"
#include "WiFiService.h"
#include "LampService.h"
#include "StateService.h"

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

const uint32_t MqttService::RECONNECT_MS  = 3000;   // retry tiap 3 detik
uint32_t       MqttService::lastReconnectAttempt = 0;

const uint32_t MqttService::HEARTBEAT_MS  = 30000;  // 30 detik
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
  _net.setInsecure();          // gampang dulu; kalau punya CA, ganti setCACert(...)
#endif

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(onMqttMessage);
  client.setSocketTimeout(5);  // detik; biar gak nge-freeze lama

  lastReconnectAttempt = 0;
  lastHeartbeat = 0;

  // coba connect awal kalau WiFi sudah ada
  if (WiFiService::isConnected()) {
    reconnect();
  }
}

// ====== loop ======
void MqttService::loop() {
  // mqtt loop (non-blocking cepat)
  if (client.connected()) {
    client.loop();

    // heartbeat
    uint32_t now = millis();
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
      lastHeartbeat = now;
      publishHeartbeat();
    }
  } else {
    // throttle reconnect (tiap 3 detik)
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

  // LWT (QoS publish di PubSubClient = QoS0, tapi LWT flag masih bisa diberikan)
  StaticJsonDocument<64> will;
  will["state"] = "offline";
  String willStr; serializeJson(will, willStr);

  Logger::info("MQTT reconnect to %s:%d ...", MQTT_HOST, MQTT_PORT);

  // Catatan: PubSubClient publish hanya QoS0. LWT qos param tetap diterima broker.
  bool ok;
  if (strlen(MQTT_USER)) {
    ok = client.connect(
      DEVICE_ID,
      MQTT_USER, MQTT_PASS,
      tState().c_str(),
      1 /*willQos*/,
      true /*willRetain*/,
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

    // subscribe desired + cmd (PubSubClient subscribe QoS0 saja, gapapa)
    subscribeTopic(tDesiredNS() + "#");
    subscribeTopic(tBase() + "cmd/#");

    // birth online (retained)
    StaticJsonDocument<64> birth;
    birth["state"] = "online";
    String s; serializeJson(birth, s);
    publish(tState(), s, true);

    // request sync desired
    publish(tCmdGet(), "", false);

    // kick heartbeat
    lastHeartbeat = millis() - HEARTBEAT_MS;
  } else {
    Logger::warn("MQTT connect failed, state=%d", client.state());
  }
}

// ====== message callback ======
void MqttService::onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String pl; pl.reserve(length);
  for (unsigned int i=0; i<length; i++) pl += (char)payload[i];

  Logger::info("MQTT msg %s: %s", t.c_str(), pl.c_str());
  if (onMessage) onMessage(t, pl);
}

// ====== publish / subscribe ======
void MqttService::publish(const String& topic, const String& payload, bool retain) {
  if (!client.connected()) return;
  // PubSubClient hanya QoS0
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
  d["fw"]            = "1.0.0";
  d["claimed"]       = false;

  String out; serializeJson(d, out);
  publish(tHeartbeat(), out, /*retain=*/false);
  Logger::info("Heartbeat: %s", out.c_str());
}

// ====== reported helpers ======
void MqttService::publishReportedLamp1() {
  StaticJsonDocument<64> d;
  d["id"]    = "lamp1";
  d["power"] = LampService::getLamp1().power;
  String out; serializeJson(d, out);
  publish(tReportedNS() + "lamp1", out, /*retain=*/true);
}
void MqttService::publishReportedLamp2() {
  StaticJsonDocument<64> d;
  d["id"]    = "lamp2";
  d["power"] = LampService::getLamp2().power;
  String out; serializeJson(d, out);
  publish(tReportedNS() + "lamp2", out, /*retain=*/true);
}
void MqttService::publishReportedMaster() {
  StaticJsonDocument<48> d;
  d["on"] = LampService::getMaster();
  String out; serializeJson(d, out);
  publish(tReportedNS() + "power_master", out, /*retain=*/true);
}
void MqttService::publishReportedSetpoint() {
  StaticJsonDocument<48> d;
  d["t"] = StateService::loadSetpoint();
  String out; serializeJson(d, out);
  publish(tReportedNS() + "setpoint", out, /*retain=*/true);
}
void MqttService::publishReportedSnapshot() {
  StaticJsonDocument<256> d;
  d["power_master"] = LampService::getMaster();
  JsonObject l1 = d.createNestedObject("lamp1"); l1["power"] = LampService::getLamp1().power;
  JsonObject l2 = d.createNestedObject("lamp2"); l2["power"] = LampService::getLamp2().power;
  d["setpoint"] = StateService::loadSetpoint();
  String out; serializeJson(d, out);
  publish(tReportedNS() + "snapshot", out, /*retain=*/true);
}