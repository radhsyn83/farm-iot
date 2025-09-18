#pragma once
#include <Arduino.h>

struct Logger {
  template<typename... Args>
  static void info(const char* fmt, Args... args) {
    char buf[256]; snprintf(buf,sizeof(buf),fmt,args...);
    Serial.print("[INFO] "); Serial.println(buf);
  }

  template<typename... Args>
  static void warn(const char* fmt, Args... args) {
    char buf[256]; snprintf(buf,sizeof(buf),fmt,args...);
    Serial.print("[WARN] "); Serial.println(buf);
  }

  template<typename... Args>
  static void error(const char* fmt, Args... args) {
    char buf[256]; snprintf(buf,sizeof(buf),fmt,args...);
    Serial.print("[ERR ] "); Serial.println(buf);
  }
};
