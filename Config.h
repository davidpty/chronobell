#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

// =============================================================================
// Feature flags
// =============================================================================
//
// Set to 0 to disable the feature and reclaim flash used by the library and
// all associated code paths.
//
//   ENABLE_OTA   — ArduinoOTA + mDNS-based OTA update (web UI + Arduino IDE)
//   ENABLE_MDNS  — mDNS / Bonjour hostname advertisement (hostname.local)

#define ENABLE_OTA               1
#define ENABLE_MDNS              1

// =============================================================================
// Debug logging
// =============================================================================
//
// Set DEBUG_SERIAL to 1 to enable Serial.print/println/printf output.
// When disabled (default), all logging calls compile to nothing, saving
// flash space used by format strings and call sites.

#define DEBUG_SERIAL             0

#if DEBUG_SERIAL
  #define LOG(...)     Serial.print(__VA_ARGS__)
  #define LOGLN(...)   Serial.println(__VA_ARGS__)
  #define LOGF(...)    Serial.printf(__VA_ARGS__)
#else
  #define LOG(...)
  #define LOGLN(...)
  #define LOGF(...)
#endif

// =============================================================================
// WiFi / NTP
// =============================================================================

#define KEEP_WIFI_ALIVE               1             // 0 = WiFi/ArduinoOTA power down or 1 =  stay up between hourly NTP
#define ENABLE_WIFI_SYNC              1             // 1 = try WiFi+NTP, 0 = run fully offline
#define TIME_SYNC_INTERVAL_MINUTES    60            // Minutes between periodic NTP re-syncs (0 = disable)

#define WIFI_CONNECT_ATTEMPTS         10            // Inner-loop connect attempts per sync cycle

#define NTP_SYNC_ATTEMPTS             5             // Inner-loop NTP update attempts per cycle
#define NTP_RETRY_SUCCESS_MINUTES     60            // Sync interval after a successful NTP sync
#define NTP_RETRY_FAILED_MINUTES      5             // Sync interval after a failed NTP sync

#define NTP_SERVER                    "pool.ntp.org"

#define MDNS_HOSTNAME                 "chronobell"  // Advertised as <name>.local for OTA / discovery
#define ARDUINO_OTA_PASSWORD          "chronobell"  // Password required to push firmware via ArduinoOTA

// Config-mode access point (when the device opens its own WiFi network).
#define AP_SSID                       "ChronoBell"
#define AP_PASSWORD                   ""            // Empty string = open (no password) network
#define AP_CHANNEL                    1             // 1..13

// =============================================================================
// Guest WiFi password display
// =============================================================================
//
// Fetches a password from an HTTP URL at boot and daily at the configured time,
// then shows it in the middle-button view cycle (as "GUEST"). The password is
// rendered in small font — one line if it fits, two lines split at the
// proportional midpoint if it doesn't.
//
// Set GUEST_WIFI_URL to "" to disable the entire feature at runtime 

#define GUEST_WIFI_URL               "http://192.168.8.1/qr/guest.txt"
#define GUEST_WIFI_FETCH_HOUR        0
#define GUEST_WIFI_FETCH_MINUTE      1
#define GUEST_WIFI_FETCH_TIMEOUT_SECONDS  5
#define GUEST_WIFI_TEXT_MAX_LEN           64
#define GUEST_WIFI_SSID_MAX_LEN           32
#define GUEST_WIFI_VIEW_TIMEOUT_SECONDS   60
#define GUEST_WIFI_SSID_SHOW_SECONDS      3
#define GUEST_WIFI_PASS_SHOW_SECONDS      7

// =============================================================================
// Night mode display wake
// =============================================================================
//
// NIGHT_DISPLAY_WAKE_MINUTES - When night mode has the display suppressed and
//                              the user presses a touch pad, the first press
//                              is consumed as a wake and subsequent presses
//                              act normally for this many minutes. The wake
//                              window is extended on every touch activity.

#define NIGHT_DISPLAY_WAKE_MINUTES         1

#define NIGHT_DIM_START_HOUR          19
#define NIGHT_DIM_END_HOUR            6
#define NIGHT_DARK_START_HOUR         22
#define NIGHT_DARK_END_HOUR           6
#define NIGHT_MUTE_START_HOUR         22
#define NIGHT_MUTE_END_HOUR           6

