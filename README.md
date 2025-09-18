# ESP32 MQTT Project for DOC Brooder Control

## 📌 Overview
This project uses an ESP32 Devkit with PlatformIO to control heating lamps for a DOC (Day Old Chick) brooder.  
It supports MQTT for remote monitoring/control and HTTP API for sending notifications.

### Features
- WiFi connection with auto-reconnect
- MQTT client (publish/subscribe)
- Topics for:
  - Telemetry (temperatures, lamp states, master power)
  - Lamp1, Lamp2, Master power control
  - Setpoint updates
- DHT22 sensors (2 pcs) for monitoring temperature
- Lamp control via PWM (dimmer) or relay
- API service for sending notifications/webhooks
- Modular service architecture (WiFi, MQTT, Sensors, Lamps, API)

---

## ⚙️ Requirements
- ESP32 Devkit
- PlatformIO
- MQTT Broker (e.g., Mosquitto)
- DHT22 sensors (x2)
- Relay modules or PWM dimmer modules for lamps
- Infrared/Pijar lamps

---

## 🔌 Pin Mapping (default)
- **DHT1** → GPIO4  
- **DHT2** → GPIO16  
- **Lamp1** → GPIO17  
- **Lamp2** → GPIO5  
- **Master Relay** → GPIO18  

Modify in `pins.h` if needed.

---

## 📡 MQTT Topics
Namespace: `farm/doc1` (change in `config.h`)

### Telemetry (publish every 5s)
- `farm/doc1/telemetry`
```json
{
  "device": "doc-kandang-01",
  "temp1": 31.8,
  "temp2": 30.9,
  "lamp1": 60,
  "lamp2": 40,
  "power_master": true,
  "ts": 12345
}
```

### State Topics (publish on change)
- `farm/doc1/state/lamp1` → `{"power":60,"on":true}`  
- `farm/doc1/state/lamp2` → `{"power":40,"on":true}`  
- `farm/doc1/state/power_master` → `{"on":true}`  

### Command Topics (subscribe)
- `farm/doc1/cmd/lamp1` → `{"power":75}` or `{"on":true}`  
- `farm/doc1/cmd/lamp2` → `{"power":0}`  
- `farm/doc1/cmd/power_master` → `{"on":false}`  
- `farm/doc1/cmd/setpoint` → `{"t":32.0}`  

---

## 🚀 Usage
1. Clone this repo or extract the zip.
2. Open with PlatformIO (VSCode).
3. Edit `src/config.h`:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `MQTT_HOST` / `MQTT_PORT`
4. Build and upload to ESP32.
5. Monitor serial log (115200 baud).

---

## 📖 Notes
- Change `LAMP_MODE` in `config.h` to `LampMode::RELAY` if using relay ON/OFF instead of PWM dimmer.
- Ensure MQTT broker is reachable from ESP32 network.
- Adjust telemetry interval with `TELEMETRY_MS` in `config.h`.

