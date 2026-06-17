#include "DriftClock.h"

#include "Config.h"

#include <esp_system.h>

namespace {
static constexpr int MINUTES_PER_DAY = 24 * 60;
static constexpr unsigned long FRESH_DISPLAYED_MINUTE_MS = 1500UL;

struct DriftPreset {
    int holdMinSec;
    int holdMaxSec;
    uint8_t nudgeChance;
    uint8_t doubleStepChance;
    uint8_t catchUpChance;
    uint8_t anchorChance;
};

static const DriftPreset gPresets[] = {
    // Wary
    { 45, 150, 26, 4, 26, 2 },
    // Restless
    { 15, 75, 45, 10, 30, 4 },
    // Haunted
    { 45, 180, 18, 6, 24, 28 },
    // Tired
    { 120, 240, 10, 2, 70, 1 },
};

static bool elapsed(unsigned long nowMs, unsigned long deadlineMs) {
    return (int32_t)(nowMs - deadlineMs) >= 0;
}
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

    enforceStillnessCap(realTime, nowMs);

    if (offsetMinutes(realTime) <= -maxOffsetMinutes()) {
        startCatchUp(realTime, nowMs);
        processCatchUp(realTime, nowMs);
        return;
    }

    if (_phase == Phase::CatchUp) {
        processCatchUp(realTime, nowMs);
        return;
    }

    if (!elapsed(nowMs, _eventDeadlineMs)) {
        return;
    }

    decideNextEvent(realTime, nowMs);
}

void DriftClock::reset(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs, false);
}

void DriftClock::activate(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs, DRIFT_START_WITH_OFFSET);
}

void DriftClock::initialize(const ClockTime& realTime, unsigned long nowMs, bool randomizeStart) {
    int realMinute = minuteOfDay(realTime);
    int startOffset = randomizeStart ? randomStartOffsetMinutes() : 0;

    _initialized = true;
    _phase = Phase::Hold;
    _displayedMinute = wrapMinuteOfDay(realMinute + startOffset);
    _lastRealMinute = realMinute;
    _targetOffset = startOffset;
    _catchUpStepsRemaining = 0;
    _nextCatchUpStepMs = 0;
    _lastDisplayChangeMs = nowMs;
    scheduleEvent(realTime, nowMs);
}

