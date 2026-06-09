#include "ClockApp.h"

#include <Wire.h>
#include <time.h>
#include <esp_system.h>

#include "Config.h"
#include "fonts.h"

static const uint16_t COUNTDOWN_PRESET_MINUTES[] = {
    1, 3, 5, 10, 15, 20, 25, 30, 45, 60, 90
};
static const uint8_t COUNTDOWN_PRESET_COUNT =
    sizeof(COUNTDOWN_PRESET_MINUTES) / sizeof(COUNTDOWN_PRESET_MINUTES[0]);

namespace {
bool sameDate(const ClockDate& a, const ClockDate& b) {
    return a.day == b.day &&
           a.date == b.date &&
           a.month == b.month &&
           a.year == b.year;
}

static const DisplayMode RANDOM_STYLE_POOL[] = {
    DisplayMode::LargeDigitsOnly,
    DisplayMode::TimeWithSeconds,
    DisplayMode::TimeWithDeciseconds,
    DisplayMode::TimeWithDate,
    DisplayMode::Word,
    DisplayMode::Roma,
    DisplayMode::Bin,
};

static const uint8_t RANDOM_STYLE_POOL_COUNT =
    sizeof(RANDOM_STYLE_POOL) / sizeof(RANDOM_STYLE_POOL[0]);
}

// =============================================================================
// Construction
// =============================================================================

ClockApp::ClockApp()
    : _leds(MD_MAX72XX::FC16_HW, MAX7219_CS, MAX7219_NUM_MODULES)
    , _wifiManager(_settingsStore)
    , _wifiSync(_wifiManager, _timeProvider, _rtcClock, _settingsStore, _appSettings)
    , _display(_leds, _menuController, _timerController, _timeProvider, _settingsStore, _wifiManager)
    , _menuBindings{_appSettings, _settingsStore, _display, _bellController,
                    _timeProvider, _bellMode, _savedDisplayMode, _timeFormat, _nightMode}
{
}

// =============================================================================
// Boot phases
// =============================================================================

void ClockApp::beginControllers() {
    _bellController.begin();
    _timeProvider.begin(_rtcClock, _wifiSync.getNtpClient());
    _menuController.begin(MENU_ITEMS, MENU_ITEM_COUNT);
    _guestWifi.begin();

    _menuController.setContext(&_menuBindings);
    _menuController.setSettingsStore(&_settingsStore);
    _display.setMenuBindings(&_menuBindings);
    _display.setRuntimeMode(&_displayMode, &_bellMode);
    _display.setTimeFormat(&_timeFormat);
    _display.setDateStyle(&_activeDateStyle);
    _display.setGuestWifiController(&_guestWifi);

    // GuestWifi callback is wired in the .ino file via a trampoline
}

void ClockApp::wireTimerCallbacks(SavePresetFn savePreset,
                                  QueueAlertFn queueAlert,
                                  BellBusyFn   bellBusy,
                                  StopBellFn   stopBell) {
    _timerController.setCallbacks(savePreset, queueAlert, bellBusy, stopBell);
}

void ClockApp::installTouchHandlers(OnTouchFn onPad1Press,
                                    OnTouchFn onPad8Press,
                                    OnTouchFn onPad4Release,
                                    OnTouchFn onPad4Hold) {
    TouchPadConfig pad1;
    pad1.onPress = onPad1Press;
    _touchController.setHandler(1, pad1);

    TouchPadConfig pad4;
    pad4.onRelease = onPad4Release;
    pad4.holdMs    = MENU_EXTRA_LONG_PRESS;
    pad4.onHold    = onPad4Hold;
    _touchController.setHandler(4, pad4);

    TouchPadConfig pad8;
    pad8.onPress = onPad8Press;
    _touchController.setHandler(8, pad8);
}

void ClockApp::initSerialAndPins() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    Serial.begin(SERIAL_BAUD);
    LOGLN("\nESP32 MAX7219 Digital Clock Starting...");
    _wifiManager.setNetworkServiceConfig(MDNS_HOSTNAME, ARDUINO_OTA_PASSWORD);
}

void ClockApp::initDisplay() {
    _display.begin();
}

