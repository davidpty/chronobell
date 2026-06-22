#ifndef SCREEN_TRANSITION_H
#define SCREEN_TRANSITION_H

#include <Arduino.h>
#include "Config.h"

enum class ScreenTransitionType : uint8_t {
    None,
    Retune
};

class ScreenTransition {
public:
    void start(const uint32_t oldFrame[TOTAL_ROWS], const uint32_t newFrame[TOTAL_ROWS], uint32_t nowMs);
    bool render(uint32_t nowMs, uint32_t outputFrame[TOTAL_ROWS]);
    bool active() const { return _active; }

    static void clearFrame(uint32_t frame[TOTAL_ROWS]);
    static void copyFrame(uint32_t destination[TOTAL_ROWS], const uint32_t source[TOTAL_ROWS]);
    static bool getPixelFromFrame(const uint32_t frame[TOTAL_ROWS], uint8_t x, uint8_t y);
    static void setPixelInFrame(uint32_t frame[TOTAL_ROWS], int x, int y);

private:
    bool renderRetune(uint32_t nowMs, uint32_t outputFrame[TOTAL_ROWS]);

    bool _active = false;
    uint32_t _oldFrame[TOTAL_ROWS] = {};
    uint32_t _newFrame[TOTAL_ROWS] = {};
    uint32_t _startMs = 0;
    uint16_t _durationMs = SCREEN_TRANSITION_MS;
    ScreenTransitionType _type = ScreenTransitionType::None;
    ScreenTransitionType _preferredType = ScreenTransitionType::Retune;
};

#endif
