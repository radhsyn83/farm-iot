#include "SensorService.h"

DHT SensorService::dht1(DHT1_PIN, DHTTYPE);
DHT SensorService::dht2(DHT2_PIN, DHTTYPE);

void SensorService::begin() {
  dht1.begin();
  dht2.begin();
}

bool SensorService::readTemps(float& t1, float& t2) {
  t1 = dht1.readTemperature();
  t2 = dht2.readTemperature();
  if (isnan(t1) || isnan(t2)) return false;
  return true;
}
