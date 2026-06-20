#ifndef TIMER_RENDERER_H
#define TIMER_RENDERER_H

#include <Arduino.h>

#include "DigitTransition.h"
#include "TimerController.h"

class ClockRenderer;
class Display;

class TimerRenderer {
public:
    TimerRenderer() = default;

    // Two-phase init: called by Display::begin() after Display's own
    // members are constructed.
    void init(Display& display,
              TimerController& timer,
              ClockRenderer& clockRenderer);

    // Top-level: renders the current timer screen (stopwatch / countdown
    // / alert) and flushes the buffer. Called by Display::showTime().
    void renderTimerScreen();

    // Renders the date view.
    void renderDateView();

private:
    void drawTimerDuration(uint32_t totalSeconds, bool blinkSeparator, uint8_t transitionKind);
    void drawStopwatchTime(uint64_t totalSec, uint8_t centisec, bool running);
    void renderStopwatch();
    void renderCountdown();
    void renderCountdownAlert();

    Display* _display = nullptr;
    TimerController* _timer = nullptr;
    ClockRenderer* _clockRenderer = nullptr;
#if DIGIT_TRANSITIONS
    digit_transition::DigitCellState _digitStates[5];
    uint8_t _transitionKind = 0xFF;
    uint8_t _transitionLayout = 0xFF;
#endif
};

#endif // TIMER_RENDERER_H
