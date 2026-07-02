#include "Display.h"

#include "AppSettings.h"
#include "ClockRenderer.h"
#include "DriftTimeModel.h"
#include "MenuBindings.h"
#include "MenuRenderer.h"
#include "NewYearController.h"
#include "NewYearRenderer.h"
#include "PongClockRenderer.h"
#include "RtcClock.h"
#include "SettingsStore.h"
#include "TimeProvider.h"
#include "TimerController.h"
#include "TimerRenderer.h"
#if GUEST_WIFI_ENABLED
#include "GuestWifiController.h"
#endif
#include "WiFiManagerLite.h"
#include "MenuConfig.h"
#include "fonts.h"
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
#include "DigitTransition.h"
#endif

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
    memset(_snapshotFrame, 0, sizeof(_snapshotFrame));
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
    _newYearRenderer = new NewYearRenderer();
    _pongRenderer = new PongClockRenderer();

    _clockRenderer->init(*this, _timeProvider);
    _clockRenderer->setTimeFormat(_timeFormat);
    _clockRenderer->setInfoLineMode(_appSettings ? &_appSettings->infoLineMode : nullptr);
    _menuRenderer->init(*this, _menu);
    _timerRenderer->init(*this, _timer, *_clockRenderer);
    if (_newYear) {
        _newYearRenderer->init(*this, *_newYear);
    }
}

void Display::displayHardRefresh() {
    // Put all modules into shutdown so no glitching is visible while we
    // re-send every configuration register to each MAX7219 in the chain.
    _leds.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::ON);

    // Re-send all MAX7219 control registers to every module.
    _leds.control(MD_MAX72XX::TEST, MD_MAX72XX::OFF);       // display test off
    _leds.control(MD_MAX72XX::DECODE, MD_MAX72XX::OFF);     // no BCD decode
    _leds.control(MD_MAX72XX::SCANLIMIT, 7);                 // all 8 rows enabled
    _leds.control(MD_MAX72XX::INTENSITY, _brightness);       // current effective brightness
    _leds.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);   // normal operation

    // Clear internal MAX7219 display RAM on all modules.
    _leds.clear();
    _leds.update();

    // Force a full redraw from the software framebuffer.
    flushBufferToLeds();
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

void Display::requestScreenTransition() {
#if SCREEN_TRANSITION
    _screenTransitionPending = true;
#endif
}

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
bool Display::animationsEnabled() const {
    return _appSettings && _appSettings->transitionMode == TransitionMode::Morph;
}
#endif

