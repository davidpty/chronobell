#ifndef CLOCK_CONFIG_H
#define CLOCK_CONFIG_H

// ═════════════════════════════════════════════════════════════════════════════
// SECTION 1 — BEHAVIOR & SOFTWARE SETTINGS
// ═════════════════════════════════════════════════════════════════════════════
// Knobs you tweak most often: debug, features, night mode, bell, alerts, UI.

// ---------------------------------------------------------------------------
// Debug logging
// ---------------------------------------------------------------------------

#define DEBUG                                      0                                          // 1 = enable all logging (LOGLN, LOG, LOGF + hotspot DBG logs); 0 = compile out

#if DEBUG
  #define LOG(...)     Serial.print(__VA_ARGS__)
  #define LOGLN(...)   Serial.println(__VA_ARGS__)
  #define LOGF(...)    Serial.printf(__VA_ARGS__)
#else
  #define LOG(...) 
  #define LOGLN(...)
  #define LOGF(...)
#endif

// ---------------------------------------------------------------------------
// Feature flags
// ---------------------------------------------------------------------------

#define ENABLE_OTA                                  1                                          // ArduinoOTA (IDE network OTA); web portal upload is always enabled
#define ENABLE_MDNS                                 1                                          // mDNS / Bonjour hostname advertisement

#define DIGIT_TRANSITIONS                           1                                          // 0=remove engine, 1=include digit morph engine
#define DIGIT_TRANSITION_MS                         300                                        // per-digit morph duration in ms

#define SCREEN_TRANSITION                           1                                          // 0=remove engine, 1=include screen retune engine
#define SCREEN_TRANSITION_MS                        450                                        // whole-screen transition duration

#define REGION_TRANSITION                           1                                          // 0=remove engine, 1=include region centered-wipe engine
#define REGION_TRANSITION_MS                        450                                        // region transition duration in ms

#define FEATURE_NEW_YEAR                            1                                          // NYE countdown/sparkles/celebration from 21:00 to 00:02

// ---------------------------------------------------------------------------
// Night mode
// ---------------------------------------------------------------------------

#define NIGHT_DISPLAY_WAKE_MINUTES                  1                                          // First press consumed as wake; window extends on each touch

#define NIGHT_DIM_START_HOUR                        19                                         // Dimmed display starts
#define NIGHT_DIM_END_HOUR                          6                                          // Dimmed display ends

#define NIGHT_DARK_START_HOUR                       22                                         // Display-off period starts
#define NIGHT_DARK_END_HOUR                         6                                          // Display-off period ends

#define NIGHT_MUTE_START_HOUR                       22                                         // Bell-silence period starts
#define NIGHT_MUTE_END_HOUR                         6                                          // Bell-silence period ends

// ---------------------------------------------------------------------------
// Bell output timing
// ---------------------------------------------------------------------------

#define BELL_COIL_ON_MS                             300                                        // Minimum coil energize time for one strike
#define BELL_COIL_OFF_MS                            100                                        // Minimum coil de-energize time between strikes

#define BELL_STRIKE_GAP_MS                          100                                        // Additional pause within a group (added after COIL_OFF)
#define BELL_GROUP_GAP_MS                           600                                        // Additional pause between groups / un-grouped strikes (added after COIL_OFF)

// ---------------------------------------------------------------------------
// Timer alerts
// ---------------------------------------------------------------------------

#define TIMER_ALERT_DURATION_MINUTES                5                                          // Auto-silience after N min (0 = ring until manual tap)
#define TIMER_ALERT_SHOW_TIMEOUT_MINUTES            25                                         // Auto-dismiss after additional N min (0 = stay until manual tap)
#define TIMER_ALERT_REPEAT_SECONDS                  5                                          // Gap between repeat alerts

// ---------------------------------------------------------------------------
// UI / interaction
// ---------------------------------------------------------------------------

#define MENU_LONG_PRESS_MS                          1500                                       // 1.5 s hold → context menu / exit timer / cancel
#define MENU_REPEAT_RATE_MS                         500                                        // Repeat rate for held touch input

#define MENU_TIMEOUT_SHORT_SECONDS                  15                                         // Auto-exit short-lived views after this much inactivity
#define MENU_TIMEOUT_LONG_SECONDS                   30                                         // Auto-cancel longer edit flows after this much inactivity

