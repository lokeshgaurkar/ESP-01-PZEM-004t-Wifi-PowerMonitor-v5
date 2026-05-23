# PowerMonitor (ESP-01 + PZEM-004T v3.0)

PowerMonitor is a lightweight electricity monitoring firmware for ESP-01 (ESP8266) with a modern web dashboard and offline OTA updates.

It reads electrical data from a PZEM-004T v3.0 module and serves a local dashboard over Wi-Fi AP mode for live monitoring and bill estimation.

Features
Live electrical metrics:
Voltage (V)
Current (A)
Power (W)
Energy (kWh)
Frequency (Hz)
Power factor (PF)
Live system metrics (web UI bars):
CPU usage estimate (% loop load)
RAM usage (% of boot-time free heap)
Free heap (bytes)
Smooth reading display using EMA filtering
Energy reset with confirmation flow
Tariff-based electricity bill calculator
EEPROM persistence:
Energy value / offset
Tariff settings
Local font and favicon hosting from LittleFS
Offline OTA firmware update page (/update)
mDNS support (powermonitor.local when supported by client)
Hardware
ESP-01 / ESP8266
PZEM-004T v3.0
Stable 3.3V power supply for ESP-01
UART wiring suitable for your board configuration
Firmware Stack
Arduino core for ESP8266
ESP8266WiFi
ESP8266WebServer
ESP8266mDNS
PZEM004Tv30
EEPROM
LittleFS
Updater
Network Behavior
Device starts in Access Point mode:

SSID: PowerMonitor
Password: 11223344
Hostname: powermonitor
Typical URL: http://192.168.4.1/
Project Structure (Runtime)
Main sketch: sketch_mar2b_2_newUISkin.ino
LittleFS assets:
/fonts/Sora-Regular.woff2
/fonts/Sora-SemiBold.woff2
/fonts/Sora-Bold.woff2
/fonts/JetBrainsMono-SemiBold.woff2
/favicon.png
HTTP Endpoints
GET / : Main dashboard
GET /data : JSON payload with live measurements + tariff + system usage
GET /reset : Reset energy counter (logical reset via offset)
GET /tariff?meter=...&ship=...&tax=...&r1=...&r2=...&r3=... : Save tariff
GET /update : OTA upload page
POST /updatefw : Upload compiled .bin firmware
/data JSON Keys
Measurement keys:
v, c, p, e, f, pf
Tariff keys:
meter, ship, tax, r1, r2, r3
System keys:
cpu (loop-load estimate %)
ramu (RAM used %)
ramf (free heap bytes)
Build and Flash
Open the sketch in Arduino IDE.
Select an ESP8266 board profile suitable for ESP-01.
Install required libraries (including PZEM004Tv30).
Upload filesystem assets to LittleFS (fonts + favicon).
Flash firmware to ESP-01.
Connect to AP PowerMonitor and open http://192.168.4.1/.
OTA Update Workflow
Build new firmware and export .bin.
Open dashboard and go to Firmware Update.
Upload the .bin file.
Device restarts automatically on successful update.
Energy and Billing Logic
PZEM hardware energy is used as the source of truth.
Logical reset is performed by storing an energy_offset_kwh.
Bill calculation uses slab rates:
0-100 units at r1
101-300 units at r2
300+ units at r3
Additional costs:
Meter charge
Shipping per unit
Tax on subtotal
ESP-01 Constraints and Notes
GPIO and memory are limited on ESP-01.
CPU metric is an approximation from firmware loop timing, not a hardware performance counter.
RAM usage is relative to boot-time free heap; useful for trend monitoring.
Keep additional libraries minimal to avoid heap pressure.
Troubleshooting
Dashboard not loading:
Confirm connection to PowerMonitor AP
Visit http://192.168.4.1/
Missing styles/fonts:
Re-upload LittleFS assets
OTA failed:
Ensure valid .bin and stable Wi-Fi connection
Check available sketch space
Sensor values NaN/stale:
Verify UART wiring and PZEM power
Security Considerations
AP password should be changed for production use.
OTA endpoint is local and unauthenticated in current form.
Avoid exposing this AP beyond trusted local access.
License
Add your preferred license here (MIT, Apache-2.0, etc.).
