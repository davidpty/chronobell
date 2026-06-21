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

    static const int8_t SHAPE_PX[8][9][2] = {
        {{0,0}},
        {{0,0}, {-1,0}, {1,0}, {0,-1}},
        {{0,0}, {-1,0}, {1,0}, {0,1}},
        {{0,0}, {0,-1}, {0,1}, {-1,0}, {1,0}},
        {{0,0}, {-1,-1}, {1,1}, {-1,1}, {1,-1}},
        {{0,0}, {-1,-1}, {1,-1}, {-1,1}, {1,1},
         {0,-1}, {1,0}, {0,1}, {-1,0}},
        {{0,0}, {-1,-2}, {1,-2}, {-2,-1}, {2,-1},
         {-2,1}, {2,1}, {-1,2}, {1,2}},
        {{0,0}, {0,-2}, {-1,-1}, {1,-1},
         {-2,0}, {2,0}, {-1,1}, {1,1}, {0,2}},
    };
    static const uint8_t SHAPE_PC[8] = {1, 4, 4, 5, 5, 9, 9, 9};
    static const uint16_t SHAPE_MS[8] = {180, 300, 300, 450, 550, 450, 550, 550};

    const uint8_t count = _controller->particleCount();
    const uint32_t now = _controller->phaseMilliseconds();
    const uint16_t period = _controller->accentPeriodMs();
    if (count == 0 || period == 0) return;

    const int eventKey = _controller->eventKey();
    const bool takeover = _controller->takesOverDisplay();

    uint8_t maxShape;
    if (_controller->phase() == NewYearPhase::Ambient && now < 7200000UL) {
        uint32_t bucket = now / 1800000UL;
        static const uint8_t MAX_SHAPE[] = {0, 1, 2, 3};
        maxShape = (bucket > 3) ? 3 : MAX_SHAPE[bucket];
    } else if (_controller->phase() == NewYearPhase::Ambient) {
        maxShape = 7;
    } else {
        maxShape = 5;
    }

    uint8_t activeCount = 0;

    for (uint8_t i = 0; i < count; ++i) {
        uint32_t cycle = now / period;
        uint32_t r = (uint32_t)eventKey ^ (i * 314159U) ^ (cycle * 1234567U);
        uint32_t offset = r % period;
        uint32_t local = (now + period - offset) % period;

        uint8_t sx = (r % (COLS_PER_ROW - 4)) + 2;
        uint8_t sy = ((r >> 8) % (TOTAL_ROWS - 4)) + 2;
        uint8_t shape;
        if (maxShape >= 6 && (r & 0x3) == 0) {
            shape = 6 + ((r >> 16) & 1);
        } else {
            shape = (r >> 16) % ((maxShape + 1) < 6 ? (maxShape + 1) : 6);
        }
        uint16_t baseDur = SHAPE_MS[shape];
        uint16_t dur = baseDur * (65 + ((r >> 24) % 121)) / 100;

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

    bool minimalPhase = _controller->phase() == NewYearPhase::Ambient
                     && _controller->phaseMilliseconds() < 7200000UL;
    if (!minimalPhase && activeCount > _lastActiveCount) {
        uint8_t boost = activeCount < 7 ? activeCount : 7;
        if (boost < 3) boost = 3;
        int newFrames = boost * 50;
        if ((_burstFrames == 0 || _burstFrames < _burstFramesMax / 2) && newFrames > _burstFrames) {
            _burstBoost = boost;
            _burstFrames = newFrames;
            _burstFramesMax = newFrames;
        }
    }
    _lastActiveCount = activeCount;

    if (_burstFrames > 0) {
        _burstFrames--;
        uint8_t easeBoost = _burstFramesMax > 0
            ? (uint8_t)(_burstBoost * (_burstFrames + 1) / (_burstFramesMax + 1))
            : 0;
        _display->applyBurstBoost(easeBoost);
    } else {
        _display->applyBurstBoost(0);
    }
}

#if DIGIT_TRANSITIONS
void NewYearRenderer::drawAnimatedSmallDigit(int x, int y, uint8_t digit,
                                              digit_transition::DigitCellState& state,
                                              unsigned long nowMs) {
    auto writer = [this](int px, int py, bool on) { _display->setPixel(px, py, on); };
    auto frame = digit_transition::advance_cell(state, true, digit, nowMs);
    if (frame.transition) {
        digit_transition::draw_transition_glyph_forced(writer, FONT_SMALL,
            frame.fromVisible ? (int)frame.fromDigit : -1,
            frame.toVisible ? (int)frame.toDigit : -1,
            frame.progress, x, y);
    } else {
        digit_transition::draw_glyph(writer, FONT_SMALL, (int)digit, x, y);
    }
}

