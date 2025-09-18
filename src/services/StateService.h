#pragma once
#include <Arduino.h>
#include "../helpers/ConfigHelper.h"

class StateService {
public:
  static void begin();

  // Lamp states
  static void saveLamp1(uint8_t power);
  static void saveLamp2(uint8_t power);
  static void saveMaster(bool on);

  static uint8_t loadLamp1();
  static uint8_t loadLamp2();
  static bool loadMaster();

  // Setpoint
  static void saveSetpoint(float val);
  static float loadSetpoint();

private:
  static constexpr const char* NS = "state"; // namespace
};
