#include "SensorService.h"

DHT SensorService::dht1(DHT1_PIN, DHTTYPE);
DHT SensorService::dht2(DHT2_PIN, DHTTYPE);

void SensorService::begin() {
  dht1.begin();
  dht2.begin();
}

bool SensorService::readTemps(float& t1, float& t2, float& h1, float& h2) {
  t1 = dht1.readTemperature();
  t2 = dht2.readTemperature();
  h1 = dht1.readHumidity();
  h2 = dht2.readHumidity();
  if (isnan(t1) || isnan(t2) || isnan(h1) || isnan(h2)) return false;
  return true;
}