void NewYearRenderer::drawAnimatedBigDigit(int x, int y, uint8_t digit,
                                            digit_transition::DigitCellState& state,
                                            unsigned long nowMs) {
    auto writer = [this](int px, int py, bool on) { _display->setPixel(px, py, on); };
    auto frame = digit_transition::advance_cell(state, true, digit, nowMs);
    if (frame.transition) {
        digit_transition::draw_transition_glyph_forced(writer, FONT_BIG,
            frame.fromVisible ? (int)frame.fromDigit : -1,
            frame.toVisible ? (int)frame.toDigit : -1,
            frame.progress, x, y);
    } else {
        digit_transition::draw_glyph(writer, FONT_BIG, (int)digit, x, y);
    }
}

void NewYearRenderer::drawAnimatedCenteredSmallText(const char* s, int y) {
    int len = strlen(s);
    int totalW = 0;
    for (const char* p = s; *p; p++) {
        totalW += (*p == ':') ? 2 : 5;
    }
    int cx = (COLS_PER_ROW - totalW) / 2;
    unsigned long nowMs = millis();
    uint8_t digitIdx = 0;

    for (const char* p = s; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            if (digitIdx < NYE_SMALL_DIGITS) {
                drawAnimatedSmallDigit(cx, y, *p - '0', _smallDigitStates[digitIdx++], nowMs);
            }
        } else {
            _display->drawSmallChar(*p, cx, y);
        }
        cx += (*p == ':') ? 2 : 5;
    }
}

void NewYearRenderer::drawAnimatedCenteredBigText(const char* s, int y) {
    int len = strlen(s);
    int totalW = len * 6;
    int cx = (COLS_PER_ROW - totalW) / 2;
    unsigned long nowMs = millis();
    uint8_t digitIdx = 0;

    for (const char* p = s; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            if (digitIdx < NYE_BIG_DIGITS) {
                drawAnimatedBigDigit(cx, y, *p - '0', _bigDigitStates[digitIdx++], nowMs);
            }
        }
        cx += 6;
    }
    // Mark unused digit states as invisible so they morph out
    for (; digitIdx < NYE_BIG_DIGITS; digitIdx++) {
        auto writer = [this](int px, int py, bool on) { _display->setPixel(px, py, on); };
        auto frame = digit_transition::advance_cell(_bigDigitStates[digitIdx], false, 0, nowMs);
        if (frame.transition) {
            digit_transition::draw_transition_glyph_forced(writer, FONT_BIG,
                frame.fromVisible ? (int)frame.fromDigit : -1,
                -1, frame.progress, cx, y);
        }
    }
}
#endif

void NewYearRenderer::drawAnimatedCountdown() {
    uint32_t remainingSeconds = (_controller->millisecondsToMidnight() + 999UL) / 1000UL;

    if (remainingSeconds <= 10) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02lu", (unsigned long)remainingSeconds);
#if DIGIT_TRANSITIONS
        drawAnimatedCenteredBigText(buf, 0);
#else
        _display->drawCenteredBigText(buf, 0);
#endif

        if (remainingSeconds >= 1 && remainingSeconds <= 3) {
            _display->applyBurstBoost(12 - remainingSeconds * 2);
            return;
        }
        drawSparkles();
        return;
    }

    if (remainingSeconds <= 60) {
        if (remainingSeconds % 10 == 0) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)remainingSeconds);
            _display->drawCenteredMediumText(buf, 3);
        } else {
            uint32_t minutes = remainingSeconds / 60UL;
            uint32_t seconds = remainingSeconds % 60UL;
            char text[12];
            snprintf(text, sizeof(text), "T-%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
#if DIGIT_TRANSITIONS
            drawAnimatedCenteredSmallText(text, 5);
#else
            _display->drawCenteredSmallText(text, 5);
#endif
        }
        return;
    }

    uint32_t minutes = remainingSeconds / 60UL;
    uint32_t seconds = remainingSeconds % 60UL;
    char text[12];
    snprintf(text, sizeof(text), "T-%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
#if DIGIT_TRANSITIONS
    drawAnimatedCenteredSmallText(text, 5);
#else
    _display->drawCenteredSmallText(text, 5);
#endif
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

void NewYearRenderer::renderOverlay() {
    drawSparkles();
}

void NewYearRenderer::renderTakeover() {
    if (!_display || !_controller) return;

    NyeContentMode newMode;
    if (_controller->phase() == NewYearPhase::Celebration) {
        uint32_t slot = (_controller->phaseMilliseconds() / 3000UL) % 3UL;
        newMode = (slot == 0) ? NYE_MODE_CELEBRATION_HAPPY
                 : (slot == 1) ? NYE_MODE_CELEBRATION_NEW_YEAR
                 : NYE_MODE_CELEBRATION_YEAR;
    } else {
        uint32_t remaining = (_controller->millisecondsToMidnight() + 999UL) / 1000UL;
        if (remaining <= 10) {
            newMode = NYE_MODE_BIG;
        } else if (remaining <= 60 && remaining % 10 == 0) {
            newMode = NYE_MODE_MEDIUM;
        } else {
            newMode = NYE_MODE_SMALL;
        }
    }
    if (newMode != _lastContentMode) {
        _display->requestScreenTransition();
        _lastContentMode = newMode;
    }

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
        drawAnimatedCountdown();
        drawSparkles();
    }
}

#endif
