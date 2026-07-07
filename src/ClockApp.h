#ifndef CLOCK_APP_H
#define CLOCK_APP_H

#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <time.h>

#include "Config.h"
#include "AppSettings.h"
#include "BellController.h"
#include "DriftTimeModel.h"
#include "MenuBindings.h"
#include "MenuController.h"
#include "MenuConfig.h"
#include "NightModeController.h"
#include "NewYearController.h"
#include "RtcClock.h"
#include "SettingsStore.h"
#include "TimeProvider.h"
#include "TimerController.h"
#include "TouchController.h"
#include "WiFiManagerLite.h"
#include "WiFiSync.h"
#include "Display.h"
#if GUEST_WIFI_ENABLED
#include "GuestWifiController.h"
#endif
#if CHRONOSERVE_ENABLED
#include "MessageClient.h"
#endif

// C-ABI function-pointer aliases used by the .ino's trampolines.
using OnTouchFn       = void (*)(uint8_t pad);
using SavePresetFn    = void (*)(uint8_t presetIndex);
using QueueAlertFn    = void (*)(uint8_t groups);
using BellBusyFn      = bool (*)();
using StopBellFn      = void (*)();
using CurrentEpochFn  = bool (*)(time_t& epoch);
using SaveTargetEpochFn = bool (*)(time_t targetEpoch);
using ClearTargetEpochFn = bool (*)();
using SaveViewActiveFn = bool (*)(bool active);
using SaveUInt32Fn = bool (*)(uint32_t value);
using SaveUInt64Fn = bool (*)(uint64_t value);
using ClearFn = bool (*)();
using SaveTimeFn = bool (*)(time_t value);

class ClockApp {
public:
    ClockApp();

    // -- Boot phases (called once from .ino setup) ---------------------------
    void beginControllers();
    void wireTimerCallbacks(SavePresetFn savePreset,
                            QueueAlertFn queueAlert,
                            BellBusyFn   bellBusy,
                            StopBellFn   stopBell);
    void wireTimerPersistenceCallbacks(CurrentEpochFn currentEpoch,
                                       SaveTargetEpochFn saveTargetEpoch,
                                       ClearTargetEpochFn clearTargetEpoch,
                                       SaveViewActiveFn saveViewActive,
                                       SaveUInt32Fn saveRemaining = nullptr,
                                       ClearFn clearRemaining = nullptr);
    void wireStopwatchPersistenceCallbacks(SaveUInt64Fn saveElapsed,
                                           ClearFn clearElapsed,
                                           SaveTimeFn saveStartEpoch,
                                           ClearFn clearStartEpoch,
                                           SaveViewActiveFn saveViewActive);
    void installTouchHandlers(OnTouchFn onPad1Press,
                              OnTouchFn onPad8Press,
                              OnTouchFn onPad4Release);

    void initSerialAndPins();
    void initDisplay();
    void initI2cAndRtc();
    void initCap1188();

    void loadSettings();
    void loadTimerSettings();
    void applyDisplayBrightness();
    void runDisplayTest();

    void wifiBootSync();
    void reloadSettings();
    void applyManualTime();
    void render();

    // -- Per-tick services (called from .ino loop) --------------------------
    void pollBootButton();
    void pollTouch();
    void tickWifiManager();
    void tickWifiSync();
    void tickRtc();
    void tickTimer();
    void tickBell();
#if GUEST_WIFI_ENABLED
    void tickGuestWifi();
#endif
#if CHRONOSERVE_ENABLED
    void tickMessages();
#endif
    void tickAlarm();
    void fireAlarmBell();
    void pollLongPress();
    void tickMenu();

#if GUEST_WIFI_ENABLED
    // Public accessor for guest wifi state (used by timer controller callback)
    bool isGuestWifiAvailable() const { return _guestWifi.isTextAvailable(); }
#endif

