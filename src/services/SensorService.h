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
};
