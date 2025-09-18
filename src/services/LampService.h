#pragma once
#include <Arduino.h>
#include "../pins.h"
#include "../config.h"
#include "StateService.h"

struct LampState {
  bool on = false;
  uint8_t power = 0; // 0–100 %
};

class LampService {
public:
  static void begin();

  static void setMaster(bool on);
  static bool getMaster();

  static void setLamp1(uint8_t power);
  static void setLamp2(uint8_t power);

  static LampState getLamp1();
  static LampState getLamp2();

  static void switchLamp1(bool on);
  static void switchLamp2(bool on);

  static void saveState();
  static void restoreState();

private:
  static void apply(uint8_t pin, uint8_t percent);
  static void setupPin(uint8_t pin);

  static LampState l1;
  static LampState l2;
  static bool masterOn;
};