void ClockApp::initI2cAndRtc() {
    LOGLN("Initializing I2C for RTC...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_SPEED);

    if (_rtcClock.begin()) {
        LOGLN("RTC initialized successfully");
        _timeProvider.readRtc();
        ClockTime t = _rtcClock.getTime();
        LOGF("RTC time: %02d:%02d:%02d\n", t.hours, t.minutes, t.seconds);
    } else {
        LOGLN("RTC not available or not responding");
    }
}

void ClockApp::initCap1188() {
    LOGLN("Initializing CAP1188 touch sensor...");
    if (_touchController.begin()) {
        LOGLN("CAP1188 touch sensor initialized successfully");
    } else {
        LOGLN("CAP1188 not available or not responding");
    }
}

void ClockApp::loadSettings() {
    _appSettings = _settingsStore.load();
    _savedDisplayMode = _appSettings.displayMode;
    _activeDateStyle  = _appSettings.dateStyle;
    _temporaryDateStyle = _appSettings.dateStyle;
    _bellMode         = _appSettings.bellMode;
    _timeFormat       = _appSettings.timeFormat;
    _nightMode        = _appSettings.nightMode;
    syncDisplayModeSelection();
    syncDateStyleSelection();
    LOG("Clock style loaded: ");
    LOGLN(displayModeLabel(_appSettings.displayMode));
    LOG("Date style loaded: ");
    LOGLN(dateStyleLabel(_appSettings.dateStyle));
    LOG("Bell mode loaded: ");
    LOGLN((int)_bellMode);
    LOG("Time format loaded: ");
    LOGLN(timeFormatLabel(_appSettings.timeFormat));
    LOG("Night mode loaded: ");
    LOGLN(nightModeLabel(_appSettings.nightMode));
}

void ClockApp::loadTimerSettings() {
    uint8_t presetIndex = _settingsStore.loadCountdownPreset(COUNTDOWN_PRESET_COUNT);
    _timerController.begin(COUNTDOWN_PRESET_MINUTES, COUNTDOWN_PRESET_COUNT, presetIndex);
    LOGF("Loaded countdown preset: %u min\n",
                  (unsigned)COUNTDOWN_PRESET_MINUTES[presetIndex]);
}

void ClockApp::applyDisplayBrightness() {
    _display.loadBrightnessFromSettings();
    _nightModeController.begin(_nightMode);
    applyEffectiveDisplayBrightness();
}

void ClockApp::runDisplayTest() {
#if defined(DISPLAY_TEST) && DISPLAY_TEST > 0
    _display.runTest(DISPLAY_TEST);
    _display.setBrightness(_display.getBrightness());
#endif
}

void ClockApp::wifiBootSync() {
    _wifiSync.performBootSync();
}

void ClockApp::reloadSettings() {
    _appSettings = _settingsStore.load();
    _savedDisplayMode = _appSettings.displayMode;
    if (!_dateStyleOverrideActive) {
        _activeDateStyle = _appSettings.dateStyle;
        _temporaryDateStyle = _appSettings.dateStyle;
    }
    _bellMode         = _appSettings.bellMode;
    _timeFormat       = _appSettings.timeFormat;
    _nightMode        = _appSettings.nightMode;
    syncDisplayModeSelection();
    syncDateStyleSelection();
    LOG("Clock style loaded: ");
    LOGLN(displayModeLabel(_appSettings.displayMode));
    LOG("Date style loaded: ");
    LOGLN(dateStyleLabel(_appSettings.dateStyle));
    LOG("Bell mode loaded: ");
    LOGLN((int)_bellMode);
    LOG("Time format loaded: ");
    LOGLN(timeFormatLabel(_appSettings.timeFormat));
    LOG("Night mode loaded: ");
    LOGLN(nightModeLabel(_appSettings.nightMode));
}

void ClockApp::applyManualTime() {
    if (!_rtcClock.available() || !_appSettings.manualTime.enabled) {
        return;
    }
    unsigned long manualEpoch = _appSettings.manualTime.epoch;
    if (manualEpoch == 0) {
        return;
    }

    LOGLN("Applying manual time setting...");
    _timeProvider.setRtcFromEpoch((time_t)manualEpoch);
    _settingsStore.clearManualTime();
    _appSettings.manualTime.enabled = false;
    _appSettings.manualTime.epoch = 0;
    _timeProvider.readRtc();
    ClockTime t = _rtcClock.getTime();
    LOGF("RTC time after manual set: %02d:%02d:%02d\n",
                  t.hours, t.minutes, t.seconds);
}

void ClockApp::render() {
    syncDisplayModeSelection();
    syncDateStyleSelection();
    applyEffectiveDisplayBrightness();
    _display.showTime();
}

// =============================================================================
// Per-tick services
// =============================================================================

void ClockApp::pollBootButton() {
    bool buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    bool currentlyInConfigMode = _wifiManager.isInConfigMode();

    if (currentlyInConfigMode && !_inConfigMode) {
        _inConfigMode = true;
        LOGLN("=== Entered config/AP mode (via WiFiManager) ===");
    }

    if (buttonPressed && !_buttonWasPressed) {
        _buttonPressStart = millis();
        _buttonWasPressed = true;
    } else if (!buttonPressed && _buttonWasPressed) {
        _buttonWasPressed = false;

        if (_inConfigMode || _wifiManager.isInConfigMode()) {
            LOGLN("Boot button short press in config mode - rebooting ESP32...");
            restartFromConfigMode("Boot button");
            return;
        }

        LOG("Boot button short press: ");
        LOG(millis() - _buttonPressStart);
        LOGLN("ms - opening config portal");

        _inConfigMode = true;
        startConfigModePreferStationImmediately();
    }
}

void ClockApp::pollTouch() {
    _touchController.update();
}

void ClockApp::tickWifiManager() {
    _wifiManager.loop();
    if (_inConfigMode && !_wifiManager.isInConfigMode()) {
        _inConfigMode = false;
        _configModeStartMs = 0;
        LOGLN("Exited config mode");
    }
#if CONFIG_MODE_TIMEOUT_MINUTES > 0
    if (_inConfigMode && _configModeStartMs != 0) {
        unsigned long elapsed = (millis() - _configModeStartMs) / 60000UL;
        if (elapsed >= CONFIG_MODE_TIMEOUT_MINUTES) {
            LOG("Config mode timeout (");
            LOG(CONFIG_MODE_TIMEOUT_MINUTES);
            LOGLN(" min) — rebooting...");
            delay(100);
            ESP.restart();
        }
    }
#endif
}

void ClockApp::tickWifiSync() {
    _wifiSync.tick();
}

void ClockApp::tickRtc() {
    if (_rtcClock.available()) {
        _timeProvider.updateRtcTracking();
    }
}

void ClockApp::tickTimer() {
    _timerController.update();
}

void ClockApp::tickBell() {
    updateBellSchedule();
}

void ClockApp::tickGuestWifi() {
    if (_guestWifi.isDisabled()) {
        return;
    }

    int h = 0, m = 0, s = 0;
    if (!getCurrentClockTime(h, m, s)) {
        return;
    }

    ClockDate d;
    if (!_timeProvider.currentDate(d)) {
        return;
    }

    _guestWifi.tick(h, m, d.year, d.month, d.date);
}

void ClockApp::pollLongPress() {
    bool t4Pressed = _touchController.isPressed(4);

    if (!t4Pressed) {
        _t4LongPressHandled = false;
        return;
    }
    if (_t4LongPressHandled) return;

    uint32_t heldMs = _touchController.heldMs(4);
    if (heldMs < MENU_LONG_PRESS_MS) return;

    _t4LongPressHandled = true;

    if (_wifiManager.isInConfigMode()) {
        LOGLN("T4 1.5s: exit hotspot");
        restartFromConfigMode("Touch pad");
        return;
    }

    if (_timerController.isCountdownExpired()) {
        LOGLN("T4 1.5s: acknowledge countdown alert");
        _timerController.onLongPress();
    } else if (!_menuController.isActive() &&
               (_timerController.isClockView() || _timerController.isDateView() || _timerController.isGuestWifiView())) {
        LOGLN("T4 1.5s: enter menu");
        _menuController.enterBrowse();
    } else if (!_menuController.isActive() &&
               _timerController.onLongPress() == TimerLongPressAction::ExitTimerToClock) {
        LOGLN("T4 1.5s: exit timer to clock");
    } else {
        LOGLN("T4 1.5s: cancel & exit");
        if (_menuController.isEdit()) {
            _menuController.cancelEdit();
        }
        _menuController.exit();
    }
}

void ClockApp::tickMenu() {
    _menuController.update();
}

void ClockApp::updateBellSchedule() {
    int h = 0, m = 0, s = 0;
    bool timeValid = getCurrentClockTime(h, m, s);
    ClockTime time{h, m, s};
    bool muteAutomatic = _nightModeController.shouldMuteAutomaticBell(time);
    _bellController.update(time, timeValid, _bellMode,
                           _timerController.isCountdownExpired(),
                           muteAutomatic);
}

void ClockApp::syncDisplayModeSelection() {
    if (_displayOverrideActive &&
        LAST_STYLE_TIMEOUT_MINUTES > 0) {
        unsigned long now = millis();
        if ((int32_t)(now - _displayOverrideExpiresAt) >= 0 ||
            _savedDisplayMode != _displayOverrideSourceMode) {
            _displayOverrideActive = false;
        }
    } else if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        _displayOverrideActive = false;
    }

    DisplayMode baseMode = _savedDisplayMode;
    if (isRandomDisplayMode(_savedDisplayMode)) {
        ClockDate currentDate;
        bool haveDate = _timeProvider.currentDate(currentDate);

        if (!_randomDisplayModeValid) {
            _randomDisplayMode = pickRandomConcreteDisplayMode(DisplayMode::Rnd);
            _randomDisplayModeValid = true;
            if (haveDate) {
                _randomDisplayDate = currentDate;
                _randomDisplayDateValid = true;
            }
        }

        if (haveDate) {
            if (!_randomDisplayDateValid) {
                _randomDisplayDate = currentDate;
                _randomDisplayDateValid = true;
            } else if (!sameDate(currentDate, _randomDisplayDate)) {
                _randomDisplayDate = currentDate;
                _randomDisplayMode = pickRandomConcreteDisplayMode(_randomDisplayMode);
            }
        }

        baseMode = _randomDisplayMode;
    } else {
        _randomDisplayModeValid = false;
        _randomDisplayDateValid = false;
    }

    _displayMode = _displayOverrideActive ? _overrideDisplayMode : baseMode;
}

