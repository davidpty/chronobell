#ifndef MENU_RENDERER_H
#define MENU_RENDERER_H

#include <Arduino.h>

#include "MenuController.h"

class Display;

class MenuRenderer {
public:
    MenuRenderer() = default;

    // Two-phase init: called by Display::begin() after Display's own
    // members are constructed.
    void init(Display& display, MenuController& menu);

    // Top-level entry point: clears the buffer and renders the current
    // menu state (browse or edit). Display::showTime() then calls
    // renderBuffer() to flush.
    void renderMenu();

private:
    void renderMenuBrowse();
    void renderMenuEdit();
    void drawMenuValue(const MenuItem& it, int16_t v, int y);
    void drawMenuName(const MenuItem& it, int y);

    Display* _display = nullptr;
    MenuController* _menu = nullptr;
};

#endif // MENU_RENDERER_H
