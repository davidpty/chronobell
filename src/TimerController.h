#ifndef TIMER_CONTROLLER_H
#define TIMER_CONTROLLER_H

#include <Arduino.h>
#include <time.h>
#include "Config.h"

enum class TimerView : uint8_t {
    Clock = 0,
    Date = 1,
#if GUEST_WIFI_ENABLED
    GuestWifi = 2,
    Stopwatch = 3,
    Countdown = 4
#endif
#if !GUEST_WIFI_ENABLED
    Stopwatch = 2,
    Countdown = 3
#endif
};

class TimerController {
public:
    typedef void (*SavePresetCallback)(uint8_t presetIndex);
    typedef void (*QueueAlertCallback)(uint8_t groups);
    typedef bool (*BellBusyCallback)();
    typedef void (*StopBellCallback)();
    typedef bool (*CurrentEpochCallback)(time_t& epoch);
    typedef bool (*SaveTargetEpochCallback)(time_t targetEpoch);
    typedef bool (*ClearTargetEpochCallback)();
    typedef bool (*SaveViewActiveCallback)(bool active);
    typedef bool (*SaveUInt32Callback)(uint32_t value);
    typedef bool (*SaveUInt64Callback)(uint64_t value);
    typedef bool (*ClearCallback)();
    typedef bool (*SaveTimeCallback)(time_t value);

    void begin(const uint16_t* presetMinutes, uint8_t presetCount, uint8_t presetIndex);
    void setCallbacks(SavePresetCallback savePreset,
                      QueueAlertCallback queueAlert,
                      BellBusyCallback bellBusy,
                      StopBellCallback stopBell);
    void setPersistenceCallbacks(CurrentEpochCallback currentEpoch,
                                 SaveTargetEpochCallback saveTargetEpoch,
                                 ClearTargetEpochCallback clearTargetEpoch,
                                 SaveViewActiveCallback saveViewActive,
                                 SaveUInt32Callback saveRemaining = nullptr,
                                 ClearCallback clearRemaining = nullptr);
    void setStopwatchPersistenceCallbacks(SaveUInt64Callback saveElapsed,
                                          ClearCallback clearElapsed,
                                          SaveTimeCallback saveStartEpoch,
                                          ClearCallback clearStartEpoch,
                                          SaveViewActiveCallback saveViewActive);
    void restoreCountdown(time_t targetEpoch, bool countdownViewActive, uint32_t remainingMs = 0);
    void restoreStopwatch(uint64_t elapsedMs, time_t startEpoch, bool stopwatchViewActive);
    void update();
    void noteActivity();
    void updateAlert();
    void onLeft();
    void onRight();
    void onMiddleShort();
    void acknowledgeAlert(bool forceCountdownView);

    bool isCountdownExpired() const;
    bool isClockView() const;
    bool isDateView() const;
#if GUEST_WIFI_ENABLED
    typedef bool (*GuestWifiAvailableFn)();
    bool isGuestWifiView() const;
#endif
    bool isStopwatchView() const;
    bool isCountdownView() const;

    void showDateView();
    void showClockPreview();
    void dismissView();
#if GUEST_WIFI_ENABLED
    void setGuestWifiAvailableCallback(GuestWifiAvailableFn fn);
#endif
    bool stopwatchRunning() const;
    bool countdownRunning() const;
    uint64_t stopwatchMs() const;
    uint32_t countdownMs() const;
    uint32_t countdownElapsedSinceExpiryMs() const;

private:
    uint32_t countdownPresetMs() const;
    bool countdownAtFullPreset() const;
    bool currentEpoch(time_t& epoch) const;
    void expireCountdown(uint32_t now);
    bool startCountdownFromRemaining();
    void pauseCountdown();
    void clearPersistedTargetEpoch();
    void saveCountdownViewActive(bool active);
    void saveStopwatchViewActive(bool active);
    void beginPreview();
    void setView(TimerView view, bool persist = true);
    void setLastNonClockView(TimerView view);

    const uint16_t* _presetMinutes = nullptr;
    uint8_t _presetCount = 0;
    uint8_t _presetIndex = 0;

    TimerView _countdownAlertReturnView = TimerView::Clock;

    bool _stopwatchRunning = false;
    uint64_t _stopwatchElapsedMs = 0;
    uint32_t _stopwatchStartedMs = 0;
    time_t _stopwatchStartEpoch = 0;

    bool _countdownRunning = false;
    bool _countdownExpired = false;
    uint32_t _countdownRemainingMs = 60UL * 1000UL;
    uint32_t _countdownStartedMs = 0;
    time_t _countdownTargetEpoch = 0;
    uint32_t _countdownAlertStartedMs = 0;
    uint32_t _countdownLastAlertMs = 0;
    bool _alertBellStopped = false;
    uint32_t _alertBellStoppedMs = 0;
    bool _countdownAlertBellWasBusy = false;

    TimerView _view = TimerView::Clock;
    TimerView _lastNonClockView = TimerView::Date;
    TimerView _viewBeforePreview = TimerView::Clock;
    uint32_t _previewUntilMs = 0;
    uint32_t _viewActivityMs = 0;

    SavePresetCallback _savePreset = nullptr;
    QueueAlertCallback _queueAlert = nullptr;
    BellBusyCallback _bellBusy = nullptr;
    StopBellCallback _stopBell = nullptr;
#if GUEST_WIFI_ENABLED
    GuestWifiAvailableFn _guestWifiAvailable = nullptr;
#endif
    CurrentEpochCallback _currentEpoch = nullptr;
    SaveTargetEpochCallback _saveTargetEpoch = nullptr;
    ClearTargetEpochCallback _clearTargetEpoch = nullptr;
    SaveUInt32Callback _saveCountdownRemaining = nullptr;
    ClearCallback _clearCountdownRemaining = nullptr;
    SaveViewActiveCallback _saveViewActive = nullptr;
    bool _persistedCountdownViewActive = false;

    SaveUInt64Callback _saveStopwatchElapsed = nullptr;
    ClearCallback _clearStopwatchElapsed = nullptr;
    SaveTimeCallback _saveStopwatchStartEpoch = nullptr;
    ClearCallback _clearStopwatchStartEpoch = nullptr;
    SaveViewActiveCallback _saveStopwatchViewActive = nullptr;
    bool _persistedStopwatchViewActive = false;
};

#endif