void ClockApp::syncDateStyleSelection() {
    if (_dateStyleOverrideActive &&
        LAST_STYLE_TIMEOUT_MINUTES > 0) {
        unsigned long now = millis();
        if ((int32_t)(now - _dateStyleOverrideExpiresAt) >= 0) {
            _dateStyleOverrideActive = false;
        }
    } else if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        _dateStyleOverrideActive = false;
    }

    _activeDateStyle = _dateStyleOverrideActive
        ? _temporaryDateStyle
        : _appSettings.dateStyle;
}

void ClockApp::cycleTemporaryDisplayMode(int direction) {
    if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        return;
    }

    syncDisplayModeSelection();
    int currentIndex = -1;
    for (uint8_t i = 0; i < RANDOM_STYLE_POOL_COUNT; i++) {
        if (RANDOM_STYLE_POOL[i] == _displayMode) {
            currentIndex = (int)i;
            break;
        }
    }
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    int nextIndex = (currentIndex + direction + RANDOM_STYLE_POOL_COUNT) % RANDOM_STYLE_POOL_COUNT;
    DisplayMode mode = RANDOM_STYLE_POOL[nextIndex];
    if (_displayOverrideActive && mode == _overrideDisplayMode) {
        return;
    }

    _overrideDisplayMode = mode;
    _displayOverrideSourceMode = _savedDisplayMode;
    _displayOverrideActive = true;
    _displayOverrideExpiresAt =
        millis() + (unsigned long)LAST_STYLE_TIMEOUT_MINUTES * 60000UL;
    _displayMode = _overrideDisplayMode;
    LOG("Temporary clock style: ");
    LOGLN(displayModeLabel(mode));
}

