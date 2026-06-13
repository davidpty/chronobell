#ifndef CLOCK_APP_H
#define CLOCK_APP_H

#include <Arduino.h>
#include <MD_MAX72xx.h>

#include "AppSettings.h"
#include "BellController.h"
#include "MenuBindings.h"
#include "MenuController.h"
#include "MenuConfig.h"
#include "NightModeController.h"
#include "RtcClock.h"
#include "SettingsStore.h"
#include "TimeProvider.h"
#include "TimerController.h"
#include "TouchController.h"
#include "WiFiManagerLite.h"
#include "WiFiSync.h"
#include "Display.h"
#include "GuestWifiController.h"

// C-ABI function-pointer aliases used by the .ino's trampolines.
using OnTouchFn       = void (*)(uint8_t pad);
using SavePresetFn    = void (*)(uint8_t presetIndex);
using QueueAlertFn    = void (*)(uint8_t groups);
using BellBusyFn      = bool (*)();
using StopBellFn      = void (*)();

class ClockApp {
public:
    ClockApp();

    // -- Boot phases (called once from .ino setup) ---------------------------
    void beginControllers();
    void wireTimerCallbacks(SavePresetFn savePreset,
                            QueueAlertFn queueAlert,
                            BellBusyFn   bellBusy,
                            StopBellFn   stopBell);
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
    void tickGuestWifi();
    void pollLongPress();
    void tickMenu();

    // Public accessor for guest wifi state (used by timer controller callback)
    bool isGuestWifiAvailable() const { return _guestWifi.isTextAvailable(); }

    // -- Callback handlers (called from the .ino's trampolines) -------------
    void onTouchLeft(uint8_t pad);
    void onTouchRight(uint8_t pad);
    void onTouchMiddleShort(uint8_t pad);
    void saveCountdownPreset(uint8_t presetIndex);
    void queueBellAlert(uint8_t groups);
    bool isBellBusy() const;
    void stopBell();
    void configureTouchRepeat(uint8_t pad, OnTouchFn onRepeat, uint32_t initialDelayMs, uint32_t rateMs);
    void wireGuestWifiCallback(TimerController::GuestWifiAvailableFn fn);
    void onTouchLeftRepeat(uint8_t pad);
    void onTouchRightRepeat(uint8_t pad);
    bool onSettingsSaved(bool wifiChanged, bool tzChanged, bool manualTimeChanged);
    void onWebPreview(const String& field);

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
    GuestWifiController _guestWifi;
    Display          _display;
    NightModeController _nightModeController;

    DisplayMode _displayMode       = DisplayMode::LargeDigitsOnly;
    DisplayMode _savedDisplayMode  = DisplayMode::LargeDigitsOnly;
    DisplayMode _overrideDisplayMode = DisplayMode::LargeDigitsOnly;
    DisplayMode _displayOverrideSourceMode = DisplayMode::LargeDigitsOnly;
    DisplayMode _randomDisplayMode = DisplayMode::LargeDigitsOnly;
    ClockDate   _randomDisplayDate;
    BellMode    _bellMode          = BellMode::Off;
    TimeFormat  _timeFormat        = TimeFormat::Hours24;
    NightMode   _nightMode         = NightMode::Off;
    DateStyle   _activeDateStyle   = DateStyle::Date;
    DateStyle   _temporaryDateStyle = DateStyle::Date;
    bool        _displayOverrideActive = false;
    unsigned long _displayOverrideExpiresAt = 0;
    bool        _dateStyleOverrideActive = false;
    unsigned long _dateStyleOverrideExpiresAt = 0;
    bool        _randomDisplayModeValid = false;
    bool        _randomDisplayDateValid = false;

    MenuBindings     _menuBindings;

    unsigned long _buttonPressStart = 0;
    bool          _buttonWasPressed = false;
    bool          _t4LongPressHandled = false;

    // --- Private helpers ---
    void applyEffectiveDisplayBrightness();
    void updateBellSchedule();
    void syncDisplayModeSelection();
    void syncDateStyleSelection();
    void cycleTemporaryDisplayMode(int direction);
    void cycleTemporaryDateStyle(int direction);
    DisplayMode pickRandomConcreteDisplayMode(DisplayMode avoid) const;
    bool getCurrentClockTime(int& h, int& m, int& s) const;
    void onTouchMenuPrev(uint8_t pad);
    void onTouchMenuNext(uint8_t pad);
    void onTouchMenuOk(uint8_t pad);
};

#endif // CLOCK_APP_H
