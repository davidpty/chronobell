#ifndef PONG_CLOCK_RENDERER_H
#define PONG_CLOCK_RENDERER_H

#include "PongClock.h"

class Display;

class PongClockRenderer {
public:
    void render(Display& display, const PongClockEngine& engine, TimeFormat format);

private:
    void drawScore(Display& display, const PongClockEngine& engine, TimeFormat format);
    void drawPaddles(Display& display, const PongClockEngine::Snapshot& pong) const;
    void drawBall(Display& display, const PongClockEngine::Snapshot& pong) const;
    static int glyphWidth(uint8_t glyph);
};

#endif // PONG_CLOCK_RENDERER_H
