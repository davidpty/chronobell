#include "TimerRenderer.h"

#include "ClockRenderer.h"
#include "Config.h"
#include "DigitTransition.h"
#include "Display.h"

namespace {

enum class TimerTransitionKind : uint8_t {
    Stopwatch = 0,
    Countdown = 1,
    Alert = 2,
};

template <size_t GlyphCount, size_t Rows, size_t Cols>
void drawGlyph(Display& display,
               const uint8_t (&font)[GlyphCount][Rows][Cols],
               int glyphIndex,
               int x,
               int y) {
    digit_transition::draw_glyph(
        [&](int px, int py, bool on) {
            if (on) {
                display.setPixel(px, py, true);
            }
        },
        font,
        glyphIndex,
        x,
        y);
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
void drawAnimatedGlyph(Display& display,
                       const uint8_t (&font)[GlyphCount][Rows][Cols],
                       digit_transition::DigitCellState& state,
                       bool visible,
                       uint8_t digit,
                       int x,
                       int y,
                       unsigned long nowMs) {
    digit_transition::render_digit_cell(
        [&](int px, int py, bool on) {
            if (on) {
                display.setPixel(px, py, true);
            }
        },
        font,
        state,
        visible,
        digit,
        x,
        y,
        nowMs);
}

void resetTransitionDigits(digit_transition::DigitCellState* states, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        states[i].reset();
    }
}

void prepareTransitionState(uint8_t& kind,
                            uint8_t& layout,
                            digit_transition::DigitCellState* states,
                            size_t count,
                            uint8_t newKind,
                            uint8_t newLayout) {
    if (kind != newKind || layout != newLayout) {
        resetTransitionDigits(states, count);
        kind = newKind;
        layout = newLayout;
    }
}

}  // namespace

void TimerRenderer::init(Display& display,
                         TimerController& timer,
                         ClockRenderer& clockRenderer) {
    _display = &display;
    _timer = &timer;
    _clockRenderer = &clockRenderer;
}

void TimerRenderer::renderTimerScreen() {
    _display->clearBuffer();
    if (_timer->isCountdownExpired()) {
        renderCountdownAlert();
    } else if (_timer->isStopwatchView()) {
        renderStopwatch();
    } else if (_timer->isCountdownView()) {
        renderCountdown();
    }
    _display->renderBuffer();
}

void TimerRenderer::renderDateView() {
    _display->clearBuffer();
    _clockRenderer->drawDateView(_display->currentDateStyle());
    _display->renderBuffer();
}

void TimerRenderer::renderStopwatch() {
    uint64_t totalMs = _timer->stopwatchMs();
    drawStopwatchTime(totalMs / 1000ULL, (uint8_t)((totalMs / 10ULL) % 100), _timer->stopwatchRunning());
}

void TimerRenderer::drawStopwatchTime(uint64_t totalSec, uint8_t centisec, bool running) {
    bool blink = running && ((millis() / 500UL) % 2UL == 0);
    const int y = 3;
    const int dw = 6, sp = 1, sw = 1;
    unsigned long nowMs = millis();

#if ENABLE_TRANSITIONS
    uint8_t layoutKey = 0;
    if (totalSec < 100) {
        layoutKey = 0;
    } else if (totalSec < 600) {
        layoutKey = 1;
    } else if (totalSec < 6000) {
        layoutKey = 2;
    } else if (totalSec < 360000) {
        layoutKey = 3;
    } else {
        layoutKey = 4;
    }
    prepareTransitionState(_transitionKind,
                           _transitionLayout,
                           _digitStates,
                           5,
                           static_cast<uint8_t>(TimerTransitionKind::Stopwatch),
                           layoutKey);
#endif

    if (totalSec < 100) {
        char buf[5];
        snprintf(buf, sizeof(buf), "%02u%02u", (unsigned)totalSec, (unsigned)centisec);
        int totalW = dw * 4 + sp * 3 + sw;
        int x = (COLS_PER_ROW - totalW) / 2;
        for (int i = 0; i < 4; i++) {
            uint8_t digit = (uint8_t)(buf[i] - '0');
            if (i < 2) {
#if ENABLE_TRANSITIONS
                drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[i], true, digit, x, y, nowMs);
#else
                drawGlyph(*_display, FONT_MEDIUM, digit, x, y);
#endif
            } else {
                drawGlyph(*_display, FONT_MEDIUM, digit, x, y);
            }
            x += dw + sp;
            if (i == 1) {
                _display->setPixel(x, y + 8, true);
                x += sw + sp;
            }
        }
    } else if (totalSec < 600) {
        char buf[5];
        unsigned m = (unsigned)(totalSec / 60);
        unsigned s = (unsigned)(totalSec % 60);
        unsigned ds = centisec / 10;
        snprintf(buf, sizeof(buf), "%u%02u%u", m, s, ds);
        int totalW = dw * 4 + sp * 3 + sw * 2;
        int x = (COLS_PER_ROW - totalW) / 2;
        uint8_t d0 = (uint8_t)(buf[0] - '0');
#if ENABLE_TRANSITIONS
        drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[0], true, d0, x, y, nowMs);
#else
        drawGlyph(*_display, FONT_MEDIUM, d0, x, y);
#endif
        x += dw + sp;
        if (blink) _display->drawTimerColon(x, y);
        x += sw + sp;
        uint8_t d1 = (uint8_t)(buf[1] - '0');
#if ENABLE_TRANSITIONS
        drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[1], true, d1, x, y, nowMs);
#else
        drawGlyph(*_display, FONT_MEDIUM, d1, x, y);
#endif
        x += dw + sp;
        uint8_t d2 = (uint8_t)(buf[2] - '0');
