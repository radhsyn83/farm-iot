#pragma once
#include <Arduino.h>
#include "config.h"

// Base topics — lamp-specific topics are now built dynamically
inline String tRoot()      { return String(SITE_NS) + "/" + String(DEVICE_ID); }
inline String tTelemetry() { return tRoot() + "/telemetry"; }
inline String tState()     { return tRoot() + "/state"; }
inline String tHeartbeat() { return tRoot() + "/heartbeat"; }

inline String tReportedHealth() {
    return "esp32/" + String(DEVICE_ID) + "/reported/health";
}
