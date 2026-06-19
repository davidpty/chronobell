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
    int driftDirection() const;
    bool displayedMinuteFresh(unsigned long nowMs) const;
    bool separatorVisible() const;

private:
    enum class Phase : uint8_t {
        Away,
        Return,
        InitialToBehind,
        BehindToAhead,
        AheadToBehind
    };

    void initialize(const ClockTime& realTime, unsigned long nowMs);
    void finishCurrentPhase();
    Phase nextPhase() const;
    double phaseProgressSeconds(unsigned long elapsedMs) const;
    unsigned long currentPhaseDurationSeconds() const;

    static int secondOfDay(const ClockTime& time);
    static int wrapSecondOfDay(int64_t second);
    static int signedSecondDelta(int fromSecond, int toSecond);
    static int maxOffsetSeconds();
    static int requestedPhaseSeconds();
    static int effectivePhaseSeconds();
    static int tempoVariationPercent();
    static int directionMode();
    static int directionSign();
    static int clampInt(int value, int minValue, int maxValue);

    bool _initialized = false;
    Phase _phase = Phase::Away;
    int _displayedSecond = 0;
    int _lastRealSecond = 0;
    int64_t _phaseStartDisplayedSecond = 0;
    unsigned long _phaseStartMs = 0;
    unsigned long _lastUpdateMs = 0;
    unsigned long _lastDisplayChangeMs = 0;
};

#endif // DRIFT_TIME_MODEL_H
