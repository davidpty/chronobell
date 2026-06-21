#include "NewYearRenderer.h"

#include <stdio.h>

#include "Config.h"
#include "Display.h"
#include "NewYearController.h"

#if FEATURE_NEW_YEAR

void NewYearRenderer::init(Display& display, NewYearController& controller) {
    _display = &display;
    _controller = &controller;
}

void NewYearRenderer::drawSparkles() {
    if (!_display || !_controller) return;

    // Shape definitions: pixel offset lists ordered center → partial → full
    static const int8_t SHAPE_PX[6][9][2] = {
        {{0,0}},                                           // Dot
        {{0,0}, {0,-1}, {0,1}},                            // VBar
        {{0,0}, {-1,0}, {1,0}},                            // HBar
        {{0,0}, {0,-1}, {0,1}, {-1,0}, {1,0}},            // Cross
        {{0,0}, {-1,-1}, {1,1}, {-1,1}, {1,-1}},          // X
        {{0,0}, {-1,-1}, {1,-1}, {-1,1}, {1,1},           // Ring
         {0,-1}, {1,0}, {0,1}, {-1,0}},
    };
    static const uint8_t SHAPE_PC[6] = {1, 3, 3, 5, 5, 9};
    static const uint16_t SHAPE_MS[6] = {180, 300, 300, 450, 550, 450};

    const uint8_t count = _controller->particleCount();
    const uint32_t now = _controller->phaseMilliseconds();
    const uint16_t period = _controller->accentPeriodMs();
    if (count == 0 || period == 0) return;

    const int eventKey = _controller->eventKey();
    const bool takeover = _controller->takesOverDisplay();

    uint8_t maxShape;
    if (_controller->phase() == NewYearPhase::Ambient && now < 7200000UL) {
        uint32_t hourIdx = now / 3600000UL;
        static const uint8_t MAX_SHAPE[] = {0, 1};
        if (hourIdx > 1) hourIdx = 1;
        maxShape = MAX_SHAPE[hourIdx];
    } else {
        maxShape = 5;
    }

    uint8_t activeCount = 0;

    for (uint8_t i = 0; i < count; ++i) {
        uint32_t cycle = now / period;
        uint32_t r = (uint32_t)eventKey ^ (i * 314159U) ^ (cycle * 1234567U);
        uint32_t offset = r % period;
        uint32_t local = (now + period - offset) % period;

        uint8_t sx = (r % (COLS_PER_ROW - 2)) + 1;
        uint8_t sy = ((r >> 8) % (TOTAL_ROWS - 2)) + 1;
        uint8_t shape = (r >> 16) % (maxShape + 1);
        uint16_t dur = SHAPE_MS[shape];

        if (local >= dur) continue;
        activeCount++;

        uint8_t totalPx = SHAPE_PC[shape];
        uint8_t halfPx = (totalPx + 1) / 2;
        uint16_t t1 = dur * 20 / 100;
        uint16_t t2 = dur * 35 / 100;
        uint16_t t3 = dur * 65 / 100;
        uint16_t t4 = dur * 80 / 100;

        uint8_t drawCount;
        if (local < t1) {
            drawCount = 1;
        } else if (local < t2) {
            drawCount = halfPx;
        } else if (local < t3) {
            drawCount = totalPx;
        } else if (local < t4) {
            drawCount = halfPx;
        } else {
            drawCount = 1;
        }

        for (uint8_t p = 0; p < drawCount; p++) {
            uint8_t px = sx + SHAPE_PX[shape][p][0];
            uint8_t py = sy + SHAPE_PX[shape][p][1];
            if (takeover) {
                _display->setPixel(px, py, true);
            } else {
                _display->togglePixel(px, py);
            }
        }
    }

    // Burst fires once per quiet→active transition
    bool minimalPhase = _controller->phase() == NewYearPhase::Ambient
                     && _controller->phaseMilliseconds() < 7200000UL;
    if (!minimalPhase && activeCount > 0 && _lastActiveCount == 0) {
        _burstBoost = activeCount;
        if (_burstBoost > 7) _burstBoost = 7;
        _burstFrames = 5;
    }
    _lastActiveCount = activeCount;

    if (_burstFrames > 0) {
        _burstFrames--;
        uint8_t easeBoost = (uint8_t)(_burstBoost * (_burstFrames + 1U) / 6U + 0.5f);
        _display->applyBurstBoost(easeBoost);
    } else {
        _display->applyBurstBoost(0);
    }
}

void NewYearRenderer::renderOverlay() {
    drawSparkles();
}

void NewYearRenderer::drawCountdown() {
    uint32_t remainingSeconds = (_controller->millisecondsToMidnight() + 999UL) / 1000UL;

    if (remainingSeconds <= 10) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)remainingSeconds);
        _display->drawCenteredBigText(buf, 0);

        if (remainingSeconds >= 1 && remainingSeconds <= 3) {
            _display->applyBurstBoost(12 - remainingSeconds * 2);
            return;
        }
        drawSparkles();
        return;
    }

    if (remainingSeconds <= 60) {
        uint32_t minutes = remainingSeconds / 60UL;
        uint32_t seconds = remainingSeconds % 60UL;
        char text[12];
        snprintf(text, sizeof(text), "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
        _display->drawCenteredMediumText(text, 3);
        drawSparkles();
        return;
    }

    uint32_t minutes = remainingSeconds / 60UL;
    uint32_t seconds = remainingSeconds % 60UL;
    char text[12];
    snprintf(text, sizeof(text), "T-%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
    _display->drawCenteredSmallText(text, 5);
}

void NewYearRenderer::drawCelebration() {
    uint32_t slot = (_controller->phaseMilliseconds() / 3000UL) % 3UL;
    if (slot == 0) {
        _display->drawCenteredSmallText("HAPPY", 5);
    } else if (slot == 1) {
        _display->drawCenteredSmallText("NEW", 0);
        _display->drawCenteredSmallText("YEAR", 10);
    } else {
        char year[6];
        snprintf(year, sizeof(year), "%d", _controller->incomingYear());
        _display->drawCenteredMediumText(year, 3);
    }
    drawSparkles();
}

void NewYearRenderer::renderTakeover() {
    if (!_display || !_controller) return;
    _display->clearBuffer();

    if (_controller->phase() == NewYearPhase::Celebration) {
        if (!_didMidnightFlash) {
            _didMidnightFlash = true;
            for (uint8_t y = 0; y < TOTAL_ROWS; y++) {
                for (uint8_t x = 0; x < COLS_PER_ROW; x++) {
                    _display->setPixel(x, y, true);
                }
            }
            _display->applyBurstBoost(15);
        }
        drawCelebration();
    } else {
        _didMidnightFlash = false;
        drawCountdown();
        if (_controller->phase() != NewYearPhase::FinalTenMinutes) {
            drawSparkles();
        }
    }
}

#endif
