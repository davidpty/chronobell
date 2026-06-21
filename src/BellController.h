#ifndef BELL_CONTROLLER_H
#define BELL_CONTROLLER_H

#include <Arduino.h>
#include "AppSettings.h"
#include "RtcClock.h"

class BellController {
public:
    void begin();
    void update(const ClockTime& currentTime, bool timeValid, BellMode mode,
                bool countdownExpired, bool muteAutomatic = false,
                bool suppressScheduledStrike = false);
    void preview(BellMode mode, const ClockTime& currentTime, bool timeValid);
    void queueCountdownAlert();
    void queueNewYearAlert();
    void queueCountdownStartAlert();
    void stop();
    bool isBusy() const;

private:
    static uint8_t hourCount12(int hours);
    static uint8_t shipBellCount(int hours, int minutes);
    static uint8_t computeStrikesForEvent(BellMode mode, int eventH, int eventM, uint8_t& strikeGroupSize);
    static bool computeMostRecentEventTime(BellMode mode, int currentH, int currentM, int& eventH, int& eventM);
    void queue(uint8_t count, uint8_t groupSize, bool force, const char* label = nullptr);
    void queuePattern(uint8_t count, const uint8_t* groupPattern, uint8_t patternCount, bool force, const char* label = nullptr);
    void queuePreview(uint8_t count, uint8_t groupSize);
    void updateSequence();
    void triggerPulse();
    void updatePulse();

    unsigned long _pulseStartMs = 0;
    bool _pulseActive = false;
    bool _sequenceActive = false;
    uint8_t _sequenceGroupSize = 0;
    const uint8_t* _sequenceGroupPattern = nullptr;
    uint8_t _sequenceGroupPatternCount = 0;
    uint8_t _sequenceGroupPatternIndex = 0;
    uint8_t _sequenceGroupProgress = 0;
    uint8_t _sequenceTotal = 0;
    uint8_t _sequenceRemaining = 0;
    uint8_t _sequenceIndex = 0;
    unsigned long _nextStrikeMs = 0;
    int _lastEventKey = -1;
};

#endif
