#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "pins.h"
#include "topics.h"

// Services
#include "services/WiFiService.h"
#include "services/MqttService.h"
#include "services/SensorService.h"
#include "services/LampService.h"
#include "services/ApiService.h"
#include "services/StateService.h"
#include "services/FailsafeService.h"

// Utils
#include "helpers/Logger.h"

// ===== Defaults / Config =====
#ifndef TELEMETRY_MS
#define TELEMETRY_MS 5000UL
#endif

// Tombol reset kredensial WiFi (long press)
#ifndef RESET_BTN_PIN
#define RESET_BTN_PIN 0
#endif
static const unsigned long RESET_HOLD_MS = 5000;

// LED indikator WiFi
#ifndef WIFI_STATE
#define WIFI_STATE LED_BUILTIN
#endif

// ===== Globals (sensor state) =====
static float g_temp1 = NAN;
static float g_temp2 = NAN;
static float g_hum1  = NAN;
static float g_hum2  = NAN;
static float g_setpoint = 32.0f;
static uint32_t lastTelemetry = 0;

// Failsafe params (diisi di setup dari StateService)
static float g_hardMin = 20.0f;
static float g_hardMax = 38.0f;
static float g_hystC   = 1.0f;

// ===== Reset button state =====
static bool btnPressed = false;
static unsigned long btnPressStart = 0;

// ===== Helpers =====
static String nowISO8601() {
  time_t now;
  struct tm ti;
  char buf[32];
  time(&now);
  localtime_r(&now, &ti);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &ti);
  return String(buf);
}

static bool parseBoolFlexible(const String& s, bool& out) {
  String v = s; v.trim(); v.toLowerCase();
  if (v == "true" || v == "1" || v == "on")  { out = true;  return true; }
  if (v == "false"|| v == "0" || v == "off") { out = false; return true; }
  return false;
}

static bool parseFloatFlexible(const String& s, float& out) {
  char* end = nullptr;
  out = strtof(s.c_str(), &end);
  return (end && end != s.c_str());
}

// ===== Forward decl =====
static void publishTelemetry();
static void handleDesiredLamp(const String& whichLamp, const String& payload, JsonDocument& doc);
static void handleDesiredMaster(const String& payload, JsonDocument& doc);
static void handleDesiredSetpoint(const String& payload, JsonDocument& doc);
static void handleDesiredAlertConfig(const String& payload); // new

// ===== MQTT message handler =====
static void handleMqttMessage(const String& topic, const String& payload) {
  Logger::info("MQTT IN %s: %s", topic.c_str(), payload.c_str());

  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, payload); // boleh gagal (kalau plaintext)

  const String base = "esp32/" + String(DEVICE_ID) + "/";

  // 0) Sync request → publish snapshot reported
  if (topic == base + "cmd/get") {
    MqttService::publishReportedLamp1();
    MqttService::publishReportedLamp2();
    MqttService::publishReportedMaster();
    MqttService::publishReportedSetpoint();
    MqttService::publishReportedSnapshot();
    return;
  }

  // 1) Namespace desired/*
  if (topic.startsWith(base + "desired/")) {
    const String leaf = topic.substring((base + "desired/").length()); // lamp1 | lamp2 | power_master | setpoint | alert_config
    if (leaf == "lamp1")            { handleDesiredLamp("lamp1", payload, doc); return; }
    else if (leaf == "lamp2")       { handleDesiredLamp("lamp2", payload, doc); return; }
    else if (leaf == "power_master"){ handleDesiredMaster(payload, doc); return; }
    else if (leaf == "setpoint")    { handleDesiredSetpoint(payload, doc); return; }
    else if (leaf == "alert_config"){ handleDesiredAlertConfig(payload);   return; } // NEW
    return;
  }

  // 2) Kompat: topic legacy command
  if (topic == tCmdLamp1())            { handleDesiredLamp("lamp1", payload, doc); return; }
  else if (topic == tCmdLamp2())       { handleDesiredLamp("lamp2", payload, doc); return; }
  else if (topic == tCmdPowerMaster()) { handleDesiredMaster(payload, doc); return; }
  else if (topic == tCmdSetpoint())    { handleDesiredSetpoint(payload, doc); return; }

  // 3) Lainnya (opsional) dibiarkan ke app handler lain kalau ada
}

