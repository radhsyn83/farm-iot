#include "StateService.h"
#include "../helpers/Logger.h"

void StateService::begin() {
  ConfigHelper::begin(NS);
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float normalizePower(float v) {
  if (v > 1.001f) return clamp01(v / 100.0f); // backward compat kalau tersimpan 0–100
  return clamp01(v);
}

void StateService::saveLamp1(float p) {
  p = clamp01(p);
  ConfigHelper::saveFloat("lamp1", p);   // <— simpan float asli
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
