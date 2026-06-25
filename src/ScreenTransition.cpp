#include "ScreenTransition.h"

#include <string.h>

namespace {

static int8_t retuneSlatOffset(uint8_t slat) {
    return ((slat * 7 + 3) % 5) - 2;
}
}

void ScreenTransition::clearFrame(uint32_t frame[TOTAL_ROWS]) {
    memset(frame, 0, sizeof(uint32_t) * TOTAL_ROWS);
}

void ScreenTransition::copyFrame(uint32_t destination[TOTAL_ROWS], const uint32_t source[TOTAL_ROWS]) {
    memcpy(destination, source, sizeof(uint32_t) * TOTAL_ROWS);
}

bool ScreenTransition::getPixelFromFrame(const uint32_t frame[TOTAL_ROWS], uint8_t x, uint8_t y) {
    return x < COLS_PER_ROW && y < TOTAL_ROWS && (frame[y] & (1UL << x)) != 0;
}

void ScreenTransition::setPixelInFrame(uint32_t frame[TOTAL_ROWS], int x, int y) {
    if (x >= 0 && x < COLS_PER_ROW && y >= 0 && y < TOTAL_ROWS) {
        frame[y] |= 1UL << x;
    }
}

void ScreenTransition::start(const uint32_t oldFrame[TOTAL_ROWS], const uint32_t newFrame[TOTAL_ROWS], uint32_t nowMs) {
    copyFrame(_oldFrame, oldFrame);
    copyFrame(_newFrame, newFrame);
    _startMs = nowMs;
    _durationMs = SCREEN_TRANSITION_MS;
    _type = _preferredType;
    _active = true;
}

bool ScreenTransition::render(uint32_t nowMs, uint32_t outputFrame[TOTAL_ROWS]) {
    if (!_active) return false;
    return renderRetune(nowMs, outputFrame);
}

bool ScreenTransition::renderRetune(uint32_t nowMs, uint32_t outputFrame[TOTAL_ROWS]) {
    uint32_t elapsed = nowMs - _startMs;
    if (elapsed >= _durationMs) {
        copyFrame(outputFrame, _newFrame);
        _active = false;
        _type = ScreenTransitionType::None;
        return true;
    }

    uint16_t t = (uint16_t)((elapsed * 1024UL) / _durationMs);
    clearFrame(outputFrame);

    // Phase boundaries:
    //   Destabilize:   0 .. 256   (0.00-0.25)
    //   Interlock:   256 .. 717   (0.25-0.70)
    //   Lock:        717 .. 1024  (0.70-1.00)

    for (uint8_t x = 0; x < 32; x++) {
        uint8_t slat = x >> 1;
        uint8_t slatOrder = (slat * 5 + 3) & 15;
        int8_t baseOff = retuneSlatOffset(slat);
        int8_t colOff = 0;

        if (t < 256) {
            // --- DESTABILIZE ---
            colOff = (int8_t)((int16_t)baseOff * (int16_t)t / 256);
            for (uint8_t y = 0; y < 16; y++) {
                int8_t srcY = (int8_t)y - colOff;
                if (srcY >= 0 && srcY < 16 && getPixelFromFrame(_oldFrame, x, srcY)) {
                    setPixelInFrame(outputFrame, x, y);
                }
            }
        } else if (t < 717) {
            // --- INTERLOCK ---
            uint16_t interT = t - 256;
            uint16_t interDur = 717 - 256;
            uint16_t offsetProg = interT * 256 / interDur;
            colOff = baseOff - (int8_t)((int16_t)baseOff * (int16_t)offsetProg / 256);

            uint8_t phase = (uint8_t)(t >> 3);
            uint8_t threshold = (uint8_t)(interT * 8 / interDur);

            for (uint8_t y = 0; y < 16; y++) {
                int8_t srcY = (int8_t)y - colOff;
                uint8_t pattern = (uint8_t)(y + slatOrder + phase) & 7;
                if (pattern < threshold) {
                    if (srcY >= 0 && srcY < 16 && getPixelFromFrame(_newFrame, x, srcY)) {
                        setPixelInFrame(outputFrame, x, y);
                    }
                } else {
                    if (srcY >= 0 && srcY < 16 && getPixelFromFrame(_oldFrame, x, srcY)) {
                        setPixelInFrame(outputFrame, x, y);
                    }
                }
            }
        } else {
            // --- LOCK ---
            uint16_t lockT = t - 717;
            int8_t wobbleDir = 0;
            if (baseOff > 0) wobbleDir = 1;
            else if (baseOff < 0) wobbleDir = -1;

            if (wobbleDir != 0) {
                uint16_t holdEnd = 16;
                uint16_t decayEnd = 48;
                if (lockT < holdEnd) {
                    colOff = wobbleDir;
                } else if (lockT < decayEnd) {
                    colOff = (int8_t)((int16_t)(decayEnd - lockT) * wobbleDir / (decayEnd - holdEnd));
                }
            }

            for (uint8_t y = 0; y < 16; y++) {
                int8_t srcY = (int8_t)y - colOff;
                if (srcY >= 0 && srcY < 16 && getPixelFromFrame(_newFrame, x, srcY)) {
                    setPixelInFrame(outputFrame, x, y);
                }
            }
        }
    }
    return true;
}
