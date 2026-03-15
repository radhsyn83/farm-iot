#include "StateService.h"
#include "../helpers/Logger.h"
#include <Preferences.h>

void StateService::begin() {
    ConfigHelper::begin(NS);
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float normalizePower(float v) {
    if (v > 1.001f) return clamp01(v / 100.0f); // backward compat 0–100
    return clamp01(v);
}

// ===== Dynamic lamp state =====
void StateService::saveLampPower(const char* lampId, float p) {
    p = clamp01(p);
    char key[16];
    snprintf(key, sizeof(key), "lp_%.11s", lampId); // "lp_lamp1" etc.
    ConfigHelper::saveFloat(key, p);
}

float StateService::loadLampPower(const char* lampId, float def) {
    char key[16];
    snprintf(key, sizeof(key), "lp_%.11s", lampId);
    return normalizePower(ConfigHelper::loadFloat(key, def));
}

void StateService::removeLampState(const char* lampId) {
    char key[16];
    snprintf(key, sizeof(key), "lp_%.11s", lampId);
    ConfigHelper::remove(key);
}

// ===== Master =====
void StateService::saveMaster(bool on) {
    ConfigHelper::save("master", on ? "1" : "0");
}
bool StateService::loadMaster() {
    return ConfigHelper::load("master", "1") == "1";
}

// ===== Setpoint =====
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
    String s = ConfigHelper::load("hard_min", String(def, 2).c_str());
    return s.toFloat();
}
float StateService::loadHardMax(float def) {
    String s = ConfigHelper::load("hard_max", String(def, 2).c_str());
    return s.toFloat();
}
float StateService::loadHyst(float def) {
    String s = ConfigHelper::load("hyst_c", String(def, 2).c_str());
    return s.toFloat();
}

bool StateService::loadFailsafeAuto(bool defval) {
    Preferences p; p.begin("doc", true);
    bool v = p.getBool("fs_auto", defval);
    p.end(); return v;
}
void StateService::saveFailsafeAuto(bool on) {
    Preferences p; p.begin("doc", false);
    p.putBool("fs_auto", on);
    p.end();
}
