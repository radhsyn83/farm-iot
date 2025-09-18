#include "LampService.h"
#include "StateService.h"
#include "../helpers/Logger.h"

#if __has_include(<RBDdimmer.h>)
#include <RBDdimmer.h>
#endif

LampState LampService::l1;
LampState LampService::l2;
bool LampService::masterOn = true;

// ===== PWM DC Config =====
static const int PWM_CHANNEL_1 = 0;
static const int PWM_CHANNEL_2 = 1;
static const int PWM_FREQ = 1000;
static const int PWM_RES  = 8;

// ===== RobotDyn AC Dimmer =====
#if __has_include(<RBDdimmer.h>)
static dimmerLamp dimmer1(ZC_PIN, LAMP1_PIN);
static dimmerLamp dimmer2(ZC_PIN, LAMP2_PIN);
#endif

void LampService::setupPin(uint8_t pin) {
  if (LAMP_MODE == LampMode::PWM_DC) {
    if (pin == LAMP1_PIN) {
      ledcSetup(PWM_CHANNEL_1, PWM_FREQ, PWM_RES);
      ledcAttachPin(LAMP1_PIN, PWM_CHANNEL_1);
    } else if (pin == LAMP2_PIN) {
      ledcSetup(PWM_CHANNEL_2, PWM_FREQ, PWM_RES);
      ledcAttachPin(LAMP2_PIN, PWM_CHANNEL_2);
    }
  } else if (LAMP_MODE == LampMode::RELAY) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  if (MASTER_RELAY_PIN != 255) {
    pinMode(MASTER_RELAY_PIN, OUTPUT);
    digitalWrite(MASTER_RELAY_PIN, HIGH);
  }
}

void LampService::saveState() {
  StateService::saveLamp1(l1.power);
  StateService::saveLamp2(l2.power);
  StateService::saveMaster(masterOn);
}

void LampService::restoreState() {
  l1.power = StateService::loadLamp1();
  l2.power = StateService::loadLamp2();
  masterOn = StateService::loadMaster();

  apply(LAMP1_PIN, l1.power);
  apply(LAMP2_PIN, l2.power);

  Logger::info("Restored state L1=%d L2=%d Master=%d",
               l1.power, l2.power, masterOn);
}

void LampService::begin() {
  if (LAMP_MODE == LampMode::ROBOTDYN_AC) {
  #if __has_include(<RBDdimmer.h>)
    dimmer1.begin(NORMAL_MODE, ON);
    dimmer2.begin(NORMAL_MODE, ON);
  #endif
  } else {
    setupPin(LAMP1_PIN);
    setupPin(LAMP2_PIN);
  }
  restoreState(); // load state terakhir
}

void LampService::apply(uint8_t pin, uint8_t percent) {
  if (!masterOn) percent = 0;
  percent = constrain(percent, 0, 100);

  if (LAMP_MODE == LampMode::ROBOTDYN_AC) {
  #if __has_include(<RBDdimmer.h>)
    if (pin == LAMP1_PIN) dimmer1.setPower(percent);
    else if (pin == LAMP2_PIN) dimmer2.setPower(percent);
  #endif
  } else if (LAMP_MODE == LampMode::PWM_DC) {
    int duty = map(percent, 0, 100, 0, 255);
    if (pin == LAMP1_PIN) ledcWrite(PWM_CHANNEL_1, duty);
    else if (pin == LAMP2_PIN) ledcWrite(PWM_CHANNEL_2, duty);
  } else { // RELAY
    digitalWrite(pin, (percent >= 50) ? HIGH : LOW);
  }
}

void LampService::setMaster(bool on) {
  masterOn = on;
  if (MASTER_RELAY_PIN != 255)
    digitalWrite(MASTER_RELAY_PIN, on ? LOW : HIGH);

  apply(LAMP1_PIN, l1.power);
  apply(LAMP2_PIN, l2.power);
  LampService::saveState();
}

bool LampService::getMaster() { return masterOn; }

void LampService::setLamp1(uint8_t power) {
  l1.power = constrain(power, 0, 100);
  l1.on = l1.power > 0;
  apply(LAMP1_PIN, l1.power);
  LampService::saveState();
}

void LampService::setLamp2(uint8_t power) {
  l2.power = constrain(power, 0, 100);
  l2.on = l2.power > 0;
  apply(LAMP2_PIN, l2.power);
  LampService::saveState();
}

LampState LampService::getLamp1() { return l1; }
LampState LampService::getLamp2() { return l2; }

void LampService::switchLamp1(bool on) { setLamp1(on ? 100 : 0); }
void LampService::switchLamp2(bool on) { setLamp2(on ? 100 : 0); }
