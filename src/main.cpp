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

// --- Suspect detection ---
#ifndef SUSPECT_MS
#define SUSPECT_MS 60000UL   // 60s invalid berturut-turut → suspect
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
static bool  g_fsAuto  = true;      // << ikut alert_config

// Restart schedule
static bool g_pendingRestart = false;
static uint32_t g_restartAt = 0;

// ===== Reset button state =====
static bool btnPressed = false;
static unsigned long btnPressStart = 0;

// ===== Suspect state =====
static bool     g_suspect1 = false;
static bool     g_suspect2 = false;
static uint32_t g_invalidStart1 = 0;
static uint32_t g_invalidStart2 = 0;

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

// Nilai temperature valid? (0, 85, NaN dianggap invalid)
static inline bool tempValid(float t) {
  return isfinite(t) && t > 10.0f && t < 60.0f && t != 0.0f && t != 85.0f;
}

// ===== Forward decl =====
static void publishTelemetry();
static void handleDesiredLamp(const String& whichLamp, const String& payload, JsonDocument& doc);
static void handleDesiredMaster(const String& payload, JsonDocument& doc);
static void handleDesiredSetpoint(const String& payload, JsonDocument& doc);
static void handleDesiredAlertConfig(const String& payload);
static void handleDesiredRestart(const String& payload);

static void publishEvent(const char* kind, const char* status) {
  StaticJsonDocument<192> d;
  d["device"] = DEVICE_ID;
  d["kind"]   = kind;   // "restart"
  d["status"] = status; // "ack"/"done"/"error"
  d["ts"]     = nowISO8601();
  String out; serializeJson(d, out);
  MqttService::publish(String("esp32/")+DEVICE_ID+"/event/"+kind, out, false);
}

static void publishEventEx(
  const char* kind,
  const char* status,
  const char* sensor_id /*nullable*/ = nullptr,
  uint32_t duration_ms = 0
) {
  StaticJsonDocument<256> d;
  d["device"] = DEVICE_ID;
  d["kind"]   = kind;       // "sensor_suspect"
  d["status"] = status;     // "up" | "down"
  if (sensor_id)  d["sensor_id"]  = sensor_id;   // "dht1"/"dht2"
  if (duration_ms) d["duration_ms"] = duration_ms;
  d["ts"]     = nowISO8601();

  String out; serializeJson(d, out);
  MqttService::publish(String("esp32/") + DEVICE_ID + "/event/" + kind, out, /*retain=*/false);
}

// Snapshot array suspects (opsional, untuk backend)
static void publishSuspectsSnapshot(bool s1, bool s2) {
  StaticJsonDocument<256> d;
  d["device"] = DEVICE_ID;
  d["kind"]   = "suspect_snapshot";
  JsonArray arr = d.createNestedArray("suspects");
  if (s1) { JsonObject o = arr.createNestedObject(); o["sensor_id"] = "dht1"; }
  if (s2) { JsonObject o = arr.createNestedObject(); o["sensor_id"] = "dht2"; }
  d["ts"] = nowISO8601();

  String out; serializeJson(d, out);
  MqttService::publish(String("esp32/") + DEVICE_ID + "/event/suspect_snapshot", out, /*retain=*/false);
}

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
    else if (leaf == "alert_config"){ handleDesiredAlertConfig(payload);   return; } 
    else if (leaf == "restart")     { handleDesiredRestart(payload);       return; }
    return;
  }

  // 2) Kompat: topic legacy command
  if (topic == tCmdLamp1())            { handleDesiredLamp("lamp1", payload, doc); return; }
  else if (topic == tCmdLamp2())       { handleDesiredLamp("lamp2", payload, doc); return; }
  else if (topic == tCmdPowerMaster()) { handleDesiredMaster(payload, doc); return; }
  else if (topic == tCmdSetpoint())    { handleDesiredSetpoint(payload, doc); return; }

  // 3) Lainnya (opsional)
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
  bool changedFS = false;

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

  if (d.containsKey("failsafe_auto")) {
    g_fsAuto = d["failsafe_auto"].as<bool>();
    StateService::saveFailsafeAuto(g_fsAuto);
    changedFS = true;
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

  if (changedFS) {
    // publish reported snapshot kecil
    StaticJsonDocument<160> r;
    r["device"] = DEVICE_ID;
    r["failsafe_auto"] = g_fsAuto;
    r["ts"] = nowISO8601();
    String out; serializeJson(r, out);
    MqttService::publish(String("esp32/")+DEVICE_ID+"/reported/failsafe_auto", out, /*retain=*/true);
    Logger::info("failsafe_auto: %s", g_fsAuto ? "true" : "false");
  }
}

