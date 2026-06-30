#include "ClockApp.h"

#include <Wire.h>
#include <time.h>
#include <esp_system.h>
#include "Config.h"
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
#include "DigitTransition.h"
#endif

static const uint16_t COUNTDOWN_PRESET_MINUTES[] = {
    1, 3, 5, 10, 15, 20, 25, 30, 45, 60, 90
};
static const uint8_t COUNTDOWN_PRESET_COUNT =
    sizeof(COUNTDOWN_PRESET_MINUTES) / sizeof(COUNTDOWN_PRESET_MINUTES[0]);

// -----------------------------------------------------------------------------
// File-scope helpers for style selection and JSON serialization
// -----------------------------------------------------------------------------

static bool sameDate(const ClockDate& a, const ClockDate& b) {
    return a.day == b.day &&
           a.date == b.date &&
           a.month == b.month &&
           a.year == b.year;
}

static const DisplayMode QUICK_STYLE_POOL[] = {
    DisplayMode::Rnd,
    DisplayMode::LargeDigitsOnly,
    DisplayMode::Info,
    DisplayMode::Word,
    DisplayMode::Roma,
    DisplayMode::Dial,
    DisplayMode::Bar,
    DisplayMode::Bin,
    DisplayMode::Pong,
    DisplayMode::Drift,
};

static const uint8_t QUICK_STYLE_POOL_COUNT =
    sizeof(QUICK_STYLE_POOL) / sizeof(QUICK_STYLE_POOL[0]);

// DRIFT intentionally excluded — interactive time-warping style unsuited
// for automatic random cycling (needs user participation).
static const DisplayMode RANDOM_STYLE_POOL[] = {
    DisplayMode::LargeDigitsOnly,
    DisplayMode::Info,
    DisplayMode::Word,
    DisplayMode::Roma,
    DisplayMode::Dial,
    DisplayMode::Bar,
    DisplayMode::Bin,
    DisplayMode::Pong,
};

static const uint8_t RANDOM_STYLE_POOL_COUNT =
    sizeof(RANDOM_STYLE_POOL) / sizeof(RANDOM_STYLE_POOL[0]);

static void appendJsonString(String& out, const String& value) {
    out += '"';
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out += c;
                break;
        }
    }
    out += '"';
}

static void appendJsonUInt64(String& out, uint64_t value) {
    char buf[21];
    size_t pos = sizeof(buf);
    buf[--pos] = '\0';
    do {
        buf[--pos] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value > 0 && pos > 0);
    out += &buf[pos];
}

// =============================================================================
// Construction
// =============================================================================

ClockApp::ClockApp()
    : _leds(MD_MAX72XX::FC16_HW, MAX7219_CS, MAX7219_NUM_MODULES)
    , _wifiManager(_settingsStore)
    , _wifiSync(_wifiManager, _timeProvider, _rtcClock, _settingsStore, _appSettings)
    , _display(_leds, _menuController, _timerController, _timeProvider, _settingsStore, _wifiManager)
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    , _menuBindings{_appSettings, _settingsStore, _display, _bellController,
                    _timeProvider, _wifiManager, _bellMode, _savedDisplayMode, _timeFormat, _nightMode,
                    _appSettings.transitionMode}
#else
    , _menuBindings{_appSettings, _settingsStore, _display, _bellController,
                    _timeProvider, _wifiManager, _bellMode, _savedDisplayMode, _timeFormat, _nightMode}
#endif
{
}

// =============================================================================
// Boot phases
// =============================================================================

