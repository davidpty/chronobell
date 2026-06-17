#include "DriftClock.h"

#include "Config.h"

#include <esp_system.h>
#include <math.h>

namespace {
static constexpr int SECONDS_PER_DAY = 24 * 60 * 60;
static constexpr int MINUTES_PER_DAY = 24 * 60;
static constexpr unsigned long FRESH_DISPLAYED_SECOND_MS = 1500UL;
static constexpr unsigned long MIN_DISPLAYED_SECOND_MS = 250UL;
static constexpr unsigned long MAX_DISPLAYED_SECOND_MS = 10000UL;
static constexpr float DRIFT_TWO_PI = 6.28318530718f;

static bool elapsed(unsigned long nowMs, unsigned long deadlineMs) {
    return (int32_t)(nowMs - deadlineMs) >= 0;
}
}

void DriftClock::update(const ClockTime& realTime, unsigned long nowMs) {
    int realSecond = secondOfDay(realTime);
    if (!_initialized) {
        reset(realTime, nowMs);
        return;
    }

    int realJump = signedSecondDelta(realSecond, _lastRealSecond);
    if (realJump < -10 || realJump > 10) {
        reset(realTime, nowMs);
        return;
    }
    _lastRealSecond = realSecond;

    unsigned long phaseDurationMs = (unsigned long)phaseSeconds() * 1000UL;
    if (elapsed(nowMs, _phaseStartMs + phaseDurationMs)) {
        Phase nextPhase = (_phase == Phase::Away) ? Phase::Return : Phase::Away;
        beginPhase(nextPhase, realTime, nowMs);
    }

    while (elapsed(nowMs, _nextDisplayedSecondMs)) {
        advanceDisplayedSecond(_nextDisplayedSecondMs);
        if (displayedProgressSeconds() >= targetDisplayedProgressSeconds()) {
            Phase nextPhase = (_phase == Phase::Away) ? Phase::Return : Phase::Away;
            beginPhase(nextPhase, realTime, nowMs);
            break;
        }
        scheduleNextDisplayedSecond(realTime, _nextDisplayedSecondMs);
    }
}

void DriftClock::reset(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs);
}

void DriftClock::activate(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs);
}

void DriftClock::initialize(const ClockTime& realTime, unsigned long nowMs) {
    _initialized = true;
    _displayedSecond = secondOfDay(realTime);
    _lastRealSecond = _displayedSecond;
    beginPhase(Phase::Away, realTime, nowMs);
}

void DriftClock::beginPhase(Phase phase, const ClockTime& realTime, unsigned long nowMs) {
    int realSecond = secondOfDay(realTime);
    _phase = phase;
    _phaseStartMs = nowMs;
    _lastRealSecond = realSecond;

    int offset = directionSign() * maxOffsetSeconds();
    if (phase == Phase::Away) {
        _displayedSecond = realSecond;
    } else {
        _displayedSecond = wrapSecondOfDay(realSecond + offset);
    }

    _phaseDisplayedProgress = 0;
    _lastDisplayChangeMs = nowMs;
    _jitterPhaseA = ((float)(esp_random() % 6283U)) / 1000.0f;
    _jitterPhaseB = ((float)(esp_random() % 6283U)) / 1000.0f;
    scheduleNextDisplayedSecond(realTime, nowMs);
}

ClockTime DriftClock::displayTime(const ClockTime& realTime, unsigned long nowMs) const {
    (void)nowMs;
    int displayedSecond = _initialized ? _displayedSecond : secondOfDay(realTime);
    displayedSecond = wrapSecondOfDay(displayedSecond);

    ClockTime time;
    time.hours = displayedSecond / 3600;
    time.minutes = (displayedSecond / 60) % 60;
    time.seconds = displayedSecond % 60;
    return time;
}

int DriftClock::offsetMinutes(const ClockTime& realTime) const {
    if (!_initialized) {
        return 0;
    }

    int offsetSeconds = signedSecondDelta(_displayedSecond, secondOfDay(realTime));
    if (offsetSeconds >= 0) {
        return (offsetSeconds + 30) / 60;
    }
    return -((-offsetSeconds + 30) / 60);
}

bool DriftClock::displayedMinuteFresh(unsigned long nowMs) const {
    return _initialized && (nowMs - _lastDisplayChangeMs) <= FRESH_DISPLAYED_SECOND_MS;
}

unsigned long DriftClock::separatorBlinkHalfPeriodMs(unsigned long nowMs) const {
    (void)nowMs;
    return _lastSecondDurationMs > 0 ? _lastSecondDurationMs : 1000UL;
}

void DriftClock::advanceDisplayedSecond(unsigned long nowMs) {
    _displayedSecond = wrapSecondOfDay(_displayedSecond + 1);
    _phaseDisplayedProgress++;
    _lastDisplayChangeMs = nowMs;
}

void DriftClock::scheduleNextDisplayedSecond(const ClockTime& realTime, unsigned long nowMs) {
    _lastSecondDurationMs = nextDisplayedSecondDurationMs(realTime, nowMs);
    _nextDisplayedSecondMs = nowMs + _lastSecondDurationMs;
}

