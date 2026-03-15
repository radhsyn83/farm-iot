#pragma once
#include <Arduino.h>
#include <PubSubClient.h>

// callback: onMessage(topic, payload)
using MqttMessageHandler = void (*)(const String&, const String&);

class MqttService {
public:
    // lifecycle
    static void begin(MqttMessageHandler handler);
    static void loop();
    static bool connected();
    static void reconnect();

    // publish/subscribe
    static void publish(const String& topic, const String& payload, bool retain=false);
    static void subscribeTopic(const String& topic);

    // topics util
    static String tBase();
    static String tState();
    static String tHeartbeat();
    static String tCmdGet();
    static String tDesiredNS();
    static String tReportedNS();

    // reported helpers (dynamic)
    static void publishReportedLamp(const char* lampId);
    static void publishReportedAllLamps();
    static void publishReportedMaster();
    static void publishReportedSetpoint();
    static void publishReportedSnapshot();
    static void publishReportedConfig();

    // Extended config with failsafe (merged into reported/config)
    struct FailsafeParams {
        bool fsAuto;
        uint8_t fsMode;
        float hardMin, hardMax, hyst, setpoint;
    };
    static void publishReportedConfigFull(const FailsafeParams& fs);

    // telemetry
    static void publishHeartbeat();

private:
    static void onMqttMessage(char* topic, byte* payload, unsigned int length);

    // state
    static PubSubClient client;
    static MqttMessageHandler onMessage;

    // reconnect policy
    static const uint32_t RECONNECT_MS;
    static uint32_t lastReconnectAttempt;

    // heartbeat
    static const uint32_t HEARTBEAT_MS;
    static uint32_t lastHeartbeat;
};