// === desired/restart: {"key":"device"|"dht1"|"dht2"}
static void handleDesiredRestart(const String& payload) {
  StaticJsonDocument<128> d;
  if (deserializeJson(d, payload) != DeserializationError::Ok || d["key"].isNull()) {
    Logger::warn("restart payload invalid: %s", payload.c_str());
    return;
  }
  const String key = d["key"].as<String>();

  if (key == "device") {
    publishEvent("restart", "ack");
    g_pendingRestart = true;
    g_restartAt = millis() + 300;
    return;
  }

  if (key == "dht1" || key == "dht2") {
    publishEvent("restart", "ack");
    SensorService::rebeginOne(key.c_str()); // implement per sensor
    publishEvent("restart", "done");
    return;
  }

  Logger::warn("restart key unknown: %s", key.c_str());
}

// ===== Telemetry publisher =====
static void publishTelemetry() {
  if (!MqttService::connected()) return;

  StaticJsonDocument<640> d; // dinaikkan sedikit
  d["device"] = DEVICE_ID;

  // sensors (publish nilai mentah/0 jika invalid—client yang abaikan)
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
  cfg["failsafe_auto"] = g_fsAuto; // << NEW

  // sensor_health + suspects (array of objects {id, device})
  const bool v1 = tempValid(g_temp1);
  const bool v2 = tempValid(g_temp2);
  JsonObject health = d.createNestedObject("sensor_health");
  health["valid_count"] = (int)v1 + (int)v2;
  health["spread_c"]    = (v1 && v2) ? fabs(g_temp1 - g_temp2) : 0.0;

  JsonArray suspects = health.createNestedArray("suspects");
  if (g_suspect1) { JsonObject o = suspects.createNestedObject(); o["id"] = "dht1"; o["device"] = DEVICE_ID; }
  if (g_suspect2) { JsonObject o = suspects.createNestedObject(); o["id"] = "dht2"; o["device"] = DEVICE_ID; }

  d["ts"] = millis();
  d["last_updated"] = nowISO8601();

  String out; serializeJson(d, out);
  MqttService::publish(tTelemetry(), out, /*retain=*/false);
  Logger::info("Telemetry: %s", out.c_str());
}



