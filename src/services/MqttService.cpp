#include "MqttService.h"
#include "../config.h"

// static member
WiFiClientSecure MqttService::wifiClient;
PubSubClient MqttService::client(MQTT_HOST, MQTT_PORT, MqttService::callback, MqttService::wifiClient);
MqttMessageHandler MqttService::onMessage = nullptr;
unsigned long MqttService::lastReconnectAttempt = 0;

void MqttService::begin(MqttMessageHandler handler) {
    onMessage = handler;
    wifiClient.setInsecure(); // TLS tanpa CA certificate
}

void MqttService::loop() {
    if (!client.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 2000) {
            lastReconnectAttempt = now;
            reconnect();
        }
    } else {
        client.loop(); // keep-alive ping otomatis
    }
}

bool MqttService::connected() {
    return client.connected();
}

void MqttService::publish(const String& topic, const String& payload, bool retain) {
    if (connected()) client.publish(topic.c_str(), payload.c_str(), retain);
}

void MqttService::subscribeTopic(const String& topic) {
    if (connected()) {
        client.subscribe(topic.c_str());
        Logger::info("MQTT subscribed: %s", topic.c_str());
    }
}

void MqttService::callback(char* topic, byte* payload, unsigned int length) {
    String t(topic);
    String pl;
    for (unsigned int i = 0; i < length; i++) pl += (char)payload[i];
    Logger::info("MQTT msg on %s: %s", t.c_str(), pl.c_str());

    if (onMessage) onMessage(t, pl);
}

void MqttService::reconnect() {
    Logger::info("Attempting MQTT connection to %s:%d ...", MQTT_HOST, MQTT_PORT);

    // PubSubClient connect signature: 
    // connect(clientId, user, password, willTopic, willQos, willRetain, willMessage, keepAlive)
    if (client.connect(
            DEVICE_ID,         // clientId
            MQTT_USER,         // username
            MQTT_PASS,         // password
            tState().c_str(),  // willTopic
            1,                 // willQos
            true,              // willRetain
            "offline",         // willMessage
            20                 // keepAlive in seconds
        )) 
    {
        Logger::info("MQTT connected");

        // subscribe all topics after connect
        subscribeTopic(tCmdLamp1());
        subscribeTopic(tCmdLamp2());
        subscribeTopic(tCmdPowerMaster());
        subscribeTopic(tCmdSetpoint());

        // publish online
        publish(tState(), "online", true);
    } else {
        Logger::warn("MQTT connect failed, rc=%d", client.state());
    }
}
