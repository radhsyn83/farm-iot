#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ===== Limits =====
#define MAX_SENSORS 8
#define MAX_LAMPS   8

// ===== Lamp mode (per-lamp) =====
enum class LampMode : uint8_t { RELAY = 0, PWM_DC = 1, ROBOTDYN_AC = 2 };

// ===== Peripheral config structs =====
struct SensorConfig {
    char    id[12];     // "dht1", "dht2", ...
    uint8_t pin;        // GPIO
    uint8_t type;       // 22 = DHT22, 11 = DHT11
};

struct LampConfig {
    char     id[12];    // "lamp1", "lamp2", ...
    uint8_t  pin;       // GPIO
    LampMode mode;      // RELAY, PWM_DC, ROBOTDYN_AC
    uint8_t  zcPin;     // zero-crossing pin (only for ROBOTDYN_AC, 255 = unused)
    uint8_t  pwmChannel;// auto-assigned, not user-facing
};

struct PeripheralConfig {
    SensorConfig sensors[MAX_SENSORS];
    uint8_t      sensorCount;
    LampConfig   lamps[MAX_LAMPS];
    uint8_t      lampCount;
    uint8_t      masterRelayPin;  // 255 = disabled
    uint8_t      sensorPowerPin;  // 255 = disabled (no HW power cycling)
    uint32_t     version;         // incremented on each save
};

// ===== GPIO validation helpers =====
namespace GpioCheck {
    inline bool isFlashPin(uint8_t p)  { return p >= 6 && p <= 11; }
    inline bool isInputOnly(uint8_t p) { return p >= 34 && p <= 39; }
    inline bool isStrapPin(uint8_t p)  { return p==0||p==2||p==5||p==12||p==15; }
}

// ===== PeripheralManager =====
class PeripheralManager {
public:
    /// Load config from NVS (or apply defaults on first boot)
    static void begin();

    /// Current config (read-only)
    static const PeripheralConfig& config();

    /// Apply a new config: validate → teardown → persist NVS → re-init services
    /// Returns true on success. On failure, `err` contains a human-readable message.
    static bool applyConfig(const PeripheralConfig& newCfg, String& err);

    /// Parse JSON string into PeripheralConfig
    static bool parseConfigJson(const String& json, PeripheralConfig& out, String& err);

    /// Serialize current config to JSON string
    static String configToJson();

    /// Fill `cfg` with factory defaults matching original hardcoded pins
    static void loadDefaults(PeripheralConfig& cfg);

    /// Lookup helpers (return -1 if not found)
    static int findSensorIndex(const char* id);
    static int findLampIndex(const char* id);

private:
    static PeripheralConfig _cfg;
    static bool _initialized;

    // NVS
    static void saveToNvs();
    static bool loadFromNvs();

    // Validation
    static bool validate(const PeripheralConfig& cfg, String& err);

    // PWM channel allocation
    static void allocatePwmChannels(PeripheralConfig& cfg);
};
