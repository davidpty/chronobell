#include "TimerController.h"

#include "Config.h"

void TimerController::begin(const uint16_t* presetMinutes, uint8_t presetCount, uint8_t presetIndex) {
    _presetMinutes = presetMinutes;
    _presetCount = presetCount;
    _presetIndex = (presetCount > 0 && presetIndex < presetCount) ? presetIndex : 0;
    _countdownRemainingMs = countdownPresetMs();
    _view = TimerView::Clock;
    _countdownAlertReturnView = TimerView::Clock;
    _viewActivityMs = millis();
    _countdownTargetEpoch = 0;
    _persistedCountdownViewActive = false;
}

void TimerController::setCallbacks(SavePresetCallback savePreset,
                                   QueueAlertCallback queueAlert,
                                   BellBusyCallback bellBusy,
                                   StopBellCallback stopBell) {
    _savePreset = savePreset;
    _queueAlert = queueAlert;
    _bellBusy = bellBusy;
    _stopBell = stopBell;
}

void TimerController::setPersistenceCallbacks(CurrentEpochCallback currentEpoch,
                                              SaveTargetEpochCallback saveTargetEpoch,
                                              ClearTargetEpochCallback clearTargetEpoch,
                                              SaveViewActiveCallback saveViewActive) {
    _currentEpoch = currentEpoch;
    _saveTargetEpoch = saveTargetEpoch;
    _clearTargetEpoch = clearTargetEpoch;
    _saveViewActive = saveViewActive;
}

void TimerController::restoreCountdown(time_t targetEpoch, bool countdownViewActive) {
    uint32_t now = millis();
    _persistedCountdownViewActive = countdownViewActive;
    _view = countdownViewActive ? TimerView::Countdown : TimerView::Clock;
    _lastNonClockView = countdownViewActive ? TimerView::Countdown : TimerView::Date;
    _viewActivityMs = now;

    if (targetEpoch <= 0) {
        _countdownRunning = false;
        _countdownTargetEpoch = 0;
        _countdownRemainingMs = countdownPresetMs();
        return;
    }

    time_t epoch = 0;
    if (!currentEpoch(epoch)) {
        _countdownRunning = false;
        _countdownTargetEpoch = 0;
        _countdownRemainingMs = countdownPresetMs();
        LOGLN("Countdown restore: no RTC epoch");
        return;
    }

    if (targetEpoch <= epoch) {
        _countdownRemainingMs = 0;
        _countdownTargetEpoch = 0;
        _countdownRunning = false;
        _countdownAlertReturnView = countdownViewActive ? TimerView::Countdown : TimerView::Clock;
        _countdownExpired = true;
        _countdownAlertStartedMs = now;
        _countdownLastAlertMs = 0;
        clearPersistedTargetEpoch();
        LOGLN("Countdown restored expired");
        return;
    }

    unsigned long remainingSeconds = (unsigned long)(targetEpoch - epoch);
    _countdownRemainingMs = remainingSeconds * 1000UL;
    _countdownStartedMs = now;
    _countdownTargetEpoch = targetEpoch;
    _countdownRunning = true;
    _countdownExpired = false;
    LOGLN("Countdown restored running");
}

void TimerController::showDateView() {
    setView(TimerView::Date);
    _viewActivityMs = millis();
    LOGLN("Date view shown");
}

void TimerController::dismissView() {
    if (_view != TimerView::Clock) {
        setView(TimerView::Clock);
        LOGLN("View dismissed to clock");
    }
}

