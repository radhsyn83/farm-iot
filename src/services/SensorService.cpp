#include "SensorService.h"

// ===== Statics =====
alignas(DHT) uint8_t SensorService::_dhtBuf[MAX_SENSORS][sizeof(DHT)];
DHT*    SensorService::_dhts[MAX_SENSORS]  = {};
uint8_t SensorService::_pins[MAX_SENSORS]  = {};
uint8_t SensorService::_types[MAX_SENSORS] = {};
uint8_t SensorService::_count = 0;

bool  SensorService::_mockOn = false;
float SensorService::_mockT[MAX_SENSORS] = {};
float SensorService::_mockH[MAX_SENSORS] = {};

// ===== begin (no-op, init called explicitly) =====
void SensorService::begin() {}

// ===== init =====
void SensorService::init(const PeripheralConfig& cfg) {
    teardown();

    _count = cfg.sensorCount;
    for (uint8_t i = 0; i < _count; i++) {
        _pins[i]  = cfg.sensors[i].pin;
        _types[i] = cfg.sensors[i].type;

        // Placement-new: construct DHT in pre-allocated buffer
        _dhts[i] = new (_dhtBuf[i]) DHT(_pins[i], _types[i]);
        _dhts[i]->begin();

        Logger::info("SensorService: init %s on pin %d (type %d)",
                     cfg.sensors[i].id, _pins[i], _types[i]);
    }
}

// ===== teardown =====
void SensorService::teardown() {
    for (uint8_t i = 0; i < _count; i++) {
        if (_dhts[i]) {
            _dhts[i]->~DHT();
            _dhts[i] = nullptr;
        }
    }
    _count = 0;
}

// ===== readAll =====
uint8_t SensorService::readAll(SensorReading* out, uint8_t maxCount) {
    uint8_t n = min(_count, maxCount);
    for (uint8_t i = 0; i < n; i++) {
        if (_mockOn) {
            out[i].temp = _mockT[i];
            out[i].hum  = _mockH[i];
            continue;
        }

        if (!_dhts[i]) {
            out[i].temp = NAN;
            out[i].hum  = NAN;
            continue;
        }

        float t = _dhts[i]->readTemperature();
        float h = _dhts[i]->readHumidity();
        out[i].temp = isfinite(t) ? t : 0.0f;
        out[i].hum  = isfinite(h) ? h : 0.0f;
    }
    return n;
}

// ===== rebeginOne =====
void SensorService::rebeginOne(const char* id) {
    int idx = PeripheralManager::findSensorIndex(id);
    if (idx < 0 || idx >= (int)_count) {
        Logger::warn("SensorService rebeginOne: unknown id %s", id);
        return;
    }

    if (_dhts[idx]) {
        _dhts[idx]->~DHT();
    }
    _dhts[idx] = new (_dhtBuf[idx]) DHT(_pins[idx], _types[idx]);
    _dhts[idx]->begin();
    Logger::info("SensorService: rebegin %s on pin %d", id, _pins[idx]);
}

// ===== count =====
uint8_t SensorService::count() { return _count; }

// ===== Mock =====
void SensorService::enableMock(bool on) { _mockOn = on; }
void SensorService::setMockValues(uint8_t index, float t, float h) {
    if (index < MAX_SENSORS) {
        _mockT[index] = t;
        _mockH[index] = h;
    }
}
