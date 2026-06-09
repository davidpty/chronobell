#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <MD_MAX72xx.h>

#include "Config.h"
#include "AppSettings.h"
#include "MenuController.h"
#include "fonts.h"

class ClockRenderer;
class MenuRenderer;
class SettingsStore;
class TimeProvider;
class TimerController;
class TimerRenderer;
class GuestWifiController;
class WiFiManagerLite;

class Display {
public:
    Display(MD_MAX72XX& leds,
            MenuController& menu,
            TimerController& timer,
            TimeProvider& timeProvider,
            SettingsStore& settings,
            WiFiManagerLite& wifiManager);

    void begin();

    // User brightness is the "saved" dim level. setUserBrightness updates
    // both the saved value and the LED intensity, so callers do not have
    // to call setBrightness in addition. The menu uses this for the DIM
    // setting's preview / commit / cancel paths.
    void setUserBrightness(int8_t v);
    int8_t getUserBrightness() const { return _userBrightness; }

    // Effective brightness is what the LED driver is currently running at.
    // It may differ from the user brightness while night mode is dimming
    // the display. Only the orchestrator (ClockApp) writes this.
    void setBrightness(int8_t v);
    int8_t getBrightness() const { return _brightness; }
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }

    // Runtime mode accessors. App layer sets pointers to its own state so
    // Display can read the live display mode without a global.
    void setMenuBindings(void* bindings);
    void setRuntimeMode(DisplayMode* displayMode, BellMode* bellMode);
    void setTimeFormat(TimeFormat* timeFormat);
    void setDateStyle(DateStyle* dateStyle);
    void* getMenuBindings() const { return _menuBindings; }
    DateStyle currentDateStyle() const;

    // Persistence helpers used by setup() and the brightness menu accessor.
    void loadBrightnessFromSettings();

    // Top-level render dispatcher used by ClockApp::render().
    void showTime();
    void showHotspotSymbol();

    // Renders a live clock preview in the given style. Used by the menu
    // Edit-mode preview phase. The caller is responsible for clearBuffer()
    // and renderBuffer() bracketing.
    void drawStylePreview(DisplayMode mode);

    // Boot-time LED self-test: all LEDs on for `seconds`, then cleared.
    void runTest(uint8_t seconds);

    // --- Pixel buffer / font helpers exposed to the renderer classes ---
    void setPixel(uint8_t x, uint8_t y, bool value);
    void clearBuffer();
    void renderBuffer();

    // Tabular digit helpers for live time/timer displays. These keep every
    // digit in a fixed-width cell so changing values do not wobble.
    void drawMediumDigit(uint8_t digit, int x, int y);
    void drawTimerColon(int x, int y);

    // Text helpers are proportional: empty glyph columns are trimmed so menu,
    // word clock, date, and status labels center optically.
    void drawMediumChar(char c, int x, int y);
    void drawSmallChar(char c, int x, int y);
    void drawBigChar(char c, int x, int y);
    void drawText(const char* s, int x, int y, bool small, int letterSpacing, int wordGap);
    void drawMediumText(const char* s, int x, int y);
    void drawSmallText(const char* s, int x, int y);
    void drawBigText(const char* s, int x, int y);
    void drawCenteredMediumText(const char* s, int y);
    void drawCenteredSmallText(const char* s, int y);
    void drawCenteredBigText(const char* s, int y);

    // Guest WiFi alternating SSID / password display
    void setGuestWifiController(GuestWifiController* c);
    void drawGuestWifiText(bool showSsid);

    static int charWidth(char c, bool small);
    static int textWidth(const char* s, bool small, int letterSpacing, int wordGap);
    static int charWidthBig(char c);
    static int textWidthBig(const char* s, int letterSpacing, int wordGap);
    static int menuTextWidth(const char* s, int cellW, int spacing);

private:
    void drawHotspotSymbolGlyph();

    void showHotspotOnBuffer();

    // --- Owned child renderers (constructed in ctor) ---
    ClockRenderer*  _clockRenderer  = nullptr;
    MenuRenderer*   _menuRenderer   = nullptr;
    TimerRenderer*  _timerRenderer  = nullptr;

    MD_MAX72XX& _leds;
    MenuController& _menu;
    TimerController& _timer;
    TimeProvider& _timeProvider;
    SettingsStore& _settings;
    WiFiManagerLite& _wifiManager;
    GuestWifiController* _guestWifi = nullptr;

    void* _menuBindings = nullptr;
    DisplayMode* _displayMode = nullptr;
    BellMode* _bellMode = nullptr;
    TimeFormat* _timeFormat = nullptr;
    DateStyle* _dateStyle = nullptr;

    bool pixelBuffer[COLS_PER_ROW][TOTAL_ROWS];
    int8_t _userBrightness = DISPLAY_BRIGHTNESS;
    int8_t _brightness     = DISPLAY_BRIGHTNESS;
    bool   _enabled = true;

    // Guest WiFi alternating display state
    unsigned long _guestWifiViewStartMs = 0;
    bool _wasGuestWifiView = false;
};

#endif // DISPLAY_H
