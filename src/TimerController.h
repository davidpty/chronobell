#ifndef TIMER_CONTROLLER_H
#define TIMER_CONTROLLER_H

#include <Arduino.h>
#include <time.h>

enum class TimerView : uint8_t {
    Clock = 0,
    Date = 1,
    GuestWifi = 2,
    Stopwatch = 3,
    Countdown = 4
};

enum class TimerLongPressAction : uint8_t {
    None = 0,
    AcknowledgedAlert = 1,
    ExitTimerToClock = 2
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

    void begin(const uint16_t* presetMinutes, uint8_t presetCount, uint8_t presetIndex);
    void setCallbacks(SavePresetCallback savePreset,
                      QueueAlertCallback queueAlert,
                      BellBusyCallback bellBusy,
                      StopBellCallback stopBell);
    void setPersistenceCallbacks(CurrentEpochCallback currentEpoch,
                                 SaveTargetEpochCallback saveTargetEpoch,
                                 ClearTargetEpochCallback clearTargetEpoch,
                                 SaveViewActiveCallback saveViewActive);
    void restoreCountdown(time_t targetEpoch, bool countdownViewActive);
    void update();
    void noteActivity();
    void updateAlert();
    void onLeft();
    void onRight();
    void onMiddleShort();
    TimerLongPressAction onLongPress();
    void acknowledgeAlert();

    typedef bool (*GuestWifiAvailableFn)();

    bool isCountdownExpired() const;
    bool isClockView() const;
    bool isDateView() const;
    bool isGuestWifiView() const;
    bool isStopwatchView() const;
    bool isCountdownView() const;

    void showDateView();
    void dismissView();
    void setGuestWifiAvailableCallback(GuestWifiAvailableFn fn);
    bool stopwatchRunning() const;
    bool countdownRunning() const;
    uint32_t stopwatchMs() const;
    uint32_t countdownMs() const;

private:
    uint32_t countdownPresetMs() const;
    bool countdownAtFullPreset() const;
    bool currentEpoch(time_t& epoch) const;
    void expireCountdown(uint32_t now);
    bool startCountdownFromRemaining();
    void pauseCountdown();
    void clearPersistedTargetEpoch();
    void saveCountdownViewActive(bool active);
    void setView(TimerView view, bool persist = true);
    void setLastNonClockView(TimerView view);

    const uint16_t* _presetMinutes = nullptr;
    uint8_t _presetCount = 0;
    uint8_t _presetIndex = 0;

    TimerView _countdownAlertReturnView = TimerView::Clock;

    bool _stopwatchRunning = false;
    uint32_t _stopwatchElapsedMs = 0;
    uint32_t _stopwatchStartedMs = 0;

    bool _countdownRunning = false;
    bool _countdownExpired = false;
    uint32_t _countdownRemainingMs = 60UL * 1000UL;
    uint32_t _countdownStartedMs = 0;
    time_t _countdownTargetEpoch = 0;
    uint32_t _countdownAlertStartedMs = 0;
    uint32_t _countdownLastAlertMs = 0;
    bool _alertBellStopped = false;
    uint32_t _alertBellStoppedMs = 0;

    TimerView _view = TimerView::Clock;
    TimerView _lastNonClockView = TimerView::Date;
    uint32_t _viewActivityMs = 0;

    SavePresetCallback _savePreset = nullptr;
    QueueAlertCallback _queueAlert = nullptr;
    BellBusyCallback _bellBusy = nullptr;
    StopBellCallback _stopBell = nullptr;
    GuestWifiAvailableFn _guestWifiAvailable = nullptr;
    CurrentEpochCallback _currentEpoch = nullptr;
    SaveTargetEpochCallback _saveTargetEpoch = nullptr;
    ClearTargetEpochCallback _clearTargetEpoch = nullptr;
    SaveViewActiveCallback _saveViewActive = nullptr;
    bool _persistedCountdownViewActive = false;
};

#endif
