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
#include "services/PeripheralManager.h"
#include "services/ScheduleManager.h"

// Utils
#include "helpers/Logger.h"
#include "web_api_bridge.h"

// ===== Defaults / Config =====
#ifndef TELEMETRY_MS
#define TELEMETRY_MS 5000UL
#endif

#ifndef RESET_BTN_PIN
#define RESET_BTN_PIN 0
#endif
static const unsigned long RESET_HOLD_MS = 5000;

#ifndef WIFI_STATE
#define WIFI_STATE LED_BUILTIN
#endif

#ifndef SUSPECT_MS
#define SUSPECT_MS 60000UL
#endif

#ifndef OUTLIER_THRESHOLD_C
#define OUTLIER_THRESHOLD_C 10.0f
#endif

#ifndef SENSOR_FAIL_TIMEOUT_MS
#define SENSOR_FAIL_TIMEOUT_MS 60000UL
#endif

// ===== Globals (dynamic sensor state) — non-static for web_api_bridge.h =====
SensorReading g_readings[MAX_SENSORS];
uint8_t       g_nSensors = 0;
float         g_setpoint = 32.0f;
static uint32_t lastTelemetry = 0;

// Failsafe params
float g_hardMin = 20.0f;
float g_hardMax = 38.0f;
float g_hystC   = 1.0f;
bool  g_fsAuto  = true;

// Restart schedule
bool     g_pendingRestart = false;
uint32_t g_restartAt = 0;

// Reset button state
static bool          btnPressed = false;
static unsigned long btnPressStart = 0;
static unsigned long btnLedToggle = 0;
static bool          btnLedState  = false;

// Suspect tracking (dynamic arrays)
bool     g_suspect[MAX_SENSORS]      = {};
static uint32_t g_invalidStart[MAX_SENSORS] = {};
static uint32_t g_lastRebegin[MAX_SENSORS]  = {};  // last auto-recovery attempt
bool     g_outlier[MAX_SENSORS]      = {};  // outlier detection per sensor

static const uint32_t REBEGIN_INTERVAL_MS = 2 * 60 * 1000; // retry recovery every 2 min

// Consecutive NaN tracking (power cycle escalation)
static uint8_t  g_nanCount[MAX_SENSORS] = {};
static const uint8_t NAN_COUNT_THRESHOLD = 3;
static uint32_t g_lastPowerCycle = 0;
static const uint32_t POWER_CYCLE_COOLDOWN_MS = 30000; // 30s between power cycles

// All-sensors-fail fallback
static uint32_t g_allSensorsFailedAt = 0;
bool     g_timeFallbackActive = false;

// Schedule runtime state
static int8_t g_activeWindowIdx = -1; // -1 = no window active (global scope)

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

static inline bool tempValid(float t) {
    return isfinite(t) && t > 10.0f && t < 60.0f && t != 0.0f && t != 85.0f;
}

