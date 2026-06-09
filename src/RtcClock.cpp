#include "RtcClock.h"

#include <Wire.h>
#include "Config.h"

bool RtcClock::begin() {
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) {
        _available = false;
        return false;
    }

    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(0);
    if (Wire.endTransmission() != 0) {
        _available = false;
        return false;
    }

    Wire.requestFrom(RTC_I2C_ADDRESS, 1);
    if (Wire.available()) {
        uint8_t seconds = Wire.read();
        if (seconds & 0x80) {
            LOGLN("RTC clock was stopped, starting it...");
            Wire.beginTransmission(RTC_I2C_ADDRESS);
            Wire.write(0);
            Wire.write(seconds & 0x7F);
            Wire.endTransmission();
        }
    }

    _available = true;
    return true;
}

bool RtcClock::available() const {
    return _available;
}

bool RtcClock::read() {
    if (!_available) return false;

    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(0);
    if (Wire.endTransmission() != 0) {
        _available = false;
        LOGLN("RTC read failed");
        return false;
    }

    Wire.requestFrom(RTC_I2C_ADDRESS, 7);
    if (Wire.available() < 7) {
        return false;
    }

    uint8_t seconds = Wire.read();
    uint8_t minutes = Wire.read();
    uint8_t hours = Wire.read();
    uint8_t day = Wire.read();
    uint8_t date = Wire.read();
    uint8_t month = Wire.read();
    uint8_t year = Wire.read();

    _time.seconds = ((seconds & 0x70) >> 4) * 10 + (seconds & 0x0F);
    _time.minutes = ((minutes & 0x70) >> 4) * 10 + (minutes & 0x0F);
    _time.hours = ((hours & 0x30) >> 4) * 10 + (hours & 0x0F);
    _date.day = day;
    _date.date = ((date & 0x30) >> 4) * 10 + (date & 0x0F);
    _date.month = ((month & 0x10) >> 4) * 10 + (month & 0x0F);
    _date.year = 2000 + (((year & 0xF0) >> 4) * 10 + (year & 0x0F));

    if (hours & 0x40) {
        if (hours & 0x20) {
            _time.hours += 12;
        }
    }

    _lastUpdateMs = millis();
    return true;
}

void RtcClock::tick() {
    if (!_available) return;

    unsigned long currentMs = millis();
    unsigned long elapsedMs = currentMs - _lastUpdateMs;
    if (elapsedMs < 1000UL) return;

    unsigned long elapsedSeconds = elapsedMs / 1000UL;
    _lastUpdateMs += elapsedSeconds * 1000UL;

    while (elapsedSeconds-- > 0) {
        _time.seconds++;
        if (_time.seconds >= 60) {
            _time.seconds = 0;
            _time.minutes++;
            if (_time.minutes >= 60) {
                _time.minutes = 0;
                _time.hours++;
                if (_time.hours >= 24) {
                    _time.hours = 0;
                    advanceDateOneDay();
                }
            }
        }
    }
}

void RtcClock::setFromEpoch(time_t epochTime) {
    if (!_available) return;

    struct tm *timeinfo = localtime(&epochTime);
    if (!timeinfo) return;

    uint8_t seconds = timeinfo->tm_sec;
    uint8_t minutes = timeinfo->tm_min;
    uint8_t hours = timeinfo->tm_hour;
    uint8_t day = timeinfo->tm_wday + 1;
    uint8_t date = timeinfo->tm_mday;
    uint8_t month = timeinfo->tm_mon + 1;
    uint8_t year = timeinfo->tm_year - 100;

    Wire.beginTransmission(RTC_I2C_ADDRESS);
    Wire.write(0);
    Wire.write(((seconds / 10) << 4) | (seconds % 10));
    Wire.write(((minutes / 10) << 4) | (minutes % 10));
    Wire.write(((hours / 10) << 4) | (hours % 10));
    Wire.write(day);
    Wire.write(((date / 10) << 4) | (date % 10));
    Wire.write(((month / 10) << 4) | (month % 10));
    Wire.write(((year / 10) << 4) | (year % 10));
    Wire.endTransmission();

    read();
    LOGLN("RTC updated successfully");
}

ClockTime RtcClock::getTime() const {
    return _time;
}

ClockDate RtcClock::getDate() const {
    return _date;
}

uint16_t RtcClock::milliseconds() const {
    return (uint16_t)((millis() - _lastUpdateMs) % 1000UL);
}

bool RtcClock::isLeapYear(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t RtcClock::daysInMonth(int month, int year) {
    static const uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    if (month < 1 || month > 12) {
        return 31;
    }
    return DAYS[month - 1];
}

void RtcClock::advanceDateOneDay() {
    _date.day++;
    if (_date.day > 7) _date.day = 1;

    _date.date++;
    if (_date.date > daysInMonth(_date.month, _date.year)) {
        _date.date = 1;
        _date.month++;
        if (_date.month > 12) {
            _date.month = 1;
            _date.year++;
        }
    }
}
