#pragma once
#include <Arduino.h>
#include "config.h"

inline String tRoot() { return String(SITE_NS); }

inline String tTelemetry() { return tRoot() + "/telemetry"; }

inline String tState() { return tRoot() + "/state"; }
inline String tStateLamp1() { return tRoot() + "/state/lamp1"; }
inline String tStateLamp2() { return tRoot() + "/state/lamp2"; }
inline String tStateMaster() { return tRoot() + "/state/power_master"; }

inline String tCmdLamp1() { return tRoot() + "/cmd/lamp1"; }
inline String tCmdLamp2() { return tRoot() + "/cmd/lamp2"; }
inline String tCmdPowerMaster() { return tRoot() + "/cmd/power_master"; }
inline String tCmdSetpoint() { return tRoot() + "/cmd/setpoint"; }
