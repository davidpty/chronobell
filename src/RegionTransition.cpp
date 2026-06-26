#include "RegionTransition.h"

#if REGION_TRANSITION

#include "Display.h"
#include <string.h>

RegionTransition::RegionTransition()
    : _x(0)
    , _y(0)
    , _w(0)
    , _h(0)
    , _startMs(0)
    , _durationMs(REGION_TRANSITION_MS)
    , _active(false)
    , _centerOutward(false)
{
    memset(_oldRows, 0, sizeof(_oldRows));
}

void RegionTransition::setDurationMs(uint16_t ms) {
    _durationMs = ms;
}

void RegionTransition::start(unsigned long nowMs,
                              int x, int y, int w, int h,
                              const uint32_t oldRows[],
                              bool centerOutward) {
    _x = x;
    _y = y;
    _w = w;
    _h = (h > TOTAL_ROWS) ? TOTAL_ROWS : h;
    _startMs = nowMs;
    _centerOutward = centerOutward;
    _active = true;

    for (int i = 0; i < _h; i++) {
        _oldRows[i] = oldRows[i];
    }
}

bool RegionTransition::render(unsigned long nowMs, Display& display) {
    if (!_active) return false;

    uint32_t elapsed = nowMs - _startMs;
    if (elapsed >= _durationMs) {
        _active = false;
        return false;
    }

    uint16_t t = (uint16_t)((elapsed * 1024UL) / _durationMs);
    int center = _w / 2;

    int leftEdge, rightEdge;
    if (_centerOutward) {
        leftEdge  = center - (center * (int)t / 1024);
        rightEdge = center + ((_w - center) * (int)t / 1024);
    } else {
        leftEdge  = center * (int)t / 1024;
        rightEdge = _w - ((_w - center) * (int)t / 1024);
    }

    for (int row = 0; row < _h; row++) {
        int absY = _y + row;
        if (absY < 0 || absY >= TOTAL_ROWS) continue;

        uint32_t oldRow = _oldRows[row];

        for (int col = 0; col < _w; col++) {
            int absX = _x + col;
            if (absX < 0 || absX >= COLS_PER_ROW) continue;

            bool showNew;
            if (_centerOutward) {
                showNew = (col >= leftEdge && col < rightEdge);
            } else {
                showNew = (col < leftEdge || col >= rightEdge);
            }

            bool pixel = showNew
                ? display.getPixel(absX, absY)
                : ((oldRow >> col) & 1);

            display.setAnimationPixel(absX, absY, pixel);
        }
    }
    return true;
}

void RegionTransition::reset() {
    _active = false;
}

#endif // REGION_TRANSITION
