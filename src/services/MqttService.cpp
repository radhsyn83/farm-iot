#include "MqttService.h"
#include "../config.h"          // DEVICE_ID, MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS
#include "../helpers/Logger.h"
#include "WiFiService.h"
#include "LampService.h"
#include "StateService.h"

#ifndef USE_TLS
#define USE_TLS 1               // set 0 kalau broker non-TLS
#endif

// ===== Statics =====
AsyncMqttClient MqttService::client;
Ticker MqttService::reconnectTimer;
Ticker MqttService::heartbeatTimer;
MqttMessageHandler MqttService::onMessage = nullptr;

// ===== Topics util =====
String MqttService::tBase()        { return "esp32/" + String(DEVICE_ID) + "/"; }
String MqttService::tState()       { return tBase() + "status/online"; }
String MqttService::tHeartbeat()   { return tBase() + "heartbeat"; }
String MqttService::tCmdGet()      { return tBase() + "cmd/get"; }
String MqttService::tDesiredNS()   { return tBase() + "desired/"; }
String MqttService::tReportedNS()  { return tBase() + "reported/"; }

// ===== Internal helpers =====
void MqttService::scheduleReconnect(uint32_t ms) {
  reconnectTimer.detach();
  reconnectTimer.once_ms(ms, []() { MqttService::reconnect(); });
}

void MqttService::onWifiReady() {
  if (WiFiService::isConnected() && !client.connected()) {
    reconnect();
  }
}

// ===== Public API =====
void MqttService::begin(MqttMessageHandler handler) {
  onMessage = handler;

  // TLS
#if USE_TLS
  client.setSecure(true);
  // Kalau tidak punya CA / pakai server publik, pakai insecure:
  client.setInsecure(true);
#else
  client.setSecure(false);
#endif

  // Broker & identitas
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setClientId(DEVICE_ID);
  if (strlen(MQTT_USER)) client.setCredentials(MQTT_USER, MQTT_PASS);

  // Clean Session & KeepAlive
  client.setCleanSession(false);   // sesi persisten
  client.setKeepAlive(20);

  // LWT: offline (retained, QoS1)
  {
    StaticJsonDocument<64> will;
    will["state"] = "offline";
    String s; serializeJson(will, s);
    client.setWill(tState().c_str(), 1 /*QoS*/, true /*retain*/, s.c_str(), s.length());
  }

  // Callbacks
  client.onConnect(onConnect);
  client.onDisconnect(onDisconnect);
  client.onMessage(onMessageCb);

  // Kick connect kalau WiFi sudah siap
  onWifiReady();

  // Heartbeat timer – akan diaktifkan setelah connect
  heartbeatTimer.detach();
}

void MqttService::loop() {
  // Async — tidak perlu apa-apa. Tetap disediakan agar kompatibel.
}

bool MqttService::connected() {
  return client.connected();
}

void MqttService::reconnect() {
  if (!WiFiService::isConnected()) {
    scheduleReconnect(1000);
    return;
  }
  Logger::info("MQTT connecting to %s:%d (AsyncMqttClient)...", MQTT_HOST, MQTT_PORT);
  client.connect();
}

// ===== MQTT Callbacks =====
void MqttService::onConnect(bool sessionPresent) {
  Logger::info("MQTT connected (sessionPresent=%d)", sessionPresent);

  // 1) Subscribe namespace dulu (QoS1)
  subscribeTopic(tDesiredNS() + "#", 1);
  subscribeTopic(tBase() + "cmd/#",    1);

  // 2) Birth online (retained, QoS1)
  {
    StaticJsonDocument<64> birth;
    birth["state"] = "online";
    String s; serializeJson(birth, s);
    publish(tState(), s, /*retain=*/true, /*qos=*/1);
  }

  // 3) Request sync desired (fallback)
  publish(tCmdGet(), "", /*retain=*/false, /*qos=*/1);

  // 4) Start heartbeat timer (30s)
  heartbeatTimer.detach();
  heartbeatTimer.once_ms(1000, [](){ MqttService::publishHeartbeat(); }); // kick awal
  heartbeatTimer.attach_ms(30000, [](){ MqttService::publishHeartbeat(); });
}