#define BLINK_ON_MS                                 900                                        // Blink visible duration (menu, timer alert, etc.)
#define BLINK_OFF_MS                                100                                        // Blink blank duration (menu, timer alert, etc.)

#define LAST_STYLE_TIMEOUT_MINUTES                  720                                        // Reset remembered clock view to configured View after N idle min (0=off)
#define LAST_DATEVIEW_TIMEOUT_MINUTES               15                                         // Reset remembered date view to configured after N idle min (0=off)

// ---------------------------------------------------------------------------
// DATA style tuning
// ---------------------------------------------------------------------------

#define INFO_ALT_INTERVAL_SECONDS                   20                                         // ALT switches between DATE and WDAY every N seconds after entering ALT (valid 1-3600)

// ---------------------------------------------------------------------------
// BAR clock tuning
// ---------------------------------------------------------------------------

#define BAR_ALIGNMENT                               1                                          // 0 = left aligned, 1 = centered layout

#define BAR_HOUR_TOP_Y                              0                                          // Top row for the hour bar

#define BAR_HOUR_THICKNESS_NO_SECONDS               9                                          // Hour bar thickness when seconds are hidden
#define BAR_MINUTE_TOP_Y_NO_SECONDS                 13                                         // Top row for the minute bar when seconds are hidden

#define BAR_HOUR_THICKNESS_WITH_SECONDS             6                                          // Hour bar thickness when seconds are shown
#define BAR_MINUTE_TOP_Y_WITH_SECONDS               10                                         // Top row for the minute bar when seconds are shown
#define BAR_SECOND_TOP_Y                            14                                         // Top row for the seconds bar

// ---------------------------------------------------------------------------
// DRIFT display tuning
// ---------------------------------------------------------------------------

#define DRIFT_MAX_OFFSET_MINUTES                    60                                         // Maximum distance from real time in either direction, in minutes
#define DRIFT_PATTERN                               0                                          // 0 = behind<->ahead, 1 = real->behind->real, 2 = real->ahead->real
#define DRIFT_TIME_TO_MAX_OFFSET_MINUTES            120                                        // Minutes from real time to max offset; pattern 0 takes twice this between extremes; unsafe values are extended

// ---------------------------------------------------------------------------
// WiFi reconnection backoff (used by WiFiManagerLite)
// ---------------------------------------------------------------------------

#define CONNECTION_SLOW_RETRY_LIMIT                 12                                         // Internal backoff threshold before deep retry spacing
#define CONNECTION_DEEP_BACKOFF_INTERVAL_MINUTES    60                                         // Internal deep retry spacing, in minutes

// ═════════════════════════════════════════════════════════════════════════════
// SECTION 2 — NETWORK & ENVIRONMENT
// ═════════════════════════════════════════════════════════════════════════════
// Per-deployment: WiFi, NTP, AP, guest-password source.

#define KEEP_WIFI_ALIVE                             1                                          // 0 = WiFi/ArduinoOTA power down or 1 = stay up between hourly NTP
#define ENABLE_WIFI_SYNC                            1                                          // 1 = try WiFi+NTP, 0 = run fully offline
#define TIME_SYNC_INTERVAL_MINUTES                  60                                         // Minutes between periodic NTP re-syncs (0 = disable)

#define WIFI_CONNECT_ATTEMPTS                       10                                         // Internal connect attempts per sync cycle

#define NTP_SYNC_ATTEMPTS                           5                                          // Internal NTP update attempts per cycle
#define NTP_RETRY_SUCCESS_MINUTES                   60                                         // Internal sync interval after a successful NTP sync
#define NTP_RETRY_FAILED_MINUTES                    5                                          // Internal sync interval after a failed NTP sync

#define NTP_SERVER                                  "pool.ntp.org"

#define MDNS_HOSTNAME                               "chronobell"                               // Advertised as <name>.local for OTA / discovery
#define ARDUINO_OTA_PASSWORD                        "chronobell"                               // Password required to push firmware via ArduinoOTA

#define AP_SSID                                     "ChronoBell"                               // Config-mode access point (when the device opens its own WiFi network).
#define AP_PASSWORD                                 ""                                         // Empty string = open (no password) network
#define AP_CHANNEL                                  1                                          // 1..13

