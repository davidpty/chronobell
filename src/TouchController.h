#ifndef TOUCH_CONTROLLER_H
#define TOUCH_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

struct TouchPadConfig {
    void (*onPress)(uint8_t pad) = nullptr;
    void (*onRelease)(uint8_t pad) = nullptr;
    void (*onHold)(uint8_t pad) = nullptr;
    void (*onRepeat)(uint8_t pad) = nullptr;
    uint32_t holdMs = 0;
    uint32_t repeatInitialDelayMs = 0;
    uint32_t repeatRateMs = 0;
};

class TouchController {
public:
    bool begin();
    void setHandler(uint8_t pad, const TouchPadConfig& config);
    void setPadRepeat(uint8_t pad, void (*onRepeat)(uint8_t), uint32_t initialDelayMs, uint32_t rateMs);
    void update();
    bool isPressed(uint8_t pad) const;
    uint32_t heldMs(uint8_t pad) const;

private:
    bool _available = false;
    volatile uint8_t _touchStatus = 0;
    TouchPadConfig _configs[CAP1188_NUM_TOUCHES];
    uint8_t _prevStatus = 0;
    uint8_t _prevRawStatus = 0;
    uint32_t _rawChangeMs[CAP1188_NUM_TOUCHES] = {0};
    uint32_t _pressStartMs[CAP1188_NUM_TOUCHES] = {0};
    bool _holdFired[CAP1188_NUM_TOUCHES] = {false};
    uint32_t _lastRepeatMs[CAP1188_NUM_TOUCHES] = {0};
    bool _repeatActive[CAP1188_NUM_TOUCHES] = {false};
    bool _firstRead = true;
    uint8_t _pressEdgeMask = 0;
    uint8_t _releaseEdgeMask = 0;
};

#endif