void MqttService::onDisconnect(AsyncMqttClientDisconnectReason reason) {
  Logger::warn("MQTT disconnected (reason=%d)", (int)reason);
  heartbeatTimer.detach();

  // Backoff sederhana
  static uint32_t backoff = 1000;           // 1s
  scheduleReconnect(backoff);
  backoff = min<uint32_t>(backoff * 2, 30000);
}

void MqttService::onMessageCb(char* topic, char* payload,
                              AsyncMqttClientMessageProperties props,
                              size_t len, size_t index, size_t total) {
  // payload bisa chunked; kita sambung manual
  static String bufTopic;
  static String bufPayload;

  if (index == 0) {
    bufTopic = String(topic);
    bufPayload.reserve(total);
    bufPayload = "";
  }
  bufPayload.concat(String(payload).substring(0, len)); // safe enough for text payload

  if (index + len == total) {
    Logger::info("MQTT msg %s: %s", bufTopic.c_str(), bufPayload.c_str());
    if (onMessage) onMessage(bufTopic, bufPayload);
    bufPayload = "";
    bufTopic = "";
  }
}

// ===== Publish / Subscribe =====
void MqttService::publish(const String& topic, const String& payload, bool retain, uint8_t qos) {
  if (!client.connected()) return;
  client.publish(topic.c_str(), qos, retain, payload.c_str(), payload.length(), false);
}

void MqttService::subscribeTopic(const String& topic, uint8_t qos) {
  if (!client.connected()) return;
  auto pkid = client.subscribe(topic.c_str(), qos);
  Logger::info("Subscribe %s (QoS=%d, pkid=%u)", topic.c_str(), qos, (unsigned)pkid);
}

// ===== Heartbeat =====
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
  publish(tHeartbeat(), out, /*retain=*/false, /*qos=*/0);  // telemetry QoS0 cukup
  Logger::info("Heartbeat: %s", out.c_str());
}

// ===== Reported helpers (retained, QoS1) =====
void MqttService::publishReportedLamp1() {
  StaticJsonDocument<64> d;
  d["id"]    = "lamp1";
  d["power"] = LampService::getLamp1().power;
  String out; serializeJson(d, out);
  publish(tReportedNS() + "lamp1", out, /*retain=*/true, /*qos=*/1);
}
void MqttService::publishReportedLamp2() {
  StaticJsonDocument<64> d;
  d["id"]    = "lamp2";
  d["power"] = LampService::getLamp2().power;
  String out; serializeJson(d, out);
  publish(tReportedNS() + "lamp2", out, /*retain=*/true, /*qos=*/1);
}
void MqttService::publishReportedMaster() {
  StaticJsonDocument<48> d;
  d["on"] = LampService::getMaster();
  String out; serializeJson(d, out);
  publish(tReportedNS() + "power_master", out, /*retain=*/true, /*qos=*/1);
}
void MqttService::publishReportedSetpoint() {
  StaticJsonDocument<48> d;
  d["t"] = StateService::loadSetpoint();
  String out; serializeJson(d, out);
  publish(tReportedNS() + "setpoint", out, /*retain=*/true, /*qos=*/1);
}
void MqttService::publishReportedSnapshot() {
  StaticJsonDocument<256> d;
  d["power_master"] = LampService::getMaster();
  JsonObject l1 = d.createNestedObject("lamp1"); l1["power"] = LampService::getLamp1().power;
  JsonObject l2 = d.createNestedObject("lamp2"); l2["power"] = LampService::getLamp2().power;
  d["setpoint"] = StateService::loadSetpoint();
  String out; serializeJson(d, out);
  publish(tReportedNS() + "snapshot", out, /*retain=*/true, /*qos=*/1);
}