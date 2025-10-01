#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>

// Callback tipe: (topic, payload)
typedef void (*MqttMessageHandler)(const String&, const String&);

class MqttService {
public:
  static void begin(MqttMessageHandler handler);
  static void loop();                    // no-op (async), tetap disediakan
  static bool connected();
  static void reconnect();

  static void publish(const String& topic, const String& payload, bool retain=false, uint8_t qos=0);
  static void subscribeTopic(const String& topic, uint8_t qos=1);

  static void publishHeartbeat();

  // Reported (retained)
  static void publishReportedLamp1();
  static void publishReportedLamp2();
  static void publishReportedMaster();
  static void publishReportedSetpoint();
  static void publishReportedSnapshot();

  // Topics util
  static String tBase();                 // esp32/<dev>/
  static String tState();                // esp32/<dev>/status/online
  static String tHeartbeat();            // esp32/<dev>/heartbeat
  static String tCmdGet();               // esp32/<dev>/cmd/get
  static String tDesiredNS();            // esp32/<dev>/desired/
  static String tReportedNS();           // esp32/<dev>/reported/

private:
  static AsyncMqttClient client;
  static Ticker reconnectTimer;
  static Ticker heartbeatTimer;

  static MqttMessageHandler onMessage;

  static void scheduleReconnect(uint32_t ms);
  static void onWifiReady();             // dipanggil dari begin/loop kalau perlu

  // Handlers
  static void onConnect(bool sessionPresent);
  static void onDisconnect(AsyncMqttClientDisconnectReason reason);
  static void onMessageCb(char* topic, char* payload, AsyncMqttClientMessageProperties props,
                          size_t len, size_t index, size_t total);
};