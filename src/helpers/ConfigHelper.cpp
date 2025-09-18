#include "ConfigHelper.h"

// Definisi variabel statis
Preferences ConfigHelper::prefs;
bool ConfigHelper::_started = false;

void ConfigHelper::begin(const char* ns) {
  if (!_started) {
    prefs.begin(ns, false);
    _started = true;
    Logger::info("ConfigHelper started (ns=%s)", ns);
  }
}

void ConfigHelper::save(const char* key, const String& value) {
  prefs.putString(key, value);
  Logger::info("Saved [%s] = %s", key, value.c_str());
}

String ConfigHelper::load(const char* key, const char* def) {
  return prefs.getString(key, def);
}

void ConfigHelper::saveFloat(const char* key, float value) {
  prefs.putFloat(key, value);
  Logger::info("Saved [%s] = %.2f", key, value);
}

float ConfigHelper::loadFloat(const char* key, float def) {
  return prefs.getFloat(key, def);
}

void ConfigHelper::saveInt(const char* key, int value) {
  prefs.putInt(key, value);
  Logger::info("Saved [%s] = %d", key, value);
}

int ConfigHelper::loadInt(const char* key, int def) {
  return prefs.getInt(key, def);
}

void ConfigHelper::saveBool(const char* key, bool value) {
  prefs.putBool(key, value);
  Logger::info("Saved [%s] = %s", key, value ? "true" : "false");
}

bool ConfigHelper::loadBool(const char* key, bool def) {
  return prefs.getBool(key, def);
}