// Simple insertion sort for small arrays (max 8 elements)
static void sortFloats(float* arr, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {
        float key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}

// Check if current hour is "night" (heaters-on window)
static bool isNightHour() {
    struct tm ti;
    time_t now;
    time(&now);
    if (now < 100000) return true; // NTP not synced yet → assume night (safer)
    localtime_r(&now, &ti);
    // NIGHT_START_HOUR=16, NIGHT_END_HOUR=10 → night = 16:00–09:59
    if (NIGHT_START_HOUR > NIGHT_END_HOUR) {
        return ti.tm_hour >= NIGHT_START_HOUR || ti.tm_hour < NIGHT_END_HOUR;
    } else {
        return ti.tm_hour >= NIGHT_START_HOUR && ti.tm_hour < NIGHT_END_HOUR;
    }
}

// ===== Forward decl =====
static void publishTelemetry();
static void handleDesiredLamp(const String& whichLamp, const String& payload, JsonDocument& doc);
static void handleDesiredMaster(const String& payload, JsonDocument& doc);
static void handleDesiredSetpoint(const String& payload, JsonDocument& doc);
static void handleDesiredAlertConfig(const String& payload);
static void handleDesiredRestart(const String& payload);
static void handleDesiredConfig(const String& payload);
static void handleDesiredSchedule(const String& payload);
static void applyLampControl(float avgT, bool nowCycling);

// ===== Event publishers =====
static void publishEvent(const char* kind, const char* status) {
    StaticJsonDocument<192> d;
    d["device"] = DEVICE_ID;
    d["kind"]   = kind;
    d["status"] = status;
    d["ts"]     = nowISO8601();
    String out; serializeJson(d, out);
    MqttService::publish(String("esp32/") + DEVICE_ID + "/event/" + kind, out, false);
}

static void publishEventEx(
    const char* kind,
    const char* status,
    const char* sensor_id = nullptr,
    uint32_t duration_ms = 0
) {
    StaticJsonDocument<256> d;
    d["device"] = DEVICE_ID;
    d["kind"]   = kind;
    d["status"] = status;
    if (sensor_id)   d["sensor_id"]   = sensor_id;
    if (duration_ms) d["duration_ms"] = duration_ms;
    d["ts"] = nowISO8601();
    String out; serializeJson(d, out);
    MqttService::publish(String("esp32/") + DEVICE_ID + "/event/" + kind, out, false);
}

// ===== Suspect snapshot (dynamic) =====
static void publishSuspectsSnapshot() {
    const auto& cfg = PeripheralManager::config();
    StaticJsonDocument<384> d;
    d["device"] = DEVICE_ID;
    d["kind"]   = "suspect_snapshot";
    JsonArray arr = d.createNestedArray("suspects");
    for (uint8_t i = 0; i < cfg.sensorCount; i++) {
        if (g_suspect[i]) {
            JsonObject o = arr.createNestedObject();
            o["sensor_id"] = cfg.sensors[i].id;
        }
    }
    d["ts"] = nowISO8601();
    String out; serializeJson(d, out);
    MqttService::publish(String("esp32/") + DEVICE_ID + "/event/suspect_snapshot", out, false);
}

// ===== MQTT message handler =====
static void handleMqttMessage(const String& topic, const String& payload) {
    Logger::info("MQTT IN %s: %s", topic.c_str(), payload.c_str());

    StaticJsonDocument<384> doc;
    deserializeJson(doc, payload); // may fail for plaintext, that's ok

    const String base = "esp32/" + String(DEVICE_ID) + "/";

    // cmd/get → publish full snapshot
    if (topic == base + "cmd/get") {
        MqttService::publishReportedAllLamps();
        MqttService::publishReportedMaster();
        MqttService::publishReportedSetpoint();
        MqttService::publishReportedSnapshot();
        // Merged config with failsafe
        MqttService::FailsafeParams fp;
        fp.fsAuto   = g_fsAuto;
        fp.fsMode   = (uint8_t)Failsafe::currentMode();
        fp.hardMin  = g_hardMin;
        fp.hardMax  = g_hardMax;
        fp.hyst     = g_hystC;
        fp.setpoint = g_setpoint;
        MqttService::publishReportedConfigFull(fp);
        return;
    }

    // desired/* namespace
    if (topic.startsWith(base + "desired/")) {
        const String leaf = topic.substring((base + "desired/").length());

        // Dynamic lamp routing: check if leaf matches any lamp id
        if (PeripheralManager::findLampIndex(leaf.c_str()) >= 0) {
            handleDesiredLamp(leaf, payload, doc);
            return;
        }

        if (leaf == "power_master")  { handleDesiredMaster(payload, doc); return; }
        if (leaf == "setpoint")      { handleDesiredSetpoint(payload, doc); return; }
        if (leaf == "alert_config")  { handleDesiredAlertConfig(payload); return; }
        if (leaf == "restart")       { handleDesiredRestart(payload); return; }
        if (leaf == "config")        { handleDesiredConfig(payload); return; }
        if (leaf == "schedule")      { handleDesiredSchedule(payload); return; }
        return;
    }
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
        LampState cur;
        if (LampService::getLampById(whichLamp.c_str(), cur)) {
            float nv = (cur.power >= 0.5f) ? 0.0f : 1.0f;
            LampService::setLampById(whichLamp.c_str(), nv);
        }
    } else if (!isnan(val)) {
        val = constrain(val, 0.0f, 1.0f);
        LampService::setLampById(whichLamp.c_str(), val);
    }

    MqttService::publishReportedLamp(whichLamp.c_str());
}

