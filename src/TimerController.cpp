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
    _countdownAlertBellWasBusy = false;

    _stopwatchRunning = false;
    _stopwatchElapsedMs = 0;
    _stopwatchStartedMs = 0;
    _stopwatchStartEpoch = 0;
    _persistedStopwatchViewActive = false;
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
                                               SaveViewActiveCallback saveViewActive,
                                               SaveUInt32Callback saveRemaining,
                                               ClearCallback clearRemaining) {
    _currentEpoch = currentEpoch;
    _saveTargetEpoch = saveTargetEpoch;
    _clearTargetEpoch = clearTargetEpoch;
    _saveViewActive = saveViewActive;
    _saveCountdownRemaining = saveRemaining;
    _clearCountdownRemaining = clearRemaining;
}

void TimerController::setStopwatchPersistenceCallbacks(SaveUInt64Callback saveElapsed,
                                                       ClearCallback clearElapsed,
                                                       SaveTimeCallback saveStartEpoch,
                                                       ClearCallback clearStartEpoch,
                                                       SaveViewActiveCallback saveViewActive) {
    _saveStopwatchElapsed = saveElapsed;
    _clearStopwatchElapsed = clearElapsed;
    _saveStopwatchStartEpoch = saveStartEpoch;
    _clearStopwatchStartEpoch = clearStartEpoch;
    _saveStopwatchViewActive = saveViewActive;
}

