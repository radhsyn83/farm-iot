#pragma once
#include <Preferences.h>
#include "Logger.h"

class ConfigHelper {
public:
  // Inisialisasi Preferences (namespace)
  static void begin(const char* ns = "config");

  // Simpan & Baca String
  static void save(const char* key, const String& value);
  static String load(const char* key, const char* def = "");

  // Simpan & Baca Float
  static void saveFloat(const char* key, float value);
  static float loadFloat(const char* key, float def = 0.0);

  // Simpan & Baca Integer
  static void saveInt(const char* key, int value);
  static int loadInt(const char* key, int def = 0);

  // Simpan & Baca Boolean
  static void saveBool(const char* key, bool value);
  static bool loadBool(const char* key, bool def = false);

  static void remove(const char* key);

private:
  static Preferences prefs;
  static bool _started;
};