void TimerController::update() {
    uint32_t now = millis();

    if (_countdownRunning) {
        time_t epoch = 0;
        if (_countdownTargetEpoch > 0 && currentEpoch(epoch)) {
            if (epoch >= _countdownTargetEpoch) {
                expireCountdown(now);
            } else {
                unsigned long remainingSeconds = (unsigned long)(_countdownTargetEpoch - epoch);
                _countdownRemainingMs = remainingSeconds * 1000UL;
                _countdownStartedMs = now;
            }
        } else {
            uint32_t elapsed = now - _countdownStartedMs;
            if (elapsed >= _countdownRemainingMs) {
                expireCountdown(now);
            }
        }
    }

    updateAlert();

    if (_countdownExpired) {
        return;
    }

    if (_view == TimerView::Date) {
        if (now - _viewActivityMs >= MENU_TIMEOUT_SHORT_SECONDS * 1000UL) {
            setView(TimerView::Clock);
            LOGLN("View timeout: date -> clock");
        }
        return;
    }

    if (_view == TimerView::GuestWifi) {
#if GUEST_WIFI_VIEW_TIMEOUT_SECONDS * 1000UL > 0
        if (now - _viewActivityMs >= GUEST_WIFI_VIEW_TIMEOUT_SECONDS * 1000UL) {
            setView(TimerView::Clock);
            LOGLN("View timeout: guest wifi -> clock");
        }
#endif
        return;
    }

    if (_view == TimerView::Stopwatch && !_stopwatchRunning &&
        now - _viewActivityMs >= MENU_TIMEOUT_SHORT_SECONDS * 1000UL) {
        setView(TimerView::Clock);
        LOGLN("View timeout: stopwatch -> clock");
        return;
    }

    if (_view == TimerView::Countdown && !_countdownRunning &&
        now - _viewActivityMs >= MENU_TIMEOUT_SHORT_SECONDS * 1000UL) {
        setView(TimerView::Clock);
        LOGLN("View timeout: countdown -> clock");
        return;
    }

#if LAST_DATEVIEW_TIMEOUT_MINUTES > 0
    if (_lastNonClockView != TimerView::Date &&
        now - _viewActivityMs >= ((uint32_t)LAST_DATEVIEW_TIMEOUT_MINUTES * 60 * 1000)) {
        setLastNonClockView(TimerView::Date);
        LOGLN("Last view timeout: reset to Date");
    }
#endif
}

void TimerController::setGuestWifiAvailableCallback(GuestWifiAvailableFn fn) {
    _guestWifiAvailable = fn;
}

void TimerController::noteActivity() {
    _viewActivityMs = millis();
}

void TimerController::updateAlert() {
    if (!_countdownExpired) {
        return;
    }

    uint32_t now = millis();

#if defined(TIMER_ALERT_DURATION_MINUTES) && TIMER_ALERT_DURATION_MINUTES > 0
    if (!_alertBellStopped && now - _countdownAlertStartedMs >= TIMER_ALERT_DURATION_MINUTES * 60000UL) {
        if (_stopBell) _stopBell();
        _alertBellStopped = true;
        _alertBellStoppedMs = now;
        LOGLN("Countdown alert bell silenced");
        return;
    }
#endif

#if defined(TIMER_ALERT_SHOW_TIMEOUT_MINUTES) && TIMER_ALERT_SHOW_TIMEOUT_MINUTES > 0
    if (_alertBellStopped && now - _alertBellStoppedMs >= TIMER_ALERT_SHOW_TIMEOUT_MINUTES * 60000UL) {
        acknowledgeAlert();
        LOGLN("Countdown 00:00 timed out");
        return;
    }
#endif

    if (!_alertBellStopped) {
        if (_countdownLastAlertMs == 0 || now - _countdownLastAlertMs >= TIMER_ALERT_REPEAT_SECONDS * 1000UL) {
            bool busy = _bellBusy && _bellBusy();
            if (!busy && _queueAlert) {
                _queueAlert(3);
                _countdownLastAlertMs = now;
            }
        }
    }
}

