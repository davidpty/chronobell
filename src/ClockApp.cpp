#include "ClockApp.h"

#include <Wire.h>
#include <time.h>
#include <esp_system.h>

#include "Config.h"

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
    DisplayMode::Drift,
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
                    _timeProvider, _wifiManager, _bellMode, _savedDisplayMode, _timeFormat, _nightMode}
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
    _display.setRuntimeMode(&_displayMode);
    _display.setDriftClock(&_driftClock);
    _display.setTimeFormat(&_timeFormat);
    _display.setDateStyle(&_activeDateStyle);
    _display.setGuestWifiController(&_guestWifi);

    _wifiManager.setOtaDisplayCallback(handleOtaDisplay, this);
    _wifiManager.setTimeProvider(&_timeProvider);

    _wifiManager.setSaveCallback(handleSettingsSaved, this);
    _wifiManager.setReconnectResultCallback(handleReconnectResult, this);
    _wifiManager.setPreviewCallback(handleWebPreview, this);

    // GuestWifi callback is wired in the .ino file via a trampoline
}

void ClockApp::wireTimerCallbacks(SavePresetFn savePreset,
                                  QueueAlertFn queueAlert,
                                  BellBusyFn   bellBusy,
                                  StopBellFn   stopBell) {
    _timerController.setCallbacks(savePreset, queueAlert, bellBusy, stopBell);
}

void ClockApp::wireTimerPersistenceCallbacks(CurrentEpochFn currentEpoch,
                                             SaveTargetEpochFn saveTargetEpoch,
                                             ClearTargetEpochFn clearTargetEpoch,
                                             SaveViewActiveFn saveViewActive,
                                             SaveUInt32Fn saveRemaining,
                                             ClearFn clearRemaining) {
    _timerController.setPersistenceCallbacks(currentEpoch, saveTargetEpoch,
                                             clearTargetEpoch, saveViewActive,
                                             saveRemaining, clearRemaining);
}

void ClockApp::wireStopwatchPersistenceCallbacks(SaveUInt64Fn saveElapsed,
                                                 ClearFn clearElapsed,
                                                 SaveTimeFn saveStartEpoch,
                                                 ClearFn clearStartEpoch,
                                                 SaveViewActiveFn saveViewActive) {
    _timerController.setStopwatchPersistenceCallbacks(saveElapsed, clearElapsed,
                                                      saveStartEpoch, clearStartEpoch,
                                                      saveViewActive);
}

void ClockApp::installTouchHandlers(OnTouchFn onPad1Press,
                                    OnTouchFn onPad8Press,
                                    OnTouchFn onPad4Release) {
    TouchPadConfig pad1;
    pad1.onPress = onPad1Press;
    _touchController.setHandler(1, pad1);

    TouchPadConfig pad4;
    pad4.onRelease = onPad4Release;
    _touchController.setHandler(4, pad4);

    TouchPadConfig pad8;
    pad8.onPress = onPad8Press;
    _touchController.setHandler(8, pad8);
}

void ClockApp::handleOtaDisplay(void* context, bool active, unsigned int progress, unsigned int total) {
    static_cast<ClockApp*>(context)->_display.showOtaUpdate(active, progress, total);
}

bool ClockApp::handleSettingsSaved(void* context,
                                   bool wifiChanged,
                                   bool tzChanged,
                                   bool manualTimeChanged,
                                   const String& wifiSsid,
                                   const String& wifiPassword) {
    return static_cast<ClockApp*>(context)->onSettingsSaved(wifiChanged, tzChanged, manualTimeChanged,
                                                            wifiSsid, wifiPassword);
}

void ClockApp::handleReconnectResult(void* context, bool success) {
    ClockApp* app = static_cast<ClockApp*>(context);
    LOGLN(success ? "Wi-Fi reconnect test finished" : "Wi-Fi reconnect test failed");
    app->reloadSettings();
    if (success) {
        app->_wifiSync.requestSync();
    }
}

void ClockApp::handleWebPreview(void* context, const String& field) {
    static_cast<ClockApp*>(context)->onWebPreview(field);
}