static void handleDesiredMaster(const String& payload, JsonDocument& doc) {
    bool on = false; bool valid = false; bool toggle = false;

    if (payload == "toggle") toggle = true;
    else if (doc.is<JsonObject>()) {
        if (!doc["on"].isNull())          { on = doc["on"].as<bool>(); valid = true; }
        else if (!doc["toggle"].isNull()) { toggle = doc["toggle"].as<bool>(); }
        else if (!doc["value"].isNull())  { on = doc["value"].as<int>() != 0; valid = true; }
    } else if (doc.is<bool>())            { on = doc.as<bool>(); valid = true; }
    else if (doc.is<int>())               { on = doc.as<int>() != 0; valid = true; }
    else                                  { valid = parseBoolFlexible(payload, on); }

    if (toggle)     LampService::setMaster(!LampService::getMaster());
    else if (valid) LampService::setMaster(on);

    MqttService::publishReportedMaster();
}

static void handleDesiredSetpoint(const String& payload, JsonDocument& doc) {
    float t = NAN;
    if (doc.is<JsonObject>() && !doc["t"].isNull())                    t = doc["t"].as<float>();
    else if (doc.is<float>() || doc.is<int>() || doc.is<double>())     t = doc.as<float>();
    else                                                               parseFloatFlexible(payload, t);

    if (!isnan(t)) {
        g_setpoint = t;
        StateService::saveSetpoint(g_setpoint);
        MqttService::publishReportedSetpoint();
    }
}

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
        Failsafe::Config fs;
        fs.HARD_MIN = g_hardMin;
        fs.HARD_MAX = g_hardMax;
        fs.HYST     = g_hystC;
        Failsafe::updateConfig(fs);  // preserve current mode, only update thresholds
        Logger::info("alert_config applied: sp=%.2f min=%.2f max=%.2f hyst=%.2f",
                     g_setpoint, g_hardMin, g_hardMax, g_hystC);
    }

    if (changedFS) {
        // When turning OFF failsafe auto, release lamp control immediately
        if (!g_fsAuto && Failsafe::isActive()) {
            Failsafe::forceOff();
        }
        Logger::info("failsafe_auto: %s", g_fsAuto ? "true" : "false");
    }

    if (changed || changedFS) {
        // Publish merged reported/config with failsafe state
        MqttService::FailsafeParams fp;
        fp.fsAuto   = g_fsAuto;
        fp.fsMode   = (uint8_t)Failsafe::currentMode();
        fp.hardMin  = g_hardMin;
        fp.hardMax  = g_hardMax;
        fp.hyst     = g_hystC;
        fp.setpoint = g_setpoint;
        MqttService::publishReportedConfigFull(fp);
        // Immediate telemetry so mobile sees updated state right away
        publishTelemetry();
    }
}

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

    // Reset ALL sensors: teardown + re-init + clear suspect state
    if (key == "all_sensors") {
        publishEvent("restart", "ack");
        Logger::info("[RESTART] all_sensors — reinit %d sensors, clearing suspects", g_nSensors);

        // Re-init all sensor GPIO
        SensorService::init(PeripheralManager::config());

        // Clear all suspect tracking state
        memset(g_suspect, 0, sizeof(g_suspect));
        memset(g_invalidStart, 0, sizeof(g_invalidStart));
        memset(g_lastRebegin, 0, sizeof(g_lastRebegin));
        memset(g_outlier, 0, sizeof(g_outlier));
        memset(g_nanCount, 0, sizeof(g_nanCount));

        // Reset all-sensors-fail fallback
        g_allSensorsFailedAt = 0;
        g_timeFallbackActive = false;

        publishSuspectsSnapshot();
        publishEvent("restart", "done");
        return;
    }

    // Dynamic: check if key matches any sensor id
    if (PeripheralManager::findSensorIndex(key.c_str()) >= 0) {
        publishEvent("restart", "ack");
        SensorService::rebeginOne(key.c_str());

        // Clear suspect state for this sensor
        int idx = PeripheralManager::findSensorIndex(key.c_str());
        g_suspect[idx] = false;
        g_invalidStart[idx] = 0;
        g_lastRebegin[idx] = 0;
        g_outlier[idx] = false;

        publishSuspectsSnapshot();
        publishEvent("restart", "done");
        return;
    }

    Logger::warn("restart key unknown: %s", key.c_str());
}