// ===== Desired handlers =====
static void handleDesiredLamp(const String& whichLamp, const String& payload, JsonDocument& doc) {
  bool doToggle = false;
  float val = NAN;

  if (payload == "toggle") {
    doToggle = true;
  } else if (doc.is<JsonObject>()) {
    if (!doc["toggle"].isNull() && doc["toggle"].as<bool>()) {
      doToggle = true;
    } else if (!doc["power"].isNull()) {
      val = doc["power"].as<float>();
    } else if (!doc["value"].isNull()) {
      val = doc["value"].as<float>();
    }
  } else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) {
    val = doc.as<float>();
  } else {
    if (!parseFloatFlexible(payload, val)) {
      if (payload.equalsIgnoreCase("on"))  val = 1.0f;
      if (payload.equalsIgnoreCase("off")) val = 0.0f;
    }
  }

  if (doToggle) {
    const float cur = (whichLamp == "lamp1") ? LampService::getLamp1().power
                                             : LampService::getLamp2().power;
    const float nv = (cur >= 0.5f) ? 0.0f : 1.0f;
    if (whichLamp == "lamp1") LampService::setLamp1(nv);
    else                      LampService::setLamp2(nv);
  } else if (!isnan(val)) {
    val = constrain(val, 0.0f, 1.0f);
    if (whichLamp == "lamp1") LampService::setLamp1(val);
    else                      LampService::setLamp2(val);
  }

  // Reported (retained)
  if (whichLamp == "lamp1") MqttService::publishReportedLamp1();
  else                      MqttService::publishReportedLamp2();
}

static void handleDesiredMaster(const String& payload, JsonDocument& doc) {
  bool on = false; bool valid = false; bool toggle = false;

  if (payload == "toggle") toggle = true;
  else if (doc.is<JsonObject>()) {
    if (!doc["on"].isNull())                { on = doc["on"].as<bool>(); valid = true; }
    else if (!doc["toggle"].isNull())       { toggle = doc["toggle"].as<bool>(); }
    else if (!doc["value"].isNull())        { on = doc["value"].as<int>() != 0; valid = true; }
  } else if (doc.is<bool>())                { on = doc.as<bool>(); valid = true; }
  else if (doc.is<int>())                   { on = doc.as<int>() != 0; valid = true; }
  else                                      { valid = parseBoolFlexible(payload, on); }

  if (toggle) {
    LampService::setMaster(!LampService::getMaster());
  } else if (valid) {
    LampService::setMaster(on);
  }
  MqttService::publishReportedMaster();
}

static void handleDesiredSetpoint(const String& payload, JsonDocument& doc) {
  float t = NAN;
  if (doc.is<JsonObject>() && !doc["t"].isNull())      t = doc["t"].as<float>();
  else if (doc.is<float>() || doc.is<int>() || doc.is<double>()) t = doc.as<float>();
  else                                                 parseFloatFlexible(payload, t);

  if (!isnan(t)) {
    g_setpoint = t;
    StateService::saveSetpoint(g_setpoint);
    MqttService::publishReportedSetpoint();
  }
}

// === NEW: desired/alert_config → update hard_min, hard_max, hyst ===
static void handleDesiredAlertConfig(const String& payload) {
  StaticJsonDocument<256> d;
  if (deserializeJson(d, payload) != DeserializationError::Ok) {
    Logger::warn("alert_config payload invalid: %s", payload.c_str());
    return;
  }

  bool changed = false;

  if (d.containsKey("setpoint")) {
    g_setpoint = d["setpoint"].as<float>();
    StateService::saveSetpoint(g_setpoint);
    MqttService::publishReportedSetpoint();
    changed = true;
  }
  if (d.containsKey("hard_min")) {
    g_hardMin = d["hard_min"].as<float>();
    StateService::saveHardMin(g_hardMin);
    changed = true;
  }
  if (d.containsKey("hard_max")) {
    g_hardMax = d["hard_max"].as<float>();
    StateService::saveHardMax(g_hardMax);
    changed = true;
  }
  if (d.containsKey("hyst")) {
    g_hystC = d["hyst"].as<float>();
    StateService::saveHyst(g_hystC);
    changed = true;
  }

  if (changed) {
    // re-init failsafe dengan param baru
    Failsafe::Config fs;
    fs.HARD_MIN = g_hardMin;
    fs.HARD_MAX = g_hardMax;
    fs.HYST     = g_hystC;
    Failsafe::begin(fs);

    Logger::info("alert_config applied: setpoint=%.2f, hard_min=%.2f, hard_max=%.2f, hyst=%.2f",
                 g_setpoint, g_hardMin, g_hardMax, g_hystC);
  }
}

