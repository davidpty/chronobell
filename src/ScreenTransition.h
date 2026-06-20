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
    void start(const uint32_t oldFrame[16], const uint32_t newFrame[16], uint32_t nowMs);
    bool render(uint32_t nowMs, uint32_t outputFrame[16]);
    bool active() const { return _active; }

    static void clearFrame(uint32_t frame[16]);
    static void copyFrame(uint32_t destination[16], const uint32_t source[16]);
    static bool getPixelFromFrame(const uint32_t frame[16], uint8_t x, uint8_t y);
    static void setPixelInFrame(uint32_t frame[16], int x, int y);

private:
    bool renderRetune(uint32_t nowMs, uint32_t outputFrame[16]);

    bool _active = false;
    uint32_t _oldFrame[16] = {};
    uint32_t _newFrame[16] = {};
    uint32_t _startMs = 0;
    uint16_t _durationMs = SCREEN_TRANSITION_MS;
    ScreenTransitionType _type = ScreenTransitionType::None;
    ScreenTransitionType _preferredType = ScreenTransitionType::Retune;
};

#endif
