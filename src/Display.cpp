#include "Display.h"

#include "AppSettings.h"
#include "ClockRenderer.h"
#include "MenuBindings.h"
#include "MenuRenderer.h"
#include "RtcClock.h"
#include "SettingsStore.h"
#include "TimeProvider.h"
#include "TimerController.h"
#include "TimerRenderer.h"
#include "GuestWifiController.h"
#include "WiFiManagerLite.h"
#include "MenuConfig.h"
#include "fonts.h"

Display::Display(MD_MAX72XX& leds,
                 MenuController& menu,
                 TimerController& timer,
                 TimeProvider& timeProvider,
                 SettingsStore& settings,
                 WiFiManagerLite& wifiManager)
    : _leds(leds)
    , _menu(menu)
    , _timer(timer)
    , _timeProvider(timeProvider)
    , _settings(settings)
    , _wifiManager(wifiManager)
{
    memset(pixelBuffer, 0, sizeof(pixelBuffer));
}

void Display::begin() {
    _leds.begin();
    _leds.clear();
    _leds.update();
    setUserBrightness(_userBrightness);

    // Two-phase init: renderers default-construct as Display members
    // (constructed after _display's own data), then bind to *this* and
    // their respective controller refs.
    _clockRenderer  = new ClockRenderer();
    _menuRenderer   = new MenuRenderer();
    _timerRenderer  = new TimerRenderer();

    _clockRenderer->init(*this, _timeProvider);
    _clockRenderer->setTimeFormat(_timeFormat);
    _menuRenderer->init(*this, _menu);
    _timerRenderer->init(*this, _timer, *_clockRenderer);
}

void Display::setUserBrightness(int8_t v) {
    if (v < 0) v = 0;
    if (v > 15) v = 15;
    _userBrightness = v;
    _brightness = v;
    _leds.control(MD_MAX72XX::INTENSITY, v);
}

void Display::setBrightness(int8_t v) {
    if (v < 0) v = 0;
    if (v > 15) v = 15;
    _brightness = v;
    _leds.control(MD_MAX72XX::INTENSITY, v);
}