#define HOTSPOT_TIMEOUT_MINUTES                     60                                         // Auto-stop hotspot after N min (0 = stay until manual exit)

// ---------------------------------------------------------------------------
// Local LAN HTTP / background work defaults
// ---------------------------------------------------------------------------

#define LOCAL_HTTP_CONNECT_TIMEOUT_MS               500                                        // TCP connect timeout for router-local HTTP fetches
#define LOCAL_HTTP_TOTAL_TIMEOUT_MS                 3000                                       // Total HTTP fetch budget for router-local background tasks
#define LOCAL_NETWORK_TASK_PRIORITY                 1                                          // Lower than display/input timing work

// Internal text limit for short non-ChronoMsg local text.
#define LOCAL_DISPLAY_TEXT_MAX_LEN                  64                                         // Max short text displayed from guest WiFi and other local services

// ---------------------------------------------------------------------------
// Guest WiFi password display
// ---------------------------------------------------------------------------
// Fetches guest WiFi details from the router-side ChronoMsg JSON payload at
// boot and daily at the configured time, then shows them in the middle-button
// view cycle (as "GUEST"). Set GUEST_WIFI_ENABLED to 0 to compile it out.

#define GUEST_WIFI_ENABLED                          1                                          // 1 = compile guest WiFi in, 0 = remove it at build time

#define GUEST_WIFI_FETCH_HOUR                       0                                          // Hour of day to fetch guest Wi-Fi (0-23)
#define GUEST_WIFI_FETCH_MINUTE                     1                                          // Minute of hour to fetch guest Wi-Fi (0-59)

// Guest WiFi fetch implementation limits. Usually left alone.
#define GUEST_WIFI_FETCH_TIMEOUT_SECONDS            60                                         // HTTP timeout and retry cadence for guest Wi-Fi fetch attempts
#define GUEST_WIFI_FETCH_MAX_FAILURES               10                                         // Stop retrying after this many failed fetches
#define GUEST_WIFI_TASK_STACK_WORDS                 4096                                       // FreeRTOS stack for background guest Wi-Fi fetch

#define GUEST_WIFI_VIEW_TIMEOUT_SECONDS             60                                         // Time before the guest Wi-Fi view hides
#define GUEST_WIFI_SSID_SHOW_SECONDS                2                                          // Seconds to show SSID before switching to password
#define GUEST_WIFI_PASS_SHOW_SECONDS                8                                          // Seconds to show password in the guest Wi-Fi view

// ---------------------------------------------------------------------------
// Local JSON message notifications
// ---------------------------------------------------------------------------
// Generic LAN message endpoint. ChronoBell polls this in a low-priority task,
// caches active messages, and overlays previews/indicators without blocking
// the clock renderer.
//
// Firmware owns the visible behavior here: scroll timing, scroll-out
// completion, and how message text is measured/drawn.

#define CHRONOMSG_ENABLED                           1                                          // 1 = compile ChronoMsg in, 0 = remove it at build time

#define CHRONOMSG_URL                               "http://192.168.8.1/cgi-bin/chronomsg"     // Router-side CGI endpoint for ChronoMsg
#define CHRONOMSG_POLL_INTERVAL_SEC                 10                                         // Background poll interval in seconds
#define CHRONOMSG_DEFAULT_DURATION_SEC              10                                         // Default message lifetime fallback used by firmware
#define CHRONOMSG_SONG_REPEAT_SEC                   30                                         // Default repeat interval for MPD / repeat-policy messages
#define CHRONOMSG_SCROLL_STEP_MS                    140                                        // Firmware scroll animation step in milliseconds
#define CHRONOMSG_MIN_SCROLL_CYCLES                 1                                          // Minimum complete scroll passes before firmware may time out
#define CHRONOMSG_SCROLL_DEBUG                      0                                          // 1 = log ChronoMsg scroll position each rendered frame
#define CHRONOMSG_SCROLL_WORD_GAP_PX                4                                          // Gap between words in ChronoMsg scroll text
#define CHRONOMSG_SCROLL_EXIT_PAD_PX                0                                          // Extra blank pixels after the last glyph before finish

