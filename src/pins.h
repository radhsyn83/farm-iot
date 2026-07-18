#pragma once
#include <Arduino.h>

// ====== System pins (not configurable peripherals) ======
static const uint8_t WIFI_LED_PIN = 2;

// ====== Default peripheral pins ======
// Used only by PeripheralManager::loadDefaults() for first-boot compatibility.
// After first boot, config is loaded from NVS and these are ignored.
namespace DefaultPins {
    static constexpr uint8_t DHT1 = 4;    // was 12 (strapping pin MTDI!)
    static constexpr uint8_t DHT2 = 13;
    static constexpr uint8_t DHT3 = 14;
    static constexpr uint8_t DHT4 = 16;   // was 15 (strapping pin MTDO!)
    static constexpr uint8_t LAMP1 = 25;
    static constexpr uint8_t LAMP2 = 26;
    static constexpr uint8_t ZC = 27;
    static constexpr uint8_t MASTER_RELAY = 33;
    static constexpr uint8_t SENSOR_POWER = 23;  // shared DHT VCC control for HW power cycling
}
