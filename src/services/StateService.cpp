#include "StateService.h"
#include "../helpers/Logger.h"

void StateService::begin() {
  ConfigHelper::begin(NS);
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float normalizePower(float v) {
  if (v > 1.001f) return clamp01(v / 100.0f); // backward compat nilai 0–100
  return clamp01(v);
}

void StateService::saveLamp1(float p) {
  p = clamp01(p);
  ConfigHelper::saveFloat("lamp1", p);
}
float StateService::loadLamp1() {
  return normalizePower(ConfigHelper::loadFloat("lamp1", 0.0));
}

void StateService::saveLamp2(float p) {
  p = clamp01(p);
  ConfigHelper::saveFloat("lamp2", p);
}
float StateService::loadLamp2() {
  return normalizePower(ConfigHelper::loadFloat("lamp2", 0.0));
}

void StateService::saveMaster(bool on) {
  ConfigHelper::save("master", on ? "1" : "0");
}
bool StateService::loadMaster() {
  return ConfigHelper::load("master", "1") == "1";
}

void StateService::saveSetpoint(float val) {
  ConfigHelper::save("setpoint", String(val, 2));
}
float StateService::loadSetpoint() {
  return ConfigHelper::load("setpoint", "32.0").toFloat();
}

// ===== Failsafe params =====
void StateService::saveHardMin(float c) {
  ConfigHelper::save("hard_min", String(c, 2));
}
void StateService::saveHardMax(float c) {
  ConfigHelper::save("hard_max", String(c, 2));
}
void StateService::saveHyst(float c) {
  ConfigHelper::save("hyst_c", String(c, 2));
}

float StateService::loadHardMin(float def) {
  String s = ConfigHelper::load("hard_min", String(def, 2));
  return s.toFloat();
}
float StateService::loadHardMax(float def) {
  String s = ConfigHelper::load("hard_max", String(def, 2));
  return s.toFloat();
}
float StateService::loadHyst(float def) {
  String s = ConfigHelper::load("hyst_c", String(def, 2));
  return s.toFloat();
}