#include "PeripheralManager.h"
#include <Preferences.h>
#include "../helpers/Logger.h"
#include "../pins.h"

// ===== Statics =====
PeripheralConfig PeripheralManager::_cfg;
bool PeripheralManager::_initialized = false;

static const char* NVS_NS  = "periph";
static const char* NVS_KEY = "cfg";
static const uint32_t CONFIG_VERSION_MARKER = 0x50455202; // "PER\x02" — bumped to force re-default (strapping pin fix)

// ===== begin =====
void PeripheralManager::begin() {
    if (_initialized) return;

    if (!loadFromNvs()) {
        Logger::info("PeripheralManager: no saved config, loading defaults");
        loadDefaults(_cfg);
        saveToNvs();
    }
    allocatePwmChannels(_cfg);
    _initialized = true;

    Logger::info("PeripheralManager: %d sensors, %d lamps, master_pin=%d, ver=%u",
                 _cfg.sensorCount, _cfg.lampCount, _cfg.masterRelayPin, _cfg.version);
}

const PeripheralConfig& PeripheralManager::config() {
    return _cfg;
}

// ===== Defaults (matches original hardcoded pins) =====
void PeripheralManager::loadDefaults(PeripheralConfig& cfg) {
    memset(&cfg, 0, sizeof(cfg));

    // Sensor defaults
    cfg.sensorCount = 4;
    strncpy(cfg.sensors[0].id, "dht1", sizeof(cfg.sensors[0].id));
    cfg.sensors[0].pin  = DefaultPins::DHT1;
    cfg.sensors[0].type = 22;
    strncpy(cfg.sensors[1].id, "dht2", sizeof(cfg.sensors[1].id));
    cfg.sensors[1].pin  = DefaultPins::DHT2;
    cfg.sensors[1].type = 22;
    strncpy(cfg.sensors[2].id, "dht3", sizeof(cfg.sensors[2].id));
    cfg.sensors[2].pin  = DefaultPins::DHT3;
    cfg.sensors[2].type = 22;
    strncpy(cfg.sensors[3].id, "dht4", sizeof(cfg.sensors[3].id));
    cfg.sensors[3].pin  = DefaultPins::DHT4;
    cfg.sensors[3].type = 22;

    // Lamp defaults
    cfg.lampCount = 2;
    strncpy(cfg.lamps[0].id, "lamp1", sizeof(cfg.lamps[0].id));
    cfg.lamps[0].pin    = 25;
    cfg.lamps[0].mode   = LampMode::RELAY;
    cfg.lamps[0].zcPin  = 255;
    strncpy(cfg.lamps[1].id, "lamp2", sizeof(cfg.lamps[1].id));
    cfg.lamps[1].pin    = 26;
    cfg.lamps[1].mode   = LampMode::RELAY;
    cfg.lamps[1].zcPin  = 255;

    cfg.masterRelayPin = 33;
    cfg.version = CONFIG_VERSION_MARKER;
}

// ===== NVS =====
void PeripheralManager::saveToNvs() {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putBytes(NVS_KEY, &_cfg, sizeof(_cfg));
    prefs.end();
    Logger::info("PeripheralManager: config saved to NVS (%u bytes)", sizeof(_cfg));
}

bool PeripheralManager::loadFromNvs() {
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    size_t len = prefs.getBytesLength(NVS_KEY);
    if (len != sizeof(PeripheralConfig)) {
        prefs.end();
        return false;
    }
    PeripheralConfig tmp;
    prefs.getBytes(NVS_KEY, &tmp, sizeof(tmp));
    prefs.end();

    // Version check — reject old config to force re-default (e.g. strapping pin fix)
    if (tmp.version != CONFIG_VERSION_MARKER) {
        Logger::warn("PeripheralManager: NVS config version 0x%08X != expected 0x%08X, re-defaulting",
                     tmp.version, CONFIG_VERSION_MARKER);
        return false;
    }
    // Basic sanity
    if (tmp.sensorCount > MAX_SENSORS || tmp.lampCount > MAX_LAMPS) {
        Logger::warn("PeripheralManager: NVS config corrupt (counts out of range)");
        return false;
    }
    _cfg = tmp;
    return true;
}