void Display::applyBurstBoost(int8_t boost) {
    int8_t base = _brightness;
    if (boost > 0 && base == 0) base = 1;
    int8_t v = base + boost;
    if (v < 0) v = 0;
    if (v > 15) v = 15;
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

void Display::setRuntimeMode(DisplayMode* displayMode) {
    _displayMode = displayMode;
}

void Display::setAppSettings(AppSettings* settings) {
    _appSettings = settings;
    if (_clockRenderer && _appSettings) {
        _clockRenderer->setInfoLineMode(&_appSettings->infoLineMode);
    }
}

void Display::setInfoLineMode(InfoLineMode* infoLineMode) {
    if (_clockRenderer) {
        _clockRenderer->setInfoLineMode(infoLineMode);
    }
}

void Display::setDriftTimeModel(DriftTimeModel* driftTimeModel) {
    _driftTimeModel = driftTimeModel;
}

void Display::setTimeFormat(TimeFormat* timeFormat) {
    _timeFormat = timeFormat;
}

void Display::setDateStyle(DateStyle* dateStyle) {
    _dateStyle = dateStyle;
}

#if GUEST_WIFI_ENABLED
void Display::setGuestWifiController(GuestWifiController* c) {
    _guestWifi = c;
}
#endif

void Display::setNewYearController(NewYearController* c) {
    _newYear = c;
    if (_newYearRenderer && _newYear) {
        _newYearRenderer->init(*this, *_newYear);
    }
}

void Display::drawPongTime(ClockTime time) {
    unsigned long nowMs = millis();
    _pong.update(time, nowMs, _timeFormat ? *_timeFormat : TimeFormat::Hours24);
    _pongRenderer->render(*this, _pong, _timeFormat ? *_timeFormat : TimeFormat::Hours24);
}

void Display::resetPong(ClockTime time) {
    _pong.startFreshServe(time, millis(), _timeFormat ? *_timeFormat : TimeFormat::Hours24);
}

DateStyle Display::currentDateStyle() const {
    return _dateStyle ? *_dateStyle : DateStyle::Date;
}

void Display::loadBrightnessFromSettings() {
    _userBrightness = _settings.loadBrightness(4);
    LOGF("Brightness NVS: %d\n", (int)_userBrightness);
    setUserBrightness(_userBrightness);
}

// =============================================================================
// Top-level render dispatchers
// =============================================================================

void Display::drawClockContent() {
    clearBuffer();
#if SCREEN_TRANSITION
    noteScreenIdentity();
#endif
#if DIGIT_TRANSITIONS
    digit_transition::set_transition_mode(_appSettings ? _appSettings->transitionMode : TransitionMode::Morph);
#endif
#if REGION_TRANSITION
    {
        bool animOn = true;
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
        if (_appSettings) animOn = (_appSettings->transitionMode == TransitionMode::Morph);
#endif
        _clockRenderer->setAnimationsEnabled(animOn);
    }
#endif

    ClockTime time = _timeProvider.displayTime();
    int hours = time.hours;
    int minutes = time.minutes;
    int seconds = time.seconds;

    DisplayMode mode = _displayMode ? *_displayMode : DisplayMode::LargeDigitsOnly;
    SeparatorMode separatorMode = _appSettings ? separatorModeFor(*_appSettings, mode) : SeparatorMode::Steady;
    SeparatorMode driftSeparatorMode = _appSettings ? _appSettings->driftSeparator : SeparatorMode::Steady;
    _clockRenderer->setSeparatorModes(separatorMode, driftSeparatorMode);
    _clockRenderer->setDriftStyleActive(mode == DisplayMode::Drift);
    _clockRenderer->setBarSecondsMode(_appSettings ? _appSettings->barSeconds : BarSecondsMode::Off);
    _clockRenderer->setBinSecondsMode(_appSettings ? _appSettings->binSeconds : BinSecondsMode::On);
    switch (mode) {
        case DisplayMode::Info:
            _clockRenderer->drawInfoTime(time);
            break;
        case DisplayMode::Word:
            _clockRenderer->drawWordTime(hours, minutes);
            break;
        case DisplayMode::Roma:
            _clockRenderer->drawRomanTime(hours, minutes);
            break;
        case DisplayMode::Dial:
            _clockRenderer->drawDialTime(hours, minutes,
                !_appSettings || _appSettings->dialMarks == DialMarksMode::On);
            break;
        case DisplayMode::Bar:
            _clockRenderer->drawBarTime(hours, minutes, seconds);
            break;
        case DisplayMode::Bin:
            _clockRenderer->drawBinaryTime(hours, minutes, seconds);
            break;
        case DisplayMode::Pong:
            drawPongTime(time);
            break;
        case DisplayMode::Drift:
            if (_driftTimeModel) {
                unsigned long nowMs = millis();
                _driftTimeModel->update(time, nowMs);
                ClockTime driftTime = _driftTimeModel->displayTime(time, nowMs);
                int offsetMinutes = _driftTimeModel->offsetMinutes(time);
                int driftDirection = _driftTimeModel->driftDirection();
                bool freshChange = _driftTimeModel->displayedMinuteFresh(nowMs);
                bool separatorVisible = driftSeparatorMode == SeparatorMode::Steady ||
                                        _driftTimeModel->separatorVisible();
                _clockRenderer->drawDriftTime(driftTime.hours, driftTime.minutes, driftTime.seconds, offsetMinutes, freshChange, separatorVisible, driftDirection);
            } else {
                _clockRenderer->drawDriftTime(hours, minutes, seconds, 0, false, true, 0);
            }
            break;
        case DisplayMode::Rnd:
            _clockRenderer->drawPreview(mode, time);
            break;
        case DisplayMode::LargeDigitsOnly:
        default:
            _clockRenderer->drawBigTime(hours, minutes, seconds);
            break;
    }

    if (_newYear && _newYear->isActive() && _newYearRenderer) {
        if (_newYear->takesOverDisplay()) {
            _newYearRenderer->renderTakeover();
        } else {
            _newYearRenderer->renderOverlay();
        }
    }
}

void Display::showTime() {
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

#if GUEST_WIFI_ENABLED
    bool wasInGuestWifi = _wasGuestWifiView;
    _wasGuestWifiView = false;
    if (_timer.isGuestWifiView()) {
        clearBuffer();
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
#endif

    if (_timer.isStopwatchView() || _timer.isCountdownView()) {
        _timerRenderer->renderTimerScreen();
        return;
    }

    drawClockContent();
    renderBuffer();
}

void Display::drawStylePreview(DisplayMode mode) {
    ClockTime time = _timeProvider.displayTime();
#if DIGIT_TRANSITIONS
    digit_transition::set_transition_mode(_appSettings ? _appSettings->transitionMode : TransitionMode::Morph);
#endif
#if REGION_TRANSITION
    {
        bool animOn = true;
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
        if (_appSettings) animOn = (_appSettings->transitionMode == TransitionMode::Morph);
#endif
        _clockRenderer->setAnimationsEnabled(animOn);
    }
#endif
    SeparatorMode separatorMode;
    SeparatorMode driftSeparatorMode;
    DialMarksMode dialMarksMode;
    BarSecondsMode barSeconds;
    BinSecondsMode binSeconds;
    if (styleMenuIsEditing()) {
        separatorMode = g_stylePending.separator;
        driftSeparatorMode = g_stylePending.driftSeparator;
        dialMarksMode = g_stylePending.dialMarks;
        barSeconds = g_stylePending.barSeconds;
        binSeconds = g_stylePending.binSeconds;
    } else {
        separatorMode = _appSettings ? separatorModeFor(*_appSettings, mode) : SeparatorMode::Steady;
        driftSeparatorMode = _appSettings ? _appSettings->driftSeparator : SeparatorMode::Steady;
        dialMarksMode = _appSettings ? _appSettings->dialMarks : DialMarksMode::On;
        barSeconds = _appSettings ? _appSettings->barSeconds : BarSecondsMode::Off;
        binSeconds = _appSettings ? _appSettings->binSeconds : BinSecondsMode::On;
    }
    _clockRenderer->setSeparatorModes(separatorMode, driftSeparatorMode);
    _clockRenderer->setDriftStyleActive(mode == DisplayMode::Drift);
    _clockRenderer->setBarSecondsMode(barSeconds);
    _clockRenderer->setBinSecondsMode(binSeconds);
    if (mode == DisplayMode::Info && styleMenuInfoPreviewActive()) {
        InfoLineMode pendingInfo = g_stylePending.infoLine;
        InfoLineMode* restoreInfo = _appSettings ? &_appSettings->infoLineMode : nullptr;
        _clockRenderer->setInfoLineMode(&pendingInfo);
        _clockRenderer->drawPreview(mode, time, dialMarksMode == DialMarksMode::On);
        _clockRenderer->setInfoLineMode(restoreInfo);
        return;
    }
    if (mode == DisplayMode::Pong) {
        drawPongTime(time);
        return;
    }
    if (mode == DisplayMode::Drift && _driftTimeModel) {
        unsigned long nowMs = millis();
        _driftTimeModel->update(time, nowMs);
        ClockTime driftTime = _driftTimeModel->displayTime(time, nowMs);
        bool visible = driftSeparatorMode == SeparatorMode::Steady || _driftTimeModel->separatorVisible();
        int driftDirection = _driftTimeModel->driftDirection();
        _clockRenderer->drawDriftTime(driftTime.hours, driftTime.minutes, driftTime.seconds,
                                      _driftTimeModel->offsetMinutes(time),
                                      _driftTimeModel->displayedMinuteFresh(nowMs), visible,
                                      driftDirection);
    } else {
        _clockRenderer->drawPreview(mode, time, dialMarksMode == DialMarksMode::On);
    }
}

void Display::runTest(uint8_t seconds) {
    LOGF("Display test %ds blink %dms\n", (int)seconds, DISPLAY_TEST_BLINK_MS);

    bool wasSuppressed = !_enabled;
    if (wasSuppressed) {
        _leds.control(MD_MAX72XX::SHUTDOWN, MD_MAX72XX::OFF);
        _leds.control(MD_MAX72XX::INTENSITY, 0);
    }

    int chronoW = textWidth("CHRONO", true, 1, 2);
    int bellW   = textWidth("BELL",   true, 1, 2);
    int chronoX = (COLS_PER_ROW - chronoW) / 2;
    int bellX   = (COLS_PER_ROW - bellW) / 2;
    int chronoY = 2;
    int bellY   = chronoY + SEC_FONT_HEIGHT + 1;

    unsigned long startMs = millis();
    unsigned long lastBlinkMs = startMs;
    bool textVisible = true;

    while (true) {
        unsigned long nowMs = millis();
        if (nowMs - startMs >= (unsigned long)seconds * 1000UL) break;

#if DISPLAY_TEST_BLINK_MS > 0
        if (nowMs - lastBlinkMs >= (unsigned long)DISPLAY_TEST_BLINK_MS) {
            textVisible = !textVisible;
            lastBlinkMs = nowMs;
        }
#endif

        memset(pixelBuffer, 1, sizeof(pixelBuffer));

        if (textVisible) {
            drawInvertedSmallText("CHRONO", chronoX, chronoY);
            drawInvertedSmallText("BELL", bellX, bellY);
        }

        flushBufferToLeds();
        delay(1);
    }

    memset(pixelBuffer, 0, sizeof(pixelBuffer));
    if (wasSuppressed) {
        _brightness = 0;
    }
    flushBufferToLeds();
}

// =============================================================================
// Pixel buffer
// =============================================================================

void Display::clearBuffer() {
    memset(pixelBuffer, 0, sizeof(pixelBuffer));
    memset(_snapshotFrame, 0, sizeof(_snapshotFrame));
}

void Display::setPixel(int x, int y, bool value) {
    if (x >= 0 && x < COLS_PER_ROW && y >= 0 && y < TOTAL_ROWS) {
        pixelBuffer[x][y] = value;
        setSnapshotPixel(x, y, value);
    }
}

bool Display::getPixel(int x, int y) const {
    if (x < 0 || x >= COLS_PER_ROW || y < 0 || y >= TOTAL_ROWS) return false;
    return pixelBuffer[x][y];
}

void Display::togglePixel(int x, int y) {
    if (x >= 0 && x < COLS_PER_ROW && y >= 0 && y < TOTAL_ROWS) {
        pixelBuffer[x][y] = !pixelBuffer[x][y];
        setSnapshotPixel(x, y, pixelBuffer[x][y]);
    }
}

void Display::setAnimationPixel(int x, int y, bool value) {
    if (x >= 0 && x < COLS_PER_ROW && y >= 0 && y < TOTAL_ROWS) {
        pixelBuffer[x][y] = value;
    }
}

void Display::setSnapshotPixel(int x, int y, bool value) {
    if (x >= 0 && x < COLS_PER_ROW && y >= 0 && y < TOTAL_ROWS) {
        uint32_t mask = 1UL << x;
        if (value) {
            _snapshotFrame[y] |= mask;
        } else {
            _snapshotFrame[y] &= ~mask;
        }
    }
}

void Display::renderBuffer() {
#if SCREEN_TRANSITION
    uint32_t targetFrame[TOTAL_ROWS];
    uint32_t outputFrame[TOTAL_ROWS];
    bufferToFrame(targetFrame);
    if (_screenTransitionPending && _hasLastFrame) {
        _screenTransition.start(_lastFrame, targetFrame, millis());
    }
    _screenTransitionPending = false;
    if (_screenTransition.render(millis(), outputFrame)) {
        frameToBuffer(outputFrame);
        ScreenTransition::copyFrame(_lastFrame, outputFrame);
    } else {
        ScreenTransition::copyFrame(_lastFrame, targetFrame);
    }
    _hasLastFrame = true;
#endif
    flushBufferToLeds();
}

#if CHRONOSERVE_ENABLED
void Display::renderChronoMessageBuffer() {
#if SCREEN_TRANSITION
    _screenTransition.cancel();
    _screenTransitionPending = false;
    bufferToFrame(_lastFrame);
    _hasLastFrame = true;
#endif
    flushBufferToLeds();
}
#endif

void Display::flushBar(int bufferRow, bool flipX, bool flipY, int colOffset) {
    for (int col = 0; col < COLS_PER_ROW; col++) {
        uint8_t colData = 0;
        for (int row = 0; row < ROWS_PER_MODULE; row++) {
            if (pixelBuffer[col][row + bufferRow]) {
                int bitPosition = flipY ? (ROWS_PER_MODULE - 1 - row) : row;
                colData |= (1 << bitPosition);
            }
        }
        int displayCol = flipX ? (colOffset + COLS_PER_ROW - 1 - col) : (colOffset + col);
        _leds.setColumn(displayCol, colData);
    }
}

void Display::flushBufferToLeds() {
#if DISPLAY_FLIP == 0
    flushBar(0, true, false, 0);
    flushBar(ROWS_PER_MODULE, true, false, COLS_PER_ROW);
#else
    flushBar(ROWS_PER_MODULE, false, true, 0);
    flushBar(0, false, true, COLS_PER_ROW);
#endif
    _leds.update();
}

#if SCREEN_TRANSITION
void Display::bufferToFrame(uint32_t frame[TOTAL_ROWS]) const {
    ScreenTransition::clearFrame(frame);
    for (int y = 0; y < TOTAL_ROWS; y++) {
        for (int x = 0; x < COLS_PER_ROW; x++) {
            if (pixelBuffer[x][y]) ScreenTransition::setPixelInFrame(frame, x, y);
        }
    }
}

void Display::frameToBuffer(const uint32_t frame[TOTAL_ROWS]) {
    for (int y = 0; y < TOTAL_ROWS; y++) {
        for (int x = 0; x < COLS_PER_ROW; x++) {
            pixelBuffer[x][y] = ScreenTransition::getPixelFromFrame(frame, x, y);
        }
    }
}

void Display::noteScreenIdentity() {
    uint16_t identity;
    uint16_t contentHash = 0;

    if (_menu.isActive()) {
        identity = 0x100;
    } else if (_timer.isDateView()) {
        identity = 0x200 | (uint8_t)currentDateStyle();
#if GUEST_WIFI_ENABLED
    } else if (_timer.isGuestWifiView()) {
        identity = 0x300;
        unsigned long period = (unsigned long)GUEST_WIFI_SSID_SHOW_SECONDS * 1000UL +
                               (unsigned long)GUEST_WIFI_PASS_SHOW_SECONDS * 1000UL;
        if (period > 0 && _guestWifi && _guestWifi->ssid()[0] != '\0') {
            unsigned long elapsed = millis() - _guestWifiViewStartMs;
            contentHash = (elapsed % period) < (unsigned long)GUEST_WIFI_SSID_SHOW_SECONDS * 1000UL ? 1U : 2U;
        }
#endif
    } else {
        if (_timer.isStopwatchView()) {
            identity = 0x400;
        } else if (_timer.isCountdownView() || _timer.isCountdownExpired()) {
            identity = 0x500;
        } else if (_newYear && _newYear->isActive() && _newYear->takesOverDisplay()) {
            identity = 0x580 | (uint8_t)_newYear->phase();
        } else {
            DisplayMode mode = _displayMode ? *_displayMode : DisplayMode::LargeDigitsOnly;
            identity = 0x600 | (uint8_t)mode;
            if (mode == DisplayMode::Word || mode == DisplayMode::Roma) {
                ClockTime t = _timeProvider.displayTime();
                contentHash = (uint16_t)(t.hours * 60U + t.minutes);
            }
        }
    }

    bool animOn = (_appSettings && _appSettings->transitionMode == TransitionMode::Morph);

    if (_hasScreenIdentity && identity != _screenIdentity) {
        if (animOn || (identity & 0xF80) == 0x580) _screenTransitionPending = true;
    } else if (_hasScreenIdentity && contentHash != 0 && contentHash != _lastContentHash) {
        if (animOn) _screenTransitionPending = true;
    }
    _screenIdentity = identity;
    _lastContentHash = contentHash;
    _hasScreenIdentity = true;
}
#endif

String Display::snapshotSvg() const {
    String svg;
    svg.reserve(4096);
    svg = "<svg class=\"pixel-display\" viewBox=\"0 0 ";
    svg += String(COLS_PER_ROW);
    svg += " ";
    svg += String(TOTAL_ROWS);
    svg += "\" preserveAspectRatio=\"none\" aria-label=\"ChronoBell display snapshot\">";
    svg += "<defs><pattern id=\"pixel-off-pattern\" patternUnits=\"userSpaceOnUse\" width=\"1\" height=\"1\">";
    svg += "<circle class=\"pixel-dot off\" cx=\"0.5\" cy=\"0.5\" r=\"0.42\"></circle>";
    svg += "</pattern></defs>";
    svg += "<rect x=\"0\" y=\"0\" width=\"";
    svg += String(COLS_PER_ROW);
    svg += "\" height=\"";
    svg += String(TOTAL_ROWS);
    svg += "\" fill=\"url(#pixel-off-pattern)\"></rect>";
    for (int y = 0; y < TOTAL_ROWS; ++y) {
        for (int x = 0; x < COLS_PER_ROW; ++x) {
            if ((_snapshotFrame[y] & (1UL << x)) == 0) {
                continue;
            }
            svg += "<circle class=\"pixel-dot on\" cx=\"";
            svg += String(x + 0.5f, 1);
            svg += "\" cy=\"";
            svg += String(y + 0.5f, 1);
            svg += "\" r=\"0.42\"></circle>";
        }
    }
    svg += "</svg>";
    return svg;
}

// =============================================================================
// Font helpers (used by all renderers)
// =============================================================================

static bool fontPixel(uint8_t i, bool small, int row, int col) {
    return small ? FONT_SMALL[i][row][col] : FONT_MEDIUM[i][row][col];
}

static void glyphBounds(char c, bool small, int& left, int& right) {
    if (c == ' ') {
        left = 0;
        right = -1;
        return;
    }

    uint8_t i = charToGlyphIndex(c);
    int cellW = small ? FONT_SMALL_COLS : FONT_MEDIUM_COLS;
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

    uint8_t i = charToGlyphIndex(c);
    left = FONT_MEDIUM_COLS;
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
    return textWidth(s, cellW <= FONT_SMALL_COLS, 1, 2);
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

    uint8_t i = charToGlyphIndex(c);
    int left, right;
    bigGlyphBounds(c, left, right);
    for (int r = 0; r < TIME_FONT_BIG_HEIGHT; r++) {
        for (int col = left; col <= right; col++) {
            if (FONT_BIG[i][r][col]) setPixel(x + col - left, y + r, true);
        }
    }
}

void Display::drawTimerColon(int x, int y) {
    setPixel(x, y + 3, true);
    setPixel(x, y + 6, true);
}

// Draw one proportional text glyph by trimming the empty columns from its font cell.
void Display::drawMediumChar(char c, int x, int y) {
    if (c == ' ') {
        return;
    }
    uint8_t i = charToGlyphIndex(c);
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
    uint8_t i = charToGlyphIndex(c);
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

void Display::drawInvertedSmallText(const char* s, int x, int y) {
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
            uint8_t i = charToGlyphIndex(*s);
            int left, right;
            glyphBounds(*s, true, left, right);
            for (int r = 0; r < SEC_FONT_HEIGHT; r++) {
                for (int col = left; col <= right; col++) {
                    if (FONT_SMALL[i][r][col]) {
                        setPixel(x + col - left, y + r, false);
                    }
                }
            }
            x += (right - left + 1);
            inWord = true;
        }
        s++;
    }
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
    clearBuffer();
    drawCenteredSmallText("UPDATE", 2);
    if (active && total > 0) {
        unsigned int pct = (progress * 100) / total;
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", pct);
        drawCenteredSmallText(buf, 9);
    }
    renderBuffer();
}

#if GUEST_WIFI_ENABLED
void Display::drawGuestWifiText(bool showSsid) {
    if (!_guestWifi || !_guestWifi->isTextAvailable()) {
        return;
    }

    char ssid[LOCAL_DISPLAY_TEXT_MAX_LEN];
    char password[LOCAL_DISPLAY_TEXT_MAX_LEN];
    if (!_guestWifi->copyText(ssid, sizeof(ssid), password, sizeof(password))) {
        return;
    }

    const char* text = showSsid ? ssid : password;
    if (text[0] == '\0') return;

    int fullWidth = textWidth(text, true, 1, 2);
    if (fullWidth <= COLS_PER_ROW) {
        drawCenteredSmallText(text, 5);
        return;
    }

    size_t len = strlen(text);
    int halfTarget = fullWidth / 2;

    size_t split = 0;
    int cum = 0;
    bool inWord = false;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == ' ') {
            if (inWord) {
                cum += 2;
                inWord = false;
            }
        } else {
            if (inWord) {
                cum += 1;
            }
            cum += charWidth(c, true);
            inWord = true;
        }
        if (cum >= halfTarget) {
            split = i + 1;
            break;
        }
    }
    if (split == 0) split = len / 2;

    char line1[LOCAL_DISPLAY_TEXT_MAX_LEN];
    memcpy(line1, text, split);
    line1[split] = '\0';

    const char* line2 = text + split;
    while (*line2 == ' ') line2++;

    int w1 = textWidth(line1, true, 1, 2);
    int w2 = textWidth(line2, true, 1, 2);

    if (w1 <= COLS_PER_ROW && w2 <= COLS_PER_ROW) {
        drawCenteredSmallText(line1, 1);
        drawCenteredSmallText(line2, 10);
    }
}
#endif