void ClockApp::beginControllers() {
    _bellController.begin();
    _timeProvider.begin(_rtcClock, _wifiSync.getNtpClient());
    _menuController.begin(MENU_ITEMS, MENU_ITEM_COUNT);
#if GUEST_WIFI_ENABLED
    _guestWifi.begin();
#endif
#if CHRONOMSG_ENABLED
    _messageClient.begin(&_timeProvider);
#endif

    _menuController.setContext(&_menuBindings);
    _menuController.setSettingsStore(&_settingsStore);
    _display.setMenuBindings(&_menuBindings);
    _display.setRuntimeMode(&_displayMode);
    _display.setAppSettings(&_appSettings);
    _display.setDriftTimeModel(&_driftTimeModel);
    _display.setTimeFormat(&_timeFormat);
    _display.setDateStyle(&_activeDateStyle);
#if GUEST_WIFI_ENABLED
    _display.setGuestWifiController(&_guestWifi);
#endif
    _display.setNewYearController(&_newYearController);

    _wifiManager.setOtaDisplayCallback(handleOtaDisplay, this);
    _wifiManager.setTimeProvider(&_timeProvider);

    _wifiManager.setSaveCallback(handleSettingsSaved, this);
    _wifiManager.setReconnectResultCallback(handleReconnectResult, this);
    _wifiManager.setPreviewCallback(handleWebPreview, this);
    _wifiManager.setTimerStatusCallback(handleTimerStatus, this);

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
    syncRuntimeSettingsFromLoaded(true);
    logLoadedSettings();
    restoreTemporaryStyleOverride();
}

void ClockApp::syncRuntimeSettingsFromLoaded(bool forceDateStyleReset) {
    _savedDisplayMode = _appSettings.displayMode;
    if (forceDateStyleReset || !_dateStyleOverrideActive) {
        _activeDateStyle = _appSettings.dateStyle;
        _temporaryDateStyle = _appSettings.dateStyle;
    }
    _bellMode         = _appSettings.bellMode;
    _timeFormat       = _appSettings.timeFormat;
    _nightMode        = _appSettings.nightMode;
    syncDisplayModeSelection();
    syncDateStyleSelection();
#if DIGIT_TRANSITIONS
    digit_transition::set_transition_mode(_appSettings.transitionMode);
#endif
}

void ClockApp::logLoadedSettings() const {
    LOG("Clock style loaded: ");
    LOGLN(displayModeLabel(_appSettings.displayMode));
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    LOG("Animation mode loaded: ");
    LOGLN(transitionModeLabel(_appSettings.transitionMode));
#endif
    LOG("Date style loaded: ");
    LOGLN(dateStyleLabel(_appSettings.dateStyle));
    LOG("Bell mode loaded: ");
    LOGLN((int)_bellMode);
    LOG("Time format loaded: ");
    LOGLN(timeFormatLabel(_appSettings.timeFormat));
    LOG("Night mode loaded: ");
    LOGLN(nightModeLabel(_appSettings.nightMode));
}

