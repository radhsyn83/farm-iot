#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "../helpers/Logger.h"
#include "Topics.h"

// handler supaya app lain bisa dapet pesan
typedef std::function<void(const String& topic, const String& payload)> MqttMessageHandler;

class MqttService {
public:
    static void begin(MqttMessageHandler handler = nullptr);
    static void loop();
    static bool connected();

    static void publish(const String& topic, const String& payload, bool retain = false);
    static void subscribeTopic(const String& topic);

private:
    static WiFiClientSecure wifiClient;
    static PubSubClient client;
    static MqttMessageHandler onMessage;
    static unsigned long lastReconnectAttempt;

    static void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
};
