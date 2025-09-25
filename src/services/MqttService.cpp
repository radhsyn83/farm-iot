#include "MqttService.h"
#include "../config.h"
#include "../helpers/Logger.h"
#include "WiFiService.h"    // <— supaya bisa cek WiFiService::isConnected()

// ===== Static members =====
WiFiClientSecure MqttService::wifiClient;
PubSubClient MqttService::client(MQTT_HOST, MQTT_PORT, MqttService::callback, MqttService::wifiClient);
MqttMessageHandler MqttService::onMessage = nullptr;
unsigned long MqttService::lastReconnectAttempt = 0;
uint32_t MqttService::reconnectBackoffMs = 1000;  // start 1s, max 30s
unsigned long MqttService::lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 30000; // 30 detik

void MqttService::begin(MqttMessageHandler handler) {
  onMessage = handler;

  wifiClient.setInsecure();


  // PubSubClient tuning
  client.setBufferSize(2048);  // jika payload telemetry bisa “gemuk”
  client.setKeepAlive(20);
  client.setSocketTimeout(5);  // detik

  // Note: kalau kamu pakai konstruktor tanpa host/port, bisa panggil:
  // client.setServer(MQTT_HOST, MQTT_PORT);
  // client.setCallback(MqttService::callback);
}

void MqttService::loop() {
  // Jangan coba MQTT kalau Wi-Fi belum connect
  if (!WiFiService::isConnected()) {
    // reset backoff biar cepat coba lagi ketika Wi-Fi balik normal
    reconnectBackoffMs = 1000;
    return;
  }

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt >= reconnectBackoffMs) {
      lastReconnectAttempt = now;
      reconnect();
      // Exponential backoff (maks 30s) + jitter kecil (0–200ms)
      if (!client.connected()) {
        reconnectBackoffMs = min<uint32_t>(reconnectBackoffMs * 2, 30000);
        reconnectBackoffMs += (uint32_t)random(0, 200);
      } else {
        reconnectBackoffMs = 1000; // reset kalau sukses
      }
    }
  } else {
    client.loop(); // proses keep-alive & callback

    // 🔹 Heartbeat
    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
      lastHeartbeat = millis();
      publishHeartbeat();
    }
  }
}

bool MqttService::connected() { return client.connected(); }

void MqttService::publish(const String& topic, const String& payload, bool retain) {
  if (connected()) {
    // PubSubClient hanya QoS0 untuk publish; pastikan payload < bufferSize
    if (!client.publish(topic.c_str(), payload.c_str(), retain)) {
      Logger::warn("MQTT publish failed (topic=%s, len=%u)", topic.c_str(), payload.length());
    }
  }
}

void MqttService::subscribeTopic(const String& topic) {
  if (connected()) {
    if (client.subscribe(topic.c_str())) {
      Logger::info("MQTT subscribed: %s", topic.c_str());
    } else {
      Logger::warn("MQTT subscribe failed: %s", topic.c_str());
    }
  }
}

void MqttService::callback(char* topic, byte* payload, unsigned int length) {
  String t(topic);
  String pl;
  pl.reserve(length);
  for (unsigned int i = 0; i < length; i++) pl += (char)payload[i];

  Logger::info("MQTT msg on %s: %s", t.c_str(), pl.c_str());
  if (onMessage) onMessage(t, pl);
}

void MqttService::reconnect() {
  Logger::info("Attempting MQTT TLS connection to %s:%d ...", MQTT_HOST, MQTT_PORT);

  // (Opsional) SNI—kebanyakan broker modern butuh ini aktif
  wifiClient.setHandshakeTimeout(15);
  // WiFiClientSecure di ESP32 set SNI otomatis sesuai host yang dipakai di connect(),
  // jadi pastikan MQTT_HOST adalah hostname (bukan IP) — sudah benar di config.h.

  // Last-Will & KeepAlive
  bool ok = client.connect(
    DEVICE_ID,             // clientId
    MQTT_USER,             // username
    MQTT_PASS,             // password
    tState().c_str(),      // willTopic
    1,                     // willQos (PubSubClient: hanya dipakai untuk LWT)
    true,                  // willRetain
    "offline",             // willMessage
    20                     // keepAlive (detik)
  );

  if (ok) {
    Logger::info("MQTT connected");
    // Subscribe channel command
    subscribeTopic(tCmdLamp1());
    subscribeTopic(tCmdLamp2());
    subscribeTopic(tCmdPowerMaster());
    subscribeTopic(tCmdSetpoint());

    // Publish ONLINE state (retained)
    publish(tState(), "online", true);
  } else {
    Logger::warn("MQTT connect failed, rc=%d", client.state());
  }
}

void MqttService::publishHeartbeat() {
    if (!connected()) return;

    JsonDocument doc;
    doc["device"]    = DEVICE_ID;
    doc["ts"]        = millis();
    doc["uptime_s"]  = millis() / 1000;
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"]   = WiFi.localIP().toString();
    doc["wifi"]["rssi"] = WiFi.RSSI();
    doc["sys"]["heap_free"] = ESP.getFreeHeap();
    doc["sys"]["cpu_mhz"]   = ESP.getCpuFreqMHz();
    doc["sys"]["temp_c"]    = temperatureRead();
    doc["fw"] = "1.0.0"; // bisa ambil dari config.h
    doc["claimed"] = false; // atau ambil dari StateService kalau ada

    String out;
    serializeJson(doc, out);
    publish(tHeartbeat(), out, false);
    Logger::info("Heartbeat published: %s", out.c_str());
  }
