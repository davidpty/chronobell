#ifndef NEW_YEAR_RENDERER_H
#define NEW_YEAR_RENDERER_H

#include <Arduino.h>

#include "Config.h"
#if DIGIT_TRANSITIONS
#include "DigitTransition.h"
#endif

class Display;
class NewYearController;

#if FEATURE_NEW_YEAR

enum NyeContentMode : uint8_t {
    NYE_MODE_NONE = 0,
    NYE_MODE_SMALL,
    NYE_MODE_MEDIUM,
    NYE_MODE_BIG,
    NYE_MODE_CELEBRATION_HAPPY,
    NYE_MODE_CELEBRATION_NEW_YEAR,
    NYE_MODE_CELEBRATION_YEAR,
};

class NewYearRenderer {
public:
    void init(Display& display, NewYearController& controller);
    void renderOverlay();
    void renderTakeover();

private:
    void drawSparkles();
    void drawCelebration();
    void drawAnimatedCountdown();
#if DIGIT_TRANSITIONS
    void drawAnimatedSmallDigit(int x, int y, uint8_t digit,
                                digit_transition::DigitCellState& state,
                                unsigned long nowMs);
    void drawAnimatedBigDigit(int x, int y, uint8_t digit,
                              digit_transition::DigitCellState& state,
                              unsigned long nowMs);
    void drawAnimatedCenteredSmallText(const char* s, int y);
    void drawAnimatedCenteredBigText(const char* s, int y);
#endif

    Display* _display = nullptr;
    NewYearController* _controller = nullptr;
    bool _didMidnightFlash = false;
    uint8_t _lastActiveCount = 0;
    uint8_t _burstFrames = 0;
    uint8_t _burstFramesMax = 0;
    uint8_t _burstBoost = 0;
    NyeContentMode _lastContentMode = NYE_MODE_NONE;
#if DIGIT_TRANSITIONS
    static const uint8_t NYE_SMALL_DIGITS = 4;
    static const uint8_t NYE_BIG_DIGITS = 2;
    digit_transition::DigitCellState _smallDigitStates[NYE_SMALL_DIGITS];
    digit_transition::DigitCellState _bigDigitStates[NYE_BIG_DIGITS];
#endif
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
