#include "ScheduleManager.h"
#include <Preferences.h>
#include <math.h>
#include "../helpers/Logger.h"

// ===== Statics =====
LampScheduleConfig ScheduleManager::_cfg;
bool ScheduleManager::_initialized = false;

static const char* NVS_NS  = "sched";
static const char* NVS_KEY = "cfg";
static const uint32_t CONFIG_VERSION_MARKER = 0x53434801; // "SCH\x01"

// ===== begin =====
void ScheduleManager::begin() {
    if (_initialized) return;

    if (!loadFromNvs()) {
        Logger::info("ScheduleManager: no saved config, loading defaults");
        loadDefaults(_cfg);
        saveToNvs();
    }
    _initialized = true;

    Logger::info("ScheduleManager: enabled=%d mode=%d scope=%d windows=%d ver=%u",
                 _cfg.enabled, _cfg.mode, _cfg.scope, _cfg.windowCount, _cfg.version);
}

const LampScheduleConfig& ScheduleManager::config() {
    return _cfg;
}

// ===== Defaults =====
void ScheduleManager::loadDefaults(LampScheduleConfig& cfg) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled     = false;
    cfg.mode        = 0;
    cfg.scope       = 0;
    cfg.windowCount = 0;
    cfg.version     = CONFIG_VERSION_MARKER;
}

// ===== NVS =====
void ScheduleManager::saveToNvs() {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putBytes(NVS_KEY, &_cfg, sizeof(_cfg));
    prefs.end();
    Logger::info("ScheduleManager: config saved to NVS (%u bytes)", sizeof(_cfg));
}

bool ScheduleManager::loadFromNvs() {
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    size_t len = prefs.getBytesLength(NVS_KEY);
    if (len != sizeof(LampScheduleConfig)) {
        prefs.end();
        return false;
    }
    LampScheduleConfig tmp;
    prefs.getBytes(NVS_KEY, &tmp, sizeof(tmp));
    prefs.end();

    if (tmp.version != CONFIG_VERSION_MARKER) {
        Logger::warn("ScheduleManager: NVS version 0x%08X != 0x%08X, re-defaulting",
                     tmp.version, CONFIG_VERSION_MARKER);
        return false;
    }
    if (tmp.windowCount > MAX_SCHEDULE_WINDOWS) {
        Logger::warn("ScheduleManager: NVS corrupt (windowCount=%d)", tmp.windowCount);
        return false;
    }
    _cfg = tmp;
    return true;
}

// ===== Validation =====
bool ScheduleManager::validate(const LampScheduleConfig& cfg, String& err) {
    if (cfg.mode > 2)  { err = "Invalid mode (0-2)";  return false; }
    if (cfg.scope > 1) { err = "Invalid scope (0-1)"; return false; }
    if (cfg.windowCount > MAX_SCHEDULE_WINDOWS) {
        err = "Too many windows (max " + String(MAX_SCHEDULE_WINDOWS) + ")";
        return false;
    }
    for (uint8_t i = 0; i < cfg.windowCount; i++) {
        const auto& w = cfg.windows[i];
        if (w.startHour > 23 || w.endHour > 23) { err = "Hour out of range (0-23)"; return false; }
        if (w.startMin > 59  || w.endMin > 59)  { err = "Minute out of range (0-59)"; return false; }
        // Allow start==end as a 24h window? Reject to avoid ambiguity.
        if (w.startHour == w.endHour && w.startMin == w.endMin) {
            err = "Window " + String(i) + " has zero duration";
            return false;
        }
    }
    return true;
}

// ===== Apply =====
bool ScheduleManager::applyConfig(const LampScheduleConfig& newCfg, String& err) {
    if (!validate(newCfg, err)) return false;
    _cfg = newCfg;
    _cfg.version = CONFIG_VERSION_MARKER;
    saveToNvs();
    Logger::info("ScheduleManager: config applied (enabled=%d, %d windows)",
                 _cfg.enabled, _cfg.windowCount);
    return true;
}

// ===== JSON serialize =====
String ScheduleManager::toJson() {
    StaticJsonDocument<1024> doc;
    doc["enabled"] = _cfg.enabled;
    doc["mode"]    = _cfg.mode;
    doc["scope"]   = _cfg.scope;

    JsonArray arr = doc.createNestedArray("windows");
    for (uint8_t i = 0; i < _cfg.windowCount; i++) {
        const auto& w = _cfg.windows[i];
        JsonObject o = arr.createNestedObject();
        o["sh"]   = w.startHour;
        o["sm"]   = w.startMin;
        o["eh"]   = w.endHour;
        o["em"]   = w.endMin;
        o["on"]   = w.lampOn;
        o["lamp"] = w.lampId;
        if (isfinite(w.criticalTemp)) o["ct"] = w.criticalTemp;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ===== JSON parse =====
bool ScheduleManager::parseJson(const String& json, LampScheduleConfig& out, String& err) {
    StaticJsonDocument<1536> doc;
    DeserializationError de = deserializeJson(doc, json);
    if (de) {
        err = "JSON parse error: " + String(de.c_str());
        return false;
    }

    memset(&out, 0, sizeof(out));
    out.enabled = doc["enabled"] | false;
    out.mode    = doc["mode"]    | 0;
    out.scope   = doc["scope"]   | 0;

    if (doc.containsKey("windows") && doc["windows"].is<JsonArray>()) {
        JsonArray arr = doc["windows"].as<JsonArray>();
        if (arr.size() > MAX_SCHEDULE_WINDOWS) {
            err = "Too many windows (max " + String(MAX_SCHEDULE_WINDOWS) + ")";
            return false;
        }
        out.windowCount = arr.size();
        for (uint8_t i = 0; i < out.windowCount; i++) {
            JsonObject o = arr[i];
            out.windows[i].startHour    = o["sh"] | 0;
            out.windows[i].startMin     = o["sm"] | 0;
            out.windows[i].endHour      = o["eh"] | 0;
            out.windows[i].endMin       = o["em"] | 0;
            out.windows[i].lampOn       = o["on"] | false;
            out.windows[i].criticalTemp = o.containsKey("ct") ? o["ct"].as<float>() : NAN;
            const char* lid = o["lamp"] | "*";
            strncpy(out.windows[i].lampId, lid, sizeof(out.windows[i].lampId) - 1);
            out.windows[i].lampId[sizeof(out.windows[i].lampId) - 1] = '\0';
        }
    }

    out.version = 0; // set by applyConfig
    return true;
}

// ===== Window evaluation =====
bool ScheduleManager::inWindow(const LampScheduleWindow& w, const struct tm& now) {
    uint16_t nowMin   = now.tm_hour * 60 + now.tm_min;
    uint16_t startMin = w.startHour * 60 + w.startMin;
    uint16_t endMin   = w.endHour   * 60 + w.endMin;

    if (startMin < endMin) {
        // Non-wrapping: [start, end)
        return nowMin >= startMin && nowMin < endMin;
    } else {
        // Wrapping: [start, 24:00) OR [00:00, end)
        return nowMin >= startMin || nowMin < endMin;
    }
}

int ScheduleManager::findWindow(const struct tm& now, const char* lampIdFilter) {
    for (uint8_t i = 0; i < _cfg.windowCount; i++) {
        const auto& w = _cfg.windows[i];
        if (!inWindow(w, now)) continue;
        if (lampIdFilter != nullptr) {
            // Per-lamp: match "*" (all) or exact id
            if (strcmp(w.lampId, "*") != 0 && strcmp(w.lampId, lampIdFilter) != 0) continue;
        }
        return i;
    }
    return -1;
}
