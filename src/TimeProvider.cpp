#include "TimeProvider.h"

#include <string.h>

void TimeProvider::begin(RtcClock& rtcClock, NTPClient& timeClient) {
    _rtcClock = &rtcClock;
    _timeClient = &timeClient;
}

bool TimeProvider::readRtc() {
    if (!_rtcClock) {
        return false;
    }
    if (!_rtcClock->available()) {
        return false;
    }
    if (!_rtcClock->read()) {
        return false;
    }
    return true;
}

void TimeProvider::updateRtcTracking() {
    if (!_rtcClock || !_rtcClock->available()) {
        return;
    }
    _rtcClock->tick();
}

void TimeProvider::setRtcFromEpoch(time_t epochTime) {
    if (!_rtcClock || !_rtcClock->available()) {
        return;
    }
    _rtcClock->setFromEpoch(epochTime);
}

bool TimeProvider::currentEpoch(time_t& epochTime) const {
    if (_rtcClock && _rtcClock->available()) {
        ClockDate date = _rtcClock->getDate();
        ClockTime time = _rtcClock->getTime();
        if (date.year < 2024 || date.year > 2099 ||
            date.month < 1 || date.month > 12 ||
            date.date < 1 || date.date > 31 ||
            time.hours < 0 || time.hours > 23 ||
            time.minutes < 0 || time.minutes > 59 ||
            time.seconds < 0 || time.seconds > 59) {
            return false;
        }

        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_year = date.year - 1900;
        tm.tm_mon = date.month - 1;
        tm.tm_mday = date.date;
        tm.tm_hour = time.hours;
        tm.tm_min = time.minutes;
        tm.tm_sec = time.seconds;
        tm.tm_isdst = -1;
        epochTime = mktime(&tm);
        return epochTime > 0;
    }

    if (_timeClient && _timeClient->isTimeSet()) {
        epochTime = (time_t)_timeClient->getEpochTime();
        return epochTime > 0;
    }

    return false;
}

bool TimeProvider::currentTime(ClockTime& time) const {
    if (_rtcClock && _rtcClock->available()) {
        time = _rtcClock->getTime();
        return true;
    }

    if (_timeClient && _timeClient->isTimeSet()) {
        time.hours = _timeClient->getHours();
        time.minutes = _timeClient->getMinutes();
        time.seconds = _timeClient->getSeconds();
        return true;
    }

    return false;
}

ClockTime TimeProvider::displayTime() const {
    ClockTime time;
    if (currentTime(time)) {
        return time;
    }

    if (_timeClient) {
        time.hours = _timeClient->getHours();
        time.minutes = _timeClient->getMinutes();
        time.seconds = _timeClient->getSeconds();
    }
    return time;
}

bool TimeProvider::currentDate(ClockDate& date) const {
    if (_rtcClock && _rtcClock->available()) {
        date = _rtcClock->getDate();
        return date.month >= 1 && date.month <= 12 && date.date >= 1 && date.date <= 31;
    }

    if (_timeClient && _timeClient->isTimeSet()) {
        time_t epoch = (time_t)_timeClient->getEpochTime();
        struct tm* timeinfo = gmtime(&epoch);
        if (!timeinfo) {
            return false;
        }
        date.day = timeinfo->tm_wday + 1;
        date.date = timeinfo->tm_mday;
        date.month = timeinfo->tm_mon + 1;
        date.year = timeinfo->tm_year + 1900;
        return true;
    }

    return false;
}

uint16_t TimeProvider::milliseconds() const {
    if (_rtcClock && _rtcClock->available()) {
        return _rtcClock->milliseconds();
    }
    return (uint16_t)(millis() % 1000UL);
}
