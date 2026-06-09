#ifndef TOUCH_CONTROLLER_H
#define TOUCH_CONTROLLER_H

#include <Arduino.h>

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
    bool available() const;
    bool isPressed(uint8_t pad) const;
    uint32_t heldMs(uint8_t pad) const;
    bool consumePress(uint8_t pad);
    bool consumeRelease(uint8_t pad);

private:
    bool _available = false;
    volatile uint8_t _touchStatus = 0;
    TouchPadConfig _configs[8];
    uint8_t _prevStatus = 0;
    uint8_t _prevRawStatus = 0;
    uint32_t _rawChangeMs[8] = {0};
    uint32_t _pressStartMs[8] = {0};
    bool _holdFired[8] = {false};
    uint32_t _lastRepeatMs[8] = {0};
    bool _repeatActive[8] = {false};
    bool _firstRead = true;
    uint8_t _pressEdgeMask = 0;
    uint8_t _releaseEdgeMask = 0;
};

#endif
