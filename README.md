# Weather Clock for T-Keyboard

🇬🇧 English · [🇪🇸 Español](README.es.md)

A clock and weather display for the LilyGO **T-Keyboard** (ESP32-C3 with a 0.99" GC9D01N screen, 40×160 px). It shows the time in large digits and, with the space bar, the current weather for a Spanish city.

> by **@mmoroca & Leo.ai** · 2026 · [MIT License](LICENSE)

![Status](https://img.shields.io/badge/platform-ESP32--C3-blue) ![License](https://img.shields.io/badge/license-MIT-green)

## Features

- **Large clock** without seconds, time from NTP with the `Europe/Madrid` time zone (summer/winter time handled automatically).
- **Current weather** from [Open-Meteo](https://open-meteo.com) (no API key): temperature, humidity, and a colour icon (sun, clouds, rain, snow, fog, storm).
- **Configuration portal**: no WiFi credentials in the code. The device creates its own network and you configure it from the phone or the computer.
- **Choose your city** from a list of Spanish regional capitals, Ceuta, and Melilla.
- **4 clock styles**, saved between reboots.
- **Auto screen-off** after 5 s without key presses. Any key wakes it up.
- **Non-blocking start**: the keyboard and screen respond while WiFi connects.
- **WiFi icon** in the top-right corner: white when connected, red and crossed out when not.
- **Offline time**: without network, the clock keeps running while powered and resumes from the last saved time after a reboot.

## Hardware

- LilyGO **T-Keyboard** (ESP32-C3, 0.99" GC9D01N screen 40×160). Product page: <https://lilygo.cc/products/t-keyboard>
- USB-C cable.

The public documentation does not mention a battery-backed real-time clock (RTC), so the time is synchronised by NTP and, without network, it is approximate (see [Time](#time)).

## Controls

| Key | Clock view | Weather view |
|---|---|---|
| **Space bar** | Switches to the weather view | Switches back to the clock |
| **ALT** | Changes the clock style (0 → 1 → 2 → 3 → 0) | No effect |
| **ALT held while powering on** | Opens the configuration portal | |
| **Any key** | Wakes the screen (after 5 s of inactivity it turns off) | Same |

### Clock styles

| # | Style | Description |
|---|---|---|
| 0 | White | White digits on black |
| 1 | Colour per digit | Each digit has its own colour |
| 2 | Night | Red digits on black |
| 3 | Cyberpunk | Full yellow background with black digits |

## First-time setup (configuration portal)

The portal opens when there is no saved network, or when you hold **ALT** while powering on.

1. Power on the device. The screen shows the configuration mode.
2. On your phone or computer, connect to the WiFi network **`RelojClima-XXXX`** (the last four characters are different for each device). It is an open network, with no password.
3. A configuration page should open automatically. If it does not, open **http://192.168.4.1** in your browser.
4. Choose your WiFi network (SSID), enter its password, and choose the city.
5. Press **Save**. The device **tests the network first** (up to 25 s):
   - If it connects, it saves the settings and **restarts by itself**.
   - If it does not connect, it shows *"No conecta la red"* and stays in the portal without saving. Check the password and try again.

If the saved network is not in range later, the clock keeps working (see [Time](#time)). To change the network or city, hold **ALT** while powering on.

## Time

- Synchronised with `pool.ntp.org` and `time.google.com` when WiFi is connected. Time zone: `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Madrid).
- **Without network:** the clock keeps running while the device is powered, although the internal oscillator can drift by several minutes a day.
- **Every 30 minutes** the synchronised time is saved in flash memory. After a reboot without network, the device shows the last saved time. It will be behind by the time the device was off, until it reconnects and NTP corrects it.
- The **red crossed WiFi icon** tells you the time and weather may be approximate.

## Weather

- Data from [Open-Meteo](https://open-meteo.com) (`current`: temperature, relative humidity, and WMO weather code).
- Updated every **15 minutes**; if it fails, it retries after **60 s**.
- The weather view shows: `NN grados, HH%` on the first line and a short description (up to 16 characters) on the second.
- WMO codes: 0 clear; 1–2 partly cloudy; 3 cloudy; 45–48 fog; 51–67 and 80–82 rain; 71–77 and 85–86 snow; 95–99 storm.

### Cities

Sevilla, Zaragoza, Oviedo, Palma de Mallorca, Santa Cruz de Tenerife, Las Palmas de Gran Canaria, Santander, Toledo, Valladolid, Barcelona, Mérida, Santiago de Compostela, Logroño, Madrid, Murcia, Pamplona, Vitoria-Gasteiz, Valencia, Ceuta, Melilla, and Tudela (default). To add or change cities, edit the `CITIES` table in `src/main.cpp`.

## Build and upload (VSCode + PlatformIO)

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension.
2. Open this folder with **File → Open Folder**.
3. Wait for PlatformIO to install the platform and libraries (first time only).
4. Click **Build** (✓ in the bottom bar), then **Upload** (→).
5. If the upload fails, put the T-Keyboard in download mode: hold **BOOT**, press **RESET** (or connect USB), and release **BOOT**. See the [T-Keyboard README](https://github.com/Xinyuan-LilyGO/T-keyboard) for details.

From the terminal (with PlatformIO CLI installed):

```bash
pio run -e T-Keyboard-clima                 # build
pio run -e T-Keyboard-clima -t upload       # upload
pio device monitor -b 115200                # serial monitor
```

The `platformio.ini` file uses `espressif32 @ 6.1` with the board `ttgo-t-oi-plus`, and the partition table in `partitions_clima.csv` (single app, no OTA).

## Security notes

- The WiFi password is stored in the device flash memory (in the `clima` namespace of `Preferences`).
- The configuration portal is an **open network without a password**. Anyone within range can connect to it while it is open, so configure the device at home.
- The HTTPS request to Open-Meteo uses `WiFiClientSecure::setInsecure()`, which **does not verify the server certificate**. This is a deliberate trade-off: the data is public weather information and no credentials are sent to that server, but someone on the same network could feed false weather data. Use it only on networks you trust.

## Limitations

- No real-time clock with battery: after a power cut, the time is only approximate until NTP syncs.
- Some Spanish on-screen texts avoid the letter **ó** while a font issue is pending.
- The free Open-Meteo API is for **non-commercial use** only (fewer than 10,000 calls per day). Commercial use requires a paid plan.

## Credits and licenses

- **Author:** @mmoroca & Leo.ai (2026).
- **License:** [MIT](LICENSE).
- **Display driver:** `lib/TFT_GC9D01N-Patched` (MIT, Copyright (c) 2022 Xinyuan-LilyGO), from the [T-Keyboard project](https://github.com/Xinyuan-LilyGO/T-keyboard).
- **Libraries:** [ArduinoJson](https://arduinojson.org) (MIT), Arduino-ESP32 (LGPL-2.1).
- **Weather data:** [Open-Meteo.com](https://open-meteo.com), under [CC BY 4.0](https://open-meteo.com/en/licence). Please keep the attribution if you redistribute the data.
