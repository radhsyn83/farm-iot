#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Ticker.h>
#include <Preferences.h>
#include <ESP32Ping.h>
#include <DNSServer.h> // captive
#include <ESPmDNS.h>   // hostname.local
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
  static String mdnsName(); // returns e.g. "farmiot.local"

private:
  // ===== STATE =====
  static inline Ticker wifiReconnectTimer;
  static inline Preferences prefs;
  static inline WebServer server{80};
  static inline DNSServer dns;           // captive portal
  static inline bool provisioningActive = false;
  static inline uint8_t reconnectAttempts = 0;
  static constexpr uint8_t MAX_ATTEMPTS_BEFORE_RESET = 10;

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
  static void handleSettings();      // "/settings"
  static void handleCss();           // "/app.css"

  // ===== Captive Portal =====
  static void handleCaptivePortal(); // catch-all redirect → /wifi

  // ===== Wi-Fi API Routes =====
  static void handleScan();          // "/wifi/scan"
  static void handleProvision();     // "/wifi/provision" (POST)
  static void handleWifiStatus();    // "/wifi/status"
  static void handleWifiReset();     // "/wifi/reset" (POST)

  // ===== Peripheral Config Routes =====
  static void handleConfigPage();    // "/config"
  static void handleGetPeripherals(); // "/api/peripherals" (GET)
  static void handlePostPeripherals();// "/api/peripherals" (POST)

  // ===== Schedule Routes =====
  static void handleSchedulePage();   // "/schedule"
  static void handleGetSchedule();    // "/api/schedule" (GET)
  static void handlePostSchedule();   // "/api/schedule" (POST)

  // ===== New API & Pages =====
  static void handleSystem();         // "/system"
  static void handleApiStatus();      // "/api/status" (GET)
  static void handleApiLamp();        // "/api/lamp" (POST)
  static void handleApiMaster();      // "/api/master" (POST)
  static void handleApiSetpoint();    // "/api/setpoint" (POST)
  static void handleApiFailsafe();    // "/api/failsafe" (POST)
  static void handleApiRestart();     // "/api/restart" (POST)

  // ===== OTA Firmware Update =====
  static void handleOtaPage();        // "/ota" (GET)
  static void handleOtaResult();      // "/ota/upload" (POST complete)
  static void handleOtaUpload();      // "/ota/upload" (POST upload chunks)

  // legacy
  static void handleRoot();
  static void handleStatus();
  static void handleReset();

  // ===== Utils =====
  static void registerRoutes();
  static void startCaptiveAP();
  static void startMdns();
  static inline bool serverActive = false;
  static bool loadCredentials(String& ssid, String& pass);
  static void saveCredentials(const String& ssid, const String& pass);
};
