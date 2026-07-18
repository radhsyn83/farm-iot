#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

// ===== Limits =====
#define MAX_SCHEDULE_WINDOWS 8

// ===== Schedule structs =====
struct LampScheduleWindow {
    uint8_t startHour;    // 0-23
    uint8_t startMin;     // 0-59
    uint8_t endHour;      // 0-23
    uint8_t endMin;       // 0-59
    bool    lampOn;       // default state during window
    float   criticalTemp; // force ON below this temp (NAN = disabled)
    char    lampId[12];   // "*" = all lamps; otherwise specific lamp id
};

struct LampScheduleConfig {
    bool     enabled;
    uint8_t  mode;        // 0 = replace heating, 1 = layered, 2 = fully replace
    uint8_t  scope;       // 0 = global, 1 = per-lamp
    uint8_t  windowCount;
    LampScheduleWindow windows[MAX_SCHEDULE_WINDOWS];
    uint32_t version;
};

class ScheduleManager {
public:
    /// Load config from NVS (or defaults on first boot)
    static void begin();

    /// Current config (read-only)
    static const LampScheduleConfig& config();

    /// Validate + persist + activate new config
    static bool applyConfig(const LampScheduleConfig& cfg, String& err);

    /// Parse JSON into LampScheduleConfig
    static bool parseJson(const String& json, LampScheduleConfig& out, String& err);

    /// Serialize current config to JSON
    static String toJson();

    /// Apply defaults (empty schedule, disabled)
    static void loadDefaults(LampScheduleConfig& cfg);

    /// Find the index of the currently-active window for `now` (-1 if none).
    /// If scope=per-lamp, returns first matching window for lampIdFilter (nullptr = any).
    static int findWindow(const struct tm& now, const char* lampIdFilter = nullptr);

    /// Does `now` fall inside `w`? Handles midnight wrap-around.
    static bool inWindow(const LampScheduleWindow& w, const struct tm& now);

private:
    static LampScheduleConfig _cfg;
    static bool _initialized;

    static void saveToNvs();
    static bool loadFromNvs();
    static bool validate(const LampScheduleConfig& cfg, String& err);
};
