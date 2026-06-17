#ifndef DRIFT_TIME_MODEL_H
#define DRIFT_TIME_MODEL_H

#include <Arduino.h>

#include "RtcClock.h"

class DriftTimeModel {
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
        Away,
        Return
    };

    void initialize(const ClockTime& realTime, unsigned long nowMs);
    void beginPhase(Phase phase, const ClockTime& realTime, unsigned long nowMs);
    void advanceDisplayedSecond(unsigned long nowMs);
    void scheduleNextDisplayedSecond(const ClockTime& realTime, unsigned long nowMs);
    unsigned long nextDisplayedSecondDurationMs(const ClockTime& realTime, unsigned long nowMs) const;
    int targetDisplayedProgressSeconds() const;
    int displayedProgressSeconds() const;
    float jitterMultiplier(unsigned long nowMs) const;

    static int minuteOfDay(const ClockTime& time);
    static int secondOfDay(const ClockTime& time);
    static int wrapMinuteOfDay(int minute);
    static int wrapSecondOfDay(int second);
    static int signedMinuteDelta(int fromMinute, int toMinute);
    static int signedSecondDelta(int fromSecond, int toSecond);
    static int maxOffsetMinutes();
    static int maxOffsetSeconds();
    static int phaseSeconds();
    static int jitterPercent();
    static int directionSign();
    static int clampInt(int value, int minValue, int maxValue);

    bool _initialized = false;
    Phase _phase = Phase::Away;
    int _displayedSecond = 0;
    int _phaseDisplayedProgress = 0;
    int _lastRealSecond = 0;
    unsigned long _phaseStartMs = 0;
    unsigned long _nextDisplayedSecondMs = 0;
    unsigned long _lastDisplayChangeMs = 0;
    unsigned long _lastSecondDurationMs = 1000;
    float _jitterPhaseA = 0.0f;
    float _jitterPhaseB = 0.0f;
};

#endif // DRIFT_TIME_MODEL_H