void ClockApp::initSerialAndPins() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    Serial.begin(SERIAL_BAUD);
    LOGLN("\nClock starting...");
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
        LOGLN("CAP1188 touch init OK");
    } else {
        LOGLN("CAP1188 not found");
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
    time_t targetEpoch = _settingsStore.loadCountdownTargetEpoch();
    uint32_t remainingMs = _settingsStore.loadCountdownRemainingMs();
    bool countdownViewActive = _settingsStore.loadCountdownViewActive();
    _timerController.restoreCountdown(targetEpoch, countdownViewActive, remainingMs);
    LOGF("Countdown preset: %u min\n",
                  (unsigned)COUNTDOWN_PRESET_MINUTES[presetIndex]);

    uint64_t swElapsed = _settingsStore.loadStopwatchElapsed();
    time_t swStartEpoch = _settingsStore.loadStopwatchStartEpoch();
    bool swViewActive = _settingsStore.loadStopwatchViewActive();
    _timerController.restoreStopwatch(swElapsed, swStartEpoch, swViewActive);
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
    _timeProvider.readRtc();
    ClockTime t = _rtcClock.getTime();
    LOGF("RTC after manual set: %02d:%02d:%02d\n",
                  t.hours, t.minutes, t.seconds);
}

void ClockApp::render() {
    syncDisplayModeSelection();
    syncDateStyleSelection();
    applyEffectiveDisplayBrightness();

    if (_displayMode == DisplayMode::Drift) {
        if (_lastDisplayModeSeen != DisplayMode::Drift) {
            ClockTime time;
            if (_timeProvider.currentTime(time)) {
                _driftClock.activate(time, millis());
                _lastDisplayModeSeen = DisplayMode::Drift;
            }
        }
    } else {
        _lastDisplayModeSeen = _displayMode;
    }

    _display.showTime();
}

// =============================================================================
// Per-tick services
// =============================================================================

void ClockApp::pollBootButton() {
    bool buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);

    if (buttonPressed && !_buttonWasPressed) {
        _buttonPressStart = millis();
        _buttonWasPressed = true;
    } else if (!buttonPressed && _buttonWasPressed) {
        _buttonWasPressed = false;

        LOG("Boot button short press: ");
        LOG(millis() - _buttonPressStart);
        LOGLN("ms - starting hotspot");

        _wifiManager.startHotspot();
    }
}

void ClockApp::pollTouch() {
    _touchController.update();
}

void ClockApp::tickWifiManager() {
    _wifiManager.loop();
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

    if (_menuController.isActive()) {
        LOGLN("T4 1.5s: cancel & exit menu");
        if (_menuController.isEdit()) {
            _menuController.cancelEdit();
        }
        _menuController.exit();
        return;
    }

    LOGLN("T4 1.5s: enter menu");
    _menuController.enterBrowse();
}

void ClockApp::tickMenu() {
    _menuController.update();
}

void ClockApp::updateBellSchedule() {
    int h = 0, m = 0, s = 0;
    bool timeValid = getCurrentClockTime(h, m, s);
    ClockTime time{h, m, s};
    if (timeValid && _displayMode == DisplayMode::Drift) {
        unsigned long nowMs = millis();
        _driftClock.update(time, nowMs);
        time = _driftClock.displayTime(time, nowMs);
    }
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
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    if (_timerController.isCountdownExpired()) {
        _timerController.onLeft();
        return;
    }
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
    if (_t4LongPressHandled) {
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

void ClockApp::onTouchMenuPrev(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive()) return;
    _menuController.onPrev();
}

void ClockApp::onTouchMenuNext(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive()) return;
    _menuController.onNext();
}

void ClockApp::onTouchMenuOk(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive()) return;
    _menuController.onOk();
}

void ClockApp::wireGuestWifiCallback(TimerController::GuestWifiAvailableFn fn) {
    _timerController.setGuestWifiAvailableCallback(fn);
}

void ClockApp::configureTouchRepeat(uint8_t pad, OnTouchFn onRepeat, uint32_t initialDelayMs, uint32_t rateMs) {
    _touchController.setPadRepeat(pad, onRepeat, initialDelayMs, rateMs);
}

void ClockApp::onTouchLeftRepeat(uint8_t pad) {
    if (!_menuController.isActive()) return;
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
    if (_timerController.isCountdownView()) {
        _timerController.noteActivity();
        _timerController.onRight();
    }
}

void ClockApp::saveCountdownPreset(uint8_t presetIndex) {
    _settingsStore.saveCountdownPreset(presetIndex);
}

bool ClockApp::currentEpoch(time_t& epoch) const {
    if (!_rtcClock.available()) {
        return false;
    }
    return _timeProvider.currentEpoch(epoch);
}

