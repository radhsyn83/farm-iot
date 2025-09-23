#pragma once
#include <WiFi.h>
#include <Ticker.h>
#include "../config.h"
#include "../helpers/Logger.h"
#include <ESP32Ping.h>

class WiFiService {
public:
  static void begin() {
    Logger::info("WiFi connecting to %s ...", WIFI_SSID);
    WiFi.mode(WIFI_STA);

    // pasang handler event
    WiFi.onEvent(WiFiService::WiFiEvent);

    connect();
  }

  static bool isConnected() { return WiFi.status() == WL_CONNECTED; }

  // tidak perlu loop() polling lagi

  static IPAddress ip() { return WiFi.localIP(); }

  static bool hasInternetPing() {
    if (!isConnected()) return false;
    return Ping.ping("103.175.220.242", 3); // ping 3x ke server
  }

private:
  static Ticker wifiReconnectTimer;

  static void connect() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  static void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
      case SYSTEM_EVENT_STA_CONNECTED:
        Logger::info("WiFi connected (association).");
        break;
      case SYSTEM_EVENT_STA_GOT_IP:
        Logger::info("WiFi got IP: %s", WiFi.localIP().toString().c_str());
        break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
        Logger::warn("WiFi disconnected, reconnecting...");
        wifiReconnectTimer.once(3, connect);
        break;
      default:
        break;
    }
  }
};

// definisi static member
Ticker WiFiService::wifiReconnectTimer;