void TimerController::onLeft() {
    if (_countdownExpired) {
        return;
    }

    if (_view == TimerView::Stopwatch) {
        if (_stopwatchRunning) {
            _stopwatchElapsedMs += millis() - _stopwatchStartedMs;
            _stopwatchRunning = false;
            LOGLN("Stopwatch paused");
        } else {
            if (_countdownRunning) {
                pauseCountdown();
            }
            _stopwatchStartedMs = millis();
            _stopwatchRunning = true;
            LOGLN("Stopwatch started");
        }
        return;
    }

    if (_view == TimerView::Countdown) {
        if (_countdownRunning) {
            pauseCountdown();
        } else if (_countdownRemainingMs > 0) {
            if (_stopwatchRunning) {
                _stopwatchElapsedMs += millis() - _stopwatchStartedMs;
                _stopwatchRunning = false;
                LOGLN("Stopwatch paused");
            }
            startCountdownFromRemaining();
        }
    }
}

void TimerController::onRight() {
    if (_countdownExpired) {
        return;
    }

    if (_view == TimerView::Stopwatch) {
        if (!_stopwatchRunning) {
            _stopwatchElapsedMs = 0;
            LOGLN("Stopwatch reset");
        }
        return;
    }

    if (_view == TimerView::Countdown) {
        if (_countdownRunning || _presetCount == 0) {
            return;
        }

        if (!countdownAtFullPreset()) {
            _countdownRemainingMs = countdownPresetMs();
            _countdownTargetEpoch = 0;
            clearPersistedTargetEpoch();
            LOGLN("Countdown reset");
            return;
        }

        _presetIndex = (_presetIndex + 1) % _presetCount;
        _countdownRemainingMs = countdownPresetMs();
        _countdownTargetEpoch = 0;
        clearPersistedTargetEpoch();
        if (_savePreset) _savePreset(_presetIndex);
        LOGF("Countdown preset: %u min\n",
                      (unsigned)_presetMinutes[_presetIndex]);
    }
}

void TimerController::onMiddleShort() {
    if (_countdownExpired) {
        acknowledgeAlert();
        return;
    }

    bool guestAvailable = _guestWifiAvailable && _guestWifiAvailable();
    TimerView nextView;

    if (_view == TimerView::Clock) {
        nextView = _lastNonClockView;
        if (nextView == TimerView::GuestWifi && !guestAvailable) {
            nextView = TimerView::Date;
        }
    } else {
        switch (_view) {
            case TimerView::Date:
                nextView = guestAvailable ? TimerView::GuestWifi : TimerView::Stopwatch;
                break;
            case TimerView::GuestWifi:
                nextView = TimerView::Stopwatch;
                break;
            case TimerView::Stopwatch:
                nextView = TimerView::Countdown;
                break;
            case TimerView::Countdown:
                nextView = TimerView::Clock;
                setLastNonClockView(TimerView::Date);
                break;
            default:
                nextView = TimerView::Clock;
                break;
        }
    }

    if (nextView != TimerView::Clock) {
        setLastNonClockView(nextView);
    }
    setView(nextView);
    noteActivity();
    LOGF("View: %u\n", (unsigned)_view);
}

TimerLongPressAction TimerController::onLongPress() {
    if (_countdownExpired) {
        acknowledgeAlert();
        return TimerLongPressAction::AcknowledgedAlert;
    }
    if (_view != TimerView::Clock) {
        setView(TimerView::Clock);
        noteActivity();
        LOGLN("View: clock");
        return TimerLongPressAction::ExitTimerToClock;
    }
    return TimerLongPressAction::None;
}

void TimerController::acknowledgeAlert() {
    _countdownExpired = false;
    _countdownRunning = false;
    _countdownRemainingMs = countdownPresetMs();
    _countdownTargetEpoch = 0;
    _countdownAlertStartedMs = 0;
    _countdownLastAlertMs = 0;
    _alertBellStopped = false;
    _alertBellStoppedMs = 0;
    clearPersistedTargetEpoch();
    if (_stopBell) _stopBell();
    setView(_countdownAlertReturnView);
    noteActivity();
    LOGLN("Countdown alert acknowledged");
}

bool TimerController::isCountdownExpired() const {
    return _countdownExpired;
}

bool TimerController::isClockView() const {
    return _view == TimerView::Clock;
}

