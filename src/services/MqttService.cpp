#include "MqttService.h"
#include <WiFi.h>

AsyncMqttClient MqttService::client;
Ticker MqttService::reconnectTimer;
MqttMessageHandler MqttService::onMessage = nullptr;

void MqttService::begin(MqttMessageHandler handler) {
  onMessage = handler;
  client.setServer(MQTT_HOST, MQTT_PORT);
  if (strlen(MQTT_USER)) client.setCredentials(MQTT_USER, MQTT_PASS);
  client.onConnect([](bool sp){ MqttService::onMqttConnect(sp); });
  client.onDisconnect([](AsyncMqttClientDisconnectReason r){ MqttService::onMqttDisconnect(r); });
  client.onMessage([](char* t, char* p, AsyncMqttClientMessageProperties pr, size_t len, size_t idx, size_t tot){
    MqttService::onMqttMessage(t, p, pr, len, idx, tot);
  });
  connect();
}

void MqttService::connect() {
  if (WiFi.status() == WL_CONNECTED) {
    Logger::info("MQTT connecting to %s:%u", MQTT_HOST, MQTT_PORT);
    client.setClientId(DEVICE_ID);
    client.connect();
  } else {
    Logger::warn("MQTT wait WiFi...");
    reconnectTimer.once(2, [](){ connect(); });
  }
}

void MqttService::onMqttConnect(bool) {
  Logger::info("MQTT connected");
  subscribeTopic(tCmdLamp1());
  subscribeTopic(tCmdLamp2());
  subscribeTopic(tCmdPowerMaster());
  subscribeTopic(tCmdSetpoint());
  publish(tRoot()+"/status", String("{\"online\":true,\"ip\":\"") + WiFi.localIP().toString() + "\"}", true, 1);
}

void MqttService::onMqttDisconnect(AsyncMqttClientDisconnectReason) {
  Logger::warn("MQTT disconnected -> reconnect in 3s");
  reconnectTimer.once(3, [](){ connect(); });
}

void MqttService::onMqttMessage(char* topic, char* payload,
  AsyncMqttClientMessageProperties, size_t len, size_t, size_t) {
  String t(topic);
  String pl; pl.reserve(len+1);
  for (size_t i=0;i<len;i++) pl += payload[i];
  Logger::info("MQTT msg on %s: %s", t.c_str(), pl.c_str());
  if (onMessage) onMessage(t, pl);
}

void MqttService::loop() { }
bool MqttService::connected() { return client.connected(); }
void MqttService::publish(const String& topic, const String& payload, bool retain, uint8_t qos) {
  if (!connected()) return;
  client.publish(topic.c_str(), qos, retain, payload.c_str(), payload.length());
}
void MqttService::subscribeTopic(const String& topic, uint8_t qos) {
  if (!connected()) return;
  client.subscribe(topic.c_str(), qos);
  Logger::info("MQTT subscribed: %s", topic.c_str());
}