void ClockApp::restoreTemporaryStyleOverride() {
    if (LAST_STYLE_TIMEOUT_MINUTES > 0) {
        DisplayMode savedLabel;
        time_t tempEpoch;
        if (_settingsStore.loadTemporaryStyle(savedLabel, tempEpoch)) {
            time_t now;
            if (_timeProvider.currentEpoch(now)) {
                time_t elapsed = now - tempEpoch;
                unsigned long timeoutSecs = (unsigned long)LAST_STYLE_TIMEOUT_MINUTES * 60UL;
                if ((unsigned long)elapsed < timeoutSecs) {
                    savedLabel = clampDisplayMode((int)savedLabel);
                    DisplayMode mode = isRandomDisplayMode(savedLabel)
                        ? pickRandomConcreteDisplayMode(DisplayMode::Rnd)
                        : savedLabel;
                    _overrideState.active = true;
                    _overrideState.mode = mode;
                    _overrideState.sourceMode = _savedDisplayMode;
                    _overrideState.expiresAt = millis() + (timeoutSecs - (unsigned long)elapsed) * 1000UL;
                    _stylePreviewLabel = savedLabel;
                    _displayMode = _overrideState.mode;
                    LOG("Restored temporary style: ");
                    LOGLN(displayModeLabel(savedLabel));
                }
            }
        }
    }
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
    syncRuntimeSettingsFromLoaded(false);
    logLoadedSettings();
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
    updateNewYearState();
    syncDisplayModeSelection();
    syncDateStyleSelection();
    applyEffectiveDisplayBrightness();

#if CHRONOMSG_ENABLED
    ChronoMessage previewMessage;
    if (_messageClient.currentPreview(previewMessage)) {
        bool finished = _display.drawChronoMessage(previewMessage, millis(), _messageClient.previewStartMs());
        _messageClient.noteCurrentPreviewRendered(finished);
        return;
    }
#endif

    if (_styleNamePreviewEndMs > 0 && _overrideState.active) {
        unsigned long now = millis();
        if (now < _styleNamePreviewEndMs) {
            DisplayMode m = _displayMode;
            if (_stylePreviewPrevMode != m) {
#if SCREEN_TRANSITION
                if (_appSettings.transitionMode == TransitionMode::Morph)
                    _display.requestScreenTransition();
#endif
                _stylePreviewPrevMode = m;
            }
            _display.clearBuffer();
            _display.drawCenteredMediumText(displayModeLabel(_stylePreviewLabel), 3);
            _display.renderBuffer();
            return;
        }
        _styleNamePreviewEndMs = 0;
    }

    if (_displayMode == DisplayMode::Drift) {
        if (_lastDisplayModeSeen != DisplayMode::Drift) {
            ClockTime time;
            if (_timeProvider.currentTime(time)) {
                _driftTimeModel.activate(time, millis());
                _lastDisplayModeSeen = DisplayMode::Drift;
            }
        }
    } else if (_displayMode == DisplayMode::Pong) {
        refreshPongOnEntry();
    } else {
        _lastDisplayModeSeen = _displayMode;
    }

    _display.showTime();

#if CHRONOMSG_ENABLED
    if (!_messageClient.isPreviewVisible() && _messageClient.hasUnread()) {
        _display.drawUnreadMessageIndicator(_messageClient.unreadCount(), _messageClient.highestPriority(), millis());
    }
#endif
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

#if GUEST_WIFI_ENABLED
void ClockApp::tickGuestWifi() {
    if (_guestWifi.isDisabled()) {
        return;
    }

    if (!_guestWifi.bootFetchDone()) {
        _guestWifi.tick(0, 0, 0, 0, 0);
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
#endif

#if CHRONOMSG_ENABLED
void ClockApp::tickMessages() {
    _messageClient.update();
    if (_messageClient.isPreviewVisible() && !_msgBellFired) {
        ChronoMessage msg;
        if (_messageClient.currentPreview(msg) && msg.bellPatternCount > 0) {
            uint8_t total = 0;
            for (uint8_t i = 0; i < msg.bellPatternCount; i++) {
                total += msg.bellPattern[i];
            }
            _bellController.queuePattern(total, msg.bellPattern, msg.bellPatternCount, false, "msg");
        }
        _msgBellFired = true;
    } else if (!_messageClient.isPreviewVisible()) {
        _msgBellFired = false;
    }
}
#endif

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
    onTouchMiddleLong(4);
}

void ClockApp::tickMenu() {
    _menuController.update();
}

void ClockApp::updateBellSchedule() {
    ClockTime realTime{};
    ClockDate realDate;
    bool timeValid = updateNewYearState(&realTime, &realDate);
    ClockTime time = realTime;
    if (timeValid && _displayMode == DisplayMode::Drift) {
        unsigned long nowMs = millis();
        _driftTimeModel.update(time, nowMs);
        time = _driftTimeModel.displayTime(time, nowMs);
    }

    bool muteAutomatic = timeValid
        ? _nightModeController.shouldMuteAutomaticBell(realTime)
        : false;
    bool suppressScheduledStrike = false;

    if (timeValid) {
        if (_newYearController.hasMidnightBellRequest()) {
            if (!_timerController.isCountdownExpired() && !_bellController.isBusy()) {
                _bellController.queueNewYearAlert();
                _newYearController.resolveMidnightBellRequest();
            }
        }

        if (_newYearController.hasCountdownTickRequest()) {
            if (!_bellController.isBusy()) {
                _bellController.queueCountdownTickAlert();
                _newYearController.resolveCountdownTickRequest();
            }
        }

        if (_newYearController.hasCountdownSecondTickRequest()) {
            if (!_bellController.isBusy()) {
                _bellController.queueCountdownSecondTickAlert();
                _newYearController.resolveCountdownSecondTickRequest();
            }
        }

        if (_newYearController.hasCountdownTenSecRequest()) {
            if (!_bellController.isBusy()) {
                _bellController.queueCountdownTenSecAlert();
                _newYearController.resolveCountdownTenSecRequest();
            }
        }

        suppressScheduledStrike = _newYearController.isCelebrating() &&
                                  realTime.hours == 0 && realTime.minutes == 0 &&
                                  realTime.seconds <= 1;
    }

    _bellController.update(time, timeValid, _bellMode,
                           _timerController.isCountdownExpired(),
                           muteAutomatic, suppressScheduledStrike);
}

bool ClockApp::updateNewYearState(ClockTime* timeOut, ClockDate* dateOut) {
    ClockTime time;
    ClockDate date;
    bool valid = _timeProvider.currentTime(time) && _timeProvider.currentDate(date);
    if (valid) {
        _newYearController.update(date, time, _timeProvider.milliseconds());
        if (timeOut) *timeOut = time;
        if (dateOut) *dateOut = date;
    }
    return valid;
}

void ClockApp::syncDisplayModeSelection() {
    if (_overrideState.active &&
        LAST_STYLE_TIMEOUT_MINUTES > 0) {
        unsigned long now = millis();
        if ((int32_t)(now - _overrideState.expiresAt) >= 0 ||
            _savedDisplayMode != _overrideState.sourceMode) {
            _overrideState.active = false;
            _settingsStore.clearTemporaryStyle();
        }
    } else if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        _overrideState.active = false;
        _settingsStore.clearTemporaryStyle();
    }

    DisplayMode baseMode = _savedDisplayMode;
    if (isRandomDisplayMode(_savedDisplayMode)) {
        ClockDate currentDate;
        bool haveDate = _timeProvider.currentDate(currentDate);
        ClockTime currentTime;
        bool haveTime = _timeProvider.currentTime(currentTime);
        uint16_t intervalMin = rndIntervalMinutes(_appSettings.rndInterval);
        if (intervalMin < 1) intervalMin = 1;
        if (intervalMin > 1440) intervalMin = 1440;
        uint8_t currentSlot = haveTime
            ? (uint8_t)((currentTime.hours * 60 + currentTime.minutes) / intervalMin)
            : 0;

        if (!_randomState.valid) {
            _randomState.mode = pickRandomConcreteDisplayMode(DisplayMode::Rnd);
            _randomState.valid = true;
            if (haveDate && haveTime) {
                _randomState.date = currentDate;
                _randomState.dateValid = true;
                _randomState.hourSlot = currentSlot;
            }
        }

        if (haveDate && haveTime) {
            if (!_randomState.dateValid) {
                _randomState.date = currentDate;
                _randomState.dateValid = true;
                _randomState.hourSlot = currentSlot;
            } else if (!sameDate(currentDate, _randomState.date) ||
                       currentSlot != _randomState.hourSlot) {
                _randomState.date = currentDate;
                _randomState.hourSlot = currentSlot;
                _randomState.mode = pickRandomConcreteDisplayMode(_randomState.mode);
            }
        }

        baseMode = _randomState.mode;
    } else {
        _randomState.valid = false;
        _randomState.dateValid = false;
    }

    _displayMode = _overrideState.active ? _overrideState.mode : baseMode;
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
    DisplayMode lookup = _overrideState.active ? _stylePreviewLabel : _displayMode;
    int currentIndex = -1;
    for (uint8_t i = 0; i < QUICK_STYLE_POOL_COUNT; i++) {
        if (QUICK_STYLE_POOL[i] == lookup) {
            currentIndex = (int)i;
            break;
        }
    }
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    int nextIndex = (currentIndex + direction + QUICK_STYLE_POOL_COUNT) % QUICK_STYLE_POOL_COUNT;
    DisplayMode mode = QUICK_STYLE_POOL[nextIndex];
    _stylePreviewLabel = mode;
    if (mode == DisplayMode::Rnd) {
        mode = pickRandomConcreteDisplayMode(DisplayMode::Rnd);
    }
    if (_overrideState.active && mode == _overrideState.mode) {
        return;
    }

    _overrideState.mode = mode;
    _overrideState.sourceMode = _savedDisplayMode;
    _overrideState.active = true;
    _overrideState.expiresAt =
        millis() + (unsigned long)LAST_STYLE_TIMEOUT_MINUTES * 60000UL;
    _displayMode = _overrideState.mode;
    _styleNamePreviewEndMs = millis() + 1500;
    _stylePreviewPrevMode = (DisplayMode)0xFF;
    LOG("Temporary clock style: ");
    LOGLN(displayModeLabel(mode));
    time_t epoch;
    if (_timeProvider.currentEpoch(epoch)) {
        _settingsStore.saveTemporaryStyle(_stylePreviewLabel, epoch);
    } else {
        _settingsStore.saveTemporaryStyle(_stylePreviewLabel, 0);
    }
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

    ClockTime now;
    now.hours = h;
    now.minutes = m;
    now.seconds = s;
    int8_t effective = _nightModeController.tick(now, _display.getUserBrightness());
    effective = _newYearController.boostedBrightness(effective, _display.getUserBrightness());
    bool enabled = !_nightModeController.isDisplaySuppressed() ||
                   _newYearController.shouldWakeDisplay();
    if (enabled != _display.isEnabled()) {
        _display.setEnabled(enabled);
    }
    if (effective != _display.getBrightness()) {
        _display.setBrightness(effective);
    }
}

void ClockApp::refreshPongOnEntry() {
    if (_lastDisplayModeSeen == DisplayMode::Pong) {
        return;
    }

    ClockTime time = _timeProvider.displayTime();
    _display.resetPong(time);
    _lastDisplayModeSeen = DisplayMode::Pong;
    LOG("Pong re-entry reset to fresh serve\n");
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
#if CHRONOMSG_ENABLED
    if (_messageClient.isPreviewVisible()) {
        _messageClient.showPrevUnread();
        return;
    }
#endif
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
#if CHRONOMSG_ENABLED
    if (_messageClient.isPreviewVisible()) {
        _messageClient.showNextUnread();
        return;
    }
#endif
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
#if CHRONOMSG_ENABLED
    if (_messageClient.isPreviewVisible()) {
        _messageClient.hidePreview();
        return;
    } else {
        if (_timerController.isClockView() && _messageClient.hasUnread()) {
            if (_messageClient.showCurrentNow()) {
                return;
            }
        }
    }
#endif
    _timerController.onMiddleShort();
}

void ClockApp::onTouchMiddleLong(uint8_t pad) {
    (void)pad;
#if CHRONOMSG_ENABLED
    if (_messageClient.isPreviewVisible()) {
        LOGLN("T4 1.5s: dismiss message");
        _messageClient.dismissCurrentOrHide();
        return;
    }
#endif

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

#if GUEST_WIFI_ENABLED
void ClockApp::wireGuestWifiCallback(TimerController::GuestWifiAvailableFn fn) {
    _timerController.setGuestWifiAvailableCallback(fn);
}
#endif

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
    if (field == "timer:left") {
        onTouchLeft(0);
        return;
    }
    if (field == "timer:middle") {
        onTouchMiddleShort(0);
        return;
    }
    if (field == "timer:middle-long") {
        onTouchMiddleLong(0);
        return;
    }
    if (field == "timer:right") {
        onTouchRight(0);
        return;
    }
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
#if DIGIT_TRANSITIONS
    } else if (field == "anim") {
        digit_transition::set_transition_mode(_appSettings.transitionMode);
#endif
    } else {
        _timerController.showClockPreview();
    }
}