void ClockApp::cycleTemporaryDateStyle(int direction) {
    if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        return;
    }

    syncDateStyleSelection();
    int currentIndex = (int)_activeDateStyle;
    if (currentIndex < (int)DateStyle::Date || currentIndex > (int)DateStyle::Czod) {
        currentIndex = (int)DateStyle::Date;
    }

    int nextIndex = (currentIndex + direction + ((int)DateStyle::Czod + 1)) % ((int)DateStyle::Czod + 1);
    _temporaryDateStyle = (DateStyle)nextIndex;
    _dateStyleOverrideActive = true;
    _dateStyleOverrideExpiresAt =
        millis() + (unsigned long)LAST_STYLE_TIMEOUT_MINUTES * 60000UL;
    _activeDateStyle = _temporaryDateStyle;
    LOG("Temporary date style: ");
    LOGLN(dateStyleLabel(_temporaryDateStyle));
}

DisplayMode ClockApp::pickRandomConcreteDisplayMode(DisplayMode avoid) const {
    if (RANDOM_STYLE_POOL_COUNT == 0) {
        return DisplayMode::LargeDigitsOnly;
    }

    int avoidIndex = -1;
    for (uint8_t i = 0; i < RANDOM_STYLE_POOL_COUNT; i++) {
        if (RANDOM_STYLE_POOL[i] == avoid) {
            avoidIndex = (int)i;
            break;
        }
    }

    int selectableCount = (avoidIndex >= 0 && RANDOM_STYLE_POOL_COUNT > 1)
        ? RANDOM_STYLE_POOL_COUNT - 1
        : RANDOM_STYLE_POOL_COUNT;

    int pick = (int)(esp_random() % (uint32_t)selectableCount);
    if (avoidIndex >= 0 && RANDOM_STYLE_POOL_COUNT > 1 && pick >= avoidIndex) {
        pick++;
    }

    return RANDOM_STYLE_POOL[pick];
}

