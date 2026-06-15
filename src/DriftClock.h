#ifndef DRIFT_CLOCK_H
#define DRIFT_CLOCK_H

#include <Arduino.h>

#include "RtcClock.h"

class DriftClock {
public:
    void update(const ClockTime& realTime, unsigned long nowMs);
    void reset(const ClockTime& realTime, unsigned long nowMs);

    ClockTime displayTime(const ClockTime& realTime, unsigned long nowMs) const;
    int offsetMinutes(const ClockTime& realTime) const;
    bool displayedMinuteFresh(unsigned long nowMs) const;

private:
    enum class Phase : uint8_t {
        Hold,
        Rush
    };

    void scheduleHold(unsigned long nowMs, int landedMinute = -1);
    void startRush(const ClockTime& realTime, unsigned long nowMs);
    void decideNextEvent(const ClockTime& realTime, unsigned long nowMs);
    void moveDisplayedMinute(const ClockTime& realTime, bool allowJump, unsigned long nowMs);
    void clampToRealTime(const ClockTime& realTime, unsigned long nowMs);

    static int minuteOfDay(const ClockTime& time);
    static int wrapMinuteOfDay(int minute);
    static int signedMinuteDelta(int fromMinute, int toMinute);
    static bool isAnchorMinute(int displayedMinute);
    static int anchorHoldMultiplier(int displayedMinute);
    static int randomRange(int minValue, int maxValue);
    static int clampInt(int value, int minValue, int maxValue);

    bool _initialized = false;
    Phase _phase = Phase::Hold;
    int _displayedMinute = 0;
    int _lastRealMinute = 0;
    unsigned long _holdUntilMs = 0;
    unsigned long _nextRushStepMs = 0;
    unsigned long _lastDisplayChangeMs = 0;
    uint8_t _rushStepsRemaining = 0;
};

#endif // DRIFT_CLOCK_H
