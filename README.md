# PRESSENCE---AN-ANTI-THEFT-MAT
PRESSENSE — ESP32 Anti-Theft Mat Monitor
An ESP32-based pressure-mat monitor that logs every press with a timestamp, serves a monochrome web dashboard over Wi-Fi, and sounds a buzzer only during a nightly alert window.
![Platform](https://img.shields.io/badge/platform-ESP32-black)
![IDE](https://img.shields.io/badge/Arduino%20IDE-supported-black)
![Timezone](https://img.shields.io/badge/timezone-IST%20(UTC%2B5%3A30)-black)
---
Overview
Time window	Buzzer	Logging
18:30:00 → 08:00:00 (next morning)	Pulses on every press	Yes
08:00:00 → 18:29:59	Silent	Yes
The ESP32 acts as a Wi-Fi web server and real-time logger.
A 10 kΩ pull-down resistor keeps the input stable (`LOW`) when the mat is untouched.
Time is synced via NTP (`pool.ntp.org`) using IST (`UTC+5:30`).
The PRESSENSE GUI (white / `#F7F7F7` / `#111111`, 1 px `#E5E5E5` borders) is served directly from the ESP32 and polls the API every 1.5 s.
Features
Timestamped log of every mat press (date + `HH:MM:SS`)
Time-window based buzzer logic (3-pulse alert)
250 ms software debounce with rising-edge detection
Live dashboard: device clock, alarm-window status, total movements, alarm triggers
Searchable log table (stores the latest 100 events)
JSON API (`/api/status`, `/api/clear`)
---
Hardware
Components: ESP32 dev board, pressure/contact mat (or switch), 10 kΩ resistor, buzzer, jumper wires, USB cable.
Wiring
Component	ESP32 pin	Notes
Mat terminal A	`3.3V`	Supplies the HIGH signal when pressed
Mat terminal B	`GPIO 27`	Reads press state (`HIGH` = pressed)
10 kΩ resistor	`GPIO 27` ↔ `GND`	Pull-down so the pin reads `LOW` when untouched
Buzzer (+)	`GPIO 26`	Alert output
Buzzer (−)	`GND`	Common ground
```
3.3V ──\[ Mat ]──┬── GPIO 27
                │
              \[10kΩ]
                │
               GND

GPIO 26 ──(+) Buzzer (−)── GND
```
> \*\*Notes\*\*
> - A 10 kΩ pull-down suits a \*\*digital switch or two-layer foil/conductive mat\*\* that closes to 3.3 V. An analog FSR can be read on an ADC pin, but this sketch uses digital detection with debounce.
> - Keep buzzer current within ESP32 GPIO limits (a few mA). For larger buzzers, drive them through a transistor.
> - Never connect the mat to 5 V — ESP32 GPIOs are 3.3 V only.
---
Software Setup (Arduino IDE)
Install ESP32 support
`File > Preferences` → add the Espressif ESP32 board manager URL to Additional boards manager URLs.
`Tools > Board > Boards Manager` → search esp32 (Espressif Systems) → install.
Select board and port
Board: `ESP32 Dev Module` (or your variant)
Port: the COM/serial port for your ESP32
Configure Wi-Fi in `PRESSENSE.ino`:
```cpp
   const char\* WIFI\_SSID     = "YOUR\_WIFI\_SSID";
   const char\* WIFI\_PASSWORD = "YOUR\_WIFI\_PASSWORD";
   ```
Internet access is required for NTP time sync.
Upload the sketch and wait for `Done uploading`.
Open Serial Monitor at `115200` baud and note the IP address (e.g. `http://192.168.1.45`).
Browse to that IP from any device on the same network to open the PRESSENSE dashboard.
No external libraries are needed — only `WiFi.h`, `WebServer.h` and `time.h`, which ship with the ESP32 core.
Repository Structure
```
PRESSENSE/
├── PRESSENSE.ino   # Full sketch: firmware, API and embedded web GUI
└── README.md
```
---
How It Works
`loop()` reads `GPIO 27` and applies a 250 ms debounce.
On a stable rising edge (mat pressed), `recordMovementEvent()` runs:
gets the current IST time,
checks `isAlarmWindowActive()`,
stores the event (newest first, max 100),
pulses the buzzer 3× if inside the alert window.
The browser polls `/api/status` every 1.5 s and renders the dashboard.
```cpp
// Alert window: 18:30:00 → 08:00:00
bool active = (secOfDay >= 18\*3600 + 30\*60) || (secOfDay < 8\*3600);
```
API
Method	Endpoint	Description
`GET`	`/`	PRESSENSE web GUI
`GET`	`/api/status`	JSON: date, time, window state, counters, logs
`POST`	`/api/clear`	Clears logs and counters
Example response:
```json
{
  "date": "2026-11-24",
  "time": "21:14:07",
  "alarmWindow": true,
  "totalEvents": 3,
  "alarmCount": 2,
  "logs": \[
    { "id": 3, "date": "2026-11-24", "time": "21:13:55",
      "mode": "ALERT WINDOW (6:30 PM - 8:00 AM)", "alarm": true }
  ]
}
```
---
Customization
What	Where
Alert window times	`NIGHT\_START` / `MORNING\_END` in `isAlarmWindowActive()`
Timezone	`GMT\_OFFSET\_SEC`
Debounce	`DEBOUNCE\_MS`
Max stored events	`MAX\_LOGS`
Pins	`MAT\_PIN`, `BUZZER\_PIN`
Buzzer pattern	`soundAlarmPulse()`
Troubleshooting
Symptom	Fix
Date shows `Unsynced`	Check Wi-Fi credentials and internet access (NTP)
Constant false triggers	Verify the 10 kΩ pull-down to GND; shorten wires; raise `DEBOUNCE\_MS`
Buzzer silent at night	Confirm the clock has synced and the buzzer polarity/pin is correct
Can't open the dashboard	Use the IP from Serial Monitor; phone/PC must be on the same network
Known Limitations
Logs are held in RAM and are lost on reboot.
The dashboard has no authentication — use only on a trusted network.
The buzzer pulse uses `delay()` (~0.7 s), briefly blocking the web server.
Local clock falls back to uptime if NTP never syncs.
Ideas for Future Work
Persist logs to flash (SPIFFS/LittleFS) or an SD card
Push notifications (Telegram / email) on night-time triggers
Password-protected dashboard
Non-blocking buzzer using `millis()`
Configurable alert window from the GUI
License
MIT — add a `LICENSE` file to your repository.
