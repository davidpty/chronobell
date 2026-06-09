#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

// =============================================================================
// I2C devices
// =============================================================================

#define RTC_I2C_ADDRESS             0x68            // DS1307/DS3231 RTC

#define CAP1188_I2C_ADDRESS         0x29            // CAP1188 touch controller (ADDR pin tied to VCC)
#define CAP1188_NUM_TOUCHES         8               // Channels on the chip (CS1..CS8)
#define CAP1188_ENABLED_INPUTS      0x89            // Bitmask of enabled channels: 0x89 = CS1 | CS4 | CS8 (0x01|0x08|0x80)

// =============================================================================
// CAP1188 touch sensitivity / debounce
// =============================================================================
//
// DELTA_SENSE   (reg 0x1F, bits 4-6, 0..7) - analog sensitivity multiplier.
//     HIGHER = more amplification = more sensitive to small capacitance
//     changes. 3 is a moderate default; raise toward 7 for very small
//     pads, lower toward 0 for noisy/large pads.
// TOUCH_THRESHOLD (reg 0x30, 0..127, per channel) - absolute trip point.
//     LOWER = trips sooner. 0x35 is moderate; raise toward 0x60+ for
//     false touches, lower toward 0x20 for missed touches.

#define CAP1188_DELTA_SENSE           3
#define CAP1188_TOUCH_THRESHOLD       0x35

// Debounce applied in TouchController.cpp before reporting an edge.
// CAP1188_MIN_ACTION_TOUCH_MS discards releases shorter than this after
// a press (so a quick tap fires onRelease exactly once).
#define CAP1188_PRESS_DEBOUNCE_MS     30
#define CAP1188_RELEASE_DEBOUNCE_MS   250
#define CAP1188_MIN_ACTION_TOUCH_MS   30

// =============================================================================
// WiFi / NTP
// =============================================================================

#define KEEP_WIFI_ALIVE               1             // 1 = WiFi/STA/mDNS/ArduinoOTA stay up after the first successful connect
                                                    // 0 = WiFi/STA powers down between hourly NTP
#define ENABLE_WIFI_SYNC              1             // 1 = try WiFi+NTP, 0 = run fully offline
#define TIME_SYNC_INTERVAL_MINUTES    60            // Minutes between periodic NTP re-syncs (0 = disable)

#define WIFI_CONNECT_TIMEOUT          10            // Inner-loop connect attempts per sync cycle
#define NTP_SYNC_TIMEOUT              5             // Inner-loop NTP update attempts per cycle

#define NTP_SERVER                    "pool.ntp.org"

#define MDNS_HOSTNAME                 "chronobell" // Advertised as <name>.local for OTA / discovery
#define ARDUINO_OTA_PASSWORD          "chronobell" // Password required to push firmware via ArduinoOTA

// Config-mode access point (when the device opens its own WiFi network).
#define AP_SSID                       "ChronoBell"
#define AP_PASSWORD                   ""            // Empty string = open (no password) network
#define AP_CHANNEL                    1             // 1..13

// Auto-restart after this many minutes in hotspot/config mode.
// Set to 0 to disable (stay in config mode until manually exited).
#define CONFIG_MODE_TIMEOUT_MINUTES     60

// =============================================================================
// SPI bus (MAX7219 LED matrix chain)
// =============================================================================

#define MAX7219_CS                    5             // SPI chip-select
#define MAX7219_SCK                   18            // SPI clock (data on default MOSI)
#define MAX7219_NUM_MODULES           8             // Number of daisy-chained MAX7219 chips

// =============================================================================
// I2C bus
// =============================================================================

#define I2C_SDA_PIN                   22
#define I2C_SCL_PIN                   21
#define I2C_CLOCK_SPEED               100000        // Hz. 100000 = standard, 400000 = fast mode

// =============================================================================
// Serial
// =============================================================================

#define SERIAL_BAUD                   115200        // USB serial monitor / debug logs

// =============================================================================
// LED display
// =============================================================================
//
// DISPLAY_BRIGHTNESS  : 0..15, MAX7219 INTENSITY register. Default value used
//                       when NVS has no saved brightness.
// DISPLAY_FLIP        : 0 = as wired, 1 = rotate buffer 180° at render time
//                       (for boards physically mounted upside-down).
// DISPLAY_TEST        : Seconds to light every LED on boot as a visual
//                       self-test. 0 = skip the test.
// BIG_TIME_BLINK_SEPARATOR : 1 = colon between hours and minutes in the big
//                            time display blinks every second; 0 = static.

#define DISPLAY_BRIGHTNESS            4
#define DISPLAY_FLIP                  0
#define DISPLAY_TEST                  0
#define BIG_TIME_BLINK_SEPARATOR      0

// Display geometry. Each MAX7219 module is an 8x8 LED matrix; the
// MODULE_COLS x MODULE_ROWS arrangement matches the physical wiring.
#define MODULE_WIDTH                  8             // LEDs across one module (== MAX7219 width)
#define MODULE_HEIGHT                 8             // LEDs down one module  (== MAX7219 height)
#define MODULE_COLS                   4             // Modules side-by-side
#define MODULE_ROWS                   2             // Modules stacked

#define COLS_PER_ROW                  32            // = MODULE_COLS * MODULE_WIDTH
#define ROWS_PER_MODULE               8             // = MODULE_HEIGHT
#define TOTAL_ROWS                    16            // = MODULE_ROWS * ROWS_PER_MODULE

