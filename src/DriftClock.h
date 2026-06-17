#ifndef DRIFT_CLOCK_H
#define DRIFT_CLOCK_H

#include <Arduino.h>

#include "RtcClock.h"

class DriftClock {
public:
    void update(const ClockTime& realTime, unsigned long nowMs);
    void reset(const ClockTime& realTime, unsigned long nowMs);
    void activate(const ClockTime& realTime, unsigned long nowMs);

    ClockTime displayTime(const ClockTime& realTime, unsigned long nowMs) const;
    int offsetMinutes(const ClockTime& realTime) const;
    bool displayedMinuteFresh(unsigned long nowMs) const;
    unsigned long separatorBlinkHalfPeriodMs(unsigned long nowMs) const;

private:
    enum class Phase : uint8_t {
        Hold,
        Nudge,
        DoubleStep,
        CatchUp,
        AnchorHold,
        WaitCorrection
    };

    void initialize(const ClockTime& realTime, unsigned long nowMs, bool randomizeStart);
    void scheduleEvent(const ClockTime& realTime, unsigned long nowMs);
    void scheduleHold(unsigned long nowMs, unsigned long holdMs, Phase phase);
    void startCatchUp(const ClockTime& realTime, unsigned long nowMs);
    void decideNextEvent(const ClockTime& realTime, unsigned long nowMs);
    void processCatchUp(const ClockTime& realTime, unsigned long nowMs);
    bool advanceDisplayed(const ClockTime& realTime, unsigned long nowMs, int steps = 1);
    bool canAdvance(const ClockTime& realTime, int steps = 1) const;
    void enforceStillnessCap(const ClockTime& realTime, unsigned long nowMs);

    static int minuteOfDay(const ClockTime& time);
    static int wrapMinuteOfDay(int minute);
    static int signedMinuteDelta(int fromMinute, int toMinute);
    static bool isAnchorMinute(int displayedMinute);
    static int distanceToNextAnchor(int displayedMinute, int maxDistance);
    static int randomStartOffsetMinutes();
    static unsigned long cappedHoldMs(unsigned long nowMs, unsigned long lastChangeMs, int minSec, int maxSec);
    static int maxOffsetMinutes();
    static int maxStillMinutes();
    static uint8_t personality();
    static int randomRange(int minValue, int maxValue);
    static int clampInt(int value, int minValue, int maxValue);

    bool _initialized = false;
    Phase _phase = Phase::Hold;
    int _displayedMinute = 0;
    int _lastRealMinute = 0;
    int _targetOffset = 0;
    unsigned long _eventDeadlineMs = 0;
    unsigned long _nextCatchUpStepMs = 0;
    unsigned long _lastDisplayChangeMs = 0;
    uint8_t _catchUpStepsRemaining = 0;
};

#endif // DRIFT_CLOCK_H