bool ClockApp::getCurrentClockTime(int& h, int& m, int& s) const {
    ClockTime t;
    bool ok = _timeProvider.currentTime(t);
    h = t.hours;
    m = t.minutes;
    s = t.seconds;
    return ok;
}

void ClockApp::applyEffectiveDisplayBrightness() {
    // While the user is editing the BRIGHT item, pin the LED to the current
    // user brightness so the selected value is visible on the whole screen.
    // This bypasses night-mode dimming and dark-mode suppression for the
    // duration of the preview; the normal path resumes on
    // save / cancel / exit / timeout.
    if (_menuController.isBrightPreviewActive()) {
        if (!_display.isEnabled()) {
            _display.setEnabled(true);
        }
        if (_display.getBrightness() != _display.getUserBrightness()) {
            _display.setBrightness(_display.getUserBrightness());
        }
        return;
    }

    // Sync the night-mode controller with the latest live state, then
    // let it decide the effective brightness for this tick. Avoid spamming
    // the LED driver by only writing when the value actually changes.
    _nightModeController.setMode(_nightMode);

    int h = 0, m = 0, s = 0;
    if (!getCurrentClockTime(h, m, s)) {
        return;
    }

    ClockTime now{h, m, s};
    int8_t effective = _nightModeController.tick(now, _display.getUserBrightness());
    bool enabled = !_nightModeController.isDisplaySuppressed();
    if (enabled != _display.isEnabled()) {
        _display.setEnabled(enabled);
    }
    if (effective != _display.getBrightness()) {
        _display.setBrightness(effective);
    }
}

// =============================================================================
// Callback handlers (invoked from .ino trampolines)
// =============================================================================

void ClockApp::onTouchLeft(uint8_t pad) {
    if (_menuController.isActive()) {
        onTouchMenuPrev(pad);
        return;
    }
    if (_wifiManager.isInConfigMode()) return;
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    if (_timerController.isDateView()) {
        cycleTemporaryDateStyle(-1);
        return;
    }
    if (_timerController.isClockView()) {
        if (LAST_STYLE_TIMEOUT_MINUTES > 0) {
            cycleTemporaryDisplayMode(-1);
            return;
        }
    }
    _timerController.onLeft();
}

void ClockApp::onTouchRight(uint8_t pad) {
    if (_menuController.isActive()) {
        onTouchMenuNext(pad);
        return;
    }
    if (_wifiManager.isInConfigMode()) return;
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    if (_timerController.isDateView()) {
        cycleTemporaryDateStyle(1);
        return;
    }
    if (_timerController.isClockView()) {
        if (LAST_STYLE_TIMEOUT_MINUTES > 0) {
            cycleTemporaryDisplayMode(1);
            return;
        }
    }
    _timerController.onRight();
}

