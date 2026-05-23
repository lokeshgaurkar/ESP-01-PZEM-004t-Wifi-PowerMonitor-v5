# PowerMonitor (ESP8266 + PZEM-004T v5)

PowerMonitor is an ESP8266-based energy monitoring system with a rich web dashboard, Wi-Fi AP mode, captive portal behavior, bill estimation, and OTA firmware updates.

## Core Features

- Live electrical monitoring:
  - Voltage (V)
  - Current (A)
  - Power (W)
  - Energy (kWh)
  - Frequency (Hz)
  - Power Factor (PF)
- Smoothed live readings using EMA for stable UI values.
- Hardware-backed energy source using PZEM cumulative kWh.
- Energy reset with confirmation flow.

## System Monitoring Features

- CPU loop load estimation (percentage).
- RAM usage estimation (used % + free bytes).
- Runtime performance sampling and smoothing.

## Web UI Features

- Single-file embedded web pages (inside `.ino`) for:
  - Main Dashboard (`/`)
  - Firmware Update Page (`/update`)
  - Wi-Fi Settings Page (`/wifi`)
- Modern responsive design with animated cards and status indicators.
- Power spike animation on Power card for meaningful jumps.
- Dashboard Wi-Fi chip button opens Wi-Fi settings page.

## Wi-Fi / Network Features

- ESP8266 Access Point mode.
- Configurable AP SSID/password via Wi-Fi settings page.
- Wi-Fi credentials validation:
  - SSID: 1 to 32 chars
  - Password: 8 to 64 chars
- Credential persistence in EEPROM.
- Auto reboot after saving new Wi-Fi credentials.
- Captive portal support (DNS catch-all + probe route redirects):
  - `/generate_204`
  - `/hotspot-detect.html`
  - `/ncsi.txt`
  - `/connecttest.txt`

## Bill Calculator Features

- Configurable tariff inputs:
  - Meter charge
  - Shipping per unit
  - Tax percent
  - Slab rates (0–100, 101–300, 300+)
- Real-time bill estimate from live kWh.
- Tariff persistence in EEPROM.

## OTA Firmware Update Features

- Web-based firmware upload via `/update`.
- Upload progress UI.
- Update validation via `Updater` API.
- Automatic device restart on successful update.

## Data Persistence (EEPROM)

- Energy value and offset.
- Tariff structure.
- Wi-Fi config structure (with magic marker validation).

## API / Routes

- `GET /` -> Main dashboard page.
- `GET /data` -> JSON live data payload.
- `GET /reset` -> Reset energy counter.
- `GET /tariff?...` -> Save tariff values.
- `GET /wifi` -> Wi-Fi settings page.
- `GET /wifi-save?ssid=...&pass=...` -> Save AP credentials + reboot.
- `GET /update` -> OTA update page.
- `POST /updatefw` -> OTA firmware binary upload.

## File System / Assets

- Uses LittleFS for static assets:
  - `/fonts/...`
  - `/favicon.png`
  - `/favicon.ico`

## Dependencies

- `ESP8266WiFi`
- `ESP8266WebServer`
- `ESP8266mDNS`
- `DNSServer`
- `PZEM004Tv30`
- `EEPROM`
- `LittleFS`
- `Updater`

## Typical Usage Flow

1. Power on device.
2. Connect to AP.
3. Open dashboard (or captive portal popup when supported).
4. Monitor live electrical values.
5. Configure tariff and view bill estimate.
6. Open Wi-Fi page to change AP SSID/password when needed.
7. Use OTA page to update firmware.

## Notes

- Captive portal auto-popup behavior depends on client OS and is not 100% guaranteed on every Windows configuration.
- After changing Wi-Fi credentials, reconnect using the new SSID/password after reboot.

## Developer 

- Name: Lokesh Gaurkar
- Email: lokeshgaurkar444@gmail.com