void Display::setEnabled(bool enabled) {
    if (_enabled == enabled) {
        return;
    }
    _enabled = enabled;
    _leds.control(MD_MAX72XX::SHUTDOWN, enabled ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
}

void Display::setMenuBindings(void* bindings) {
    _menuBindings = bindings;
}

void Display::setRuntimeMode(DisplayMode* displayMode, BellMode* bellMode) {
    _displayMode = displayMode;
    _bellMode = bellMode;
}

void Display::setTimeFormat(TimeFormat* timeFormat) {
    _timeFormat = timeFormat;
}

void Display::setDateStyle(DateStyle* dateStyle) {
    _dateStyle = dateStyle;
}

void Display::setGuestWifiController(GuestWifiController* c) {
    _guestWifi = c;
}

DateStyle Display::currentDateStyle() const {
    return _dateStyle ? *_dateStyle : DateStyle::Date;
}

void Display::loadBrightnessFromSettings() {
    _userBrightness = _settings.loadBrightness(4);
    LOGF("Loaded brightness from NVS: %d\n", (int)_userBrightness);
    setUserBrightness(_userBrightness);
}

// =============================================================================
// Top-level render dispatchers
// =============================================================================

void Display::showTime() {
    memset(pixelBuffer, 0, sizeof(pixelBuffer));

    bool wasInGuestWifi = _wasGuestWifiView;
    _wasGuestWifiView = false;

    if (_menu.isActive()) {
        _menuRenderer->renderMenu();
        renderBuffer();
        return;
    }

    if (_timer.isCountdownExpired()) {
        _timerRenderer->renderTimerScreen();
        return;
    }

    if (_timer.isDateView()) {
        _timerRenderer->renderDateView();
        return;
    }

    if (_timer.isGuestWifiView()) {
        memset(pixelBuffer, 0, sizeof(pixelBuffer));
        _wasGuestWifiView = true;

        if (!wasInGuestWifi) {
            _guestWifiViewStartMs = millis();
        }

        unsigned long elapsed = millis() - _guestWifiViewStartMs;
        unsigned long phase = elapsed % (GUEST_WIFI_SSID_SHOW_SECONDS * 1000UL + GUEST_WIFI_PASS_SHOW_SECONDS * 1000UL);

        bool showSsid = (_guestWifi && _guestWifi->ssid()[0] != '\0')
                        ? (phase < GUEST_WIFI_SSID_SHOW_SECONDS * 1000UL)
                        : false;

        drawGuestWifiText(showSsid);
        renderBuffer();
        return;
    }

    if (_timer.isStopwatchView() || _timer.isCountdownView()) {
        _timerRenderer->renderTimerScreen();
        return;
    }

    ClockTime time = _timeProvider.displayTime();
    int hours = time.hours;
    int minutes = time.minutes;
    int seconds = time.seconds;

    DisplayMode mode = _displayMode ? *_displayMode : DisplayMode::LargeDigitsOnly;
    switch (mode) {
        case DisplayMode::TimeWithSeconds:
            _clockRenderer->drawTime(hours, minutes, seconds);
            _clockRenderer->drawSeconds(seconds);
            break;
        case DisplayMode::TimeWithDeciseconds:
            _clockRenderer->drawTime(hours, minutes, seconds);
            _clockRenderer->drawDeciseconds(seconds, _clockRenderer->currentClockDeciseconds());
            break;
        case DisplayMode::Word:
            _clockRenderer->drawWordTime(hours, minutes);
            break;
        case DisplayMode::Roma:
            _clockRenderer->drawRomanTime(hours, minutes);
            break;
        case DisplayMode::Bin:
            _clockRenderer->drawBinaryTime(hours, minutes, seconds);
            break;
        case DisplayMode::Rnd:
            _clockRenderer->drawPreview(mode, time);
            break;
        case DisplayMode::TimeWithDate:
            _clockRenderer->drawDateTime(time);
            break;
        case DisplayMode::LargeDigitsOnly:
        default:
            _clockRenderer->drawBigTime(hours, minutes, seconds);
            break;
    }

    renderBuffer();
}

void Display::showHotspotSymbol() {
    showHotspotOnBuffer();
    renderBuffer();
}

void Display::drawStylePreview(DisplayMode mode) {
    ClockTime time = _timeProvider.displayTime();
    _clockRenderer->drawPreview(mode, time);
}

void Display::runTest(uint8_t seconds) {
    LOGF("Running display test for %d seconds...\n", (int)seconds);
    for (int col = 0; col < MAX7219_NUM_MODULES * 8; col++) {
        _leds.setColumn(col, 0xFF);
    }
    _leds.update();
    delay(seconds * 1000);
    _leds.clear();
    _leds.update();
}

void Display::showHotspotOnBuffer() {
    memset(pixelBuffer, 0, sizeof(pixelBuffer));
    drawHotspotSymbolGlyph();
}

// =============================================================================
// Pixel buffer
// =============================================================================

void Display::clearBuffer() {
    memset(pixelBuffer, 0, sizeof(pixelBuffer));
}

void Display::setPixel(uint8_t x, uint8_t y, bool value) {
    if (x < COLS_PER_ROW && y < TOTAL_ROWS) {
        pixelBuffer[x][y] = value;
    }
}

void Display::renderBuffer() {
#if DISPLAY_FLIP == 0
    const bool flipBarOneX = true;
    const bool flipBarOneY = false;
    const bool flipBarTwoX = true;
    const bool flipBarTwoY = false;
    const bool swapBars = false;
#else
    const bool flipBarOneX = false;
    const bool flipBarOneY = true;
    const bool flipBarTwoX = false;
    const bool flipBarTwoY = true;
    const bool swapBars = true;
#endif

    int upperBufferRow = swapBars ? ROWS_PER_MODULE : 0;
    int lowerBufferRow = swapBars ? 0 : ROWS_PER_MODULE;

    for (int col = 0; col < COLS_PER_ROW; col++) {
        uint8_t colData = 0;
        for (int row = 0; row < ROWS_PER_MODULE; row++) {
            if (pixelBuffer[col][row + upperBufferRow]) {
                int bitPosition = flipBarOneY ? (ROWS_PER_MODULE - 1 - row) : row;
                colData |= (1 << bitPosition);
            }
        }
        int displayCol = flipBarOneX ? (COLS_PER_ROW - 1 - col) : col;
        _leds.setColumn(displayCol, colData);
    }

    for (int col = 0; col < COLS_PER_ROW; col++) {
        uint8_t colData = 0;
        for (int row = 0; row < ROWS_PER_MODULE; row++) {
            if (pixelBuffer[col][row + lowerBufferRow]) {
                int bitPosition = flipBarTwoY ? (ROWS_PER_MODULE - 1 - row) : row;
                colData |= (1 << bitPosition);
            }
        }
        int displayCol = flipBarTwoX ? (2 * COLS_PER_ROW - 1 - col) : (COLS_PER_ROW + col);
        _leds.setColumn(displayCol, colData);
    }

    _leds.update();
}

// =============================================================================
// Font helpers (used by all renderers)
// =============================================================================

// FONT_MEDIUM and FONT_SMALL are combined: index 0-9 = '0'-'9', index 10-35 = 'A'-'Z', index 36 = '%', index 37 = '-', index 38 = '+', index 39 = 'o', index 40 = '^', index 41 = '@', index 42 = 'v'.
static uint8_t fontIndex(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 10);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c == '%') return 36;
    if (c == '-') return 37;
    if (c == '+') return 38;
    if (c == 'o') return 39;
    if (c == '^') return 40;
    if (c == '@') return 41;
    if (c == 'v') return 42;
    if (c == ':') return 43;
    if (c == '/') return 44;
    return 0;
}

