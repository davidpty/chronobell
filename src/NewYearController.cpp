#include "NewYearController.h"

#include "Config.h"

#if ENABLE_NEW_YEAR_EASTER_EGG

namespace {
#if defined(ESP32)
RTC_DATA_ATTR int g_lastNewYearBellKey = 0;
#else
int g_lastNewYearBellKey = 0;
#endif
}

bool NewYearController::isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t NewYearController::daysInMonth(int month, int year) {
    static const uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) return 29;
    if (month < 1 || month > 12) return 31;
    return DAYS[month - 1];
}

int NewYearController::eventKeyForDate(const ClockDate& date) {
#if NEW_YEAR_DAILY_TEST
    return date.year * 10000 + date.month * 100 + date.date;
#else
    return date.year;
#endif
}

int NewYearController::previousDateKey(const ClockDate& date) {
    ClockDate previous = date;
    previous.date--;
    if (previous.date < 1) {
        previous.month--;
        if (previous.month < 1) {
            previous.month = 12;
            previous.year--;
        }
        previous.date = daysInMonth(previous.month, previous.year);
    }
    return eventKeyForDate(previous);
}

void NewYearController::update(const ClockDate& date, const ClockTime& time, uint16_t milliseconds) {
    const uint32_t secondOfDay = (uint32_t)time.hours * 3600UL +
                                 (uint32_t)time.minutes * 60UL +
                                 (uint32_t)time.seconds;
    const bool eveDate = NEW_YEAR_DAILY_TEST || (date.month == 12 && date.date == 31);
    const bool celebrationDate = NEW_YEAR_DAILY_TEST || (date.month == 1 && date.date == 1);
    const bool beforeMidnight = eveDate && time.hours >= 18;
    const bool afterMidnight = celebrationDate && secondOfDay < 120UL;

    if (!beforeMidnight && !afterMidnight) {
        _phase = NewYearPhase::Inactive;
        _phaseMs = 0;
        _remainingMs = 0;
        _eventKey = 0;
        _midnightBellPending = false;
        return;
    }

    if (beforeMidnight) {
        _eventKey = eventKeyForDate(date);
        _incomingYear = (date.month == 12 && date.date == 31) ? date.year + 1 : date.year;
        _remainingMs = (86400UL - secondOfDay) * 1000UL - milliseconds;

        if (secondOfDay >= 23UL * 3600UL + 59UL * 60UL + 50UL) {
            _phase = NewYearPhase::FinalTenSeconds;
            _phaseMs = (secondOfDay - (23UL * 3600UL + 59UL * 60UL + 50UL)) * 1000UL + milliseconds;
        } else if (secondOfDay >= 23UL * 3600UL + 59UL * 60UL) {
            _phase = NewYearPhase::FinalMinute;
            _phaseMs = (secondOfDay - (23UL * 3600UL + 59UL * 60UL)) * 1000UL + milliseconds;
        } else if (secondOfDay >= 23UL * 3600UL + 50UL * 60UL) {
            if (_phase != NewYearPhase::FinalTenMinutes) {
                _countdownStartPending = true;
            }
            _phase = NewYearPhase::FinalTenMinutes;
            _phaseMs = (secondOfDay - (23UL * 3600UL + 50UL * 60UL)) * 1000UL + milliseconds;
        } else {
            _phase = NewYearPhase::Ambient;
            _phaseMs = (secondOfDay - 18UL * 3600UL) * 1000UL + milliseconds;
        }
        return;
    }

    _eventKey = previousDateKey(date);
    _incomingYear = date.year;
    _phase = NewYearPhase::Celebration;
    _phaseMs = secondOfDay * 1000UL + milliseconds;
    _remainingMs = 0;

    if (secondOfDay <= 1UL && g_lastNewYearBellKey != _eventKey) {
        g_lastNewYearBellKey = _eventKey;
        _midnightBellPending = true;
    }
}

bool NewYearController::takesOverDisplay() const {
    switch (_phase) {
        case NewYearPhase::FinalTenMinutes:
            return (_phaseMs % 15000UL) >= 10000UL;
        case NewYearPhase::FinalMinute:
            return (_phaseMs % 10000UL) >= 5000UL;
        case NewYearPhase::FinalTenSeconds:
            return true;
        case NewYearPhase::Celebration:
            return (_phaseMs % 12000UL) < 9000UL;
        default:
            return false;
    }
}

bool NewYearController::shouldWakeDisplay() const {
#if NEW_YEAR_WAKE_DISPLAY
    return _phase == NewYearPhase::FinalTenSeconds || _phase == NewYearPhase::Celebration;
#else
    return false;
#endif
}

uint8_t NewYearController::particleCount() const {
    if (_phase == NewYearPhase::Celebration || _phase == NewYearPhase::FinalTenSeconds) return 12;
    if (_phase == NewYearPhase::FinalMinute || _phase == NewYearPhase::FinalTenMinutes) return 8;
    if (_phase != NewYearPhase::Ambient) return 0;

    uint32_t hourIntoSequence = _phaseMs / 3600000UL;
    static const uint8_t COUNTS[] = {1, 2, 3, 4, 6, 10};
    if (hourIntoSequence > 5) hourIntoSequence = 5;
    return COUNTS[hourIntoSequence];
}

uint16_t NewYearController::accentPeriodMs() const {
    if (_phase != NewYearPhase::Ambient) return 2000;
    uint32_t hourIntoSequence = _phaseMs / 3600000UL;
    static const uint16_t PERIODS[] = {15000, 12000, 10000, 8000, 6000, 4000};
    if (hourIntoSequence > 5) hourIntoSequence = 5;
    return PERIODS[hourIntoSequence];
}

int8_t NewYearController::boostedBrightness(int8_t normalBrightness, int8_t userBrightness) const {
#if !NEW_YEAR_BRIGHTNESS_BOOST
    return normalBrightness;
#else
    if (normalBrightness < 0) normalBrightness = 0;
    if (userBrightness < normalBrightness) return normalBrightness;

    uint32_t progress = 0;
    uint32_t total = 600000UL;
    if (_phase == NewYearPhase::FinalTenMinutes) {
        progress = _phaseMs;
    } else if (_phase == NewYearPhase::FinalMinute || _phase == NewYearPhase::FinalTenSeconds) {
        progress = total;
    } else if (_phase == NewYearPhase::Celebration) {
        if (_phaseMs < 110000UL) return userBrightness;
        uint32_t fade = _phaseMs - 110000UL;
        if (fade > 10000UL) fade = 10000UL;
        return (int8_t)(userBrightness - ((userBrightness - normalBrightness) * fade) / 10000UL);
    } else {
        return normalBrightness;
    }

    if (progress > total) progress = total;
    return (int8_t)(normalBrightness + ((userBrightness - normalBrightness) * progress) / total);
#endif
}

#endif