#if CHRONOSERVE_ENABLED
static void drawChronoGlyphClipped(Display& display, char c, int x, int y, uint8_t mode) {
    if (c == ' ') return;

    uint8_t glyph = charToGlyphIndex(c);
    if (mode == 2) {
        int left, right;
        bigGlyphBounds(c, left, right);
        for (int row = 0; row < TIME_FONT_BIG_HEIGHT; row++) {
            for (int col = left; col <= right; col++) {
                if (FONT_BIG[glyph][row][col]) {
                    display.setPixel(x + col - left, y + row, true);
                }
            }
        }
        return;
    }

    bool small = mode == 0;
    int left, right;
    glyphBounds(c, small, left, right);
    int height = small ? SEC_FONT_HEIGHT : TIME_FONT_MEDIUM_HEIGHT;
    for (int row = 0; row < height; row++) {
        for (int col = left; col <= right; col++) {
            if (fontPixel(glyph, small, row, col)) {
                display.setPixel(x + col - left, y + row, true);
            }
        }
    }
}

static int chronoGlyphWidth(char c, uint8_t mode) {
    return mode == 2 ? Display::charWidthBig(c) : Display::charWidth(c, mode == 0);
}

static int chronoTextWidth(const String& text, uint8_t mode) {
    int width = 0;
    bool inWord = false;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == ' ') {
            if (inWord) {
                width += CHRONOSERVE_SCROLL_WORD_GAP_PX;
                inWord = false;
            }
            continue;
        }
        if (inWord) {
            width += 1;
        }
        width += chronoGlyphWidth(c, mode);
        inWord = true;
    }
    if (width > 0) {
        width += CHRONOSERVE_SCROLL_EXIT_PAD_PX;
    }
    return mode == 2
        ? width
        : width;
}