// Update flag “suspect” per sensor (invalid terus ≥ SUSPECT_MS)
static inline void updateSuspects(bool v1, bool v2) {
  const uint32_t now = millis();

  bool prev1 = g_suspect1;
  bool prev2 = g_suspect2;

  // --- dht1 ---
  if (!v1) {
    if (g_invalidStart1 == 0) g_invalidStart1 = now;
    if (!g_suspect1 && (now - g_invalidStart1) >= SUSPECT_MS) {
      g_suspect1 = true;
      Logger::warn("[SUSPECT] dht1 -> suspect");
      publishEventEx("sensor_suspect", "up", "dht1", /*duration_ms*/0);
    }
  } else {
    if (g_suspect1) {
      Logger::info("[SUSPECT] dht1 recovered");
      const uint32_t dur = (g_invalidStart1 ? (now - g_invalidStart1) : 0);
      publishEventEx("sensor_suspect", "down", "dht1", dur);
    }
    g_invalidStart1 = 0;
    g_suspect1 = false;
  }

  // --- dht2 ---
  if (!v2) {
    if (g_invalidStart2 == 0) g_invalidStart2 = now;
    if (!g_suspect2 && (now - g_invalidStart2) >= SUSPECT_MS) {
      g_suspect2 = true;
      Logger::warn("[SUSPECT] dht2 -> suspect");
      publishEventEx("sensor_suspect", "up", "dht2", /*duration_ms*/0);
    }
  } else {
    if (g_suspect2) {
      Logger::info("[SUSPECT] dht2 recovered");
      const uint32_t dur = (g_invalidStart2 ? (now - g_invalidStart2) : 0);
      publishEventEx("sensor_suspect", "down", "dht2", dur);
    }
    g_invalidStart2 = 0;
    g_suspect2 = false;
  }

  // Kirim snapshot hanya kalau ada perubahan (anti-spam)
  if (prev1 != g_suspect1 || prev2 != g_suspect2) {
    publishSuspectsSnapshot(g_suspect1, g_suspect2);
  }
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

  #if SENSOR_MOCK == 1
    SensorService::enableMock(true);
    SensorService::setMockValues(36.0f, 36.0f, 70.0f, 70.0f);
  #endif

  LampService::begin();
  WiFiService::begin(); // provisioning + connect tersimpan

  // Load setpoint & failsafe params dari pref
  g_setpoint = StateService::loadSetpoint();            // default 32.0
  g_hardMin  = StateService::loadHardMin();             // default 20.0
  g_hardMax  = StateService::loadHardMax();             // default 38.0
  g_hystC    = StateService::loadHyst();                // default 1.0

  g_fsAuto   = StateService::loadFailsafeAuto(/*default*/ true);

  Logger::info("Loaded setpoint=%.2f hardMin=%.2f hardMax=%.2f hyst=%.2f fsAuto=%d",
               g_setpoint, g_hardMin, g_hardMax, g_hystC, g_fsAuto);

  // Inisialisasi failsafe dengan nilai dari pref
  Failsafe::Config fs;
  fs.HARD_MIN = g_hardMin;
  fs.HARD_MAX = g_hardMax;
  fs.HYST     = g_hystC;
  // (opsional) timing FS:
  // fs.NET_DEAD_MS = 30000; fs.MIN_ON_MS = 20000; fs.MIN_OFF_MS = 20000;
  Failsafe::begin(fs);

  // MQTT (Async) — auto connect saat WiFi siap
  MqttService::begin(handleMqttMessage);
}

void loop() {
  // WiFi provisioning/http dll
  WiFiService::loop();

  // LED WiFi
  digitalWrite(WIFI_STATE, WiFiService::isConnected() ? HIGH : LOW);

  // Pastikan connect MQTT saat WiFi siap
  if (WiFiService::isConnected() && !MqttService::connected()) {
    MqttService::reconnect();
  }
  MqttService::loop();

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

  // ===== Read sensors =====
  (void)SensorService::readTemps(g_temp1, g_temp2, g_hum1, g_hum2);

  // ===== Validity & Suspect tracking =====
  const bool v1 = tempValid(g_temp1);
  const bool v2 = tempValid(g_temp2);
  updateSuspects(v1, v2);

  // ===== Robust aggregation untuk failsafe =====
  float avgT = NAN; // gunakan arsitektur lama Failsafe::tick(avgT,...)
  if (v1 && v2) {
    // dua-duanya valid → pakai rata-rata
    avgT = (g_temp1 + g_temp2) * 0.5f;
  } else if (v1 ^ v2) {
    // hanya satu valid → pakai yang valid
    avgT = v1 ? g_temp1 : g_temp2;
  } else {
    // tidak ada yang valid → NAN (failsafe akan hold, tidak ganti state)
    avgT = NAN;
  }

  // ===== Failsafe tick =====
  Failsafe::tick(
    avgT,
    /*mqttOk*/ MqttService::connected(),
    /*wifiOk*/ WiFiService::isConnected()
  );

  // ===== Telemetry interval =====
  if (millis() - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = millis();
    publishTelemetry();
  }

  // handle scheduled restart
  if (g_pendingRestart && millis() >= g_restartAt) {
    publishEvent("restart", "done");
    delay(50);
    ESP.restart();
  }
}