#pragma once
#include <Arduino.h>
#include "../helpers/ConfigHelper.h"

class StateService {
public:
    static void begin();

    // Dynamic lamp state: key = "lp_" + lampId (e.g., "lp_lamp1")
    static void  saveLampPower(const char* lampId, float power01);
    static float loadLampPower(const char* lampId, float def = 0.0f);
    static void  removeLampState(const char* lampId);

    // Master (unchanged)
    static void saveMaster(bool on);
    static bool loadMaster();

    // Setpoint
    static void  saveSetpoint(float val);
    static float loadSetpoint();

    // Failsafe params (persist)
    static void   saveHardMin(float c);
    static void   saveHardMax(float c);
    static void   saveHyst(float c);
    static float  loadHardMin(float def = 20.0f);
    static float  loadHardMax(float def = 38.0f);
    static float  loadHyst(float def = 1.0f);
    static bool   loadFailsafeAuto(bool defval);
    static void   saveFailsafeAuto(bool on);

private:
    static constexpr const char* NS = "state";
};
