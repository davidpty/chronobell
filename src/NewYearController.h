#ifndef NEW_YEAR_CONTROLLER_H
#define NEW_YEAR_CONTROLLER_H

#include <Arduino.h>

#include "Config.h"
#include "RtcClock.h"

enum class NewYearPhase : uint8_t {
    Inactive = 0,
    Ambient,
    FinalTenMinutes,
    FinalMinute,
    FinalTenSeconds,
    Celebration,
};

#if FEATURE_NEW_YEAR

class NewYearController {
public:
    void update(const ClockDate& date, const ClockTime& time, uint16_t milliseconds);

    bool isActive() const { return _phase != NewYearPhase::Inactive; }
    bool isCelebrating() const { return _phase == NewYearPhase::Celebration; }
    bool takesOverDisplay() const;
    bool shouldWakeDisplay() const;
    bool hasMidnightBellRequest() const { return _midnightBellPending; }
    void resolveMidnightBellRequest() { _midnightBellPending = false; }
    bool hasCountdownTickRequest() const { return _countdownTickPending; }
    void resolveCountdownTickRequest() { _countdownTickPending = false; }
    bool hasCountdownSecondTickRequest() const { return _countdownSecondTickPending; }
    void resolveCountdownSecondTickRequest() { _countdownSecondTickPending = false; }
    bool hasCountdownTenSecRequest() const { return _countdownTenSecPending; }
    void resolveCountdownTenSecRequest() { _countdownTenSecPending = false; }

    NewYearPhase phase() const { return _phase; }
    uint8_t particleCount() const;
    uint16_t accentPeriodMs() const;
    uint32_t phaseMilliseconds() const { return _phaseMs; }
    uint32_t millisecondsToMidnight() const { return _remainingMs; }
    int incomingYear() const { return _incomingYear; }
    int eventKey() const { return _eventKey; }

    int8_t boostedBrightness(int8_t normalBrightness, int8_t userBrightness) const;

private:
    static int eventKeyForDate(const ClockDate& date);
    static int previousDateKey(const ClockDate& date);
    static bool isLeapYear(int year);
    static uint8_t daysInMonth(int month, int year);

    NewYearPhase _phase = NewYearPhase::Inactive;
    uint32_t _phaseMs = 0;
    uint32_t _remainingMs = 0;
    int _incomingYear = 0;
    int _eventKey = 0;
    bool _midnightBellPending = false;
    bool _countdownTickPending = false;
    bool _countdownSecondTickPending = false;
    bool _countdownTenSecPending = false;
    int8_t _lastCountdownMinute = -1;
    int8_t _lastCountdownSecond = -1;
};

#else

class NewYearController {
public:
    void update(const ClockDate&, const ClockTime&, uint16_t) {}
    bool isActive()               const { return false; }
    bool isCelebrating()          const { return false; }
    bool takesOverDisplay()       const { return false; }
    bool shouldWakeDisplay()      const { return false; }
    bool hasMidnightBellRequest() const { return false; }
    void resolveMidnightBellRequest() {}
    bool hasCountdownTickRequest() const { return false; }
    void resolveCountdownTickRequest() {}
    bool hasCountdownSecondTickRequest() const { return false; }
    void resolveCountdownSecondTickRequest() {}
    bool hasCountdownTenSecRequest() const { return false; }
    void resolveCountdownTenSecRequest() {}
    NewYearPhase phase()          const { return NewYearPhase::Inactive; }
    uint8_t particleCount()       const { return 0; }
    uint16_t accentPeriodMs()     const { return 0; }
    uint32_t phaseMilliseconds()  const { return 0; }
    uint32_t millisecondsToMidnight() const { return 0; }
    int incomingYear()            const { return 0; }
    int eventKey()                const { return 0; }
    int8_t boostedBrightness(int8_t normal, int8_t) const { return normal; }
};

#endif

#endif
