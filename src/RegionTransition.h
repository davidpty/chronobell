#ifndef REGION_TRANSITION_H
#define REGION_TRANSITION_H

#include <Arduino.h>
#include "Config.h"

class Display;

#if REGION_TRANSITION

class RegionTransition {
public:
    RegionTransition();

    void setDurationMs(uint16_t ms);
    uint16_t getDurationMs() const { return _durationMs; }

    void start(unsigned long nowMs,
               int x, int y, int w, int h,
               const uint32_t oldRows[],
               bool centerOutward);

    bool render(unsigned long nowMs, Display& display);

    bool isActive() const { return _active; }
    void reset();

private:
    static constexpr int MAX_REGION_ROWS = 16;
    uint32_t _oldRows[MAX_REGION_ROWS];
    int _x, _y, _w, _h;
    unsigned long _startMs;
    uint16_t _durationMs;
    bool _active;
    bool _centerOutward;
};

#endif // REGION_TRANSITION
#endif // REGION_TRANSITION_H