bool TimerController::isDateView() const {
    return _view == TimerView::Date;
}

bool TimerController::isGuestWifiView() const {
    return _view == TimerView::GuestWifi;
}

bool TimerController::isStopwatchView() const {
    return _view == TimerView::Stopwatch;
}

bool TimerController::isCountdownView() const {
    return _view == TimerView::Countdown;
}

bool TimerController::stopwatchRunning() const {
    return _stopwatchRunning;
}

bool TimerController::countdownRunning() const {
    return _countdownRunning;
}

uint32_t TimerController::stopwatchMs() const {
    if (!_stopwatchRunning) {
        return _stopwatchElapsedMs;
    }
    return _stopwatchElapsedMs + (millis() - _stopwatchStartedMs);
}

uint32_t TimerController::countdownMs() const {
    if (!_countdownRunning) {
        return _countdownRemainingMs;
    }
    time_t epoch = 0;
    if (_countdownTargetEpoch > 0 && currentEpoch(epoch)) {
        if (epoch >= _countdownTargetEpoch) {
            return 0;
        }
        return (uint32_t)(_countdownTargetEpoch - epoch) * 1000UL;
    }
    uint32_t elapsed = millis() - _countdownStartedMs;
    if (elapsed >= _countdownRemainingMs) {
        return 0;
    }
    return _countdownRemainingMs - elapsed;
}

uint32_t TimerController::countdownPresetMs() const {
    if (!_presetMinutes || _presetCount == 0) {
        return 60UL * 1000UL;
    }
    return (uint32_t)_presetMinutes[_presetIndex] * 60UL * 1000UL;
}

bool TimerController::countdownAtFullPreset() const {
    return _countdownRemainingMs >= countdownPresetMs();
}

bool TimerController::currentEpoch(time_t& epoch) const {
    return _currentEpoch && _currentEpoch(epoch);
}

void TimerController::expireCountdown(uint32_t now) {
    _countdownRunning = false;
    _countdownRemainingMs = 0;
    _countdownTargetEpoch = 0;
    _countdownExpired = true;
    _countdownAlertReturnView = (_view == TimerView::Countdown)
        ? TimerView::Countdown
        : TimerView::Clock;
    _countdownAlertStartedMs = now;
    _countdownLastAlertMs = 0;
    clearPersistedTargetEpoch();
    LOGLN("Countdown expired");
}

bool TimerController::startCountdownFromRemaining() {
    _countdownStartedMs = millis();
    _countdownTargetEpoch = 0;

    time_t epoch = 0;
    if (currentEpoch(epoch)) {
        uint32_t remainingSeconds = (_countdownRemainingMs + 999UL) / 1000UL;
        _countdownTargetEpoch = epoch + (time_t)remainingSeconds;
        if (_saveTargetEpoch) {
            _saveTargetEpoch(_countdownTargetEpoch);
        }
    } else {
        clearPersistedTargetEpoch();
        LOGLN("Countdown started without RTC epoch");
    }

    _countdownRunning = true;
    LOGLN("Countdown started");
    return true;
}

void TimerController::pauseCountdown() {
    _countdownRemainingMs = countdownMs();
    _countdownRunning = false;
    _countdownTargetEpoch = 0;
    clearPersistedTargetEpoch();
    LOGLN("Countdown paused");
}

void TimerController::clearPersistedTargetEpoch() {
    if (_clearTargetEpoch) {
        _clearTargetEpoch();
    }
}

void TimerController::saveCountdownViewActive(bool active) {
    if (_persistedCountdownViewActive == active) {
        return;
    }
    _persistedCountdownViewActive = active;
    if (_saveViewActive) {
        _saveViewActive(active);
    }
}

void TimerController::setView(TimerView view, bool persist) {
    _view = view;
    if (persist) {
        saveCountdownViewActive(view == TimerView::Countdown);
    }
}

void TimerController::setLastNonClockView(TimerView view) {
    _lastNonClockView = view;
}
