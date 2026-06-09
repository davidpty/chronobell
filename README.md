# ChronoBell

An ESP32-powered LED matrix clock with capacitive touch controls, a ship's bell, 8 display modes, and dual configuration - via the on-device touch menu or the browser-based setup portal. No hardcoded credentials required.

![ChronoBell](chronobell.png)

## What makes ChronoBell different

**Ship's bell striking** - ChronoBell can ring the traditional 1–8 strike pattern used aboard vessels, plus five other bell modes (hour count, half-hour, single ding, pair). The menu lets you preview each mode live before selecting it - something no other ESP32 matrix clock offers.

**Capacitive touch controls** - Three CAP1188 capacitive pads (left, center, right) handle all interaction: tap to navigate, hold for menus, longer hold for the config portal. No physical buttons to wear out. Auto-repeat on held left/right makes menu scrolling fast.

**Guest WiFi display** - Fetches a guest network password from an HTTP endpoint at boot, then shows it in the view cycle. No phone lookup, no "what's the WiFi password?" - it's right there on the clock. Perfect for lobbies, cafes, and offices.

**Two ways to configure** - Hold the center touch pad for 1.5 seconds to open the on-device menu (bell mode, display style, time format, night mode, brightness, date style). Hold for 3 seconds to open the WiFi config portal in your browser. Or press the BOOT button. No USB reflash needed for day-to-day changes.

**8 display modes + 5 date styles** - Big digits, seconds, deciseconds, date overlay, word clock, Roman numerals, binary, or random cycling. Date modes include numeric, ISO year/week, moon phase, Western zodiac, and Chinese zodiac. Switch temporarily with a tap, or set your default in the menu.

---

## All features

- **Bell** – Off, ding, hour count, half-hour, pair, and ship's bell. Audible preview in menu.
- **Timer** – Stopwatch and countdown with multiple saved presets. Bell rings when time's up.
- **Night mode** – Dim, dark, mute, or combined modes on a schedule. Touch-to-wake when suppressed.
- **On-device menu** – Configure bell, style, time format, night mode, brightness, and date style via touch.
- **Web config portal** – Scan WiFi networks, set timezone, upload firmware - all in-browser.
- **Atomic time sync** – NTP over WiFi (traceable to atomic clocks) with DS1307/DS3231 RTC backup on I2C. Works fully offline - the RTC keeps accurate time when WiFi is unavailable, and the config portal supports manually setting the time.
- **OTA updates** – ArduinoOTA at `chronobell.local`. Wireless firmware pushes.
- **Persistence** – Display mode, brightness, bell mode, night mode, countdown preset - all survive power loss (NVS).

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 |
| Display | 8× MAX7219 8×8 LED modules (FC16_HW), 2 rows × 4 cols = 32×16px |
| RTC | DS1307 or DS3231 on I2C (address `0x68`) |
| Touch | CAP1188 capacitive touch controller on I2C (address `0x29`) |
| Bell | Active-high transistor/MOSFET driver on GPIO 13 |
| Boot button | GPIO 0 (INPUT_PULLUP) - short press opens config portal |

### Pinout

| Function | GPIO |
|----------|------|
| MAX7219 chip-select | 5 |
| MAX7219 SPI clock | 18 |
| MAX7219 SPI MOSI | default MOSI |
| I2C SDA | 22 |
| I2C SCL | 21 |
| Bell output | 13 |
| Boot button | 0 |

## Getting Started

### PlatformIO (recommended)

```bash
pio run --target upload
```

### Arduino CLI / IDE

Open `chronobell.ino` in the Arduino IDE or compile from the command line:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32
arduino-cli upload --fqbn esp32:esp32:esp32
```

### First boot

1. **Build and upload** to your ESP32 using either method above.
2. **Connect to the `ChronoBell`** access point on first boot and open `http://192.168.4.1` in your browser.
4. **Scan for WiFi networks**, enter credentials, choose your timezone, and save.

The clock will reboot, connect to your network, sync time via NTP, and start running.

### Config Portal

- Scan and join WiFi networks
- Set timezone from a picker
- See connection status and IP address
- Upload firmware via web browser

Reopen the portal by holding the center touch pad for 3 seconds, or press the BOOT button.

## Touch Controls

| Gesture | Pad | Action |
|---------|-----|--------|
| Tap | Left (pad 8) | Previous menu item / cycle mode |
| Tap | Center (pad 4) | Confirm / enter menu / ack alert |
| Tap | Right (pad 1) | Next menu item / cycle mode |
| Hold 1.5s | Center | Enter menu / exit timer / acknowledge countdown |
| Hold 3s | Center | Open WiFi config portal |
| Hold (auto-repeat) | Left / Right | Rapid scroll in menu |

When night mode has the display suppressed, the first touch wakes it for 60 seconds.

## Menu

| Item | Range | Description |
|------|-------|-------------|
| BELL | OFF / DING / HOUR / HALF / PAIR / SHIP | Bell striking mode (audible preview on selection) |
| STYLE | RND / BIG / SEC / DECI / DATE / WORD / ROMA / BIN | Display mode (RND cycles all modes randomly) |
| HOUR | 24H / 12H | Time format |
| NIGHT | OFF / LOW (dim only) / LOW+MUTE / DARK (off) / DARK+MUTE / MUTE (bell off) | Night mode |
| BRIGHT | 0–15 | LED matrix brightness |
| DATE | DATE / YEAR / MOON / ZOD / CZOD | Date style |

Hold center to enter the menu, left/right to browse, tap center to edit a value, tap center again to confirm.

## Configuration

Key constants in `Config.h`:

| Setting | Default | Notes |
|---------|---------|-------|
| `CAP1188_TOUCH_THRESHOLD` | `0x35` | Touch sensitivity; lower = more sensitive |
| `DISPLAY_BRIGHTNESS` | `4` | Default brightness (0–15) |
| `DISPLAY_FLIP` | `0` | Set to `1` to rotate display 180° |
| `BELL_PULSE_MS` | `500` | Bell coil-on duration |
| `NIGHT_DIM_START_HOUR` | `19` (7 PM) | Night dimming begins |
| `TIME_SYNC_INTERVAL_MINUTES` | `60` | NTP re-sync interval |
| `GUEST_WIFI_URL` | *(see config)* | HTTP URL for guest password; set to `""` to disable |
| `CONFIG_MODE_TIMEOUT_MINUTES` | `60` | Auto-reboot when stuck in config mode (`0` = stay indefinitely) |

## OTA Updates

Once connected to WiFi, find the device at `chronobell.local` (mDNS) and upload new firmware from the Arduino IDE or `pio run --target upload --upload-port chronobell.local`.