// ===== NEW: desired/config handler =====
static void handleDesiredConfig(const String& payload) {
    PeripheralConfig newCfg;
    String err;

    if (!PeripheralManager::parseConfigJson(payload, newCfg, err)) {
        Logger::warn("Config parse failed: %s", err.c_str());
        StaticJsonDocument<256> d;
        d["device"] = DEVICE_ID;
        d["error"]  = err;
        d["ts"]     = nowISO8601();
        String out; serializeJson(d, out);
        MqttService::publish(MqttService::tReportedNS() + "config_error", out, false);
        return;
    }

    // Teardown → apply → re-init
    SensorService::teardown();
    LampService::teardown();

    if (!PeripheralManager::applyConfig(newCfg, err)) {
        Logger::warn("Config apply failed: %s", err.c_str());
        // Reload old config
        SensorService::init(PeripheralManager::config());
        LampService::init(PeripheralManager::config());
        return;
    }

    SensorService::init(PeripheralManager::config());
    LampService::init(PeripheralManager::config());

    // Reset suspect + NaN tracking
    g_nSensors = PeripheralManager::config().sensorCount;
    memset(g_suspect, 0, sizeof(g_suspect));
    memset(g_invalidStart, 0, sizeof(g_invalidStart));
    memset(g_nanCount, 0, sizeof(g_nanCount));

    MqttService::publishReportedConfig();
    Logger::info("Peripheral config applied via MQTT");
}

// ===== Schedule handler =====
static void handleDesiredSchedule(const String& payload) {
    String err;
    if (!webApplySchedule(payload, err)) {
        Logger::warn("Schedule apply failed: %s", err.c_str());
        StaticJsonDocument<256> d;
        d["device"] = DEVICE_ID;
        d["error"]  = err;
        d["ts"]     = nowISO8601();
        String out; serializeJson(d, out);
        MqttService::publish(MqttService::tReportedNS() + "schedule_error", out, false);
        return;
    }
    // Publish reported schedule on success
    MqttService::publish(MqttService::tReportedNS() + "schedule",
                         ScheduleManager::toJson(), true);
    Logger::info("Schedule applied via MQTT");
}

// ===== Telemetry publisher (dynamic) =====
static void publishTelemetry() {
    if (!MqttService::connected()) return;

    const auto& cfg = PeripheralManager::config();
    StaticJsonDocument<1280> d;
    d["device"] = DEVICE_ID;

    // Sensors array — only include sensors with valid readings
    JsonArray sensors = d.createNestedArray("sensors");
    for (uint8_t i = 0; i < g_nSensors; i++) {
        if (!tempValid(g_readings[i].temp)) continue; // skip disconnected
        JsonObject s = sensors.createNestedObject();
        s["id"] = cfg.sensors[i].id;
        s["t"]  = g_readings[i].temp;
        s["h"]  = isnan(g_readings[i].hum) ? 0.0 : g_readings[i].hum;
    }

    // Lamps array (dynamic)
    JsonArray lamps = d.createNestedArray("lamps");
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        JsonObject l = lamps.createNestedObject();
        l["id"]    = cfg.lamps[i].id;
        l["power"] = LampService::getLamp(i).power;
    }

    d["power_master"] = LampService::getMaster();

    // Config
    JsonObject cfgObj = d.createNestedObject("config");
    cfgObj["hard_min"]       = g_hardMin;
    cfgObj["hard_max"]       = g_hardMax;
    cfgObj["hyst"]           = g_hystC;
    cfgObj["dht_setpoint"]   = g_setpoint;
    cfgObj["failsafe_auto"]  = g_fsAuto;
    cfgObj["failsafe_mode"]  = (uint8_t)Failsafe::currentMode();

    // Sensor health (dynamic)
    uint8_t validCount = 0;
    float minT = 999.0f, maxT = -999.0f;
    for (uint8_t i = 0; i < g_nSensors; i++) {
        if (tempValid(g_readings[i].temp)) {
            validCount++;
            if (g_readings[i].temp < minT) minT = g_readings[i].temp;
            if (g_readings[i].temp > maxT) maxT = g_readings[i].temp;
        }
    }
    JsonObject health = d.createNestedObject("sensor_health");
    health["valid_count"] = validCount;
    health["spread_c"]    = (validCount >= 2) ? (maxT - minT) : 0.0;

    JsonArray suspects = health.createNestedArray("suspects");
    for (uint8_t i = 0; i < cfg.sensorCount; i++) {
        if (g_suspect[i]) {
            JsonObject o = suspects.createNestedObject();
            o["id"]     = cfg.sensors[i].id;
            o["device"] = DEVICE_ID;
        }
    }

    JsonArray outliers = health.createNestedArray("outliers");
    for (uint8_t i = 0; i < cfg.sensorCount; i++) {
        if (g_outlier[i]) {
            outliers.add(cfg.sensors[i].id);
        }
    }

    health["time_fallback"]  = g_timeFallbackActive;
    health["power_cycling"]  = SensorService::isPowerCycling();

    // Schedule state (compact — full windows fetched via /api/schedule or reported/schedule)
    const auto& sc = ScheduleManager::config();
    JsonObject sched = d.createNestedObject("schedule");
    sched["enabled"]    = sc.enabled;
    sched["mode"]       = sc.mode;
    sched["scope"]      = sc.scope;
    sched["active_idx"] = g_activeWindowIdx;
    sched["count"]      = sc.windowCount;

    d["ts"]           = millis();
    d["last_updated"] = nowISO8601();

    String out; serializeJson(d, out);
    MqttService::publish(tTelemetry(), out, false);
    Logger::info("Telemetry: %s", out.c_str());
}

