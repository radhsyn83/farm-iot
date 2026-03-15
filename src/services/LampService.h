#pragma once
#include <Arduino.h>
#include "PeripheralManager.h"
#include "StateService.h"

#if __has_include(<RBDdimmer.h>)
#include <RBDdimmer.h>
#endif

struct LampState {
    float power = 0.0f; // 0.0–1.0
};

class LampService {
public:
    /// Called once in setup() — no-op; use init() after PeripheralManager::begin()
    static void begin();

    /// Hot-reload: initialize lamps from config
    static void init(const PeripheralConfig& cfg);

    /// Hot-reload: tear down all lamp GPIO/PWM/dimmer resources
    static void teardown();

    // --- Per-lamp control by index ---
    static void setLamp(uint8_t index, float power);
    static LampState getLamp(uint8_t index);

    // --- Per-lamp control by id (for MQTT routing) ---
    static bool setLampById(const char* id, float power);
    static bool getLampById(const char* id, LampState& out);

    // --- Set all lamps at once (for failsafe) ---
    static void setAll(float power);

    // --- Master relay ---
    static void setMaster(bool on);
    static bool getMaster();

    // --- Persist / restore ---
    static void saveState();
    static void restoreState();

    static uint8_t count();

private:
    static void apply(uint8_t index, float percent);
    static void setupPin(uint8_t index);
    static void teardownPin(uint8_t index);

    static LampState _lamps[MAX_LAMPS];
    static LampConfig _configs[MAX_LAMPS];
    static uint8_t _count;
    static bool _masterOn;
    static uint8_t _masterPin;

    // AC dimmer placement-new buffers
#if __has_include(<RBDdimmer.h>)
    static alignas(dimmerLamp) uint8_t _dimBuf[MAX_LAMPS][sizeof(dimmerLamp)];
    static dimmerLamp* _dimmers[MAX_LAMPS];
#endif

    static const int PWM_FREQ = 1000;
    static const int PWM_RES  = 8;
};
