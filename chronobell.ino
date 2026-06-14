/**
 * ESP32 Digital Clock with MAX7219 Modules
 * 8 x MAX7219 modules (8x8 LED each) - 2 rows x 4 columns
 * Hardware: FC16_HW arrangement
 *
 * Features:
 * - Wi-Fi configuration via captive portal (no hardcoded credentials)
 * - Timezone selection during setup
 * - NTP time synchronization
 * - DS1307 RTC backup clock (I2C)
 * - CAP1188 touch sensor for menu / timer / bell control
 * - Hourly / ship's-bell striking modes
 * - Stopwatch and countdown timer
 * - Persisted display mode, date style, bell mode, brightness, countdown preset
 *
 * All controllers, state, and behaviour live in ClockApp.{h,cpp}. This file
 * is the orchestration sketch: it wires the controllers' C-ABI callbacks to
 * ClockApp methods, then drives the boot sequence and per-tick services.
 */

#include <Arduino.h>
#include "src/ClockApp.h"
#include "Config.h"

ClockApp app;

// -----------------------------------------------------------------------------
// Trampolines: route C-style callbacks (TouchController pads, TimerController
// events) to ClockApp's C++ methods. Defining them here keeps the wiring
// visible at the sketch level.
// -----------------------------------------------------------------------------

// Touch pad wiring:
//   Pad 1 (physical right)  -> increment / next menu item
//   Pad 4 (centre)          -> OK / short action
//   Pad 8 (physical left)   -> decrement / previous menu item
static void onTouchPad1Press(uint8_t pad)    { app.onTouchRight(pad); }
static void onTouchPad8Press(uint8_t pad)    { app.onTouchLeft(pad); }
static void onTouchPad4Release(uint8_t pad)  { app.onTouchMiddleShort(pad); }
// Auto-repeat handlers: fire same action repeatedly while held for
// MENU_LONG_PRESS_MS, at MENU_REPEAT_RATE_MS intervals.
static void onTouchPad1Repeat(uint8_t pad)   { app.onTouchRightRepeat(pad); }
static void onTouchPad8Repeat(uint8_t pad)   { app.onTouchLeftRepeat(pad); }

// TimerController callbacks: persist the chosen preset, ring the bell on a
// countdown alert, allow ClockApp to query / stop the bell as needed.
static void onSaveCountdownPreset(uint8_t i) { app.saveCountdownPreset(i); }
static bool onCurrentEpoch(time_t& epoch)    { return app.currentEpoch(epoch); }
static bool onSaveCountdownTargetEpoch(time_t epoch) {
    return app.saveCountdownTargetEpoch(epoch);
}
static bool onClearCountdownTargetEpoch()    { return app.clearCountdownTargetEpoch(); }
static bool onSaveCountdownViewActive(bool active) {
    return app.saveCountdownViewActive(active);
}
static bool onSaveStopwatchElapsed(uint64_t ms) { return app.saveStopwatchElapsed(ms); }
static bool onClearStopwatchElapsed()           { return app.clearStopwatchElapsed(); }
static bool onSaveStopwatchStartEpoch(time_t e) { return app.saveStopwatchStartEpoch(e); }
static bool onClearStopwatchStartEpoch()        { return app.clearStopwatchStartEpoch(); }
static bool onSaveStopwatchViewActive(bool a)   { return app.saveStopwatchViewActive(a); }
static void onQueueBellAlert(uint8_t g)      { app.queueBellAlert(g); }
static bool onBellBusy()                     { return app.isBellBusy(); }
static void onStopBell()                     { app.stopBell(); }

// GuestWifi availability callback: returns true if a guest password has been
// fetched and is ready to display.
static bool onGuestWifiAvailable()           { return app.isGuestWifiAvailable(); }

// -----------------------------------------------------------------------------
// Boot sequence
// -----------------------------------------------------------------------------

void setup() {
    // --- Wire controllers ---
    app.beginControllers();
    app.wireTimerCallbacks(onSaveCountdownPreset, onQueueBellAlert,
                           onBellBusy, onStopBell);
    app.wireTimerPersistenceCallbacks(onCurrentEpoch, onSaveCountdownTargetEpoch,
                                      onClearCountdownTargetEpoch,
                                      onSaveCountdownViewActive);
    app.wireStopwatchPersistenceCallbacks(onSaveStopwatchElapsed, onClearStopwatchElapsed,
                                          onSaveStopwatchStartEpoch, onClearStopwatchStartEpoch,
                                          onSaveStopwatchViewActive);
    app.installTouchHandlers(onTouchPad1Press, onTouchPad8Press,
                             onTouchPad4Release);
    app.configureTouchRepeat(1, onTouchPad1Repeat,
                             MENU_LONG_PRESS_MS, MENU_REPEAT_RATE_MS);
    app.configureTouchRepeat(8, onTouchPad8Repeat,
                             MENU_LONG_PRESS_MS, MENU_REPEAT_RATE_MS);
    app.wireGuestWifiCallback(onGuestWifiAvailable);

    // --- Init hardware ---
    app.initSerialAndPins();
    app.initDisplay();
    app.initI2cAndRtc();
    app.initCap1188();

    // --- Load persisted state ---
    app.loadSettings();
    app.loadTimerSettings();
    app.applyDisplayBrightness();

    // --- Optional LED self-test (re-applies brightness afterwards) ---
    app.runDisplayTest();

    // --- Show the time while WiFi/NTP init runs ---
    app.render();

    // --- Network sync (NTP) ---
    app.wifiBootSync();
    app.tickGuestWifi();   // Boot fetch for guest wifi password
    app.reloadSettings();
    app.applyManualTime();
    app.applyDisplayBrightness();
    app.render();

    LOGLN("Clock running.");
}

// -----------------------------------------------------------------------------
// Per-tick services
// -----------------------------------------------------------------------------

void loop() {
    // --- Input ---
    app.pollBootButton();
    app.pollTouch();

    // --- Time sync ---
    app.tickWifiManager();
    app.tickWifiSync();
    app.tickRtc();

    // --- App logic ---
    app.tickTimer();
    app.tickBell();
    app.tickGuestWifi();
    app.pollLongPress();
    app.tickMenu();

    // --- Render ---
    app.render();
    delay(20);
}