    // -- Callback handlers (called from the .ino's trampolines) -------------
    void onTouchLeft(uint8_t pad);
    void onTouchLeftHold(uint8_t pad);
    void onTouchRight(uint8_t pad);
    void onTouchRightExtraHold(uint8_t pad);
    void onTouchMiddleShort(uint8_t pad);
    void onTouchMiddleLong(uint8_t pad);
    void saveCountdownPreset(uint8_t presetIndex);
    bool currentEpoch(time_t& epoch) const;
    bool saveCountdownTargetEpoch(time_t targetEpoch);
    bool clearCountdownTargetEpoch();
    bool saveCountdownViewActive(bool active);
    bool saveCountdownRemainingMs(uint32_t remainingMs);
    bool clearCountdownRemainingMs();
    bool saveStopwatchElapsed(uint64_t elapsedMs);
    bool clearStopwatchElapsed();
    bool saveStopwatchStartEpoch(time_t epoch);
    bool clearStopwatchStartEpoch();
    bool saveStopwatchViewActive(bool active);
    void queueBellAlert(uint8_t groups);
    bool isBellBusy() const;
    void stopBell();
    void configureTouchRepeat(uint8_t pad, OnTouchFn onRepeat, uint32_t initialDelayMs, uint32_t rateMs);
    void configureTouchHold(uint8_t pad, OnTouchFn onHold, uint32_t holdMs);
#if GUEST_WIFI_ENABLED
    void wireGuestWifiCallback(TimerController::GuestWifiAvailableFn fn);
#endif
    void onTouchLeftRepeat(uint8_t pad);
    void onTouchRightRepeat(uint8_t pad);
    bool onSettingsSaved(bool wifiChanged, bool tzChanged, bool manualTimeChanged, const String& wifiSsid, const String& wifiPassword);
    void onWebPreview(const String& field);
    String timerStatusJson() const;

private:
    // --- Owned controllers. Declaration order matters: dependent members
    //     below are constructed in this order, so their init-list can
    //     reference earlier members safely. ---
    SettingsStore    _settingsStore;
    AppSettings      _appSettings;
    RtcClock         _rtcClock;
    TimeProvider     _timeProvider;
    BellController   _bellController;
    MenuController   _menuController;
    TimerController  _timerController;
    TouchController  _touchController;
    MD_MAX72XX       _leds;
    WiFiManagerLite  _wifiManager;
    WiFiSync         _wifiSync;
#if GUEST_WIFI_ENABLED
    GuestWifiController _guestWifi;
#endif
#if CHRONOSERVE_ENABLED
    MessageClient   _messageClient;
    bool            _msgBellFired = false;
#endif
    DriftTimeModel  _driftTimeModel;
    NewYearController _newYearController;
    Display          _display;
    NightModeController _nightModeController;

    struct OverrideState {
        bool active = false;
        unsigned long expiresAt = 0;
        DisplayMode mode = DisplayMode::LargeDigitsOnly;
        DisplayMode sourceMode = DisplayMode::LargeDigitsOnly;
    };

    struct RandomModeState {
        bool valid = false;
        bool dateValid = false;
        DisplayMode mode = DisplayMode::LargeDigitsOnly;
        ClockDate date;
        uint8_t hourSlot = 0;
    };

    uint8_t _alarmMode = 0;
    uint8_t _alarmHour = 7;
    uint8_t _alarmMin  = 0;
    int _lastAlarmCheckMin = -1;
    unsigned long _alarmNextRingMs = 0;
    uint8_t _alarmRingCount = 0;
    bool _alarmRinging = false;

    DisplayMode     _displayMode        = DisplayMode::LargeDigitsOnly;
    DisplayMode      _savedDisplayMode   = DisplayMode::LargeDigitsOnly;
    DisplayMode      _lastDisplayModeSeen = DisplayMode::LargeDigitsOnly;
    OverrideState    _overrideState;
    RandomModeState  _randomState;

    unsigned long _styleNamePreviewEndMs = 0;
    DisplayMode  _stylePreviewPrevMode   = (DisplayMode)0xFF;
    DisplayMode  _stylePreviewLabel      = DisplayMode::LargeDigitsOnly;

    BellMode    _bellMode          = BellMode::Off;
    TimeFormat  _timeFormat        = TimeFormat::Hours24;
    NightMode   _nightMode         = NightMode::Off;
    DateStyle   _activeDateStyle   = DateStyle::Date;
    DateStyle   _temporaryDateStyle = DateStyle::Date;
    bool        _dateStyleOverrideActive = false;
    unsigned long _dateStyleOverrideExpiresAt = 0;

    MenuBindings     _menuBindings;

    unsigned long _buttonPressStart = 0;
    bool          _buttonWasPressed = false;
    bool          _t4LongPressHandled = false;

    uint8_t _lastRecoveryBucket = 0xFF;

    // --- Private helpers ---
    void syncRuntimeSettingsFromLoaded(bool forceDateStyleReset);
    void logLoadedSettings() const;
    void restoreTemporaryStyleOverride();
    void applyEffectiveDisplayBrightness();
    bool updateNewYearState(ClockTime* time = nullptr, ClockDate* date = nullptr);
    void updateBellSchedule();
    void syncDisplayModeSelection();
    void syncDateStyleSelection();
    void cycleTemporaryDisplayMode(int direction);
    void cycleTemporaryDateStyle(int direction);
    void refreshPongOnEntry();
    void checkDisplayRecovery();
    DisplayMode pickRandomConcreteDisplayMode(DisplayMode avoid) const;
    bool getCurrentClockTime(int& h, int& m, int& s) const;
    void onTouchMenuPrev(uint8_t pad);
    void onTouchMenuNext(uint8_t pad);
    void onTouchMenuOk(uint8_t pad);
    static void handleOtaDisplay(void* context, bool active, unsigned int progress, unsigned int total);
    static bool handleSettingsSaved(void* context, bool wifiChanged, bool tzChanged, bool manualTimeChanged,
                                    const String& wifiSsid, const String& wifiPassword);
    static void handleReconnectResult(void* context, bool success);
    static void handleWebPreview(void* context, const String& field);
    static String handleTimerStatus(void* context);
};

#endif // CLOCK_APP_H
