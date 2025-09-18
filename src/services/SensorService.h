#pragma once
#include <DHT.h>
#include "../pins.h"
#include "../config.h"

class SensorService {
public:
  static void begin();
  static bool readTemps(float& t1, float& t2);
private:
  static DHT dht1;
  static DHT dht2;
};