String ClockApp::timerStatusJson() const {
    ClockTime time;
    ClockDate date;
    bool timeValid = getCurrentClockTime(time.hours, time.minutes, time.seconds);
    bool dateValid = _timeProvider.currentDate(date);
#if GUEST_WIFI_ENABLED
    bool guestAvailable = _guestWifi.isTextAvailable();
    bool showSsid = false;
    if (guestAvailable) {
        unsigned long elapsed = millis() % ((GUEST_WIFI_SSID_SHOW_SECONDS + GUEST_WIFI_PASS_SHOW_SECONDS) * 1000UL);
        showSsid = elapsed < (unsigned long)GUEST_WIFI_SSID_SHOW_SECONDS * 1000UL;
    }
#endif

    String json = "{";
    json += "\"view\":";
    if (_timerController.isStopwatchView()) {
        appendJsonString(json, "stopwatch");
    } else if (_timerController.isCountdownExpired() || _timerController.isCountdownView()) {
        appendJsonString(json, "countdown");
#if GUEST_WIFI_ENABLED
    } else if (_timerController.isGuestWifiView()) {
        appendJsonString(json, "guest");
#endif
    } else if (_timerController.isDateView()) {
        appendJsonString(json, "date");
    } else {
        appendJsonString(json, "clock");
    }

    json += ",\"timeValid\":";
    json += timeValid ? "true" : "false";
    json += ",\"clockHours\":";
    json += time.hours;
    json += ",\"clockMinutes\":";
    json += time.minutes;
    json += ",\"clockSeconds\":";
    json += time.seconds;

    json += ",\"dateValid\":";
    json += dateValid ? "true" : "false";
    json += ",\"dateDay\":";
    json += date.day;
    json += ",\"dateDate\":";
    json += date.date;
    json += ",\"dateMonth\":";
    json += date.month;
    json += ",\"dateYear\":";
    json += date.year;
    json += ",\"dateStyle\":";
    json += (int)_activeDateStyle;

#if GUEST_WIFI_ENABLED
    json += ",\"guestAvailable\":";
    json += guestAvailable ? "true" : "false";
    json += ",\"guestShowSsid\":";
    json += showSsid ? "true" : "false";
    char guestSsid[LOCAL_DISPLAY_TEXT_MAX_LEN];
    char guestPassword[LOCAL_DISPLAY_TEXT_MAX_LEN];
    bool guestTextCopied = guestAvailable &&
        _guestWifi.copyText(guestSsid, sizeof(guestSsid), guestPassword, sizeof(guestPassword));
    json += ",\"guestSsid\":";
    appendJsonString(json, guestTextCopied ? guestSsid : "");
    json += ",\"guestPassword\":";
    appendJsonString(json, guestTextCopied ? guestPassword : "");
#endif

    uint64_t stopwatchMs = _timerController.stopwatchMs();
    uint32_t countdownMs = _timerController.countdownMs();
    uint32_t countdownElapsed = _timerController.countdownElapsedSinceExpiryMs();
    String displaySvg = _display.snapshotSvg();

    json += ",\"stopwatchRunning\":";
    json += _timerController.stopwatchRunning() ? "true" : "false";
    json += ",\"stopwatchMs\":";
    appendJsonUInt64(json, stopwatchMs);

    json += ",\"countdownRunning\":";
    json += _timerController.countdownRunning() ? "true" : "false";
    json += ",\"countdownExpired\":";
    json += _timerController.isCountdownExpired() ? "true" : "false";
    json += ",\"countdownMs\":";
    json += String((unsigned long)countdownMs);
    json += ",\"countdownElapsedSinceExpiryMs\":";
    json += String((unsigned long)countdownElapsed);
    json += ",\"displaySvg\":";
    appendJsonString(json, displaySvg);
    json += "}";
    return json;
}

String ClockApp::handleTimerStatus(void* context) {
    return static_cast<ClockApp*>(context)->timerStatusJson();
}
