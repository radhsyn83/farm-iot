#pragma once
#include <Arduino.h>

class ApiService {
public:
  static bool postNotify(const String& title, const String& message);
};