// ===== Validation =====
bool PeripheralManager::validate(const PeripheralConfig& cfg, String& err) {
    if (cfg.sensorCount > MAX_SENSORS) {
        err = "Too many sensors (max " + String(MAX_SENSORS) + ")";
        return false;
    }
    if (cfg.lampCount > MAX_LAMPS) {
        err = "Too many lamps (max " + String(MAX_LAMPS) + ")";
        return false;
    }

    // Collect all used pins for duplicate check
    uint8_t usedPins[MAX_SENSORS + MAX_LAMPS + 2]; // +master +potential zc
    uint8_t usedCount = 0;

    auto addPin = [&](uint8_t pin, const char* label) -> bool {
        if (pin == 255) return true; // disabled
        if (GpioCheck::isFlashPin(pin)) {
            err = String(label) + ": pin " + String(pin) + " is a flash SPI pin (6-11)";
            return false;
        }
        if (GpioCheck::isStrapPin(pin)) {
            Logger::warn("%s: pin %d is a strapping pin (0,2,5,12,15) - may cause boot issues", label, pin);
        }
        // Check duplicate
        for (uint8_t i = 0; i < usedCount; i++) {
            if (usedPins[i] == pin) {
                err = String(label) + ": pin " + String(pin) + " already used";
                return false;
            }
        }
        usedPins[usedCount++] = pin;
        return true;
    };

    // Validate sensors
    for (uint8_t i = 0; i < cfg.sensorCount; i++) {
        const auto& s = cfg.sensors[i];
        if (strlen(s.id) == 0) { err = "Sensor " + String(i) + " has empty id"; return false; }
        if (s.type != 11 && s.type != 22) { err = String(s.id) + ": invalid type (use 11 or 22)"; return false; }
        if (!addPin(s.pin, s.id)) return false;
    }

    // Validate lamps
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        const auto& l = cfg.lamps[i];
        if (strlen(l.id) == 0) { err = "Lamp " + String(i) + " has empty id"; return false; }
        if (GpioCheck::isInputOnly(l.pin)) {
            err = String(l.id) + ": pin " + String(l.pin) + " is input-only (34-39)";
            return false;
        }
        if (!addPin(l.pin, l.id)) return false;
        if (l.mode == LampMode::ROBOTDYN_AC && l.zcPin != 255) {
            if (GpioCheck::isFlashPin(l.zcPin)) {
                err = String(l.id) + ": ZC pin " + String(l.zcPin) + " is a flash pin";
                return false;
            }
        }
        if (static_cast<uint8_t>(l.mode) > 2) {
            err = String(l.id) + ": invalid mode";
            return false;
        }
    }

    // Master relay pin
    if (cfg.masterRelayPin != 255) {
        if (GpioCheck::isInputOnly(cfg.masterRelayPin)) {
            err = "Master relay pin " + String(cfg.masterRelayPin) + " is input-only";
            return false;
        }
        if (!addPin(cfg.masterRelayPin, "master_relay")) return false;
    }

    // Check duplicate IDs
    for (uint8_t i = 0; i < cfg.sensorCount; i++) {
        for (uint8_t j = i + 1; j < cfg.sensorCount; j++) {
            if (strcmp(cfg.sensors[i].id, cfg.sensors[j].id) == 0) {
                err = "Duplicate sensor id: " + String(cfg.sensors[i].id);
                return false;
            }
        }
    }
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        for (uint8_t j = i + 1; j < cfg.lampCount; j++) {
            if (strcmp(cfg.lamps[i].id, cfg.lamps[j].id) == 0) {
                err = "Duplicate lamp id: " + String(cfg.lamps[i].id);
                return false;
            }
        }
    }

    return true;
}

// ===== PWM channel allocation =====
void PeripheralManager::allocatePwmChannels(PeripheralConfig& cfg) {
    uint8_t ch = 0;
    for (uint8_t i = 0; i < cfg.lampCount; i++) {
        if (cfg.lamps[i].mode == LampMode::PWM_DC) {
            cfg.lamps[i].pwmChannel = ch++;
        } else {
            cfg.lamps[i].pwmChannel = 255;
        }
    }
}

// ===== Apply config =====
bool PeripheralManager::applyConfig(const PeripheralConfig& newCfg, String& err) {
    // Validate first (before any teardown)
    if (!validate(newCfg, err)) return false;

    // Forward declarations — these are called from SensorService/LampService
    // We declare them extern here; the actual calls are orchestrated by the caller (main.cpp / web handler)
    // Instead, we just save the config. The caller must teardown + re-init services.
    _cfg = newCfg;
    allocatePwmChannels(_cfg);
    _cfg.version = CONFIG_VERSION_MARKER;
    saveToNvs();

    Logger::info("PeripheralManager: config v%u applied (%d sensors, %d lamps)",
                 _cfg.version, _cfg.sensorCount, _cfg.lampCount);
    return true;
}

// ===== Lookup helpers =====
int PeripheralManager::findSensorIndex(const char* id) {
    for (uint8_t i = 0; i < _cfg.sensorCount; i++) {
        if (strcmp(_cfg.sensors[i].id, id) == 0) return i;
    }
    return -1;
}