// ===== Suspect tracking (dynamic) =====
static void updateSuspects() {
    // Suppress during power cycle — all readings are invalid by design
    if (SensorService::isPowerCycling()) return;

    const auto& cfg = PeripheralManager::config();
    const uint32_t now = millis();
    bool anyChanged = false;

    for (uint8_t i = 0; i < g_nSensors; i++) {
        bool prev = g_suspect[i];
        bool v = tempValid(g_readings[i].temp);

        if (!v) {
            if (g_invalidStart[i] == 0) g_invalidStart[i] = now;
            if (!g_suspect[i] && (now - g_invalidStart[i]) >= SUSPECT_MS) {
                g_suspect[i] = true;
                Logger::warn("[SUSPECT] %s -> suspect", cfg.sensors[i].id);
                publishEventEx("sensor_suspect", "up", cfg.sensors[i].id, 0);
            }
            // Auto-recovery: re-init GPIO every 5 min while suspect
            if (g_suspect[i] && (now - g_lastRebegin[i]) >= REBEGIN_INTERVAL_MS) {
                g_lastRebegin[i] = now;
                Logger::info("[RECOVERY] auto rebegin %s", cfg.sensors[i].id);
                SensorService::rebeginOne(cfg.sensors[i].id);
            }
        } else {
            if (g_suspect[i]) {
                uint32_t dur = g_invalidStart[i] ? (now - g_invalidStart[i]) : 0;
                Logger::info("[SUSPECT] %s recovered", cfg.sensors[i].id);
                publishEventEx("sensor_suspect", "down", cfg.sensors[i].id, dur);
            }
            g_invalidStart[i] = 0;
            g_lastRebegin[i] = 0;
            g_suspect[i] = false;
        }
        if (prev != g_suspect[i]) anyChanged = true;
    }

    if (anyChanged) publishSuspectsSnapshot();
}

// ===== Schedule-driven lamp control =====
// Returns true if schedule drove the decision (skip legacy failsafe).
static bool applyScheduleGlobal(float avgT, const struct tm& now) {
    const auto& sc = ScheduleManager::config();
    int idx = ScheduleManager::findWindow(now);
    g_activeWindowIdx = idx;

    if (idx < 0) {
        // No matching window
        if (sc.mode == 2) {
            // Fully-replace: IDLE when no window matches
            LampService::setMaster(false);
            LampService::setAll(0.0f);
            return true;
        }
        return false; // let legacy thermostat handle it
    }

    const auto& w = sc.windows[idx];

    // Overheat guard (modes 0 and 1 only)
    if (sc.mode != 2 && !isnan(avgT) && avgT > (g_hardMax + g_hystC)) {
        LampService::setMaster(false);
        LampService::setAll(0.0f);
        return true;
    }

    // Critical-temp override: force ON if temp below threshold
    if (!isnan(avgT) && isfinite(w.criticalTemp) && avgT < w.criticalTemp) {
        LampService::setMaster(true);
        LampService::setAll(1.0f);
        return true;
    }

    // Mode 1 (layered): legacy hard_min heating still applies
    if (sc.mode == 1 && !isnan(avgT) && avgT < (g_hardMin - g_hystC)) {
        LampService::setMaster(true);
        LampService::setAll(1.0f);
        return true;
    }

    // Default window state
    if (w.lampOn) {
        LampService::setMaster(true);
        LampService::setAll(1.0f);
    } else {
        LampService::setMaster(false);
        LampService::setAll(0.0f);
    }
    return true;
}

