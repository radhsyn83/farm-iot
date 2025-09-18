#pragma once
#include <AsyncMqttClient.h>
#include <Ticker.h>
#include "../config.h"
#include "../helpers/Logger.h"
#include "../topics.h"

typedef std::function<void(const String& topic, const String& payload)> MqttMessageHandler;

class MqttService {
public:
  static void begin(MqttMessageHandler handler);
  static void loop();
  static bool connected();
  static void publish(const String& topic, const String& payload, bool retain=false, uint8_t qos=0);
  static void subscribeTopic(const String& topic, uint8_t qos=0);
private:
  static void connect();
  static void onMqttConnect(bool sessionPresent);
  static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
  static void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
                            size_t len, size_t index, size_t total);
  static AsyncMqttClient client;
  static Ticker reconnectTimer;
  static MqttMessageHandler onMessage;
};
