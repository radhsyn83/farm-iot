#include "LampService.h"
#include "StateService.h"
#include "../helpers/Logger.h"

#if __has_include(<RBDdimmer.h>)
#include <RBDdimmer.h>
#endif

// ===== Statics =====
LampState  LampService::_lamps[MAX_LAMPS];
LampConfig LampService::_configs[MAX_LAMPS];
uint8_t    LampService::_count    = 0;
bool       LampService::_masterOn = true;
uint8_t    LampService::_masterPin = 255;

#if __has_include(<RBDdimmer.h>)
alignas(dimmerLamp) uint8_t LampService::_dimBuf[MAX_LAMPS][sizeof(dimmerLamp)];
dimmerLamp* LampService::_dimmers[MAX_LAMPS] = {};
#endif

// ===== begin (no-op) =====
void LampService::begin() {}

// ===== init =====
void LampService::init(const PeripheralConfig& cfg) {
    teardown();

    _count     = cfg.lampCount;
    _masterPin = cfg.masterRelayPin;
    _masterOn  = true;

    for (uint8_t i = 0; i < _count; i++) {
        _configs[i] = cfg.lamps[i];
        _lamps[i].power = 0.0f;
        setupPin(i);
    }

    // Master relay pin
    if (_masterPin != 255) {
        pinMode(_masterPin, OUTPUT);
        digitalWrite(_masterPin, HIGH);
    }

    restoreState();
}

// ===== teardown =====
void LampService::teardown() {
    for (uint8_t i = 0; i < _count; i++) {
        teardownPin(i);
    }
    _count = 0;
}

// ===== setupPin =====
void LampService::setupPin(uint8_t index) {
    const auto& cfg = _configs[index];

    switch (cfg.mode) {
        case LampMode::RELAY:
            Logger::info("Lamp %s: RELAY on pin %d", cfg.id, cfg.pin);
            pinMode(cfg.pin, OUTPUT);
            digitalWrite(cfg.pin, HIGH); // OFF by default (active-low)
            break;

        case LampMode::PWM_DC:
            Logger::info("Lamp %s: PWM on pin %d (ch %d)", cfg.id, cfg.pin, cfg.pwmChannel);
            ledcSetup(cfg.pwmChannel, PWM_FREQ, PWM_RES);
            ledcAttachPin(cfg.pin, cfg.pwmChannel);
            break;

        case LampMode::ROBOTDYN_AC:
#if __has_include(<RBDdimmer.h>)
            Logger::info("Lamp %s: AC_DIMMER on pin %d (zc %d)", cfg.id, cfg.pin, cfg.zcPin);
            _dimmers[index] = new (_dimBuf[index]) dimmerLamp(cfg.pin, cfg.zcPin);
            _dimmers[index]->begin(NORMAL_MODE, ON);
#endif
            break;
    }
}

// ===== teardownPin =====
void LampService::teardownPin(uint8_t index) {
    const auto& cfg = _configs[index];

    switch (cfg.mode) {
        case LampMode::RELAY:
            digitalWrite(cfg.pin, HIGH); // OFF
            break;

        case LampMode::PWM_DC:
            ledcWrite(cfg.pwmChannel, 0);
            ledcDetachPin(cfg.pin);
            break;

        case LampMode::ROBOTDYN_AC:
#if __has_include(<RBDdimmer.h>)
            if (_dimmers[index]) {
                _dimmers[index]->setPower(0);
                noInterrupts();
                _dimmers[index]->~dimmerLamp();
                interrupts();
                _dimmers[index] = nullptr;
            }
#endif
            break;
    }
}

// ===== apply =====
void LampService::apply(uint8_t index, float power) {
    if (index >= _count) return;
    if (!_masterOn) power = 0.0f;
    power = constrain(power, 0.0f, 1.0f);

    const auto& cfg = _configs[index];
    switch (cfg.mode) {
        case LampMode::RELAY:
            digitalWrite(cfg.pin, (power >= 0.5f) ? HIGH : LOW);
            break;

        case LampMode::PWM_DC:
            ledcWrite(cfg.pwmChannel, (int)(power * 255));
            break;

        case LampMode::ROBOTDYN_AC:
#if __has_include(<RBDdimmer.h>)
            if (_dimmers[index]) _dimmers[index]->setPower((int)(power * 100));
#endif
            break;
    }
}

// ===== setLamp / getLamp by index =====
void LampService::setLamp(uint8_t index, float power) {
    if (index >= _count) return;
    _lamps[index].power = constrain(power, 0.0f, 1.0f);
    apply(index, _lamps[index].power);
    saveState();
}

LampState LampService::getLamp(uint8_t index) {
    if (index >= _count) { LampState s; s.power = 0.0f; return s; }
    return _lamps[index];
}

// ===== setLamp / getLamp by id =====
bool LampService::setLampById(const char* id, float power) {
    int idx = PeripheralManager::findLampIndex(id);
    if (idx < 0) return false;
    setLamp((uint8_t)idx, power);
    return true;
}

bool LampService::getLampById(const char* id, LampState& out) {
    int idx = PeripheralManager::findLampIndex(id);
    if (idx < 0) return false;
    out = _lamps[idx];
    return true;
}

// ===== setAll (for failsafe) =====
void LampService::setAll(float power) {
    power = constrain(power, 0.0f, 1.0f);
    for (uint8_t i = 0; i < _count; i++) {
        _lamps[i].power = power;
        apply(i, power);
    }
    saveState();
}

// ===== Master relay =====
void LampService::setMaster(bool on) {
    _masterOn = on;
    Logger::info("Master set to %s", on ? "ON" : "OFF");

    if (_masterPin != 255)
        digitalWrite(_masterPin, on ? HIGH : LOW);

    // Re-apply all lamps (they respect _masterOn in apply())
    for (uint8_t i = 0; i < _count; i++) {
        apply(i, _lamps[i].power);
    }
    saveState();
}

bool LampService::getMaster() { return _masterOn; }

// ===== saveState / restoreState =====
void LampService::saveState() {
    const auto& cfg = PeripheralManager::config();
    for (uint8_t i = 0; i < _count; i++) {
        StateService::saveLampPower(cfg.lamps[i].id, _lamps[i].power);
    }
    StateService::saveMaster(_masterOn);
}

void LampService::restoreState() {
    const auto& cfg = PeripheralManager::config();
    _masterOn = StateService::loadMaster();

    for (uint8_t i = 0; i < _count; i++) {
        _lamps[i].power = StateService::loadLampPower(cfg.lamps[i].id, 0.0f);
        apply(i, _lamps[i].power);
    }

    if (_masterPin != 255) {
        digitalWrite(_masterPin, _masterOn ? HIGH : LOW);
    }

    Logger::info("Restored state: %d lamps, Master=%d", _count, _masterOn);
}

uint8_t LampService::count() { return _count; }