static bool applySchedulePerLamp(float avgT, const struct tm& now) {
    const auto& sc = ScheduleManager::config();
    const auto& pcfg = PeripheralManager::config();

    // Overheat guard: kill master if overheat in modes 0/1
    if (sc.mode != 2 && !isnan(avgT) && avgT > (g_hardMax + g_hystC)) {
        LampService::setMaster(false);
        LampService::setAll(0.0f);
        g_activeWindowIdx = -1;
        return true;
    }

    // Master ON so per-lamp control works
    LampService::setMaster(true);

    bool anyMatched = false;
    int firstIdx = -1;
    for (uint8_t i = 0; i < pcfg.lampCount; i++) {
        const char* lampId = pcfg.lamps[i].id;
        int idx = ScheduleManager::findWindow(now, lampId);
        if (idx < 0) {
            // No window for this lamp
            if (sc.mode == 2) {
                LampService::setLampById(lampId, 0.0f);
            }
            continue;
        }
        anyMatched = true;
        if (firstIdx < 0) firstIdx = idx;
        const auto& w = sc.windows[idx];

        bool forceOn = false;
        if (!isnan(avgT) && isfinite(w.criticalTemp) && avgT < w.criticalTemp) forceOn = true;
        if (sc.mode == 1 && !isnan(avgT) && avgT < (g_hardMin - g_hystC)) forceOn = true;

        float power = (forceOn || w.lampOn) ? 1.0f : 0.0f;
        LampService::setLampById(lampId, power);
    }
    g_activeWindowIdx = firstIdx;

    // If no window matched any lamp and mode is not fully-replace, let legacy handle
    if (!anyMatched && sc.mode != 2) {
        return false;
    }
    return true;
}

static void applyLampControl(float avgT, bool nowCycling) {
    const auto& sc = ScheduleManager::config();

    // User disabled auto → release control
    if (!g_fsAuto) {
        if (Failsafe::isActive() || g_timeFallbackActive) {
            if (g_timeFallbackActive) g_timeFallbackActive = false;
            Failsafe::forceOff();
        }
        g_activeWindowIdx = -1;
        return;
    }

    // Power cycling → skip (held by sensor state)
    if (nowCycling) return;

    // All-sensors-failed fallback (existing behavior, unchanged)
    if (isnan(avgT)) {
        if (g_allSensorsFailedAt == 0) g_allSensorsFailedAt = millis();
        if (millis() - g_allSensorsFailedAt > SENSOR_FAIL_TIMEOUT_MS) {
            bool night = isNightHour();
            if (night && !g_timeFallbackActive) {
                g_timeFallbackActive = true;
                LampService::setMaster(true);
                LampService::setAll(1.0f);
                Logger::warn("[SAFETY] ALL SENSORS FAILED — night fallback: HEATING ON");
            } else if (!night && g_timeFallbackActive) {
                g_timeFallbackActive = false;
                LampService::setMaster(false);
                LampService::setAll(0.0f);
                Logger::warn("[SAFETY] ALL SENSORS FAILED — day fallback: HEATING OFF");
            }
        }
        g_activeWindowIdx = -1;
        return;
    }

    // Sensor data available
    g_allSensorsFailedAt = 0;
    if (g_timeFallbackActive) {
        g_timeFallbackActive = false;
        Logger::info("[SAFETY] Sensors recovered, exiting time-based fallback");
    }

    // Try schedule first
    if (sc.enabled) {
        struct tm now;
        time_t t;
        time(&t);
        if (t > 100000) { // NTP synced
            localtime_r(&t, &now);
            bool handled = (sc.scope == 1) ? applySchedulePerLamp(avgT, now)
                                           : applyScheduleGlobal(avgT, now);
            if (handled) return;
        } else {
            Logger::warn("[SCHEDULE] NTP not synced, deferring to legacy failsafe");
        }
    }

    // Legacy failsafe thermostat
    g_activeWindowIdx = -1;
    Failsafe::tick(avgT, MqttService::connected(), WiFiService::isConnected());
}

