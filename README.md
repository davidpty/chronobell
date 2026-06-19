# ChronoBell

ChronoBell is a compact ESP32 clock with a 32x16 LED display, touch controls, configurable clock faces, and a bell output inspired by traditional ship's clocks. It keeps precise time in everyday modes, while Drift slowly moves the displayed time away from real time and back again as a quiet experiment in how time is perceived.

![ChronoBell](chronobell.png) ![Config Portal](chronoportal.png)

## What makes it different

**A bell that rings like a ship's clock** - Traditional 1-8 strike pattern from nautical tradition, plus six other modes: off, single ding, hour count, half-hour, pair, and triple. You can hear each one in the menu before you choose it.

**10 display styles + 5 date views** - Big digits, seconds, deciseconds, date and weekday overlays, word clock, roman numerals, binary, drift, or a configurable random view. Drift uses the big digital layout, but lets displayed time slowly move away from real time and return. Date views include day and month, year, moon phase, Western zodiac, and Chinese zodiac. Tap to peek at any view.

**Guest WiFi on screen** - Fetches a guest network password at boot and shows it on the clock. No phone needed. Good for lobbies, cafes, offices.

**Set time manually or let it self-correct** - It can set itself from WiFi and keep time on a battery-backed clock chip when offline. Or switch to manual mode and step through hour, minute, second, month, day, and year from the menu.

**Two ways to configure** - Tap the menu to change any setting on the device. Or press the BOOT button or flip HOTSPOT to ON in the menu to open the setup portal in your browser for WiFi, timezone, brightness - even firmware updates.

**Three touch pads, no moving parts** - Left, center, right. Tap to navigate, hold for the menu, hold longer for auto-repeat.

---

## What you can do with it

- **Bell** - Off, single ding, hour count, half-hour, pair, triple, or ship's bell. Hear a preview as you scroll.
- **Timer** - Use it as a stopwatch or a countdown timer. Countdown presets range from 1 to 90 minutes, and when time runs out the clock flashes `00:00`, rings a 3-2-1 alert, and clears itself after 15 minutes.
- **Night mode** - Dim the display, turn it off, mute the bell, or any combo - all on a schedule. Touch the clock to wake it for a minute.
- **Manual time** - Switch from atomic (NTP + RTC) to manual and step through HH→MM→SS→Month→Day→Year. Persists across reboots.
- **Config portal** - Scan WiFi networks, pick a timezone, tune display and bell settings, upload firmware - all from a browser.
- **Timekeeping** - NTP syncs every 60 minutes when WiFi is available. The RTC keeps time when it's not. Manual mode bypasses both.
- **OTA updates** - Push firmware over the air at `chronobell.local`.

---

## Clock Styles

ChronoBell has ten clock display modes. The menu label is short because the screen is only 32x16 pixels:

| Style | Menu | What it shows |
|-------|------|---------------|
| Random | RND | Changes at the configured hour interval using BIG, SEC, DECI, DATE, WDAY, WORD, ROMA, or BIN |
| Big | BIG | Large HH:MM digits, optimized for readability |
| Seconds | SEC | BIG layout with a seconds readout underneath |
| Deciseconds | DECI | BIG layout with seconds plus a live tenths digit underneath |
| Date overlay | DATE | Time on top with the selected date view underneath |
| Weekday overlay | WDAY | Time on top with a three-letter weekday underneath |
| Word clock | WORD | A compact phrase-style clock such as "TWENTY TO THREE" |
| Roman | ROMA | Roman-numeral-style hours and minutes |
| Binary | BIN | Binary hour, minute, and second rows |
| Drift | DRIFT | BIG-style digits where displayed time slowly drifts and returns |

### Date Views

The DATE clock style and the standalone date screen use the same five date views:

| Style | Menu | What it shows |
|-------|------|---------------|
| Date | DATE | Weekday and month/day |
| Year | YEAR | Calendar year and day of year |
| Moon | MOON | Lunar phase state plus the next full/new moon countdown |
| Western zodiac | ZOD | Western zodiac sign plus element |
| Chinese zodiac | CZOD | Chinese zodiac animal plus element |

## Timer

The timer screen has two jobs:

| Mode | What it does |
|------|--------------|
| Stopwatch | Counts up from zero until you stop it |
| Countdown | Counts down from a chosen preset, then alerts you when it reaches zero |

