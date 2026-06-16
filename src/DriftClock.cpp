#include "DriftClock.h"

#include "Config.h"

#include <esp_system.h>

namespace {
static constexpr int MINUTES_PER_DAY = 24 * 60;
static constexpr unsigned long FRESH_DISPLAYED_MINUTE_MS = 1500UL;
}

void DriftClock::update(const ClockTime& realTime, unsigned long nowMs) {
    int realMinute = minuteOfDay(realTime);
    if (!_initialized) {
        reset(realTime, nowMs);
        return;
    }

    int realJump = signedMinuteDelta(realMinute, _lastRealMinute);
    if (realJump < -2 || realJump > 2) {
        reset(realTime, nowMs);
        return;
    }
    _lastRealMinute = realMinute;

    clampToRealTime(realTime, nowMs);

    if (_phase == Phase::Rush) {
        if ((int32_t)(nowMs - _nextRushStepMs) < 0) {
            return;
        }

        moveDisplayedMinute(realTime, false, nowMs);
        if (_rushStepsRemaining > 0) {
            _rushStepsRemaining--;
        }

        if (_rushStepsRemaining == 0) {
            _phase = Phase::Hold;
            scheduleHold(nowMs);
        } else {
            _nextRushStepMs = nowMs + (unsigned long)DRIFT_FAST_STEP_SECONDS * 1000UL;
        }
        return;
    }

    if ((int32_t)(nowMs - _holdUntilMs) < 0) {
        return;
    }

    decideNextEvent(realTime, nowMs);
}

void DriftClock::reset(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs, false);
}

void DriftClock::activate(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs, DRIFT_START_WITH_RANDOM_OFFSET);
}

void DriftClock::initialize(const ClockTime& realTime, unsigned long nowMs, bool randomizeStart) {
    int realMinute = minuteOfDay(realTime);
    int startOffset = randomizeStart ? randomStartOffsetMinutes() : 0;
    _initialized = true;
    _phase = Phase::Hold;
    _displayedMinute = wrapMinuteOfDay(realMinute + startOffset);
    _lastRealMinute = realMinute;
    _rushStepsRemaining = 0;
    _nextRushStepMs = 0;
    _lastDisplayChangeMs = nowMs;
    scheduleHold(nowMs, randomizeStart ? _displayedMinute : -1);
}

ClockTime DriftClock::displayTime(const ClockTime& realTime, unsigned long nowMs) const {
    (void)realTime;
    ClockTime time;
    int displayedMinute = _initialized ? _displayedMinute : minuteOfDay(realTime);
    displayedMinute = wrapMinuteOfDay(displayedMinute);
    time.hours = displayedMinute / 60;
    time.minutes = displayedMinute % 60;
    time.seconds = displayedMinuteFresh(nowMs) ? 0 : 2;
    return time;
}

int DriftClock::offsetMinutes(const ClockTime& realTime) const {
    if (!_initialized) {
        return 0;
    }
    return signedMinuteDelta(_displayedMinute, minuteOfDay(realTime));
}

bool DriftClock::displayedMinuteFresh(unsigned long nowMs) const {
    return _initialized && (nowMs - _lastDisplayChangeMs) <= FRESH_DISPLAYED_MINUTE_MS;
}

void DriftClock::scheduleHold(unsigned long nowMs, int landedMinute) {
    int minHold = clampInt(DRIFT_HOLD_MIN_SECONDS, 1, 3600);
    int maxHold = clampInt(DRIFT_HOLD_MAX_SECONDS, minHold, 3600);
    unsigned long holdMs = (unsigned long)randomRange(minHold, maxHold) * 1000UL;

    int longHoldChance = clampInt(DRIFT_LONG_HOLD_CHANCE_PERCENT, 0, 100);
    if (longHoldChance > 0 && (int)(esp_random() % 100U) < longHoldChance) {
        int multiplier = clampInt(DRIFT_LONG_HOLD_MULTIPLIER, 1, 24);
        holdMs *= (unsigned long)multiplier;
    }

    int anchorMultiplier = anchorHoldMultiplier(landedMinute);
    if (anchorMultiplier > 1) {
        int multiplier = clampInt(anchorMultiplier, 1, 24);
        holdMs *= (unsigned long)multiplier;
    }

    _holdUntilMs = nowMs + holdMs;
}

void DriftClock::startRush(const ClockTime& realTime, unsigned long nowMs) {
    int maxSteps = clampInt(DRIFT_FAST_STEP_COUNT_MAX, 1, 30);
    _phase = Phase::Rush;
    _rushStepsRemaining = (uint8_t)randomRange(1, maxSteps);
    moveDisplayedMinute(realTime, false, nowMs);
    if (_rushStepsRemaining > 0) {
        _rushStepsRemaining--;
    }

    if (_rushStepsRemaining == 0) {
        _phase = Phase::Hold;
        scheduleHold(nowMs, _displayedMinute);
    } else {
        _nextRushStepMs = nowMs + (unsigned long)DRIFT_FAST_STEP_SECONDS * 1000UL;
    }
}

void DriftClock::decideNextEvent(const ClockTime& realTime, unsigned long nowMs) {
    int offset = offsetMinutes(realTime);
    int absOffset = offset < 0 ? -offset : offset;

    bool startFast = absOffset >= 4 && (esp_random() % 100U) < 55U;
    bool unsettlingRush = absOffset < DRIFT_MAX_OFFSET_MINUTES && (esp_random() % 100U) < 20U;
    if (startFast || unsettlingRush) {
        startRush(realTime, nowMs);
        return;
    }

    bool extendHold = absOffset < DRIFT_MAX_OFFSET_MINUTES && (esp_random() % 100U) < 30U;
    if (extendHold) {
        scheduleHold(nowMs);
        return;
    }

    moveDisplayedMinute(realTime, true, nowMs);
    scheduleHold(nowMs, _displayedMinute);
}