// =============================================================================
// Bell output
// =============================================================================
//
// BELL_PIN drives a transistor/MOSFET that switches the bell coil. Active HIGH.

#define BELL_PIN                      13
#define BELL_PULSE_MS                 500           // Coil-on time for one strike
#define BELL_HOUR_GAP_MS              1000          // Gap between strikes in hourly / hour-count modes
#define BELL_SHIP_PAIR_GAP_MS         125           // Gap between the two strikes of a ship's-bell "double"
#define BELL_SHIP_GROUP_GAP_MS        750           // Gap between bell groups in ship's-bell sequences

// =============================================================================
// Boot button (ESP32 GPIO 0, the dev-board BOOT/STRAP button)
// =============================================================================
//
// After boot, GPIO 0 is safe to use as a user button (INPUT_PULLUP). A short
// press opens the WiFi config portal from normal mode, and exits (reboots)
// from the config portal / hotspot.

#define BOOT_BUTTON_PIN               0

// =============================================================================
// Touch-pad-4 long-press thresholds
// =============================================================================
//
// Touch pad 4 has two stacked hold thresholds:
//   MENU_LONG_PRESS_MS     - 1.5 s: context-aware menu action
//                            (enter menu, ack countdown, exit timer, cancel).
//   MENU_EXTRA_LONG_PRESS  - 3.0 s: enter the WiFi config portal (hotspot).
//                            Exiting the hotspot uses MENU_LONG_PRESS_MS above.
//                            (Unit is ms; name lacks the _MS suffix by convention.)

#define MENU_LONG_PRESS_MS            1500
#define MENU_EXTRA_LONG_PRESS         3000
#define MENU_REPEAT_RATE_MS           500

// Shared UI inactivity timeouts.
// Use the short timeout for brief secondary views and the long timeout for
// extended edit flows.
#define MENU_TIMEOUT_SHORT_MS         15000         // Auto-exit short-lived views after this much inactivity
#define MENU_TIMEOUT_LONG_MS          30000         // Auto-cancel longer edit flows after this much inactivity
#define MENU_BLINK_ON_MS              750           // Edit-mode value/preview visible duration
#define MENU_BLINK_OFF_MS             250           // Edit-mode value/preview blank duration

// Temporary view override timeout applied from the normal clock screen.
// Set to 0 to disable left/right temporary style/date switching entirely.
#define TEMP_OVERRIDE_MINUTES          360

// =============================================================================
// Timer / clock peek
// =============================================================================
//
// TIMER_ALERT_DURATION_MS - How long a countdown alert rings/flashes before it
//                           auto-acknowledges. 0 = ring until manually aborted.
// TIMER_ALERT_REPEAT_MS  - Min ms between repeated alerts when a countdown has
//                          expired and not been acknowledged.
// TIMER_ALERT_FLASH_MS   - Period at which the countdown display flashes on/off
//                          while expired.
// CLOCK_PEEK_DURATION_MS - How long each page is shown during a clock-info
//                          peek (stopwatch / countdown / date).
// CLOCK_PEEK_MAX_PAGES   - Max number of pages that can be queued for a peek.

#define TIMER_ALERT_DURATION_MS       60000UL
#define TIMER_ALERT_REPEAT_MS         10000UL
#define TIMER_ALERT_FLASH_MS          500UL
#define CLOCK_PEEK_DURATION_MS        5000UL
#define CLOCK_PEEK_MAX_PAGES          3

// =============================================================================
// Night mode display wake
// =============================================================================
//
// NIGHT_DISPLAY_WAKE_MS - When night mode has the display suppressed and the
//                         user presses a touch pad, the first press is
//                         consumed as a wake and subsequent presses act
//                         normally for this many milliseconds. The wake
//                         window is extended on every touch activity.

#define NIGHT_DISPLAY_WAKE_MS         60000UL

#define NIGHT_DIM_START_HOUR          19
#define NIGHT_DIM_END_HOUR            6
#define NIGHT_DARK_START_HOUR         22
#define NIGHT_DARK_END_HOUR           6
#define NIGHT_MUTE_START_HOUR         22
#define NIGHT_MUTE_END_HOUR           6

// =============================================================================
// Guest WiFi password display
// =============================================================================
//
// Fetches a password from an HTTP URL at boot and daily at the configured time,
// then shows it in the middle-button view cycle (as "GUEST"). The password is
// rendered in small font — one line if it fits, two lines split at the
// proportional midpoint if it doesn't.
//
// Set GUEST_WIFI_URL to "" to disable the entire feature at runtime (no
// compilation guards needed — the ROM cost is ~300 bytes).

#define GUEST_WIFI_URL               "http://192.168.8.1/qr/guest.txt"
#define GUEST_WIFI_FETCH_HOUR        0
#define GUEST_WIFI_FETCH_MINUTE      1
#define GUEST_WIFI_FETCH_TIMEOUT_MS  5000
#define GUEST_WIFI_TEXT_MAX_LEN      64
#define GUEST_WIFI_SSID_MAX_LEN      32
#define GUEST_WIFI_VIEW_TIMEOUT_MS   60000
#define GUEST_WIFI_SSID_SHOW_MS      2500
#define GUEST_WIFI_PASS_SHOW_MS      7500

#endif
