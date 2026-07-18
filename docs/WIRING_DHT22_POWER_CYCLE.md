# DHT22 Hardware Power-Cycle — Wiring Guide

This document describes how to wire the DHT22 sensors so the ESP32 can perform an **automatic hardware power cycle** to recover sensors that return `NaN` after hours of operation.

---

## Why This Wiring?

DHT22 sensors sometimes "hang" internally after hours/days of operation. The only reliable fix is to **cut their power for 2 seconds**, then power them back on — a full hardware reset.

The firmware does this automatically whenever a sensor returns 3 consecutive invalid readings. To make this possible, **all 4 DHT22 modules must be powered through a single switchable line**, controlled by **GPIO 23** on the ESP32.

---

## Parts List

| Part | Qty | Notes |
|---|---|---|
| ESP32 DevKit V1 | 1 | Already have |
| 3-pin DHT22 modules | 4 | Already have (with built-in pull-up resistors) |
| **2N7000** N-channel MOSFET | 1 | ~$0.10, TO-92 package |
| **10kΩ resistor** | 1 | Gate pull-down (any 1/4W resistor works) |
| Jumper wires | — | For breadboard connections |

> **Alternative MOSFETs that also work**: IRLZ44N, AO3400, 2N7002 (SMD). Any logic-level N-channel MOSFET rated for ≥100 mA drain current.

---

## 2N7000 Pinout

Hold the MOSFET with the **flat side facing you**, legs pointing down:

```
      ┌─────┐
      │     │      (flat side, printed "2N7000")
      │     │
      └─┬─┬─┬─┘
        │ │ │
        S G D
       (1)(2)(3)
```

| Pin | Name | Connects to |
|---|---|---|
| 1 | **Source (S)** | ESP32 GND |
| 2 | **Gate (G)** | ESP32 GPIO 23 **and** 10kΩ to GND |
| 3 | **Drain (D)** | All 4 DHT22 module GND pins |

---

## Wiring Diagram

```
  ┌──────────────── ESP32 ────────────────┐
  │                                       │
  │  3V3 ──┬──── DHT22 #1 VCC             │
  │        ├──── DHT22 #2 VCC             │
  │        ├──── DHT22 #3 VCC             │
  │        └──── DHT22 #4 VCC             │
  │                                       │
  │  GPIO 4 ───── DHT22 #1 DATA           │
  │  GPIO 13 ──── DHT22 #2 DATA           │
  │  GPIO 14 ──── DHT22 #3 DATA           │
  │  GPIO 16 ──── DHT22 #4 DATA           │
  │                                       │
  │  GPIO 23 ─────┬──── 2N7000 Gate       │
  │               │                       │
  │               └──── 10kΩ ──── GND     │
  │                                       │
  │  GND ─────────────── 2N7000 Source    │
  │                                       │
  └───────────────────────────────────────┘

      DHT22 #1 GND ──┐
      DHT22 #2 GND ──┤
      DHT22 #3 GND ──┼──── 2N7000 Drain
      DHT22 #4 GND ──┘
```

---

## How It Works

1. When firmware sets **GPIO 23 HIGH** → MOSFET turns ON → DHT22 GND path is connected → sensors powered.
2. When firmware sets **GPIO 23 LOW** → MOSFET turns OFF → DHT22 GND path is broken → sensors fully off.
3. The **10kΩ resistor** keeps the gate pulled to GND when the ESP32 is resetting or disconnected (failsafe: sensors OFF when unpowered, then ON when ESP32 finishes booting).

This is called **low-side switching** — we switch the GND line instead of the VCC line because it's easier to drive an N-channel MOSFET from a 3.3V GPIO.

---

## Step-by-Step Wiring

1. **Disconnect** ESP32 from USB power.
2. **Remove** the existing direct GND wires from all 4 DHT22 modules — they will NOT connect directly to ESP32 GND anymore.
3. **Join** all 4 DHT22 module GND pins together (on a breadboard rail or solder them together).
4. **Connect** that combined GND to the MOSFET **Drain (D, pin 3)**.
5. **Connect** MOSFET **Source (S, pin 1)** to ESP32 **GND**.
6. **Connect** MOSFET **Gate (G, pin 2)** to ESP32 **GPIO 23**.
7. **Connect** a **10kΩ resistor** between MOSFET Gate and GND (in parallel with the GPIO connection).
8. Leave DHT22 **VCC** wires connected to ESP32 **3V3** as before — do NOT change those.
9. Leave DHT22 **DATA** wires connected to their GPIOs (4, 13, 14, 16) as before.
10. **Reconnect** ESP32 to USB power and upload the firmware.

---

## Verification

After boot, check the serial monitor for this line:

```
PeripheralManager: 4 sensors, 2 lamps, master_pin=33, sensor_power_pin=23, ver=...
SensorService: power pin 23 set HIGH
```

**To test the power cycle manually:**

1. Disconnect one DHT22 data wire (simulates a hung sensor).
2. Wait ~6–10 seconds. You should see in the serial log:
   ```
   [POWER_CYCLE] dht1: 3 consecutive NaN — triggering
   SensorService: POWER CYCLE — pin 23 LOW
   SensorService: power cycle — pin 23 HIGH (restoring)
   SensorService: power cycle COMPLETE — 4 sensors re-initialized
   ```
3. Measure GPIO 23 with a multimeter during the cycle — it should drop to 0 V for 2 seconds, then return to 3.3 V.
4. Reconnect the data wire; within a few seconds the sensor should resume valid readings.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Sensors never power on | MOSFET wired backwards (S/D swapped) | Check 2N7000 pinout — flat side facing you: **S, G, D** |
| Sensors always on, even when GPIO 23 is LOW | DHT22 GND still wired directly to ESP32 GND | Remove direct GND wires; only path to GND must be via MOSFET |
| Power cycle happens every few seconds in a loop | Wires too long (>1 m) causing real NaN | Shorten cables to <30 cm, or add 100 nF decoupling capacitor across each DHT22 VCC–GND |
| Serial shows `sensor_power_pin=255` instead of 23 | Old config in NVS | Power-cycle the ESP32; firmware will auto-migrate on next boot (version marker bumped) |
| `no power pin configured` warning | `sensorPowerPin` is 255 | Either re-flash to force defaults, or send `desired/config` MQTT with `sensorPowerPin: 23` |

---

## Notes

- **Current draw**: 4x DHT22 modules at ~1.5 mA each = ~6 mA total. Well under the 2N7000's 200 mA limit.
- **Voltage**: This guide assumes **3.3V powering** of the DHT22 modules (recommended for ESP32 compatibility). If you power at 5V, the data pins could damage the ESP32's 3.3V-only GPIOs over time. Stick with 3.3V.
- **Safety**: If the ESP32 resets, the 10kΩ pull-down keeps GPIO 23 (and thus the MOSFET gate) at 0 V, so sensors stay OFF until the firmware finishes booting and drives GPIO 23 HIGH. This prevents damage from floating-gate oscillations.
- **Soft-reset still works**: The existing `desired/restart` MQTT soft-reset is unchanged and remains as a fallback recovery tier.
