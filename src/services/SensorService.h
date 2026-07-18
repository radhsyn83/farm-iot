#pragma once
#include <DHT.h>
#include "PeripheralManager.h"
#include "../helpers/Logger.h"

struct SensorReading {
    float temp;
    float hum;
};

class SensorService {
public:
    /// Called once in setup() — no-op; use init() after PeripheralManager::begin()
    static void begin();

    /// Hot-reload: initialize sensors from config
    static void init(const PeripheralConfig& cfg);

    /// Hot-reload: tear down all sensor objects
    static void teardown();

    /// Read all sensors. Returns number of sensors read.
    static uint8_t readAll(SensorReading* out, uint8_t maxCount);

    /// Re-initialize a single sensor by id (e.g., "dht1")
    static void rebeginOne(const char* id);

    /// Set sensor power pin (call from init, 255 = disabled)
    static void setPowerPin(uint8_t pin);

    /// Start a non-blocking hardware power cycle (shared power pin)
    static void startPowerCycle();

    /// Advance power cycle state machine — call every loop()
    static bool tickPowerCycle();

    /// Is a power cycle currently in progress?
    static bool isPowerCycling();

    /// Mock support
    static void enableMock(bool on);
    static void setMockValues(uint8_t index, float t, float h);

    /// Current sensor count
    static uint8_t count();

private:
    // Placement-new buffers — no heap allocation
    static alignas(DHT) uint8_t _dhtBuf[MAX_SENSORS][sizeof(DHT)];
    static DHT*    _dhts[MAX_SENSORS];
    static uint8_t _pins[MAX_SENSORS];
    static uint8_t _types[MAX_SENSORS];
    static uint8_t _count;

    static bool  _mockOn;
    static float _mockT[MAX_SENSORS];
    static float _mockH[MAX_SENSORS];

    // Power cycle state machine
    enum class PowerState : uint8_t { IDLE, POWER_OFF, POWER_ON_WAIT };
    static PowerState _powerState;
    static uint32_t   _powerStateStart;
    static uint8_t    _powerPin;
    static constexpr uint32_t POWER_OFF_MS = 2000;  // hold LOW for 2s
    static constexpr uint32_t POWER_ON_MS  = 2000;  // wait HIGH for 2s before re-init
};