static int chronoStaticLineY(uint8_t mode) {
    int height = mode == 2 ? TIME_FONT_BIG_HEIGHT : (mode == 1 ? TIME_FONT_MEDIUM_HEIGHT : SEC_FONT_HEIGHT);
    return (TOTAL_ROWS - height) / 2;
}

static void drawChronoCenteredStaticLine(Display& display, const String& text, uint8_t mode) {
    int y = chronoStaticLineY(mode);
    switch (mode) {
        case 0:
            display.drawCenteredSmallText(text.c_str(), y);
            break;
        case 2:
            display.drawCenteredBigText(text.c_str(), y);
            break;
        case 1:
        default:
            display.drawCenteredMediumText(text.c_str(), y);
            break;
    }
}

static void drawChronoTextClipped(Display& display, const String& text, int x, int y, uint8_t mode) {
    bool inWord = false;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == ' ') {
            if (inWord) {
                x += CHRONOSERVE_SCROLL_WORD_GAP_PX;
                inWord = false;
            }
            continue;
        }
        if (inWord) x += 1;
        drawChronoGlyphClipped(display, c, x, y, mode);
        x += chronoGlyphWidth(c, mode);
        inWord = true;
    }
}

void Display::resetChronoScroll(const String& text, uint8_t mode, uint16_t stepMs, unsigned long previewStartMs) {
    _chronoScrollText = text;
    _chronoScrollMode = mode;
    _chronoScrollStepMs = stepMs > 0 ? stepMs : CHRONOSERVE_SCROLL_STEP_MS;
    _chronoScrollPreviewStartMs = previewStartMs;
    _chronoScrollLastAdvanceMs = 0;
    _chronoScrollX = COLS_PER_ROW;
    _chronoScrollTextWidth = chronoTextWidth(text, mode);
    _chronoScrollState = _chronoScrollTextWidth > 0
        ? ChronoScrollRenderState::Scrolling
        : ChronoScrollRenderState::Finished;
}

