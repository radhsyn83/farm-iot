#pragma once
#include <Arduino.h>
#include "../helpers/ConfigHelper.h"

class StateService {
public:
  static void begin();

  // Lamp states (float 0.0–1.0)
  static void saveLamp1(float power01);
  static void saveLamp2(float power01);
  static void saveMaster(bool on);
  static float loadLamp1();
  static float loadLamp2();
  static bool  loadMaster();

  // Setpoint
  static void saveSetpoint(float val);
  static float loadSetpoint();

  // ===== Failsafe params (persist) =====
  static void   saveHardMin(float c);
  static void   saveHardMax(float c);
  static void   saveHyst(float c);
  static float  loadHardMin(float def = 20.0f);
  static float  loadHardMax(float def = 38.0f);
  static float  loadHyst(float def = 1.0f);

private:
  static constexpr const char* NS = "state"; // namespace
};