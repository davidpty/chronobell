#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <MD_MAX72xx.h>

#include "Config.h"
#include "AppSettings.h"
#include "MenuController.h"
#include "RtcClock.h"
#include "PongClock.h"
#include "fonts.h"
#if SCREEN_TRANSITION
#include "ScreenTransition.h"
#endif
#if CHRONOSERVE_ENABLED
#include "MessageClient.h"
#endif

class ClockRenderer;
class DriftTimeModel;
class MenuRenderer;
class NewYearController;
class NewYearRenderer;
class PongClockRenderer;
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
    void setRuntimeMode(DisplayMode* displayMode);
    void setAppSettings(AppSettings* settings);
    void setInfoLineMode(InfoLineMode* infoLineMode);
    void setDriftTimeModel(DriftTimeModel* driftTimeModel);
    void setTimeFormat(TimeFormat* timeFormat);
    void setDateStyle(DateStyle* dateStyle);
    void* getMenuBindings() const { return _menuBindings; }
    DateStyle currentDateStyle() const;

    // Persistence helpers used by setup() and the brightness menu accessor.
    void loadBrightnessFromSettings();

    // Top-level render dispatcher used by ClockApp::render().
    void showTime();
    void drawClockContent();
    // OTA update status display (called from ArduinoOTA callbacks, which fire
    // inside the blocking handle() loop, so we must render here directly).
    void showOtaUpdate(bool active, unsigned int progress, unsigned int total);

    // Renders a live clock preview in the given style. Used by the menu
    // Edit-mode preview phase. The caller is responsible for clearBuffer()
    // and renderBuffer() bracketing.
    void drawStylePreview(DisplayMode mode);
    void resetPong(ClockTime time);

    // Boot-time LED self-test: all LEDs on for `seconds`, then cleared.
    void runTest(uint8_t seconds);

    // --- Pixel buffer / font helpers exposed to the renderer classes ---
    void setPixel(int x, int y, bool value);
    bool getPixel(int x, int y) const;
    void togglePixel(int x, int y);
    void setAnimationPixel(int x, int y, bool value);
    void setSnapshotPixel(int x, int y, bool value);
    void clearBuffer();
    void renderBuffer();
    void applyBurstBoost(int8_t boost);
    void requestScreenTransition();

    // Reinitializes the MAX7219 display chain without touching any other
    // system state.  Sends all control registers, clears MAX7219 internal
    // RAM, and force-redraws the current framebuffer.
    // Use this to recover from transient MAX7219 glitches (shutdown, test
    // mode, wrong scan limit, corrupt RAM, etc.) without rebooting.
    void displayHardRefresh();

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    bool animationsEnabled() const;
#endif
    String snapshotSvg() const;

    // Tabular digit helpers for live time/timer displays. These keep every
    // digit in a fixed-width cell so changing values do not wobble.
    void drawMediumDigit(uint8_t digit, int x, int y);
    void drawTimerColon(int x, int y);

    // Text helpers are proportional: empty glyph columns are trimmed so menu,
    // word clock, date, and status labels center optically.
    void drawText(const char* s, int x, int y, bool small, int letterSpacing, int wordGap);
    void drawMediumText(const char* s, int x, int y);
    void drawSmallText(const char* s, int x, int y);
    void drawBigText(const char* s, int x, int y);
    void drawCenteredMediumText(const char* s, int y);
    void drawCenteredSmallText(const char* s, int y);
    void drawCenteredBigText(const char* s, int y);
    void drawPongTime(ClockTime time);

// Guest WiFi alternating SSID / password display
#if GUEST_WIFI_ENABLED
    void setGuestWifiController(GuestWifiController* c);
#endif
    void setNewYearController(NewYearController* c);
#if GUEST_WIFI_ENABLED
    void drawGuestWifiText(bool showSsid);
#endif
#if CHRONOSERVE_ENABLED
    bool drawChronoMessage(const ChronoMessage& message, unsigned long nowMs, unsigned long previewStartMs);
    void drawUnreadMessageIndicator(int count, int priority, unsigned long nowMs);