// ===== Setup =====
void setup() {
    delay(300);
    Logger::begin();

    // GPIO init
    pinMode(RESET_BTN_PIN, INPUT_PULLUP);
    pinMode(WIFI_STATE, OUTPUT);
    digitalWrite(WIFI_STATE, LOW);

    // Init services
    StateService::begin();
    PeripheralManager::begin();
    ScheduleManager::begin();

    // Dynamic peripheral init
    SensorService::init(PeripheralManager::config());
    LampService::init(PeripheralManager::config());
    g_nSensors = PeripheralManager::config().sensorCount;

#if SENSOR_MOCK == 1
    SensorService::enableMock(true);
    for (uint8_t i = 0; i < g_nSensors; i++) {
        SensorService::setMockValues(i, 36.0f, 70.0f);
    }
#endif

    WiFiService::begin();

    // NTP time sync (GMT+7 WIB)
    configTime(NTP_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
    Logger::info("NTP configured (offset=%ld)", NTP_OFFSET_SEC);

    // Load setpoint & failsafe params
    g_setpoint = StateService::loadSetpoint();
    g_hardMin  = StateService::loadHardMin();
    g_hardMax  = StateService::loadHardMax();
    g_hystC    = StateService::loadHyst();
    g_fsAuto   = StateService::loadFailsafeAuto(true);

    Logger::info("Loaded setpoint=%.2f hardMin=%.2f hardMax=%.2f hyst=%.2f fsAuto=%d",
                 g_setpoint, g_hardMin, g_hardMax, g_hystC, g_fsAuto);

    Failsafe::Config fs;
    fs.HARD_MIN = g_hardMin;
    fs.HARD_MAX = g_hardMax;
    fs.HYST     = g_hystC;
    Failsafe::begin(fs);

    MqttService::begin(handleMqttMessage);
}

// ===== Loop =====
void loop() {
    WiFiService::loop();

    // Long-press reset WiFi + pairing-mode LED blink
    if (digitalRead(RESET_BTN_PIN) == LOW) {
        if (!btnPressed) { btnPressed = true; btnPressStart = millis(); btnLedToggle = millis(); btnLedState = false; }
        // Blink LED at 500ms while holding
        if (millis() - btnLedToggle >= 500) {
            btnLedToggle = millis();
            btnLedState = !btnLedState;
            digitalWrite(WIFI_STATE, btnLedState ? HIGH : LOW);
        }
        if (millis() - btnPressStart > RESET_HOLD_MS) {
            Logger::warn("Long-press: resetting WiFi & starting provisioning AP");
            WiFiService::resetCredentials();
            WiFiService::startProvisioningAP();
            btnPressed = false; // prevent re-trigger
        }
    } else {
        if (btnPressed) {
            // Released before timeout — restore normal LED state
            btnPressed = false;
            digitalWrite(WIFI_STATE, WiFiService::isConnected() ? HIGH : LOW);
        } else {
            // Normal operation: LED = WiFi connected
            digitalWrite(WIFI_STATE, WiFiService::isConnected() ? HIGH : LOW);
        }
    }

    // MQTT reconnect
    if (WiFiService::isConnected() && !MqttService::connected()) {
        MqttService::reconnect();
    }
    MqttService::loop();

    // ===== Tick power cycle state machine =====
    static bool wasPowerCycling = false;
    SensorService::tickPowerCycle();
    bool nowCycling = SensorService::isPowerCycling();
    if (wasPowerCycling && !nowCycling) {
        publishEvent("power_cycle", "done");
        Logger::info("[POWER_CYCLE] completed — sensors re-initialized");
    }
    wasPowerCycling = nowCycling;

    // ===== Read sensors (dynamic) =====
    g_nSensors = SensorService::readAll(g_readings, MAX_SENSORS);

    // ===== Consecutive NaN counting → power cycle trigger =====
    if (!nowCycling) {
        bool anyTriggered = false;
        for (uint8_t i = 0; i < g_nSensors; i++) {
            if (!tempValid(g_readings[i].temp)) {
                g_nanCount[i]++;
                if (g_nanCount[i] >= NAN_COUNT_THRESHOLD &&
                    (millis() - g_lastPowerCycle) >= POWER_CYCLE_COOLDOWN_MS) {
                    anyTriggered = true;
                    Logger::warn("[POWER_CYCLE] %s: %d consecutive NaN — triggering",
                        PeripheralManager::config().sensors[i].id, g_nanCount[i]);
                }
            } else {
                g_nanCount[i] = 0;
            }
        }
        if (anyTriggered) {
            SensorService::startPowerCycle();
            g_lastPowerCycle = millis();
            memset(g_nanCount, 0, sizeof(g_nanCount));
            publishEvent("power_cycle", "start");
        }
    }

    // ===== Suspect tracking =====
    updateSuspects();

    // ===== Temperature aggregation with outlier detection =====
    float avgT = NAN;
    memset(g_outlier, 0, sizeof(g_outlier));

    // Pass 1: collect valid readings
    float validTemps[MAX_SENSORS];
    uint8_t validIdx[MAX_SENSORS];
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < g_nSensors; i++) {
        if (tempValid(g_readings[i].temp)) {
            validTemps[validCount] = g_readings[i].temp;
            validIdx[validCount]   = i;
            validCount++;
        }
    }

    if (validCount >= 2) {
        // Sort to find median
        float sorted[MAX_SENSORS];
        memcpy(sorted, validTemps, validCount * sizeof(float));
        sortFloats(sorted, validCount);
        float median = sorted[validCount / 2];
        if (validCount % 2 == 0) {
            median = (sorted[validCount/2 - 1] + sorted[validCount/2]) / 2.0f;
        }

        // Pass 2: exclude outliers (> OUTLIER_THRESHOLD_C from median)
        float sum = 0.0f;
        uint8_t goodCount = 0;
        for (uint8_t v = 0; v < validCount; v++) {
            if (fabsf(validTemps[v] - median) > OUTLIER_THRESHOLD_C) {
                g_outlier[validIdx[v]] = true;
                Logger::warn("[OUTLIER] %s: %.1f°C (median=%.1f)",
                    PeripheralManager::config().sensors[validIdx[v]].id,
                    validTemps[v], median);
            } else {
                sum += validTemps[v];
                goodCount++;
            }
        }

        if (goodCount > 0) {
            avgT = sum / goodCount;
        }
        // If all excluded (rare), avgT stays NAN → failsafe holds current mode
    } else if (validCount == 1) {
        avgT = validTemps[0];
    }
    // validCount == 0 → avgT stays NAN

    // ===== Lamp control (schedule-aware) =====
    applyLampControl(avgT, nowCycling);

    // ===== Telemetry interval =====
    if (millis() - lastTelemetry >= TELEMETRY_MS) {
        lastTelemetry = millis();
        publishTelemetry();
    }

    // Scheduled restart
    if (g_pendingRestart && millis() >= g_restartAt) {
        publishEvent("restart", "done");
        delay(50);
        ESP.restart();
    }
}

