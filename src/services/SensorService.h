#pragma once
#include <DHT.h>
#include "../pins.h"
#include "../config.h"
#include "helpers/Logger.h"

class SensorService {
public:
  static void begin();
  static bool readTemps(float& t1, float& t2, float& h1, float& h2);
  static void enableMock(bool on);
  static void setMockValues(float t1, float t2, float h1, float h2); 
  static void rebeginOne(const char* id); // "dht1" | "dht2"
private:
  static DHT dht1;
  static DHT dht2;
};
