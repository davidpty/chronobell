#include "MenuConfig.h"

#include "AppSettings.h"
#include "BellController.h"
#include "Display.h"
#include "MenuBindings.h"
#include "RtcClock.h"
#include "SettingsStore.h"
#include "TimeProvider.h"
#include "WiFiManagerLite.h"

static int16_t getBellModeMenu(void* ctx);
static void    previewBellModeMenu(void* ctx, int16_t v);
static void    commitBellModeMenu(void* ctx, int16_t v);
static int16_t getDisplayModeMenu(void* ctx);
static void    previewDisplayModeMenu(void* ctx, int16_t v);
static void    commitDisplayModeMenu(void* ctx, int16_t v);
static int16_t getDateStyleMenu(void* ctx);
static void    previewDateStyleMenu(void* ctx, int16_t v);
static void    commitDateStyleMenu(void* ctx, int16_t v);
static int16_t getTimeFormatMenu(void* ctx);
static void    previewTimeFormatMenu(void* ctx, int16_t v);
static void    commitTimeFormatMenu(void* ctx, int16_t v);
static int16_t getBrightnessMenu(void* ctx);
static void    previewBrightnessMenu(void* ctx, int16_t v);
static void    commitBrightnessMenu(void* ctx, int16_t v);
static int16_t getNightModeMenu(void* ctx);
static void    previewNightModeMenu(void* ctx, int16_t v);
static void    commitNightModeMenu(void* ctx, int16_t v);
static int16_t getHotspotMenu(void* ctx);
static void    previewHotspotMenu(void* ctx, int16_t v);
static void    commitHotspotMenu(void* ctx, int16_t v);
static void    previewBellForMode(void* ctx, int16_t mode);

const MenuItem MENU_ITEMS[] = {
  {"STYLE",   (int16_t)DisplayMode::Rnd,  (int16_t)DisplayMode::Bin,
              getDisplayModeMenu, previewDisplayModeMenu, commitDisplayModeMenu, nullptr},
  {"DATE",    (int16_t)DateStyle::Date,    (int16_t)DateStyle::Czod,
              getDateStyleMenu, previewDateStyleMenu, commitDateStyleMenu, nullptr},
  {"FORMAT",  (int16_t)TimeFormat::Hours24, (int16_t)TimeFormat::AmPm,
             getTimeFormatMenu, previewTimeFormatMenu, commitTimeFormatMenu, nullptr},
  {"NIGHT",   (int16_t)NightMode::Off,     (int16_t)NightMode::Mute,
              getNightModeMenu, previewNightModeMenu, commitNightModeMenu, nullptr},
  {"BRIGHT",  0, 15,
              getBrightnessMenu, previewBrightnessMenu, commitBrightnessMenu, nullptr},
  {"BELL",    (int16_t)BellMode::Off,      (int16_t)BellMode::Ships,
              getBellModeMenu, previewBellModeMenu, commitBellModeMenu, previewBellForMode},
  {"HOTSPOT", 0, 1,
              getHotspotMenu, previewHotspotMenu, commitHotspotMenu, nullptr},
};
const uint8_t MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

const char* bellValueName(int16_t value) {
    static const char* const NAMES[] = {
        "OFF", "DING", "HOUR", "HALF", "PAIR", "SHIP", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

const char* styleValueName(int16_t value) {
    static const char* const NAMES[] = {
        "RND", "BIG", "SEC", "DECI", "DATE", "WORD", "ROMA", "BIN", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

const char* dateStyleValueName(int16_t value) {
    static const char* const NAMES[] = {
        "DATE", "YEAR", "MOON", "ZOD", "CZOD", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

const char* formatValueName(int16_t value) {
    static const char* const NAMES[] = {
        "24H", "12H", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

const char* nightValueName(int16_t value) {
    // LED labels avoid '+' (no glyph in the font, would render as '0').
    //   LOWM  (LOW+MUTE)                  : "LOW with Mute"
    //   DRKM  (DARK+MUTE)                 : "DARK with Mute"
    static const char* const NAMES[] = {
        "OFF", "LOW", "LOWM", "DARK", "DRKM", "MUTE", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

static const char* onOffValueName(int16_t value) {
    return value == 0 ? "OFF" : "ON";
}

const char* menuValueName(uint8_t index, int16_t value, void* /*ctx*/) {
    if (index == 0) return styleValueName(value);
    if (index == 1) return dateStyleValueName(value);
    if (index == 2) return formatValueName(value);
    if (index == 3) return nightValueName(value);
    if (index == 5) return bellValueName(value);
    if (index == 6) return onOffValueName(value);
    return nullptr;
}

bool MenuBindings::getCurrentClockTime(int& h, int& m, int& s) const {
    ClockTime t;
    bool ok = timeProvider.currentTime(t);
    h = t.hours;
    m = t.minutes;
    s = t.seconds;
    return ok;
}

static int16_t getBellModeMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.bellMode;
}
static void previewBellModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->bellMode = clampBellMode((int)v);
}
static void commitBellModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    BellMode mode = clampBellMode((int)v);
    b->appSettings.bellMode = mode;
    b->bellMode = mode;
    b->settingsStore.saveBellMode(mode);
}
static int16_t getDisplayModeMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.displayMode;
}
static void previewDisplayModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->displayMode = clampDisplayMode((int)v);
}
static void commitDisplayModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    DisplayMode mode = clampDisplayMode((int)v);
    b->appSettings.displayMode = mode;
    b->displayMode = mode;
    b->settingsStore.saveDisplayMode(mode);
}
static int16_t getDateStyleMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.dateStyle;
}
static void previewDateStyleMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->appSettings.dateStyle = clampDateStyle((int)v);
}
static void commitDateStyleMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    DateStyle style = clampDateStyle((int)v);
    b->appSettings.dateStyle = style;
    b->settingsStore.saveDateStyle(style);
}
static int16_t getTimeFormatMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.timeFormat;
}
static void previewTimeFormatMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->timeFormat = clampTimeFormat((int)v);
}
static void commitTimeFormatMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    TimeFormat format = clampTimeFormat((int)v);
    b->appSettings.timeFormat = format;
    b->timeFormat = format;
    b->settingsStore.saveTimeFormat(format);
}
static int16_t getBrightnessMenu(void* ctx) {
    return static_cast<MenuBindings*>(ctx)->display.getUserBrightness();
}
static void previewBrightnessMenu(void* ctx, int16_t v) {
    static_cast<MenuBindings*>(ctx)->display.setUserBrightness((int8_t)v);
}
static void commitBrightnessMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->display.setUserBrightness((int8_t)v);
    b->settingsStore.saveBrightness((int8_t)v);
}
static int16_t getNightModeMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.nightMode;
}
static void previewNightModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->nightMode = clampNightMode((int)v);
}
static void commitNightModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    NightMode mode = clampNightMode((int)v);
    b->appSettings.nightMode = mode;
    b->nightMode = mode;
    b->settingsStore.saveNightMode(mode);
}

static int16_t getHotspotMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->wifiManager.isHotspotActive();
}
static void previewHotspotMenu(void* ctx, int16_t v) {
    (void)ctx;
    (void)v;
}
static void commitHotspotMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (v) {
        b->wifiManager.startHotspot();
    } else {
        b->wifiManager.stopHotspot();
    }
}

static void previewBellForMode(void* ctx, int16_t mode) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    int h = 0, m = 0, s = 0;
    bool timeValid = b->getCurrentClockTime(h, m, s);
    ClockTime time{h, m, s};
    b->bellController.preview(clampBellMode((int)mode), time, timeValid);
}