// ===== Web API Bridge (called by WiFiService POST handlers) =====

void webApplySetpoint(float val) {
    g_setpoint = val;
    StateService::saveSetpoint(val);
    if (MqttService::connected()) MqttService::publishReportedSetpoint();
    Logger::info("[WEB] Setpoint → %.1f", val);
}

void webApplyFailsafeConfig(float hardMin, float hardMax, float hyst, bool fsAuto) {
    g_hardMin = hardMin;
    g_hardMax = hardMax;
    g_hystC   = hyst;
    g_fsAuto  = fsAuto;
    StateService::saveHardMin(hardMin);
    StateService::saveHardMax(hardMax);
    StateService::saveHyst(hyst);
    StateService::saveFailsafeAuto(fsAuto);

    Failsafe::Config fs;
    fs.HARD_MIN = hardMin;
    fs.HARD_MAX = hardMax;
    fs.HYST     = hyst;
    Failsafe::updateConfig(fs);

    if (!fsAuto) Failsafe::forceOff();

    if (MqttService::connected()) {
        MqttService::publishReportedConfig();
    }
    Logger::info("[WEB] Failsafe config → min=%.1f max=%.1f hyst=%.1f auto=%d", hardMin, hardMax, hyst, fsAuto);
}

void webApplyRestart(uint32_t delayMs) {
    g_pendingRestart = true;
    g_restartAt = millis() + delayMs;
    Logger::info("[WEB] Restart scheduled in %lu ms", delayMs);
}

bool webApplySchedule(const String& json, String& err) {
    LampScheduleConfig newCfg;
    if (!ScheduleManager::parseJson(json, newCfg, err)) return false;
    if (!ScheduleManager::applyConfig(newCfg, err)) return false;
    Logger::info("[WEB] Schedule applied (enabled=%d, %d windows)",
                 newCfg.enabled, newCfg.windowCount);
    return true;
}