Stopwatch and countdown both keep their state across reboots. The center button moves between the clock, date, guest WiFi, stopwatch, and countdown screens. On the countdown screen, the right button changes the preset and the left button starts or pauses the timer.

When a countdown finishes, ChronoBell shows a blinking `00:00`, plays a short alert pattern, and stays on the countdown screen until you acknowledge it.

---

## Bell Modes

The bell can be off, simple, clock-like, or nautical. Scroll through BELL in the menu to hear a live preview before saving.

| Mode | Menu | What it does |
|------|------|--------------|
| Off | OFF | No scheduled bell |
| Single ding | DING | One strike on the hour only |
| Hour count | HOUR | Rings the 12-hour count on the hour, from 1 to 12 strikes |
| Half-hour | HALF | Rings the hour count on the hour, plus one strike at :30 |
| Pair | PAIR | Rings the hour count in groups of two with a short pause between pairs |
| Triple | TRIP | Rings the hour count in groups of three with a short pause between groups |
| Ship's bell | SHIP | Traditional ship's clock pattern: 1 to 8 strikes across the four-hour watches, with the half-hour counted in the sequence |

Timer alerts are separate from scheduled bell modes: when a countdown expires, ChronoBell plays its alert pattern even if the display is not in a clock style.

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
| Tap | Left | Previous menu item / previous display style / stopwatch start-stop / acknowledge expired countdown and stay on Countdown |
| Tap | Center | Confirm a menu choice / cycle views (Clock → Date → Guest WiFi → Stopwatch → Countdown) / acknowledge timer alert (returns to Clock only if the alert came from Clock) |
| Tap | Right | Next menu item / next display style / cycle countdown preset |
| Hold 1.5s | Center | Enter the menu |
| Hold (auto-repeat) | Left or Right | Scroll fast through menu items or countdown presets |

When night mode turns the display off, any touch wakes it for a minute.

---

## Menu

| Item | Choices | What it sets |
|------|---------|-------------|
| STYLE | RND / BIG / SEC / DECI / DATE / WORD / ROMA / BIN / DRIFT / WDAY | Clock style; drift is the mode that makes now feel less fixed |
| DATE | DATE / YEAR / MOON / ZOD / CZOD | Extra info shown in the date view |
| FORMAT | 24H / 12H | 24-hour or AM/PM |
| NIGHT | OFF / LOW / LOWM / DARK / DRKM / MUTE | Dim, mute, or turn off the display and bell on a schedule |
| BRIGHT | 0-15 | How bright the LEDs shine |
| BELL | OFF / DING / HOUR / HALF / PAIR / TRIP / SHIP | Scheduled bell/chime mode (scroll to hear a preview) |
| SETTIME | AUTO / MANUAL | Time source - automatic (NTP + RTC) or manual entry |
| HOTSPOT | OFF / ON | Turn the web config portal on or off |

Hold center to enter the menu, left/right to browse, and tap center to edit. STYLE has a second step for clocks with a separator: BIG, SEC, DECI, DATE, and WDAY offer STEADY or PULSE; DRIFT offers STEADY, PULSE, SPREAD, or TRACK. Each style remembers its own separator choice. WORD, ROMA, BIN, and RND save immediately because they do not expose a separator choice.

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
| `RND_STYLE_INTERVAL_HOURS` | `24` | RND change interval aligned to local midnight (`3` = 00:00, 03:00, 06:00...) |
| `HOTSPOT_TIMEOUT_MINUTES` | `0` | Auto-stop hotspot after N minutes (`0` = stays on) |
| `DRIFT_MAX_OFFSET_MINUTES` | `8` | Maximum distance from real time in either direction |
| `DRIFT_PATTERN` | `0` | 0=behind↔ahead, 1=real→behind→real, 2=real→ahead→real |
| `DRIFT_TIME_TO_MAX_OFFSET_MINUTES` | `60` | Minutes from real time to maximum offset; pattern 0 takes twice this between extremes |

---

## OTA Updates

Once the clock is on your network, find it at `chronobell.local` and push firmware wirelessly:

```bash
pio run --target upload --upload-port chronobell.local
```

You can also upload a `.bin` file through the web config portal - no cables needed.