#if ENABLE_TRANSITIONS
        drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[2], true, d2, x, y, nowMs);
#else
        drawGlyph(*_display, FONT_MEDIUM, d2, x, y);
#endif
        x += dw + sp;
        _display->setPixel(x, y + 8, true);
        x += sw + sp;
        uint8_t d3 = (uint8_t)(buf[3] - '0');
        drawGlyph(*_display, FONT_MEDIUM, d3, x, y);
    } else if (totalSec < 6000) {
        char buf[5];
        snprintf(buf, sizeof(buf), "%02u%02u", (unsigned)(totalSec / 60ULL), (unsigned)(totalSec % 60ULL));
        int totalW = dw * 4 + sp * 3 + sw;
        int x = (COLS_PER_ROW - totalW) / 2;
        for (int i = 0; i < 4; i++) {
            uint8_t digit = (uint8_t)(buf[i] - '0');
#if ENABLE_TRANSITIONS
            drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[i], true, digit, x, y, nowMs);
#else
            drawGlyph(*_display, FONT_MEDIUM, digit, x, y);
#endif
            x += dw + sp;
            if (i == 1) {
                if (blink) _display->drawTimerColon(x, y);
                x += sw + sp;
            }
        }
    } else if (totalSec < 360000) {
        char buf[5];
        unsigned h = (unsigned)(totalSec / 3600);
        unsigned m = (unsigned)((totalSec % 3600) / 60);
        snprintf(buf, sizeof(buf), "%02u%02u", h, m);
        int totalW = dw * 4 + sp * 3 + sw;
        int x = (COLS_PER_ROW - totalW) / 2;
        for (int i = 0; i < 4; i++) {
            uint8_t digit = (uint8_t)(buf[i] - '0');
#if ENABLE_TRANSITIONS
            drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[i], true, digit, x, y, nowMs);
#else
            drawGlyph(*_display, FONT_MEDIUM, digit, x, y);
#endif
            x += dw + sp;
            if (i == 1) {
                if (blink) _display->drawTimerColon(x, y);
                x += sw + sp;
            }
        }
    } else {
        char buf[5];
        unsigned d = (unsigned)(totalSec / 86400);
        unsigned h = (unsigned)((totalSec % 86400) / 3600);
        if (d > 99) d = 99;
        snprintf(buf, sizeof(buf), "%02u%02u", d, h);
        int totalW = dw * 4 + sp * 3 + sw;
        int x = (COLS_PER_ROW - totalW) / 2;
        for (int i = 0; i < 4; i++) {
            uint8_t digit = (uint8_t)(buf[i] - '0');
#if ENABLE_TRANSITIONS
            drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[i], true, digit, x, y, nowMs);
#else
            drawGlyph(*_display, FONT_MEDIUM, digit, x, y);
#endif
            x += dw + sp;
            if (i == 1) {
                if (blink) {
                    _display->setPixel(x, y + 4, true);
                    _display->setPixel(x, y + 6, true);
                }
                x += sw + sp;
            }
        }
    }
}

void TimerRenderer::renderCountdown() {
    uint32_t remainingMs = _timer->countdownMs();
    uint32_t displaySeconds = (remainingMs + 999UL) / 1000UL;
    drawTimerDuration(displaySeconds, _timer->countdownRunning(), static_cast<uint8_t>(TimerTransitionKind::Countdown));
}

void TimerRenderer::renderCountdownAlert() {
    if ((millis() % (BLINK_ON_MS + BLINK_OFF_MS)) < BLINK_ON_MS) {
        uint32_t elapsedSec = _timer->countdownElapsedSinceExpiryMs() / 1000UL;
        drawTimerDuration(elapsedSec >= 6000 ? 0 : elapsedSec,
                          false,
                          static_cast<uint8_t>(TimerTransitionKind::Alert));
    }
}

void TimerRenderer::drawTimerDuration(uint32_t totalSeconds, bool blinkSeparator, uint8_t transitionKind) {
    char buf[6];
    uint32_t minutes = totalSeconds / 60UL;
    uint8_t seconds = totalSeconds % 60UL;

    if (minutes < 100) {
        snprintf(buf, sizeof(buf), "%02lu%02u", (unsigned long)minutes, (unsigned)seconds);
    } else {
        uint32_t hours = minutes / 60UL;
        uint8_t mins = minutes % 60UL;
        if (hours > 99) hours = 99;
        snprintf(buf, sizeof(buf), "%lu%02u", (unsigned long)hours, (unsigned)mins);
    }

    uint8_t digitCount = strlen(buf);
    int digitWidth = 6;
    int spacing = 1;
    int sepWidth = 1;
    int totalWidth = (digitWidth * digitCount) + (spacing * (digitCount - 1)) + sepWidth;
    int startX = (COLS_PER_ROW - totalWidth) / 2;
    int startY = 3;
    int x = startX;
    unsigned long nowMs = millis();

#if ENABLE_TRANSITIONS
    prepareTransitionState(_transitionKind,
                           _transitionLayout,
                           _digitStates,
                           5,
                           transitionKind,
                           digitCount);
#endif

    for (uint8_t i = 0; i < digitCount; i++) {
        uint8_t d = (uint8_t)(buf[i] - '0');
#if ENABLE_TRANSITIONS
        drawAnimatedGlyph(*_display, FONT_MEDIUM, _digitStates[i], true, d, x, startY, nowMs);
#else
        drawGlyph(*_display, FONT_MEDIUM, d, x, startY);
#endif
        x += digitWidth + spacing;
        if (i == digitCount - 3) {
            if (blinkSeparator && ((millis() / 500UL) % 2UL) == 0) {
                _display->drawTimerColon(x, startY);
            }
            x += sepWidth + spacing;
        }
    }
}
