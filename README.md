# ChronoBell

An ESP32-powered LED matrix clock with capacitive touch, a ship's bell, 8 display styles, and a web-based setup portal. You can change most settings from the clock itself - no reflashing needed.

![ChronoBell](chronobell.png) ![Config Portal](chronoportal.png)

## What makes it different

**A bell that rings like a ship's clock** - Traditional 1-8 strike pattern from nautical tradition, plus five other modes (single ding, hour count, half-hour, pair). You can preview each one live in the menu before you pick it.

**8 display styles + 5 date views** - Big digits, seconds, deciseconds, date overlay, word clock, Roman numerals, binary, or a random one each day. Date extras include moon phase, ISO year/week, Western zodiac, and Chinese zodiac. Tap to peek at any view.

**Guest WiFi on screen** - Fetches a guest network password at boot and shows it on the clock. No phone needed. Good for lobbies, cafes, offices.

**Set time manually or let it self-correct** - NTP syncs over WiFi when connected, backed by a battery-powered RTC chip that keeps time offline too. Or switch to manual mode and step through hour, minute, second, month, day, and year from the menu.

**Two ways to configure** - Tap the menu to change any setting on the device. Or press the BOOT button or flip HOTSPOT to ON in the menu to open the setup portal in your browser for WiFi, timezone, brightness - even firmware updates.

**Three touch pads, no moving parts** - Left, center, right. Tap to navigate, hold for the menu, hold longer for auto-repeat.

---

## What you can do with it

- **Bell** - Off, single ding, hour count, half-hour, pair, or ship's bell. Hear a preview as you scroll.
- **Timer** - Stopwatch and countdown with 11 built-in presets (1 to 90 minutes). When time's up: 9 bell strikes and a blinking `00:00` that auto-dismisses after 15 minutes.
- **Night mode** - Dim the display, turn it off, mute the bell, or any combo - all on a schedule. Touch the clock to wake it for a minute.
- **Manual time** - Switch from atomic (NTP + RTC) to manual and step through HH→MM→SS→Month→Day→Year. Persists across reboots.
- **Config portal** - Scan WiFi networks, pick a timezone, tune display and bell settings, upload firmware - all from a browser.
- **Timekeeping** - NTP syncs every 60 minutes when WiFi is available. The RTC keeps time when it's not. Manual mode bypasses both.
- **OTA updates** - Push firmware over the air at `chronobell.local`.

---

## Getting Started

### Build and upload

```bash
# PlatformIO
pio run --target upload

# Arduino CLI
arduino-cli compile --fqbn esp32:esp32:esp32
arduino-cli upload --fqbn esp32:esp32:esp32
```

### First boot

1. Upload the firmware to your ESP32.
2. Connect to the **ChronoBell** WiFi access point and open `http://192.168.4.1`.
3. Scan for your network, enter the password, pick your timezone, and save.

The clock reboots, connects to WiFi, syncs time, and starts running.

### Reopen the config portal

Press the **BOOT** button or toggle **HOTSPOT** to ON in the menu.

---

## Touch Controls

| Gesture | Pad | What it does |
|---------|-----|-------------|
| Tap | Left | Previous menu item / previous display style / stopwatch start-stop |
| Tap | Center | Confirm a menu choice / cycle views (Clock → Date → Guest WiFi → Stopwatch → Countdown) / acknowledge timer alert |
| Tap | Right | Next menu item / next display style / cycle countdown preset |
| Hold 1.5s | Center | Enter the menu / exit timer / acknowledge countdown |
| Hold (auto-repeat) | Left or Right | Scroll fast through menu items or countdown presets |

When night mode turns the display off, any touch wakes it for a minute.

---

## Menu

| Item | Choices | What it sets |
|------|---------|-------------|
| STYLE | RND / BIG / SEC / DECI / DATE / WORD / ROMA / BIN | How the clock looks (RND picks a random one each day) |
| DATE | DATE / YEAR / MOON / ZOD / CZOD | Extra info shown in the date view |
| FORMAT | 24H / 12H | 24-hour or AM/PM |
| NIGHT | OFF / LOW / LOWM / DARK / DRKM / MUTE | Dim, mute, or turn off the display and bell on a schedule |
| BRIGHT | 0-15 | How bright the LEDs shine |
| BELL | OFF / DING / HOUR / HALF / PAIR / SHIP | Bell mode (scroll to hear a preview) |
| SETTIME | AUTO / MANUAL | Time source - automatic (NTP + RTC) or manual entry |
| HOTSPOT | OFF / ON | Turn the web config portal on or off |

Hold center to enter the menu, left/right to browse, tap center to edit, tap center again to confirm.

### Setting the time manually

When you pick MANUAL, the clock reads the current time and lets you step through six edits:

1. **HOUR** (0-23)
2. **MIN** (0-59)
3. **SEC** (0-59)
4. **MONTH** (1-12, shown as a name like JAN)
5. **DATE** (1-31)
6. **YEAR** (2024-2035)

Tap center to advance each step. On the last step, the clock saves the new time to the RTC and remembers it across reboots.

---

## Hardware

| Part | What it is |
|------|------------|
| Brain | ESP32 |
| Display | 8x MAX7219 8x8 LED modules (32 columns x 16 rows) |
| RTC | DS1307 or DS3231 (battery-backed, keeps time when power is off) |
| Touch | CAP1188 capacitive controller, 3 pads (left / center / right) |
| Bell | Driven by GPIO 13 (active-high) |
| Boot button | GPIO 0 - short press opens the config portal |

### Pin map

| Signal | GPIO |
|--------|------|
| MAX7219 chip-select | 5 |
| MAX7219 SPI clock | 18 |
| MAX7219 SPI data | default MOSI |
| I2C data | 22 |
| I2C clock | 21 |
| Bell output | 13 |
| Boot button | 0 |

---

## A few things you can tune

Open `Config.h` to adjust these:

| Constant | Default | What it does |
|----------|---------|-------------|
| `DISPLAY_FLIP` | `0` | Set to `1` if your display is mounted upside-down |
| `CAP1188_TOUCH_THRESHOLD` | `0x35` | Touch sensitivity - lower numbers trip more easily |
| `NIGHT_DIM_START_HOUR` | `19` (7 PM) | When night dimming starts |
| `GUEST_WIFI_URL` | *(see file)* | Guest WiFi password URL; set to `""` to disable |
| `TIME_SYNC_INTERVAL_MINUTES` | `60` | How often NTP re-syncs |
| `HOTSPOT_TIMEOUT_MINUTES` | `0` | Auto-stop hotspot after N minutes (`0` = stays on) |

---

## OTA Updates

Once the clock is on your network, find it at `chronobell.local` and push firmware wirelessly:

```bash
pio run --target upload --upload-port chronobell.local
```

You can also upload a `.bin` file through the web config portal - no cables needed.
