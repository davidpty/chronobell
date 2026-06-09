#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <Arduino.h>
#include <time.h>

struct ClockTime {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
};

struct ClockDate {
    int day = 1;
    int date = 1;
    int month = 1;
    int year = 2000;
};

class RtcClock {
public:
    bool begin();
    bool available() const;
    bool read();
    void tick();
    void setFromEpoch(time_t epochTime);
    ClockTime getTime() const;
    ClockDate getDate() const;
    uint16_t milliseconds() const;

private:
    static bool isLeapYear(int year);
    static uint8_t daysInMonth(int month, int year);
    void advanceDateOneDay();

    bool _available = false;
    ClockTime _time;
    ClockDate _date;
    unsigned long _lastUpdateMs = 0;
};

#endif
