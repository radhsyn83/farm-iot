#pragma once
#include <Arduino.h>
#include "services/SensorService.h"
#include "services/PeripheralManager.h"

// ===== Globals defined in main.cpp, accessible by WiFiService for web API =====

extern SensorReading g_readings[];
extern uint8_t       g_nSensors;
extern float         g_setpoint;
extern float         g_hardMin;
extern float         g_hardMax;
extern float         g_hystC;
extern bool          g_fsAuto;
extern bool          g_suspect[];
extern bool          g_outlier[];
extern bool          g_timeFallbackActive;
extern bool          g_pendingRestart;
extern uint32_t      g_restartAt;

// Called by web API POST handlers to apply changes (defined in main.cpp)
void webApplySetpoint(float val);
void webApplyFailsafeConfig(float hardMin, float hardMax, float hyst, bool fsAuto);
void webApplyRestart(uint32_t delayMs = 500);