void ClockApp::onTouchMiddleShort(uint8_t pad) {
    if (_t4LongPressHandled || _wifiManager.isInConfigMode()) {
        return;
    }
    if (_menuController.isActive()) {
        onTouchMenuOk(pad);
        return;
    }
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    _timerController.onMiddleShort();
}

void ClockApp::onEnterConfigOrExit(uint8_t pad) {
    if (_menuController.isActive()) {
        LOGF("Touch pad %u: held past 3 s, dismissing menu and opening portal\n", pad);
        _menuController.exit();
    } else {
        LOGF("Touch pad %u: 3 s hold -> config portal\n", pad);
    }

    if (_inConfigMode || _wifiManager.isInConfigMode()) {
        restartFromConfigMode("Touch pad");
        return;
    }

    _inConfigMode = true;
    startConfigModePreferStationImmediately();
}

void ClockApp::onTouchMenuPrev(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive() || _wifiManager.isInConfigMode()) return;
    _menuController.onPrev();
}

void ClockApp::onTouchMenuNext(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive() || _wifiManager.isInConfigMode()) return;
    _menuController.onNext();
}

void ClockApp::onTouchMenuOk(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive() || _wifiManager.isInConfigMode()) return;
    _menuController.onOk();
}

void ClockApp::wireGuestWifiCallback(TimerController::GuestWifiAvailableFn fn) {
    _timerController.setGuestWifiAvailableCallback(fn);
}

void ClockApp::configureTouchRepeat(uint8_t pad, OnTouchFn onRepeat, uint32_t initialDelayMs, uint32_t rateMs) {
    _touchController.setPadRepeat(pad, onRepeat, initialDelayMs, rateMs);
}

void ClockApp::onTouchLeftRepeat(uint8_t pad) {
    if (!_menuController.isActive() || _wifiManager.isInConfigMode()) return;
    if (_menuController.isEdit()) {
        _menuController.onPrev();
    }
}

void ClockApp::onTouchRightRepeat(uint8_t pad) {
    if (_menuController.isActive()) {
        if (_menuController.isEdit()) {
            _menuController.onNext();
        }
        return;
    }
    if (_wifiManager.isInConfigMode()) return;
    if (_timerController.isCountdownView()) {
        _timerController.noteActivity();
        _timerController.onRight();
    }
}

void ClockApp::saveCountdownPreset(uint8_t presetIndex) {
    _settingsStore.saveCountdownPreset(presetIndex);
}

void ClockApp::queueBellAlert(uint8_t groups) {
    _bellController.queueForcedGrouped(groups, 3);
}

bool ClockApp::isBellBusy() const {
    return _bellController.isBusy();
}

void ClockApp::stopBell() {
    _bellController.stop();
}

void ClockApp::restartFromConfigMode(const char* source) {
    if (_wifiManager.isUpdating()) {
        LOG(source);
        LOGLN(" held during firmware update - ignoring restart");
        return;
    }

    LOG(source);
    LOGLN(" held in config mode - rebooting ESP32...");
    delay(100);
    ESP.restart();
}

// =============================================================================
// Config-mode helpers
// =============================================================================

void ClockApp::startConfigModeImmediately() {
    LOGLN("=== ENTERING CONFIGURATION MODE ===");
    LOG("AP SSID: ");
    LOGLN(AP_SSID);
    LOGLN("Connect to this AP to configure WiFi");
    LOGLN("Press BOOT button, or hold touch pad 4 for 1.5 s, to reboot");

    _configModeStartMs = millis();
    _display.begin();
    _display.showHotspotSymbol();

    LOGLN("Hotspot symbol displayed");

    _wifiManager.startConfigMode();
}

void ClockApp::startConfigModePreferStationImmediately() {
    LOGLN("=== ENTERING CONFIGURATION MODE ===");
    LOGLN("Trying saved WiFi before starting hotspot");
    LOGLN("Press BOOT button, or hold touch pad 4 for 1.5 s, to reboot");

    _configModeStartMs = millis();
    _display.begin();
    _display.showHotspotSymbol();

    LOGLN("Config symbol displayed");

    _wifiManager.startConfigModePreferStation();
}