ClockTime DriftClock::displayTime(const ClockTime& realTime, unsigned long nowMs) const {
    (void)nowMs;
    ClockTime time;
    int displayedMinute = _initialized ? _displayedMinute : minuteOfDay(realTime);
    displayedMinute = wrapMinuteOfDay(displayedMinute);
    time.hours = displayedMinute / 60;
    time.minutes = displayedMinute % 60;
    time.seconds = 0;
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

unsigned long DriftClock::separatorBlinkHalfPeriodMs(unsigned long nowMs) const {
    (void)nowMs;
    unsigned long displayedMinuteDurationMs = 60UL * 1000UL;
    if (_initialized && _phase == Phase::CatchUp) {
        displayedMinuteDurationMs = (unsigned long)clampInt(DRIFT_CATCHUP_STEP_SECONDS, 1, 60) * 1000UL;
    } else if (_initialized && elapsed(_eventDeadlineMs, _lastDisplayChangeMs)) {
        displayedMinuteDurationMs = _eventDeadlineMs - _lastDisplayChangeMs;
    }

    unsigned long halfPeriodMs = displayedMinuteDurationMs / 60UL;
    if (halfPeriodMs == 0) {
        halfPeriodMs = 1000UL;
    }
    if (DRIFT_SEPARATOR_BLINK_MIN_MS > 0 && halfPeriodMs < DRIFT_SEPARATOR_BLINK_MIN_MS) {
        halfPeriodMs = DRIFT_SEPARATOR_BLINK_MIN_MS;
    }
    return halfPeriodMs;
}

void DriftClock::scheduleEvent(const ClockTime& realTime, unsigned long nowMs) {
    const DriftPreset& preset = gPresets[personality()];
    int offset = offsetMinutes(realTime);
    _targetOffset = offset;

    if (offset > 0) {
        int waitSec = randomRange(preset.holdMinSec, preset.holdMaxSec);
        scheduleHold(nowMs, (unsigned long)waitSec * 1000UL, Phase::WaitCorrection);
        return;
    }

    int holdSec = randomRange(preset.holdMinSec, preset.holdMaxSec);
    scheduleHold(nowMs, (unsigned long)holdSec * 1000UL, Phase::Hold);
}

void DriftClock::scheduleHold(unsigned long nowMs, unsigned long holdMs, Phase phase) {
    unsigned long maxDeadline = _lastDisplayChangeMs + (unsigned long)maxStillMinutes() * 60UL * 1000UL;
    unsigned long requestedDeadline = nowMs + holdMs;
    _phase = phase;
    _eventDeadlineMs = elapsed(requestedDeadline, maxDeadline) ? maxDeadline : requestedDeadline;
}

void DriftClock::startCatchUp(const ClockTime& realTime, unsigned long nowMs) {
    int offset = offsetMinutes(realTime);
    if (offset >= 0) {
        scheduleEvent(realTime, nowMs);
        return;
    }

    int behind = -offset;
    _phase = Phase::CatchUp;
    _targetOffset = 0;
    _catchUpStepsRemaining = (uint8_t)clampInt(behind, 1, maxOffsetMinutes());
    _nextCatchUpStepMs = nowMs;
}

void DriftClock::decideNextEvent(const ClockTime& realTime, unsigned long nowMs) {
    int offset = offsetMinutes(realTime);
    int maxOffset = maxOffsetMinutes();

    if (offset < 0) {
        const DriftPreset& preset = gPresets[personality()];
        int catchUpChance = preset.catchUpChance;
        if (personality() == 3 || -offset >= maxOffset / 2 || (int)(esp_random() % 100U) < catchUpChance) {
            startCatchUp(realTime, nowMs);
            return;
        }
    }

    if (personality() == 2 && offset < maxOffset) {
        int anchorStep = distanceToNextAnchor(_displayedMinute, 8);
        if (anchorStep > 0 && canAdvance(realTime, anchorStep) &&
            (int)(esp_random() % 100U) < gPresets[personality()].anchorChance) {
            advanceDisplayed(realTime, nowMs, anchorStep);
            scheduleHold(nowMs, cappedHoldMs(nowMs, _lastDisplayChangeMs, 35, 95), Phase::AnchorHold);
            return;
        }
    }

    const DriftPreset& preset = gPresets[personality()];
    int roll = (int)(esp_random() % 100U);
    if (roll < preset.doubleStepChance && canAdvance(realTime, 2)) {
        _phase = Phase::DoubleStep;
        advanceDisplayed(realTime, nowMs, 2);
        scheduleEvent(realTime, nowMs);
        return;
    }
    if (roll < preset.doubleStepChance + preset.nudgeChance && canAdvance(realTime, 1)) {
        _phase = Phase::Nudge;
        advanceDisplayed(realTime, nowMs, 1);
        scheduleEvent(realTime, nowMs);
        return;
    }

    scheduleEvent(realTime, nowMs);
}

void DriftClock::processCatchUp(const ClockTime& realTime, unsigned long nowMs) {
    int offset = offsetMinutes(realTime);
    if (offset >= 0 || _catchUpStepsRemaining == 0) {
        _catchUpStepsRemaining = 0;
        scheduleEvent(realTime, nowMs);
        return;
    }

    if (!elapsed(nowMs, _nextCatchUpStepMs)) {
        return;
    }

    if (advanceDisplayed(realTime, nowMs, 1)) {
        _catchUpStepsRemaining--;
    } else {
        _catchUpStepsRemaining = 0;
    }

    if (_catchUpStepsRemaining == 0 || offsetMinutes(realTime) >= 0) {
        scheduleEvent(realTime, nowMs);
    } else {
        _nextCatchUpStepMs = nowMs + (unsigned long)clampInt(DRIFT_CATCHUP_STEP_SECONDS, 1, 60) * 1000UL;
    }
}

bool DriftClock::advanceDisplayed(const ClockTime& realTime, unsigned long nowMs, int steps) {
    steps = clampInt(steps, 1, maxOffsetMinutes());
    if (!canAdvance(realTime, steps)) {
        return false;
    }

    _displayedMinute = wrapMinuteOfDay(_displayedMinute + steps);
    _lastDisplayChangeMs = nowMs;
    _targetOffset = offsetMinutes(realTime);
    return true;
}

bool DriftClock::canAdvance(const ClockTime& realTime, int steps) const {
    int candidate = wrapMinuteOfDay(_displayedMinute + clampInt(steps, 1, maxOffsetMinutes()));
    int candidateOffset = signedMinuteDelta(candidate, minuteOfDay(realTime));
    return candidateOffset <= maxOffsetMinutes();
}

void DriftClock::enforceStillnessCap(const ClockTime& realTime, unsigned long nowMs) {
    unsigned long maxStillMs = (unsigned long)maxStillMinutes() * 60UL * 1000UL;
    if (!elapsed(nowMs, _lastDisplayChangeMs + maxStillMs)) {
        return;
    }

    if (advanceDisplayed(realTime, nowMs, 1)) {
        scheduleEvent(realTime, nowMs);
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
    return minute == 0 || minute == 11 || minute == 22 ||
           minute == 30 || minute == 44 || minute == 59;
}

int DriftClock::distanceToNextAnchor(int displayedMinute, int maxDistance) {
    for (int distance = 1; distance <= maxDistance; distance++) {
        if (isAnchorMinute(displayedMinute + distance)) {
            return distance;
        }
    }
    return 0;
}

int DriftClock::randomStartOffsetMinutes() {
    int maxOffset = maxOffsetMinutes();
    return randomRange(0, maxOffset);
}

unsigned long DriftClock::cappedHoldMs(unsigned long nowMs, unsigned long lastChangeMs, int minSec, int maxSec) {
    unsigned long holdMs = (unsigned long)randomRange(minSec, maxSec) * 1000UL;
    unsigned long maxDeadline = lastChangeMs + (unsigned long)maxStillMinutes() * 60UL * 1000UL;
    unsigned long requestedDeadline = nowMs + holdMs;
    if (elapsed(requestedDeadline, maxDeadline)) {
        return elapsed(maxDeadline, nowMs) ? 0 : maxDeadline - nowMs;
    }
    return holdMs;
}

int DriftClock::maxOffsetMinutes() {
    return clampInt(DRIFT_MAX_OFFSET_MINUTES, 1, 12 * 60);
}

int DriftClock::maxStillMinutes() {
    return clampInt(DRIFT_MAX_STILL_MINUTES, 1, 60);
}

uint8_t DriftClock::personality() {
    return (DRIFT_PERSONALITY >= 0 && DRIFT_PERSONALITY <= 3) ? DRIFT_PERSONALITY : 0;
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