static bool fontPixel(uint8_t i, bool small, int row, int col) {
    return small ? FONT_SMALL[i][row][col] : FONT_MEDIUM[i][row][col];
}

static void glyphBounds(char c, bool small, int& left, int& right) {
    if (c == ' ') {
        left = 0;
        right = -1;
        return;
    }

    uint8_t i = fontIndex(c);
    int cellW = small ? 4 : 6;
    int height = small ? SEC_FONT_HEIGHT : TIME_FONT_MEDIUM_HEIGHT;
    left = cellW;
    right = -1;

    for (int col = 0; col < cellW; col++) {
        for (int row = 0; row < height; row++) {
            if (fontPixel(i, small, row, col)) {
                if (col < left) left = col;
                if (col > right) right = col;
            }
        }
    }
}

static void bigGlyphBounds(char c, int& left, int& right) {
    if (c == ' ') {
        left = 0;
        right = -1;
        return;
    }

    uint8_t i = fontIndex(c);
    left = 6;
    right = -1;

    for (int col = 0; col < 6; col++) {
        for (int row = 0; row < TIME_FONT_BIG_HEIGHT; row++) {
            if (FONT_BIG[i][row][col]) {
                if (col < left) left = col;
                if (col > right) right = col;
            }
        }
    }
}

// Proportional text metrics. Live time/timer displays bypass these and use
// tabular digit cells so the clock face stays stable as digits change.
int Display::charWidth(char c, bool small) {
    int left, right;
    glyphBounds(c, small, left, right);
    return right >= left ? (right - left + 1) : 0;
}

int Display::charWidthBig(char c) {
    int left, right;
    bigGlyphBounds(c, left, right);
    return right >= left ? (right - left + 1) : 0;
}

int Display::textWidth(const char* s, bool small, int letterSpacing, int wordGap) {
    int width = 0;
    bool inWord = false;
    while (*s) {
        if (*s == ' ') {
            if (inWord) {
                width += wordGap;
                inWord = false;
            }
        } else {
            if (inWord) {
                width += letterSpacing;
            }
            width += charWidth(*s, small);
            inWord = true;
        }
        s++;
    }
    return width;
}

int Display::textWidthBig(const char* s, int letterSpacing, int wordGap) {
    int width = 0;
    bool inWord = false;
    while (*s) {
        if (*s == ' ') {
            if (inWord) {
                width += wordGap;
                inWord = false;
            }
        } else {
            if (inWord) {
                width += letterSpacing;
            }
            width += charWidthBig(*s);
            inWord = true;
        }
        s++;
    }
    return width;
}

int Display::menuTextWidth(const char* s, int cellW, int spacing) {
    (void)spacing;
    return textWidth(s, cellW <= 4, 1, 2);
}

void Display::drawMediumDigit(uint8_t digit, int x, int y) {
    if (digit > 9) return;
    for (int row = 0; row < TIME_FONT_MEDIUM_HEIGHT; row++) {
        for (int col = 0; col < 6; col++) {
            if (FONT_MEDIUM[digit][row][col]) {
                setPixel(x + col, y + row, true);
            }
        }
    }
}

void Display::drawBigChar(char c, int x, int y) {
    if (c == ' ') {
        return;
    }

    uint8_t i = fontIndex(c);
    int left, right;
    bigGlyphBounds(c, left, right);
    for (int r = 0; r < TIME_FONT_BIG_HEIGHT; r++) {
        for (int col = left; col <= right; col++) {
            if (FONT_BIG[i][r][col]) setPixel(x + col - left, y + r, true);
        }
    }
}

void Display::drawTimerColon(int x, int y) {
    setPixel(x, y + 4, true);
    setPixel(x, y + 6, true);
}

// Draw one proportional text glyph by trimming the empty columns from its font cell.
void Display::drawMediumChar(char c, int x, int y) {
    if (c == ' ') {
        return;
    }
    uint8_t i = fontIndex(c);
    int left, right;
    glyphBounds(c, false, left, right);
    for (int r = 0; r < TIME_FONT_MEDIUM_HEIGHT; r++) {
        for (int col = left; col <= right; col++) {
            if (FONT_MEDIUM[i][r][col]) setPixel(x + col - left, y + r, true);
        }
    }
}

