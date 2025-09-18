#pragma once
#include "StateService.h"
#include "../helpers/Logger.h"

void StateService::begin() {
  ConfigHelper::begin(NS);
}

// Lamp1
void StateService::saveLamp1(uint8_t power) { ConfigHelper::save("lamp1", String(power)); }
uint8_t StateService::loadLamp1() { return ConfigHelper::load("lamp1", "0").toInt(); }

// Lamp2
void StateService::saveLamp2(uint8_t power) { ConfigHelper::save("lamp2", String(power)); }
uint8_t StateService::loadLamp2() { return ConfigHelper::load("lamp2", "0").toInt(); }

// Master
void StateService::saveMaster(bool on) { ConfigHelper::save("master", on ? "1" : "0"); }
bool StateService::loadMaster() { return ConfigHelper::load("master", "1") == "1"; }

// Setpoint
void StateService::saveSetpoint(float val) { ConfigHelper::save("setpoint", String(val, 2)); }
float StateService::loadSetpoint() { return ConfigHelper::load("setpoint", "32.0").toFloat(); }
