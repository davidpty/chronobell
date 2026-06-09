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

void TimerController::update() {
    uint32_t now = millis();

    if (_countdownRunning) {
        uint32_t elapsed = now - _countdownStartedMs;
        if (elapsed >= _countdownRemainingMs) {
            _countdownRunning = false;
            _countdownRemainingMs = 0;
            _countdownExpired = true;
            _countdownAlertReturnView = (_view == TimerView::Countdown)
                ? TimerView::Countdown
                : TimerView::Clock;
            _countdownAlertStartedMs = now;
            _countdownLastAlertMs = 0;
            LOGLN("Countdown expired");
        }
    }

    updateAlert();

    if (_countdownExpired) {
        return;
    }

    if (_view == TimerView::Date) {
        if (now - _viewActivityMs >= MENU_TIMEOUT_SHORT_MS) {
            _view = TimerView::Clock;
            LOGLN("View timeout: date -> clock");
        }
        return;
    }

    if (_view == TimerView::GuestWifi) {
#if GUEST_WIFI_VIEW_TIMEOUT_MS > 0
        if (now - _viewActivityMs >= GUEST_WIFI_VIEW_TIMEOUT_MS) {
            _view = TimerView::Clock;
            LOGLN("View timeout: guest wifi -> clock");
        }
#endif
        return;
    }

    if (_view == TimerView::Stopwatch && !_stopwatchRunning &&
        now - _viewActivityMs >= MENU_TIMEOUT_SHORT_MS) {
        _view = TimerView::Clock;
        LOGLN("View timeout: stopwatch -> clock");
        return;
    }

    if (_view == TimerView::Countdown && !_countdownRunning &&
        now - _viewActivityMs >= MENU_TIMEOUT_SHORT_MS) {
        _view = TimerView::Clock;
        LOGLN("View timeout: countdown -> clock");
        return;
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
#if defined(TIMER_ALERT_DURATION_MS) && TIMER_ALERT_DURATION_MS > 0
    if (now - _countdownAlertStartedMs >= TIMER_ALERT_DURATION_MS) {
        acknowledgeAlert();
        return;
    }
#endif
    if (_countdownLastAlertMs == 0 || now - _countdownLastAlertMs >= TIMER_ALERT_REPEAT_MS) {
        bool busy = _bellBusy && _bellBusy();
        if (!busy && _queueAlert) {
            _queueAlert(3);
            _countdownLastAlertMs = now;
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
                _countdownRemainingMs = countdownMs();
                _countdownRunning = false;
                LOGLN("Countdown paused");
            }
            _stopwatchStartedMs = millis();
            _stopwatchRunning = true;
            LOGLN("Stopwatch started");
        }
        return;
    }

    if (_view == TimerView::Countdown) {
        if (_countdownRunning) {
            _countdownRemainingMs = countdownMs();
            _countdownRunning = false;
            LOGLN("Countdown paused");
        } else if (_countdownRemainingMs > 0) {
            if (_stopwatchRunning) {
                _stopwatchElapsedMs += millis() - _stopwatchStartedMs;
                _stopwatchRunning = false;
                LOGLN("Stopwatch paused");
            }
            _countdownStartedMs = millis();
            _countdownRunning = true;
            LOGLN("Countdown started");
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
            LOGLN("Countdown reset");
            return;
        }

        _presetIndex = (_presetIndex + 1) % _presetCount;
        _countdownRemainingMs = countdownPresetMs();
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
                _lastNonClockView = TimerView::Date;
                break;
            default:
                nextView = TimerView::Clock;
                break;
        }
    }

    if (nextView != TimerView::Clock) {
        _lastNonClockView = nextView;
    }
    _view = nextView;
    noteActivity();
    LOGF("View: %u\n", (unsigned)_view);
}

TimerLongPressAction TimerController::onLongPress() {
    if (_countdownExpired) {
        acknowledgeAlert();
        return TimerLongPressAction::AcknowledgedAlert;
    }
    if (_view != TimerView::Clock) {
        _view = TimerView::Clock;
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
    _countdownAlertStartedMs = 0;
    _countdownLastAlertMs = 0;
    if (_stopBell) _stopBell();
    _view = _countdownAlertReturnView;
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

TimerView TimerController::view() const {
    return _view;
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