bool Display::drawChronoScrollFrame(const String& text, int y, uint8_t mode, uint16_t stepMs, unsigned long nowMs, unsigned long previewStartMs) {
    if (stepMs == 0) stepMs = CHRONOSERVE_SCROLL_STEP_MS;
    if (_chronoScrollState == ChronoScrollRenderState::Inactive ||
        _chronoScrollPreviewStartMs != previewStartMs ||
        _chronoScrollText != text ||
        _chronoScrollMode != mode ||
        _chronoScrollStepMs != stepMs) {
        resetChronoScroll(text, mode, stepMs, previewStartMs);
    }

    if (_chronoScrollState == ChronoScrollRenderState::Scrolling) {
        drawChronoTextClipped(*this, text, _chronoScrollX, y, mode);
        if (_chronoScrollX <= -_chronoScrollTextWidth) {
            _chronoScrollState = ChronoScrollRenderState::Finished;
            return true;
        }

        bool advance = false;
        if (_chronoScrollLastAdvanceMs == 0) {
            advance = true;
        } else if ((int32_t)(nowMs - _chronoScrollLastAdvanceMs) >= (int32_t)stepMs) {
            advance = true;
        }
        if (advance) {
            _chronoScrollLastAdvanceMs = nowMs;
            _chronoScrollX--;
        }
    }

    return _chronoScrollState == ChronoScrollRenderState::Finished;
}