bool ClockApp::saveCountdownTargetEpoch(time_t targetEpoch) {
    return _settingsStore.saveCountdownTargetEpoch(targetEpoch);
}

bool ClockApp::clearCountdownTargetEpoch() {
    return _settingsStore.clearCountdownTargetEpoch();
}

bool ClockApp::saveCountdownViewActive(bool active) {
    return _settingsStore.saveCountdownViewActive(active);
}

bool ClockApp::saveCountdownRemainingMs(uint32_t remainingMs) {
    return _settingsStore.saveCountdownRemainingMs(remainingMs);
}

bool ClockApp::clearCountdownRemainingMs() {
    return _settingsStore.clearCountdownRemainingMs();
}

bool ClockApp::saveStopwatchElapsed(uint64_t elapsedMs) {
    return _settingsStore.saveStopwatchElapsed(elapsedMs);
}

bool ClockApp::clearStopwatchElapsed() {
    return _settingsStore.clearStopwatchElapsed();
}

bool ClockApp::saveStopwatchStartEpoch(time_t epoch) {
    return _settingsStore.saveStopwatchStartEpoch(epoch);
}

bool ClockApp::clearStopwatchStartEpoch() {
    return _settingsStore.clearStopwatchStartEpoch();
}

bool ClockApp::saveStopwatchViewActive(bool active) {
    return _settingsStore.saveStopwatchViewActive(active);
}

void ClockApp::queueBellAlert(uint8_t groups) {
    (void)groups;
    _bellController.queueCountdownAlert();
}

bool ClockApp::isBellBusy() const {
    return _bellController.isBusy();
}

void ClockApp::stopBell() {
    _bellController.stop();
}

// =============================================================================
// Live settings apply (called from config portal save callback)
// =============================================================================

bool ClockApp::onSettingsSaved(bool wifiChanged, bool tzChanged, bool manualTimeChanged, const String& wifiSsid, const String& wifiPassword) {
    LOGLN("Applying saved settings live...");

    int16_t oldTzOffset = _appSettings.timezone.offsetMinutes;
    bool oldManualEnabled = _appSettings.manualTime.enabled;
    reloadSettings();

    if (manualTimeChanged) {
        LOGLN("Applying manual time...");
        applyManualTime();
        if (_wifiManager.isHotspotActive()) {
            LOGLN("Manual time changed - reset hotspot timer");
            _wifiManager.resetHotspotTimer();
        }
    }

    if (tzChanged) {
        LOGLN("Applying timezone change...");
        NTPClient& ntp = _wifiSync.getNtpClient();
        ntp.setTimeOffset(_appSettings.timezone.offsetMinutes * 60);
        if (_rtcClock.available()) {
            ClockTime ct = _rtcClock.getTime();
            ClockDate cd = _rtcClock.getDate();
            struct tm tm;
            tm.tm_year = cd.year - 1900;
            tm.tm_mon = cd.month - 1;
            tm.tm_mday = cd.date;
            tm.tm_hour = ct.hours;
            tm.tm_min = ct.minutes;
            tm.tm_sec = ct.seconds;
            tm.tm_isdst = -1;
            time_t localEpoch = mktime(&tm);
            if (localEpoch > 0) {
                time_t utcEpoch = localEpoch - (oldTzOffset * 60);
                time_t newLocalEpoch = utcEpoch + (_appSettings.timezone.offsetMinutes * 60);
                _timeProvider.setRtcFromEpoch(newLocalEpoch);
            }
        }
    }

    if (oldManualEnabled && !_appSettings.manualTime.enabled) {
        LOGLN("Manual -> atomic, force NTP");
        _wifiSync.requestSync();
    }

    applyDisplayBrightness();

    if (wifiChanged) {
        LOGLN("WiFi changed, start bg test");
        if (!_wifiManager.startPendingNetworkReconnect(wifiSsid, wifiPassword, 15000)) {
            LOGLN("Unable to start pending Wi-Fi test");
            return false;
        }
    }

    return true;
}

void ClockApp::onWebPreview(const String& field) {
    if (field == "brightness") {
        int8_t b = _settingsStore.loadBrightness(4);
        _display.setUserBrightness(b);
    } else if (field == "datestyle") {
        _timerController.showDateView();
    } else if (field == "bellmode") {
        ClockTime now;
        if (getCurrentClockTime(now.hours, now.minutes, now.seconds)) {
            _bellController.preview(_appSettings.bellMode, now, true);
        }
    } else {
        _timerController.showClockPreview();
    }
}