int PeripheralManager::findLampIndex(const char* id) {
    for (uint8_t i = 0; i < _cfg.lampCount; i++) {
        if (strcmp(_cfg.lamps[i].id, id) == 0) return i;
    }
    return -1;
}

// ===== JSON serialization =====
String PeripheralManager::configToJson() {
    StaticJsonDocument<1024> doc;

    JsonArray sensors = doc.createNestedArray("sensors");
    for (uint8_t i = 0; i < _cfg.sensorCount; i++) {
        JsonObject s = sensors.createNestedObject();
        s["id"]   = _cfg.sensors[i].id;
        s["pin"]  = _cfg.sensors[i].pin;
        s["type"] = _cfg.sensors[i].type;
    }

    JsonArray lamps = doc.createNestedArray("lamps");
    for (uint8_t i = 0; i < _cfg.lampCount; i++) {
        JsonObject l = lamps.createNestedObject();
        l["id"]  = _cfg.lamps[i].id;
        l["pin"] = _cfg.lamps[i].pin;
        switch (_cfg.lamps[i].mode) {
            case LampMode::RELAY:       l["mode"] = "relay";    break;
            case LampMode::PWM_DC:      l["mode"] = "pwm";      break;
            case LampMode::ROBOTDYN_AC: l["mode"] = "ac_dimmer"; break;
        }
        if (_cfg.lamps[i].zcPin != 255) l["zcPin"] = _cfg.lamps[i].zcPin;
    }

    doc["masterRelayPin"] = _cfg.masterRelayPin;
    doc["version"]        = _cfg.version;

    String out;
    serializeJson(doc, out);
    return out;
}

bool PeripheralManager::parseConfigJson(const String& json, PeripheralConfig& out, String& err) {
    StaticJsonDocument<1024> doc;
    DeserializationError de = deserializeJson(doc, json);
    if (de) {
        err = "JSON parse error: " + String(de.c_str());
        return false;
    }

    memset(&out, 0, sizeof(out));

    // Sensors
    if (doc.containsKey("sensors")) {
        JsonArray arr = doc["sensors"].as<JsonArray>();
        if (arr.size() > MAX_SENSORS) {
            err = "Too many sensors (max " + String(MAX_SENSORS) + ")";
            return false;
        }
        out.sensorCount = arr.size();
        for (uint8_t i = 0; i < out.sensorCount; i++) {
            JsonObject s = arr[i];
            if (!s.containsKey("id") || !s.containsKey("pin")) {
                err = "Sensor " + String(i) + " missing id or pin";
                return false;
            }
            strncpy(out.sensors[i].id, s["id"].as<const char*>(), sizeof(out.sensors[i].id) - 1);
            out.sensors[i].pin  = s["pin"].as<uint8_t>();
            out.sensors[i].type = s.containsKey("type") ? s["type"].as<uint8_t>() : 22; // default DHT22
        }
    }

    // Lamps
    if (doc.containsKey("lamps")) {
        JsonArray arr = doc["lamps"].as<JsonArray>();
        if (arr.size() > MAX_LAMPS) {
            err = "Too many lamps (max " + String(MAX_LAMPS) + ")";
            return false;
        }
        out.lampCount = arr.size();
        for (uint8_t i = 0; i < out.lampCount; i++) {
            JsonObject l = arr[i];
            if (!l.containsKey("id") || !l.containsKey("pin")) {
                err = "Lamp " + String(i) + " missing id or pin";
                return false;
            }
            strncpy(out.lamps[i].id, l["id"].as<const char*>(), sizeof(out.lamps[i].id) - 1);
            out.lamps[i].pin = l["pin"].as<uint8_t>();

            // Parse mode
            out.lamps[i].mode = LampMode::RELAY; // default
            if (l.containsKey("mode")) {
                String m = l["mode"].as<String>();
                m.toLowerCase();
                if (m == "pwm" || m == "pwm_dc")          out.lamps[i].mode = LampMode::PWM_DC;
                else if (m == "ac_dimmer" || m == "ac" || m == "robotdyn_ac") out.lamps[i].mode = LampMode::ROBOTDYN_AC;
                else if (m == "relay")                     out.lamps[i].mode = LampMode::RELAY;
                else { err = String(out.lamps[i].id) + ": unknown mode '" + m + "'"; return false; }
            }

            out.lamps[i].zcPin = l.containsKey("zcPin") ? l["zcPin"].as<uint8_t>() : 255;
        }
    }

    // Master relay
    out.masterRelayPin = doc.containsKey("masterRelayPin")
                         ? doc["masterRelayPin"].as<uint8_t>()
                         : 255;

    out.version = 0; // will be set by applyConfig

    return true;
}