#endif

    static int charWidth(char c, bool small);
    static int charWidthBig(char c);
    static int textWidth(const char* s, bool small, int letterSpacing, int wordGap);
    static int textWidthBig(const char* s, int letterSpacing, int wordGap);
    static int menuTextWidth(const char* s, int cellW, int spacing);
    void drawSmallChar(char c, int x, int y);
    void drawBigChar(char c, int x, int y);

private:
#if CHRONOSERVE_ENABLED
    enum class ChronoScrollRenderState : uint8_t {
        Inactive,
        Scrolling,
        Finished
    };
#endif

    void noteScreenIdentity();
    void bufferToFrame(uint32_t frame[TOTAL_ROWS]) const;
    void frameToBuffer(const uint32_t frame[TOTAL_ROWS]);
    void flushBufferToLeds();
#if CHRONOSERVE_ENABLED
    void renderChronoMessageBuffer();
#endif
    void flushBar(int bufferRow, bool flipX, bool flipY, int colOffset);
    void drawMediumChar(char c, int x, int y);
    void drawInvertedSmallText(const char* s, int x, int y);
#if CHRONOSERVE_ENABLED
    void resetChronoScroll(const String& text, uint8_t mode, uint16_t stepMs, unsigned long previewStartMs);
    bool drawChronoScrollFrame(const String& text, int y, uint8_t mode, uint16_t stepMs, unsigned long nowMs, unsigned long previewStartMs);
#endif


    // --- Owned child renderers (constructed in ctor) ---
    ClockRenderer*  _clockRenderer  = nullptr;
    MenuRenderer*   _menuRenderer   = nullptr;
    TimerRenderer*  _timerRenderer  = nullptr;
    NewYearRenderer* _newYearRenderer = nullptr;
    PongClockRenderer* _pongRenderer = nullptr;

    MD_MAX72XX& _leds;
    MenuController& _menu;
    TimerController& _timer;
    TimeProvider& _timeProvider;
    SettingsStore& _settings;
    WiFiManagerLite& _wifiManager;
#if GUEST_WIFI_ENABLED
    GuestWifiController* _guestWifi = nullptr;
#endif
    NewYearController* _newYear = nullptr;

    void* _menuBindings = nullptr;
    DisplayMode* _displayMode = nullptr;
    AppSettings* _appSettings = nullptr;
    DriftTimeModel* _driftTimeModel = nullptr;
    TimeFormat* _timeFormat = nullptr;
    DateStyle* _dateStyle = nullptr;

    bool pixelBuffer[COLS_PER_ROW][TOTAL_ROWS];
    uint32_t _snapshotFrame[TOTAL_ROWS] = {};
#if SCREEN_TRANSITION
    ScreenTransition _screenTransition;
    uint32_t _lastFrame[TOTAL_ROWS] = {};
    bool _hasLastFrame = false;
    bool _screenTransitionPending = false;
    uint16_t _screenIdentity = 0;
    bool _hasScreenIdentity = false;
    uint16_t _lastContentHash = 0;
#endif
    int8_t _userBrightness = 4;
    int8_t _brightness     = 4;
    bool   _enabled = true;
#if CHRONOSERVE_ENABLED
    ChronoScrollRenderState _chronoScrollState = ChronoScrollRenderState::Inactive;
    String _chronoScrollText;
    uint8_t _chronoScrollMode = 255;
    uint16_t _chronoScrollStepMs = 0;
    unsigned long _chronoScrollPreviewStartMs = 0;
    unsigned long _chronoScrollLastAdvanceMs = 0;
    int _chronoScrollX = COLS_PER_ROW;
    int _chronoScrollTextWidth = 0;
#endif

    PongClockEngine _pong;

    // Guest WiFi alternating display state
#if GUEST_WIFI_ENABLED
    unsigned long _guestWifiViewStartMs = 0;
    bool _wasGuestWifiView = false;
#endif
};

#endif // DISPLAY_H
