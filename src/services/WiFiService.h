#pragma once
#include <WiFi.h>
#include "../config.h"
#include "../helpers/Logger.h"

class WiFiService {
public:
  static void begin() {
    Logger::info("WiFi connecting to %s ...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  static bool isConnected() { return WiFi.status() == WL_CONNECTED; }
  static void loop() {
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 2000) {
      lastCheck = millis();
      if (!isConnected()) {
        Logger::warn("WiFi reconnect...");
        WiFi.reconnect();
      }
    }
  }
  static IPAddress ip() { return WiFi.localIP(); }
};