void Display::drawText(const char* s, int x, int y, bool small, int letterSpacing, int wordGap) {
    bool inWord = false;
    while (*s) {
        if (*s == ' ') {
            if (inWord) {
                x += wordGap;
                inWord = false;
            }
        } else {
            if (inWord) {
                x += letterSpacing;
            }
            if (small) {
                drawSmallChar(*s, x, y);
            } else {
                drawMediumChar(*s, x, y);
            }
            x += charWidth(*s, small);
            inWord = true;
        }
        s++;
    }
}

void Display::drawMediumText(const char* s, int x, int y) {
    drawText(s, x, y, false, 1, 2);
}

void Display::drawCenteredMediumText(const char* s, int y) {
    int w = textWidth(s, false, 1, 2);
    int x = (COLS_PER_ROW - w) / 2;
    drawMediumText(s, x, y);
}

// Draw one proportional text glyph by trimming the empty columns from its font cell.
void Display::drawSmallChar(char c, int x, int y) {
    if (c == ' ') {
        return;
    }
    uint8_t i = fontIndex(c);
    int left, right;
    glyphBounds(c, true, left, right);
    for (int r = 0; r < SEC_FONT_HEIGHT; r++) {
        for (int col = left; col <= right; col++) {
            if (FONT_SMALL[i][r][col]) setPixel(x + col - left, y + r, true);
        }
    }
}

void Display::drawSmallText(const char* s, int x, int y) {
    drawText(s, x, y, true, 1, 2);
}

void Display::drawBigText(const char* s, int x, int y) {
    bool inWord = false;
    while (*s) {
        if (*s == ' ') {
            if (inWord) {
                x += 2;
                inWord = false;
            }
        } else {
            if (inWord) {
                x += 1;
            }
            drawBigChar(*s, x, y);
            x += charWidthBig(*s);
            inWord = true;
        }
        s++;
    }
}

void Display::drawCenteredSmallText(const char* s, int y) {
    int w = textWidth(s, true, 1, 2);
    int x = (COLS_PER_ROW - w) / 2;
    drawSmallText(s, x, y);
}

void Display::drawCenteredBigText(const char* s, int y) {
    int w = textWidthBig(s, 1, 2);
    int x = (COLS_PER_ROW - w) / 2;
    drawBigText(s, x, y);
}

void Display::showOtaUpdate(bool active, unsigned int progress, unsigned int total) {
    memset(pixelBuffer, 0, sizeof(pixelBuffer));
    drawCenteredSmallText("UPDATE", 2);
    if (active && total > 0) {
        unsigned int pct = (progress * 100) / total;
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", pct);
        drawCenteredSmallText(buf, 9);
    }
    renderBuffer();
}

void Display::drawGuestWifiText(bool showSsid) {
    if (!_guestWifi || !_guestWifi->isTextAvailable()) {
        return;
    }

    const char* text = showSsid ? _guestWifi->ssid() : _guestWifi->password();
    if (text[0] == '\0') return;

    int fullWidth = textWidth(text, true, 1, 2);

    // Single line fits
    if (fullWidth <= COLS_PER_ROW) {
        drawCenteredSmallText(text, 6);
        return;
    }

    // Split at proportional midpoint
    size_t len = strlen(text);
    int halfTarget = fullWidth / 2;

    size_t split = 0;
    int cum = 0;
    bool inWord = false;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == ' ') {
            if (inWord) { cum += 2; inWord = false; }
        } else {
            if (inWord) cum += 1;
            cum += charWidth(c, true);
            inWord = true;
        }
        if (cum >= halfTarget) {
            split = i + 1;
            break;
        }
    }
    if (split == 0) split = len / 2;

    char line1[GUEST_WIFI_TEXT_MAX_LEN];
    memcpy(line1, text, split);
    line1[split] = '\0';

    const char* line2 = text + split;
    while (*line2 == ' ') line2++;

    int w1 = textWidth(line1, true, 1, 2);
    int w2 = textWidth(line2, true, 1, 2);

    if (w1 <= COLS_PER_ROW && w2 <= COLS_PER_ROW) {
        drawCenteredSmallText(line1, 2);
        drawCenteredSmallText(line2, 11);
    }
    // If either line overflows, nothing is drawn (text was displayable
    // at fetch time, so this is defensive only)
}

void Display::drawHotspotSymbolGlyph() {
    for (int row = 0; row < SYMBOL_FONT_HEIGHT; row++) {
        for (int col = 0; col < SYMBOL_FONT_WIDTH; col++) {
            if (FONT_SYMBOLS[SYMBOL_HOTSPOT][row][col]) {
                setPixel(col, row, true);
            }
        }
    }
}
