#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Ticker.h>
#include <Preferences.h>
#include <ESP32Ping.h>
#include <DNSServer.h> // captive
#include "../config.h"
#include "../helpers/Logger.h"

class WiFiService {
public:
  static void begin();
  static bool isConnected();
  static IPAddress ip();
  static bool hasInternetPing();
  static void loop();
  static void resetCredentials();
  static void startProvisioningAP();
  static void stopProvisioningAP();

private:
  // ===== STATE =====
  static inline Ticker wifiReconnectTimer;
  static inline Preferences prefs;
  static inline WebServer server{80};
  static inline DNSServer dns;           // captive portal
  static inline bool provisioningActive = false;

  // ===== CONST =====
  static constexpr const char* NVS_NS = "wifi";
  static constexpr const char* KEY_SSID = "ssid";
  static constexpr const char* KEY_PASS = "pass";
  static constexpr uint8_t DNS_PORT = 53;

  // ===== FLOW =====
  static bool trySavedWifi(uint32_t timeoutMs = 12000);
  static void connect(const String& ssid, const String& pass);
  static void WiFiEvent(WiFiEvent_t event);
  static void scheduleReconnect(uint32_t sec);

  // ===== UI Routes =====
  static void handleHome();          // "/"
  static void handleWifi();          // "/wifi"
  static void handleTemp();          // "/temp"
  static void handleLamp();          // "/lamp"
  static void handleCss();           // "/app.css"

  // ===== Wi-Fi API Routes =====
  static void handleScan();          // "/wifi/scan"
  static void handleProvision();     // "/wifi/provision" (POST)
  static void handleWifiStatus();    // "/wifi/status"
  static void handleWifiReset();     // "/wifi/reset" (POST)

  // legacy (opsional kompatibel)
  static void handleRoot();          // alias ke handleHome()
  static void handleStatus();        // alias ke handleWifiStatus()
  static void handleReset();         // alias ke handleWifiReset()

  // ===== Utils =====
  static void startCaptiveAP();
  static bool loadCredentials(String& ssid, String& pass);
  static void saveCredentials(const String& ssid, const String& pass);
};
