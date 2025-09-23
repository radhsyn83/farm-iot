#pragma once
#include <Arduino.h>            // untuk String, millis()
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "../helpers/Logger.h"
// perhatikan nama file topics, samakan dengan project kamu:
// kalau file-mu bernama "topics.h", pakai lowercase:
#include "../topics.h"

// handler supaya app lain bisa dapet pesan
using MqttMessageHandler = std::function<void(const String& topic, const String& payload)>;

class MqttService {
public:
  static void begin(MqttMessageHandler handler = nullptr);
  static void loop();
  static bool connected();

  static void publish(const String& topic, const String& payload, bool retain = false);
  static void subscribeTopic(const String& topic);
  static void reconnect();

private:
  // ===== static members =====
  static WiFiClientSecure wifiClient;
  static PubSubClient client;
  static MqttMessageHandler onMessage;
  static unsigned long lastReconnectAttempt;
  static uint32_t reconnectBackoffMs;   // <— tambahkan untuk exponential backoff

  // ===== internals =====
  static void callback(char* topic, uint8_t* payload, unsigned int length);
};