unsigned long DriftClock::nextDisplayedSecondDurationMs(const ClockTime& realTime, unsigned long nowMs) const {
    (void)realTime;

    int targetProgress = targetDisplayedProgressSeconds();
    int displayedProgress = displayedProgressSeconds();
    int remainingDisplayed = targetProgress - displayedProgress;
    if (remainingDisplayed <= 0) {
        return MIN_DISPLAYED_SECOND_MS;
    }

    unsigned long phaseDurationMs = (unsigned long)phaseSeconds() * 1000UL;
    unsigned long elapsedMs = elapsed(nowMs, _phaseStartMs) ? nowMs - _phaseStartMs : 0;
    if (elapsedMs >= phaseDurationMs) {
        return MIN_DISPLAYED_SECOND_MS;
    }

    unsigned long remainingMs = phaseDurationMs - elapsedMs;
    float durationMs = (float)remainingMs / (float)remainingDisplayed;

    int fadeWindow = clampInt(targetProgress / 12, 8, 120);
    int fromStart = displayedProgress;
    int fromEnd = remainingDisplayed;
    float fade = 1.0f;
    if (fromStart < fadeWindow) {
        fade = (float)fromStart / (float)fadeWindow;
    }
    if (fromEnd < fadeWindow) {
        float endFade = (float)fromEnd / (float)fadeWindow;
        if (endFade < fade) {
            fade = endFade;
        }
    }

    durationMs *= 1.0f + ((jitterMultiplier(nowMs) - 1.0f) * fade);
    if (durationMs < (float)MIN_DISPLAYED_SECOND_MS) {
        durationMs = (float)MIN_DISPLAYED_SECOND_MS;
    }
    if (durationMs > (float)MAX_DISPLAYED_SECOND_MS) {
        durationMs = (float)MAX_DISPLAYED_SECOND_MS;
    }
    return (unsigned long)(durationMs + 0.5f);
}

int DriftClock::targetDisplayedProgressSeconds() const {
    int phase = phaseSeconds();
    int offset = directionSign() * maxOffsetSeconds();
    if (_phase == Phase::Away) {
        return clampInt(phase + offset, 1, phase + maxOffsetSeconds());
    }
    return clampInt(phase - offset, 1, phase + maxOffsetSeconds());
}

int DriftClock::displayedProgressSeconds() const {
    return _phaseDisplayedProgress;
}

float DriftClock::jitterMultiplier(unsigned long nowMs) const {
    int jitter = jitterPercent();
    if (jitter <= 0) {
        return 1.0f;
    }

    float seconds = (float)(nowMs - _phaseStartMs) / 1000.0f;
    float waveA = sinf((seconds / 73.0f) * DRIFT_TWO_PI + _jitterPhaseA);
    float waveB = sinf((seconds / 41.0f) * DRIFT_TWO_PI + _jitterPhaseB);
    float wave = (waveA * 0.65f) + (waveB * 0.35f);
    return 1.0f + (wave * ((float)jitter / 100.0f));
}

int DriftClock::minuteOfDay(const ClockTime& time) {
    return secondOfDay(time) / 60;
}

int DriftClock::secondOfDay(const ClockTime& time) {
    int hours = time.hours;
    int minutes = time.minutes;
    int seconds = time.seconds;
    while (hours < 0) hours += 24;
    hours %= 24;
    minutes = clampInt(minutes, 0, 59);
    seconds = clampInt(seconds, 0, 59);
    return (hours * 3600) + (minutes * 60) + seconds;
}

int DriftClock::wrapMinuteOfDay(int minute) {
    minute %= MINUTES_PER_DAY;
    if (minute < 0) {
        minute += MINUTES_PER_DAY;
    }
    return minute;
}

int DriftClock::wrapSecondOfDay(int second) {
    second %= SECONDS_PER_DAY;
    if (second < 0) {
        second += SECONDS_PER_DAY;
    }
    return second;
}

int DriftClock::signedMinuteDelta(int fromMinute, int toMinute) {
    int diff = wrapMinuteOfDay(fromMinute) - wrapMinuteOfDay(toMinute);
    while (diff <= -MINUTES_PER_DAY / 2) {
        diff += MINUTES_PER_DAY;
    }
    while (diff > MINUTES_PER_DAY / 2) {
        diff -= MINUTES_PER_DAY;
    }
    return diff;
}

int DriftClock::signedSecondDelta(int fromSecond, int toSecond) {
    int diff = wrapSecondOfDay(fromSecond) - wrapSecondOfDay(toSecond);
    while (diff <= -SECONDS_PER_DAY / 2) {
        diff += SECONDS_PER_DAY;
    }
    while (diff > SECONDS_PER_DAY / 2) {
        diff -= SECONDS_PER_DAY;
    }
    return diff;
}

int DriftClock::maxOffsetMinutes() {
    int maxPhaseOffset = (phaseSeconds() - 1) / 60;
    if (maxPhaseOffset < 1) {
        maxPhaseOffset = 1;
    }
    return clampInt(DRIFT_MAX_OFFSET_MINUTES, 1, maxPhaseOffset);
}

int DriftClock::maxOffsetSeconds() {
    int maxOffset = DRIFT_MAX_OFFSET_MINUTES * 60;
    return clampInt(maxOffset, 1, phaseSeconds() - 1);
}

int DriftClock::phaseSeconds() {
    return clampInt(DRIFT_PHASE_MINUTES, 1, 12 * 60) * 60;
}

int DriftClock::jitterPercent() {
    return clampInt(DRIFT_JITTER_PERCENT, 0, 25);
}

int DriftClock::directionSign() {
    return DRIFT_DIRECTION == 1 ? 1 : -1;
}

int DriftClock::clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}
