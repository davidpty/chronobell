#include "MenuRenderer.h"

#include <cstring>

#include "AppSettings.h"
#include "Config.h"
#include "Display.h"
#include "MenuBindings.h"

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
    (void)it;
    const char* name = menuValueName(_menu->index(), v, _display->getMenuBindings());
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