bool Display::drawChronoMessage(const ChronoMessage& message, unsigned long nowMs, unsigned long previewStartMs) {
    clearBuffer();

    MessageLayout layout = layoutMessageText(message.renderText.length() > 0 ? message.renderText : message.title,
                                             message.renderText.length() > 0 ? String() : message.body,
                                             message.displayMode);
    if (layout.kind == MessageLayoutKind::None) {
        renderChronoMessageBuffer();
        return true;
    }

    uint16_t stepMs = CHRONOSERVE_SCROLL_STEP_MS;
    uint8_t mode = layout.displayMode;
    bool finished = true;

    switch (layout.kind) {
        case MessageLayoutKind::CenteredOneLine:
            drawChronoCenteredStaticLine(*this, layout.line1, mode);
            break;
        case MessageLayoutKind::CenteredTwoLine: {
            int top = (TOTAL_ROWS - (SEC_FONT_HEIGHT * 2 + 1)) / 2;
            drawCenteredSmallText(layout.line1.c_str(), top);
            drawCenteredSmallText(layout.line2.c_str(), top + SEC_FONT_HEIGHT + 1);
            break;
        }
        case MessageLayoutKind::Scroll:
            finished = drawChronoScrollFrame(layout.line1, mode == 2 ? 0 : (mode == 1 ? 3 : 5), mode, stepMs, nowMs, previewStartMs);
            break;
        case MessageLayoutKind::None:
        default:
            break;
    }
    renderChronoMessageBuffer();
    return finished;
}

void Display::drawUnreadMessageIndicator(int count, int priority, unsigned long nowMs) {
    if (priority < 0 || count <= 0) return;
    unsigned long blinkPeriod = priority >= 9 ? 200UL : (priority >= 7 ? 1200UL : 2400UL);
    unsigned long halfPeriod = blinkPeriod / 2;
    unsigned long burstLen = count * blinkPeriod;
    unsigned long pauseLen = 1500UL;
    unsigned long totalLen = burstLen + pauseLen;
    unsigned long t = nowMs % totalLen;
    if (t >= burstLen) return;
    unsigned long blinkIdx = t / blinkPeriod;
    unsigned long posInBlink = t % blinkPeriod;
    if (posInBlink >= halfPeriod) return;
    int bx = 0, by = TOTAL_ROWS - 1;
    if (getPixel(bx, by)) return;
    setPixel(bx, by, true);
    renderBuffer();
}
#endif
