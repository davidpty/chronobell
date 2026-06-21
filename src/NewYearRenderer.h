#ifndef NEW_YEAR_RENDERER_H
#define NEW_YEAR_RENDERER_H

#include <Arduino.h>

#include "Config.h"

class Display;
class NewYearController;

#if FEATURE_NEW_YEAR

class NewYearRenderer {
public:
    void init(Display& display, NewYearController& controller);
    void renderOverlay();
    void renderTakeover();

private:
    void drawSparkles();
    void drawCountdown();
    void drawCelebration();

    Display* _display = nullptr;
    NewYearController* _controller = nullptr;
    bool _didMidnightFlash = false;
    uint8_t _lastActiveCount = 0;
    uint8_t _burstFrames = 0;
    uint8_t _burstBoost = 0;
};

#else

class NewYearRenderer {
public:
    void init(Display&, NewYearController&) {}
    void renderOverlay() {}
    void renderTakeover() {}
};

#endif

#endif