// =============================================================================
// Timer / clock peek
// =============================================================================
//
// TIMER_ALERT_DURATION_MINUTES - How long a countdown alert rings/flashes
//                                before it auto-acknowledges. 0 = ring
//                                until manually aborted.
// TIMER_ALERT_REPEAT_SECONDS  - Min seconds between repeated alerts when a
//                               countdown has expired and not been acknowledged.
// TIMER_ALERT_FLASH_MS        - Period at which the countdown display flashes
//                               on/off while expired.

#define TIMER_ALERT_DURATION_MINUTES       1
#define TIMER_ALERT_REPEAT_SECONDS         10
#define TIMER_ALERT_FLASH_MS               500

// =============================================================================
// Touch-pad-4 long-press thresholds
// =============================================================================
//
// Touch pad 4 has two stacked hold thresholds:
//   MENU_LONG_PRESS_MS     - 1.5 s: context-aware menu action
//                            (enter menu, exit timer, exit countdown, cancel).
//   MENU_EXTRA_LONG_PRESS  - 3.0 s: enter the WiFi config portal (hotspot).
//                            Exiting the hotspot uses MENU_LONG_PRESS_MS above.
//                            (Unit is ms; name lacks the _MS suffix by convention.)

#define MENU_LONG_PRESS_MS            1500
#define MENU_EXTRA_LONG_PRESS         3000
#define MENU_REPEAT_RATE_MS           500

// =============================================================================
// Shared UI inactivity timeouts.
// =============================================================================
// 
// Use the short timeout for brief secondary views and the long timeout for
// extended edit flows.
#define MENU_TIMEOUT_SHORT_SECONDS         15         // Auto-exit short-lived views after this much inactivity
#define MENU_TIMEOUT_LONG_SECONDS          30         // Auto-cancel longer edit flows after this much inactivity

#define MENU_BLINK_ON_MS              750           // Edit-mode value/preview visible duration
#define MENU_BLINK_OFF_MS             250           // Edit-mode value/preview blank duration

#define LAST_STYLE_TIMEOUT_MINUTES    360           // Reset remembered clock view to configured View after N idle min (0=off)
#define LAST_VIEW_TIMEOUT_MINUTES     360           // Reset remembered non-clock view to Date after N idle min (0=off)

// Auto-restart after this many minutes in hotspot/config mode.
// Set to 0 to disable (stay in config mode until manually exited).
#define CONFIG_MODE_TIMEOUT_MINUTES     60

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

#define COLS_PER_ROW                  (MODULE_COLS * MODULE_WIDTH)
#define ROWS_PER_MODULE               (MODULE_HEIGHT)
#define TOTAL_ROWS                    (MODULE_ROWS * ROWS_PER_MODULE)

// =============================================================================
// SPI bus (MAX7219 LED matrix chain)
// =============================================================================

#define MAX7219_CS                    5             // SPI chip-select
#define MAX7219_NUM_MODULES           8             // Number of daisy-chained MAX7219 chips

// =============================================================================
// I2C bus
// =============================================================================

#define I2C_SDA_PIN                   22
#define I2C_SCL_PIN                   21
#define I2C_CLOCK_SPEED               100000        // Hz. 100000 = standard, 400000 = fast mode

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
// WiFi reconnection backoff intervals (used by WiFiManagerLite)
// =============================================================================

#define CONNECTION_SLOW_RETRY_LIMIT          12    // Max slow-phase retries before deep backoff
#define CONNECTION_DEEP_BACKOFF_INTERVAL_MINUTES  60  // 1 hour

// =============================================================================
// Serial
// =============================================================================

#define SERIAL_BAUD                   115200        // USB serial monitor / debug logs

// =============================================================================
// Boot button (ESP32 GPIO 0, the dev-board BOOT/STRAP button)
// =============================================================================
//
// After boot, GPIO 0 is safe to use as a user button (INPUT_PULLUP). A short
// press opens the WiFi config portal from normal mode, and exits (reboots)
// from the config portal / hotspot.

#define BOOT_BUTTON_PIN               0

#endif
