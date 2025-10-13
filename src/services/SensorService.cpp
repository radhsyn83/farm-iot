#include "SensorService.h"

DHT SensorService::dht1(DHT1_PIN, DHTTYPE);
DHT SensorService::dht2(DHT2_PIN, DHTTYPE);

extern DHT dht1;
extern DHT dht2;

static bool  g_mockOn  = false;
static float g_mt1 = 28.0f, g_mt2 = 28.0f, g_mh1 = 70.0f, g_mh2 = 70.0f;

void SensorService::rebeginOne(const char* id) {
  if (strcmp(id,"dht1")==0) {
    dht1.~DHT();
    new(&dht1) DHT(DHT1_PIN, DHTTYPE);
    dht1.begin();
    Logger::info("SensorService rebegin dht1");
  } else if (strcmp(id,"dht2")==0) {
    dht2.~DHT();
    new(&dht2) DHT(DHT2_PIN, DHTTYPE);
    dht2.begin();
    Logger::info("SensorService rebegin dht2");
  } else {
    Logger::warn("SensorService rebeginOne: unknown id %s", id);
  }
}

void SensorService::begin() {
  dht1.begin();
  dht2.begin();
}

void SensorService::enableMock(bool on) { g_mockOn = on; }
void SensorService::setMockValues(float t1, float t2, float h1, float h2) {
  g_mt1 = t1; g_mt2 = t2; g_mh1 = h1; g_mh2 = h2;
}

bool SensorService::readTemps(float& t1, float& t2, float& h1, float& h2) {
  if (g_mockOn) {
    t1 = g_mt1; t2 = g_mt2; h1 = g_mh1; h2 = g_mh2;
    return true;
  }

  const float fallbackTemp = 0.0f;
  const float fallbackHum  = 0.0f;

  const float t1r = dht1.readTemperature();
  const float t2r = dht2.readTemperature();
  const float h1r = dht1.readHumidity();
  const float h2r = dht2.readHumidity();

  t1 = isfinite(t1r) ? t1r : fallbackTemp;
  t2 = isfinite(t2r) ? t2r : fallbackTemp;
  h1 = isfinite(h1r) ? h1r : fallbackHum;
  h2 = isfinite(h2r) ? h2r : fallbackHum;

  return isfinite(t1) && isfinite(t2) && isfinite(h1) && isfinite(h2);
}