// ===== Telemetry publisher =====
static void publishTelemetry() {
  if (!MqttService::connected()) return;

  StaticJsonDocument<512> d; // dinaikkan sedikit
  d["device"] = DEVICE_ID;

  // sensors
  JsonArray sensors = d.createNestedArray("sensors");
  JsonObject s1 = sensors.createNestedObject(); s1["id"]="dht1"; s1["t"]=isnan(g_temp1) ? 0.0 : g_temp1; s1["h"]=isnan(g_hum1) ? 0.0 : g_hum1;
  JsonObject s2 = sensors.createNestedObject(); s2["id"]="dht2"; s2["t"]=isnan(g_temp2) ? 0.0 : g_temp2; s2["h"]=isnan(g_hum2) ? 0.0 : g_hum2;

  // lamps
  JsonArray lamps = d.createNestedArray("lamps");
  JsonObject l1 = lamps.createNestedObject(); l1["id"]="lamp1"; l1["power"]=LampService::getLamp1().power;
  JsonObject l2 = lamps.createNestedObject(); l2["id"]="lamp2"; l2["power"]=LampService::getLamp2().power;

  // states & options
  d["power_master"] = LampService::getMaster();

  // kirim juga param failsafe
  JsonObject cfg = d.createNestedObject("config");
  cfg["hard_min"] = g_hardMin;
  cfg["hard_max"] = g_hardMax;
  cfg["hyst"]     = g_hystC;
  cfg["dht_setpoint"] = g_setpoint;

  d["ts"] = millis();
  d["last_updated"] = nowISO8601();

  String out; serializeJson(d, out);
  MqttService::publish(tTelemetry(), out, /*retain=*/false); // QoS0 cukup untuk telemetry
  Logger::info("Telemetry: %s", out.c_str());
}

// ===== Setup / Loop =====
void setup() {
  delay(300);
  Logger::begin();

  // GPIO init
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);
  pinMode(WIFI_STATE, OUTPUT);
  digitalWrite(WIFI_STATE, LOW);

  // Init services
  StateService::begin();
  SensorService::begin();
  LampService::begin();
  WiFiService::begin(); // provisioning + connect tersimpan

  // Load setpoint & failsafe params dari pref
  g_setpoint = StateService::loadSetpoint();            // default 32.0
  g_hardMin  = StateService::loadHardMin();             // default 20.0
  g_hardMax  = StateService::loadHardMax();             // default 38.0
  g_hystC    = StateService::loadHyst();                // default 1.0

  Logger::info("Loaded setpoint=%.2f hardMin=%.2f hardMax=%.2f hyst=%.2f",
               g_setpoint, g_hardMin, g_hardMax, g_hystC);

  // Inisialisasi failsafe dengan nilai dari pref
  Failsafe::Config fs;
  fs.HARD_MIN = g_hardMin;
  fs.HARD_MAX = g_hardMax;
  fs.HYST     = g_hystC;
  // (opsional) atur FS timing:
  // fs.NET_DEAD_MS = 30000; fs.MIN_ON_MS = 20000; fs.MIN_OFF_MS = 20000;
  Failsafe::begin(fs);

  // MQTT (Async) — auto connect saat WiFi ready
  MqttService::begin(handleMqttMessage);
}

void loop() {
  // WiFi provisioning/http dll
  WiFiService::loop();

  // LED WiFi
  digitalWrite(WIFI_STATE, WiFiService::isConnected() ? HIGH : LOW);

  // Pastikan connect MQTT saat WiFi siap (Async lib akan handle)
  if (WiFiService::isConnected() && !MqttService::connected()) {
    MqttService::reconnect();
  }
  MqttService::loop(); // no-op (kompatibilitas)

  // Long-press reset WiFi
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    if (!btnPressed) { btnPressed = true; btnPressStart = millis(); }
    else if (millis() - btnPressStart > RESET_HOLD_MS) {
      Logger::warn("Long-press: resetting WiFi credentials");
      WiFiService::resetCredentials();
      ESP.restart();
    }
  } else {
    btnPressed = false;
  }

  // Read sensors
  (void)SensorService::readTemps(g_temp1, g_temp2, g_hum1, g_hum2);

  // ===== Failsafe tick (pakai rata-rata suhu valid) =====
  float avgT = NAN; int n = 0;
  if (isfinite(g_temp1)) { avgT = isfinite(avgT)? (avgT + g_temp1) : g_temp1; n++; }
  if (isfinite(g_temp2)) { avgT = isfinite(avgT)? (avgT + g_temp2) : g_temp2; n++; }
  if (n > 1) avgT /= n;

  Failsafe::tick(
    avgT,
    /*mqttOk*/ MqttService::connected(),
    /*wifiOk*/ WiFiService::isConnected()
  );

  // Telemetry interval
  if (millis() - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = millis();
    publishTelemetry();
  }
}