void DriftClock::moveDisplayedMinute(const ClockTime& realTime, bool allowJump, unsigned long nowMs) {
    int realMinute = minuteOfDay(realTime);
    int offset = signedMinuteDelta(_displayedMinute, realMinute);
    int maxOffset = clampInt(DRIFT_MAX_OFFSET_MINUTES, 1, 12 * 60);

    int pullChance = clampInt(DRIFT_REALTIME_PULL_PERCENT, 0, 100);
    bool pullTowardReal = (int)(esp_random() % 100U) < pullChance;
    if (offset >= maxOffset) {
        pullTowardReal = true;
    } else if (offset <= -maxOffset) {
        pullTowardReal = true;
    }

    int direction = 0;
    if (pullTowardReal && offset > 0) {
        direction = -1;
    } else if (pullTowardReal && offset < 0) {
        direction = 1;
    } else if (offset > 0) {
        direction = 1;
    } else if (offset < 0) {
        direction = -1;
    } else {
        direction = (esp_random() & 1U) ? 1 : -1;
    }

    int step = 1;
    int jumpChance = clampInt(DRIFT_JUMP_CHANCE_PERCENT, 0, 100);
    int maxJump = clampInt(DRIFT_JUMP_MAX_MINUTES, 1, maxOffset);
    int gravityChance = clampInt(DRIFT_BELL_GRAVITY_CHANCE_PERCENT, 0, 100);
    bool gravityApplied = false;
    if (allowJump && maxJump > 1 && gravityChance > 0 &&
        (int)(esp_random() % 100U) < gravityChance) {
        bool preferForward = (esp_random() & 1U) != 0;
        for (int distance = 1; distance <= maxJump && !gravityApplied; distance++) {
            for (uint8_t option = 0; option < 2 && !gravityApplied; option++) {
                int signedStep = ((option == 0) == preferForward) ? distance : -distance;
                int candidate = wrapMinuteOfDay(_displayedMinute + signedStep);
                int candidateMinute = candidate % 60;
                int candidateOffset = signedMinuteDelta(candidate, realMinute);
                if ((candidateMinute == 0 || candidateMinute == 30) &&
                    candidateOffset >= -maxOffset && candidateOffset <= maxOffset) {
                    direction = signedStep > 0 ? 1 : -1;
                    step = distance;
                    gravityApplied = true;
                }
            }
        }
    }
    if (!gravityApplied && allowJump && maxJump > 1 && (int)(esp_random() % 100U) < jumpChance) {
        step = randomRange(2, maxJump);
    }

    int previous = _displayedMinute;
    _displayedMinute = wrapMinuteOfDay(_displayedMinute + direction * step);
    clampToRealTime(realTime, nowMs);
    if (_displayedMinute != previous) {
        _lastDisplayChangeMs = nowMs;
    }
}

void DriftClock::clampToRealTime(const ClockTime& realTime, unsigned long nowMs) {
    int realMinute = minuteOfDay(realTime);
    int maxOffset = clampInt(DRIFT_MAX_OFFSET_MINUTES, 1, 12 * 60);
    int offset = signedMinuteDelta(_displayedMinute, realMinute);
    int clampedOffset = clampInt(offset, -maxOffset, maxOffset);
    if (clampedOffset != offset) {
        _displayedMinute = wrapMinuteOfDay(realMinute + clampedOffset);
        _lastDisplayChangeMs = nowMs;
    }
}

int DriftClock::minuteOfDay(const ClockTime& time) {
    int hours = time.hours;
    int minutes = time.minutes;
    while (hours < 0) hours += 24;
    hours %= 24;
    if (minutes < 0) minutes = 0;
    if (minutes > 59) minutes = 59;
    return hours * 60 + minutes;
}

int DriftClock::wrapMinuteOfDay(int minute) {
    minute %= MINUTES_PER_DAY;
    if (minute < 0) {
        minute += MINUTES_PER_DAY;
    }
    return minute;
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

bool DriftClock::isAnchorMinute(int displayedMinute) {
    int minute = wrapMinuteOfDay(displayedMinute) % 60;
    return minute == 0 || minute == 15 || minute == 30 || minute == 45;
}

int DriftClock::anchorHoldMultiplier(int displayedMinute) {
    if (displayedMinute < 0 || !isAnchorMinute(displayedMinute)) {
        return 1;
    }
    int minute = wrapMinuteOfDay(displayedMinute) % 60;
    return minute == 0 ? DRIFT_FULL_HOUR_ANCHOR_MULTIPLIER : DRIFT_ANCHOR_HOLD_MULTIPLIER;
}

int DriftClock::randomStartOffsetMinutes() {
    int maxOffset = clampInt(DRIFT_MAX_OFFSET_MINUTES, 1, 12 * 60);
    int offset = randomRange(1, maxOffset);
    return (esp_random() & 1U) ? offset : -offset;
}

int DriftClock::randomRange(int minValue, int maxValue) {
    if (maxValue <= minValue) {
        return minValue;
    }
    uint32_t span = (uint32_t)(maxValue - minValue + 1);
    return minValue + (int)(esp_random() % span);
}

int DriftClock::clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}