void TimerController::restoreCountdown(time_t targetEpoch, bool countdownViewActive, uint32_t remainingMs) {
    uint32_t now = millis();
    _persistedCountdownViewActive = countdownViewActive;
    _view = countdownViewActive ? TimerView::Countdown : TimerView::Clock;
    _lastNonClockView = countdownViewActive ? TimerView::Countdown : TimerView::Date;
    _viewActivityMs = now;
    _countdownAlertBellWasBusy = false;

    if (targetEpoch <= 0) {
        _countdownRunning = false;
        _countdownTargetEpoch = 0;
        _countdownRemainingMs = remainingMs > 0 ? remainingMs : countdownPresetMs();
        LOGF("Countdown restored paused: %lu ms\n", (unsigned long)_countdownRemainingMs);
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
        _countdownAlertBellWasBusy = false;
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

void TimerController::restoreStopwatch(uint64_t elapsedMs, time_t startEpoch, bool stopwatchViewActive) {
    uint32_t now = millis();
    _persistedStopwatchViewActive = stopwatchViewActive;
    _stopwatchElapsedMs = elapsedMs;

    if (startEpoch > 0) {
        time_t epoch = 0;
        if (currentEpoch(epoch) && epoch >= startEpoch) {
            _stopwatchElapsedMs += (uint64_t)(epoch - startEpoch) * 1000ULL;
            _stopwatchStartEpoch = epoch;
            _stopwatchStartedMs = now;
            _stopwatchRunning = true;
            LOGLN("Stopwatch restored running");
        } else {
            _stopwatchRunning = false;
            _stopwatchStartEpoch = 0;
            LOGLN("Stopwatch restore: no RTC epoch or invalid");
        }
    } else {
        _stopwatchRunning = false;
        _stopwatchStartEpoch = 0;
        LOGLN("Stopwatch restored paused");
    }

    if (stopwatchViewActive && !_countdownExpired && _view != TimerView::Countdown) {
        _view = TimerView::Stopwatch;
        _lastNonClockView = TimerView::Stopwatch;
        _viewActivityMs = now;
    }
}

void TimerController::showDateView() {
    beginPreview();
    setView(TimerView::Date);
    _viewActivityMs = millis();
    LOGLN("Date view shown");
}

void TimerController::showClockPreview() {
    beginPreview();
    setView(TimerView::Clock);
    _viewActivityMs = millis();
    LOGLN("Clock preview shown");
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
            if (_previewUntilMs > 0) {
                _previewUntilMs = 0;
                setView(_viewBeforePreview);
                LOGLN("View timeout: date -> restored");
            } else {
                setView(TimerView::Clock);
                LOGLN("View timeout: date -> clock");
            }
            _viewActivityMs = now;
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

    if (_previewUntilMs > 0 && now >= _previewUntilMs) {
        _previewUntilMs = 0;
        setView(_viewBeforePreview);
        _viewActivityMs = now;
        LOGLN("Preview timeout -> restored");
    }
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
    bool busy = _bellBusy && _bellBusy();

    if (_countdownAlertBellWasBusy && !busy) {
        _countdownAlertBellWasBusy = false;
        _countdownLastAlertMs = now;
        LOGLN("Countdown alert cycle finished");
    }

#if defined(TIMER_ALERT_DURATION_MINUTES) && TIMER_ALERT_DURATION_MINUTES > 0
    if (!_alertBellStopped && now - _countdownAlertStartedMs >= TIMER_ALERT_DURATION_MINUTES * 60000UL) {
        if (_stopBell) _stopBell();
        _alertBellStopped = true;
        _alertBellStoppedMs = now;
        _countdownAlertBellWasBusy = false;
        LOGLN("Countdown alert bell silenced");
        return;
    }
#endif

#if defined(TIMER_ALERT_SHOW_TIMEOUT_MINUTES) && TIMER_ALERT_SHOW_TIMEOUT_MINUTES > 0
    if (_alertBellStopped && now - _alertBellStoppedMs >= TIMER_ALERT_SHOW_TIMEOUT_MINUTES * 60000UL) {
        acknowledgeAlert(false);
        LOGLN("Countdown 00:00 timed out");
        return;
    }
#endif

    if (!_alertBellStopped) {
        if (busy) {
            _countdownAlertBellWasBusy = true;
            return;
        }

        if (_countdownLastAlertMs == 0 || now - _countdownLastAlertMs >= TIMER_ALERT_REPEAT_SECONDS * 1000UL) {
            if (_queueAlert) {
                _queueAlert(3);
                _countdownAlertBellWasBusy = false;
                _countdownLastAlertMs = 0;
                LOGLN("Countdown alert queued");
            }
        }
    }
}

void TimerController::onLeft() {
    if (_countdownExpired) {
        acknowledgeAlert(true);
        return;
    }

    if (_view == TimerView::Stopwatch) {
        if (_stopwatchRunning) {
            _stopwatchElapsedMs += (uint64_t)(millis() - _stopwatchStartedMs);
            _stopwatchRunning = false;
            _stopwatchStartEpoch = 0;
            if (_saveStopwatchElapsed) _saveStopwatchElapsed(_stopwatchElapsedMs);
            if (_clearStopwatchStartEpoch) _clearStopwatchStartEpoch();
            LOGLN("Stopwatch paused");
        } else {
            if (_saveStopwatchElapsed) _saveStopwatchElapsed(_stopwatchElapsedMs);
            _stopwatchStartedMs = millis();
            time_t epoch = 0;
            if (currentEpoch(epoch)) {
                _stopwatchStartEpoch = epoch;
                if (_saveStopwatchStartEpoch) _saveStopwatchStartEpoch(epoch);
            } else {
                _stopwatchStartEpoch = 0;
                if (_clearStopwatchStartEpoch) _clearStopwatchStartEpoch();
            }
            _stopwatchRunning = true;
            LOGLN("Stopwatch started");
        }
        return;
    }

    if (_view == TimerView::Countdown) {
        if (_countdownRunning) {
            pauseCountdown();
        } else if (_countdownRemainingMs > 0) {
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
            _stopwatchStartEpoch = 0;
            if (_clearStopwatchElapsed) _clearStopwatchElapsed();
            if (_clearStopwatchStartEpoch) _clearStopwatchStartEpoch();
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
            if (_clearCountdownRemaining) _clearCountdownRemaining();
            LOGLN("Countdown reset");
            return;
        }

        _presetIndex = (_presetIndex + 1) % _presetCount;
        _countdownRemainingMs = countdownPresetMs();
        _countdownTargetEpoch = 0;
        clearPersistedTargetEpoch();
        if (_clearCountdownRemaining) _clearCountdownRemaining();
        if (_savePreset) _savePreset(_presetIndex);
        LOGF("Countdown preset: %u min\n",
                      (unsigned)_presetMinutes[_presetIndex]);
    }
}

void TimerController::onMiddleShort() {
    if (_countdownExpired) {
        acknowledgeAlert(false);
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

void TimerController::acknowledgeAlert(bool forceCountdownView) {
    _countdownExpired = false;
    _countdownRunning = false;
    _countdownRemainingMs = countdownPresetMs();
    _countdownTargetEpoch = 0;
    _countdownAlertStartedMs = 0;
    _countdownLastAlertMs = 0;
    _alertBellStopped = false;
    _alertBellStoppedMs = 0;
    _countdownAlertBellWasBusy = false;
    clearPersistedTargetEpoch();
    if (_stopBell) _stopBell();
    setView(forceCountdownView ? TimerView::Countdown : _countdownAlertReturnView);
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

uint64_t TimerController::stopwatchMs() const {
    if (!_stopwatchRunning) {
        return _stopwatchElapsedMs;
    }
    time_t epoch = 0;
    if (_stopwatchStartEpoch > 0 && currentEpoch(epoch) && epoch >= _stopwatchStartEpoch) {
        uint64_t epochPart = (uint64_t)(epoch - _stopwatchStartEpoch) * 1000ULL;
        uint64_t millisSub = (uint64_t)(millis() - _stopwatchStartedMs) % 1000ULL;
        return _stopwatchElapsedMs + epochPart + millisSub;
    }
    return _stopwatchElapsedMs + (uint64_t)(millis() - _stopwatchStartedMs);
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

uint32_t TimerController::countdownElapsedSinceExpiryMs() const {
    if (!_countdownExpired) return 0;
    return millis() - _countdownAlertStartedMs;
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
    _view = TimerView::Countdown;
    _countdownAlertStartedMs = now;
    _countdownLastAlertMs = 0;
    _countdownAlertBellWasBusy = false;
    clearPersistedTargetEpoch();
    if (_clearCountdownRemaining) _clearCountdownRemaining();
    LOGLN("Countdown expired");
}

bool TimerController::startCountdownFromRemaining() {
    _countdownStartedMs = millis();
    _countdownTargetEpoch = 0;
    if (_clearCountdownRemaining) _clearCountdownRemaining();

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
    if (_saveCountdownRemaining) _saveCountdownRemaining(_countdownRemainingMs);
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

void TimerController::saveStopwatchViewActive(bool active) {
    if (_persistedStopwatchViewActive == active) {
        return;
    }
    _persistedStopwatchViewActive = active;
    if (_saveStopwatchViewActive) {
        _saveStopwatchViewActive(active);
    }
}

void TimerController::beginPreview() {
    if (_previewUntilMs == 0) {
        _viewBeforePreview = _view;
    }
    _previewUntilMs = millis() + MENU_TIMEOUT_SHORT_SECONDS * 1000UL;
}

void TimerController::setView(TimerView view, bool persist) {
    _view = view;
    if (persist) {
        saveCountdownViewActive(view == TimerView::Countdown);
        saveStopwatchViewActive(view == TimerView::Stopwatch);
    }
}

void TimerController::setLastNonClockView(TimerView view) {
    _lastNonClockView = view;
}
