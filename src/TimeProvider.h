#ifndef TIME_PROVIDER_H
#define TIME_PROVIDER_H

#include <Arduino.h>
#include <NTPClient.h>
#include "RtcClock.h"

class TimeProvider {
public:
    void begin(RtcClock& rtcClock, NTPClient& timeClient);
    bool readRtc();
    void updateRtcTracking();
    void setRtcFromEpoch(time_t epochTime);
    bool currentTime(ClockTime& time) const;
    ClockTime displayTime() const;
    bool currentDate(ClockDate& date) const;
    uint16_t milliseconds() const;

private:
    RtcClock* _rtcClock = nullptr;
    NTPClient* _timeClient = nullptr;
};

#endif