// Internal ChronoMsg engine limits. These rarely need tuning.
#define CHRONOMSG_MAX_MESSAGES                      5                                          // Max cached messages kept in memory
#define CHRONOMSG_MAX_RESPONSE_BYTES                2048                                       // Max HTTP response body size to read
#define CHRONOMSG_MAX_ID_LEN                        64                                         // Max message id length
#define CHRONOMSG_MAX_TEXT_LEN                      160                                        // Max rendered title/body text length
#define CHRONOMSG_TASK_STACK_WORDS                  8192                                       // FreeRTOS stack for ChronoMsg task


// ═════════════════════════════════════════════════════════════════════════════
// SECTION 3 — PER-BOARD CALIBRATION
// ═════════════════════════════════════════════════════════════════════════════
// Tune once per enclosure: brightness, flip, touch sensitivity.

// ---------------------------------------------------------------------------
// LED display tuning
// ---------------------------------------------------------------------------

#define DISPLAY_FLIP                                0                                           // 1 = rotate buffer 180° for upside-down mount

#define DISPLAY_TEST                                0                                           // Seconds to show boot test pattern (0 = skip)
#define DISPLAY_TEST_BLINK_MS                       10                                          // Inverted text blink interval during test (0 = steady)

// ---------------------------------------------------------------------------
// Touch sensor tuning (CAP1188)
// ---------------------------------------------------------------------------

#define CAP1188_DELTA_SENSE                         3                                           // Analog sensitivity multiplier 0–7 (higher = more sensitive)
#define CAP1188_TOUCH_THRESHOLD                     0x35                                        // Absolute trip point 0–127 (lower = trips sooner)

// Debounce applied in TouchController.cpp
#define CAP1188_PRESS_DEBOUNCE_MS                   30                                          // Discard releases shorter than this after a press
#define CAP1188_RELEASE_DEBOUNCE_MS                 250                                         // Debounce period after release
#define CAP1188_MIN_ACTION_TOUCH_MS                 30                                          // Min touch duration for an actionable press

// ═════════════════════════════════════════════════════════════════════════════
// SECTION 4 — HARDWARE CONFIGURATION
// ═════════════════════════════════════════════════════════════════════════════
// Set once per PCB layout: pin assignments, geometry, I2C addresses.

// ---------------------------------------------------------------------------
// Pin assignments
// ---------------------------------------------------------------------------

#define BELL_PIN                                    13                                          // GPIO driving bell coil (active HIGH)
#define BOOT_BUTTON_PIN                             0                                           // ESP32 GPIO 0, dev-board BOOT/STRAP (INPUT_PULLUP)
#define MAX7219_CS                                  5                                           // SPI chip-select for MAX7219 chain
#define I2C_SDA_PIN                                 22                                          // I2C data pin
#define I2C_SCL_PIN                                 21                                          // I2C clock pin

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------

#define SERIAL_BAUD                                 115200                                      // USB serial monitor baud rate

// ---------------------------------------------------------------------------
// Display geometry
// ---------------------------------------------------------------------------

#define MODULE_WIDTH                                8                                           // LEDs across one MAX7219 module
#define MODULE_HEIGHT                               8                                           // LEDs down one MAX7219 module
#define MODULE_COLS                                 4                                           // Modules side-by-side in each row
#define MODULE_ROWS                                 2                                           // Modules stacked vertically

#define COLS_PER_ROW                                (MODULE_COLS * MODULE_WIDTH)
#define ROWS_PER_MODULE                             (MODULE_HEIGHT)
#define TOTAL_ROWS                                  (MODULE_ROWS * ROWS_PER_MODULE)

#define MAX7219_NUM_MODULES                         8                                           // Daisy-chained MAX7219 chips

// ---------------------------------------------------------------------------
// I2C bus
// ---------------------------------------------------------------------------

#define I2C_CLOCK_SPEED                             100000                                      // Hz (100000 standard, 400000 fast mode)

// ---------------------------------------------------------------------------
// I2C device addresses
// ---------------------------------------------------------------------------

#define RTC_I2C_ADDRESS                             0x68                                        // DS1307 / DS3231 RTC

#define CAP1188_I2C_ADDRESS                         0x29                                        // CAP1188 touch controller (ADDR pin tied to VCC)
#define CAP1188_NUM_TOUCHES                         8                                           // Channels on chip (CS1..CS8)
#define CAP1188_ENABLED_INPUTS                      0x89                                        // Bitmask: 0x89 = CS1 | CS4 | CS8

#endif
