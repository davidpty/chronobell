#include "MenuRenderer.h"

#include <cstring>

#include "AppSettings.h"
#include "Config.h"
#include "Display.h"
#include "MenuBindings.h"
#include "MenuConfig.h"
#include "WiFiManagerLite.h"

void MenuRenderer::init(Display& display, MenuController& menu) {
    _display = &display;
    _menu = &menu;
}

void MenuRenderer::renderMenu() {
    _display->clearBuffer();
    if (_menu->isBrowse()) {
        renderMenuBrowse();
    } else if (_menu->isEdit()) {
        renderMenuEdit();
    }
    // renderBuffer() is called by Display::showTime() after we return.
}

void MenuRenderer::renderMenuBrowse() {
    const MenuItem& it = _menu->currentItem();
    int16_t v = it.getValue(_display->getMenuBindings());
    drawMenuName(it, 0);
    drawMenuValue(it, v, 6);
}

void MenuRenderer::renderMenuEdit() {
    const MenuItem& it = _menu->currentItem();

    // SETTIME multi-step: custom time editing display
    if (strcmp(it.name, "SETTIME") == 0 && g_settimeStep > 0) {
        renderSetTimeEdit();
        return;
    }

    // STYLE: show the chosen mode's name once (NameIntro), then transition
    // to ClockPreview where the clock blinks on/off continuously.
    if (strcmp(it.name, "STYLE") == 0 &&
        _menu->blinkPhase() == MenuBlinkPhase::ClockPreview) {
        if (_menu->blinkOn()) {
            _display->drawStylePreview((DisplayMode)_menu->editValue());
        }
        return;
    }
    // NameIntro (or any non-STYLE item): standard value blink with label.
    drawMenuName(it, 0);
    if (_menu->blinkOn()) {
        drawMenuValue(it, _menu->editValue(), 6);
    }
}

void MenuRenderer::drawMenuValue(const MenuItem& it, int16_t v, int y) {
    const char* name = nullptr;
    if (strcmp(it.name, "HOTSPOT") == 0) {
        MenuBindings* bindings = static_cast<MenuBindings*>(_display->getMenuBindings());
        static String hotspotLabel;
        hotspotLabel = bindings->wifiManager.hotspotMenuLabel(_menu->isEdit(), v);
        name = hotspotLabel.c_str();
    } else {
        name = menuValueName(_menu->index(), v, _display->getMenuBindings());
    }
    int x;
    if (name) {
        int w = _display->menuTextWidth(name, 6, 1);
        x = (COLS_PER_ROW - w) / 2;
        _display->drawMediumText(name, x, y);
    } else {
        // BRIGHT is a numeric value, so render it as proportional text rather
        // than tabular clock digits.
        char buf[4];
        if (v < 10) {
            buf[0] = (char)('0' + v);
            buf[1] = 0;
        } else {
            buf[0] = '1';
            buf[1] = (char)('0' + (v - 10));
            buf[2] = 0;
        }
        int w = _display->menuTextWidth(buf, 6, 1);
        x = (COLS_PER_ROW - w) / 2;
        _display->drawMediumText(buf, x, y);
    }
}

void MenuRenderer::drawMenuName(const MenuItem& it, int y) {
    int w = _display->menuTextWidth(it.name, 4, 1);
    int x = (COLS_PER_ROW - w) / 2;
    _display->drawSmallText(it.name, x, y);
}

void MenuRenderer::renderSetTimeEdit() {
    uint8_t step = g_settimeStep;

    const char* label = "";
    switch (step) {
        case 1: label = "HOUR"; break;
        case 2: label = "MIN";  break;
        case 3: label = "SEC";  break;
        case 4: label = "MONTH"; break;
        case 5: label = "DATE"; break;
        case 6: label = "YEAR"; break;
    }
    int lw = _display->menuTextWidth(label, 4, 1);
    int lx = (COLS_PER_ROW - lw) / 2;
    _display->drawSmallText(label, lx, 0);

    uint8_t d0, d1, d2, d3;
    char sep = ':';
    bool blinkFirst, blinkSecond;

    if (step == 1) {
        d0 = g_setHour / 10; d1 = g_setHour % 10;
        d2 = g_setMin / 10;  d3 = g_setMin % 10;
        blinkFirst = true;  blinkSecond = false;
    } else if (step == 2) {
        d0 = g_setHour / 10; d1 = g_setHour % 10;
        d2 = g_setMin / 10;  d3 = g_setMin % 10;
        blinkFirst = false; blinkSecond = true;
    } else if (step == 3) {
        d0 = g_setSec / 10;  d1 = g_setSec % 10;
        d2 = 0; d3 = 0;
        blinkFirst = true; blinkSecond = false;
        sep = 0;
    } else if (step == 4 || step == 5) {
        d0 = g_setDay / 10; d1 = g_setDay % 10;
        d2 = 0; d3 = 0;
        blinkFirst = false; blinkSecond = false;
        sep = 0;
    } else {
        d0 = (g_setYear / 1000) % 10;
        d1 = (g_setYear / 100) % 10;
        d2 = (g_setYear / 10) % 10;
        d3 = g_setYear % 10;
        blinkFirst = true; blinkSecond = true;
        sep = 0;
    }

    bool bo = _menu->blinkOn();
    int digW = 6;
    int gap = 1;

    if (step == 4 || step == 5) {
        static const char* const MONTHS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                             "JUL","AUG","SEP","OCT","NOV","DEC"};
        uint8_t mi = (g_setMonth >= 1 && g_setMonth <= 12) ? g_setMonth - 1 : 0;
        const char* mn = MONTHS[mi];
        int mw = _display->menuTextWidth(mn, 6, 1);
        int totalW = mw + gap + digW * 2 + gap;
        int x = (COLS_PER_ROW - totalW) / 2;
        bool blinkMonth = (step == 4);
        bool blinkDay = (step == 5);

        if (!blinkMonth || bo) {
            _display->drawMediumText(mn, x, 6);
        }
        x += mw + gap;
        if (!blinkDay || bo) {
            _display->drawMediumDigit(d0, x, 6);
            _display->drawMediumDigit(d1, x + digW + gap, 6);
        }
        return;
    }

    bool twoDigit = (step == 3);
    int sepContentW = sep ? Display::charWidth(sep, false) : 0;
    int totalW = twoDigit ? (digW * 2 + gap) : (digW * 4 + gap * 4 + sepContentW);
    int startX = (COLS_PER_ROW - totalW) / 2;
    int digY = 6;
    int x = startX;

    if (!blinkFirst || bo) {
        _display->drawMediumDigit(d0, x, digY);
        x += digW + gap;
        _display->drawMediumDigit(d1, x, digY);
        if (!twoDigit) x += digW + gap;
    } else {
        x += (digW + gap) * 2;
    }

    if (!twoDigit) {
        if (sep) {
            char sepStr[2] = {sep, '\0'};
            _display->drawMediumText(sepStr, x, digY);
            x += sepContentW + gap;
        } else {
            x += gap;
        }

        if (!blinkSecond || bo) {
            _display->drawMediumDigit(d2, x, digY);
            x += digW + gap;
            _display->drawMediumDigit(d3, x, digY);
        }
    }
}
