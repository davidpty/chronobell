#include "MenuConfig.h"

#include <time.h>

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
static int16_t getSetTimeMenu(void* ctx);
static void    previewSetTimeMenu(void* ctx, int16_t v);
static void    commitSetTimeMenu(void* ctx, int16_t v);
static bool    editCommitSetTimeMenu(void* ctx, int16_t v);
static void    cancelSetTimeMenu(void* ctx);
static void    previewBellForMode(void* ctx, int16_t mode);

uint8_t  g_settimeStep = 0;
uint8_t  g_setHour = 0;
uint8_t  g_setMin = 0;
uint8_t  g_setSec = 0;
uint8_t  g_setDay = 1;
uint8_t  g_setMonth = 1;
uint16_t g_setYear = 2025;

enum MenuIndex : uint8_t {
    MENU_STYLE = 0,
    MENU_DATE,
    MENU_FORMAT,
    MENU_NIGHT,
    MENU_BRIGHT,
    MENU_BELL,
    MENU_SETTIME,
    MENU_HOTSPOT,
};

MenuItem MENU_ITEMS[] = {
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
  {"SETTIME", 0, 1,
              getSetTimeMenu, previewSetTimeMenu, commitSetTimeMenu, nullptr, editCommitSetTimeMenu, cancelSetTimeMenu},
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

static const char* setTimeValueName(int16_t value) {
    return value == 0 ? "ATOMC" : "MANUAL";
}

const char* menuValueName(uint8_t index, int16_t value, void* /*ctx*/) {
    if (index == MENU_STYLE) return styleValueName(value);
    if (index == MENU_DATE) return dateStyleValueName(value);
    if (index == MENU_FORMAT) return formatValueName(value);
    if (index == MENU_NIGHT) return nightValueName(value);
    if (index == MENU_BELL) return bellValueName(value);
    if (index == MENU_SETTIME) return setTimeValueName(value);
    if (index == MENU_HOTSPOT) return onOffValueName(value);
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

static int16_t getSetTimeMenu(void* ctx) {
    if (g_settimeStep >= 1) {
        switch (g_settimeStep) {
            case 1: return (int16_t)g_setHour;
            case 2: return (int16_t)g_setMin;
            case 3: return (int16_t)g_setSec;
            case 4: return (int16_t)g_setDay;
            case 5: return (int16_t)g_setMonth;
            case 6: return (int16_t)g_setYear;
        }
    }
    return static_cast<MenuBindings*>(ctx)->appSettings.manualTime.enabled ? 1 : 0;
}

static void previewSetTimeMenu(void* /*ctx*/, int16_t v) {
    if (g_settimeStep == 0) return;
    switch (g_settimeStep) {
        case 1: g_setHour = (uint8_t)constrain(v, 0, 23); break;
        case 2: g_setMin  = (uint8_t)constrain(v, 0, 59); break;
        case 3: g_setSec  = (uint8_t)constrain(v, 0, 59); break;
        case 4: g_setDay  = (uint8_t)constrain(v, 1, 31); break;
        case 5: g_setMonth = (uint8_t)constrain(v, 1, 12); break;
        case 6: g_setYear  = (uint16_t)constrain(v, 2024, 2035); break;
    }
}

static void commitSetTimeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (g_settimeStep == 0) {
        bool manual = (v != 0);
        b->appSettings.manualTime.enabled = manual;
        b->settingsStore.save(b->appSettings);
        return;
    }
    previewSetTimeMenu(ctx, v);
}

static void setSetTimeRange(uint8_t step) {
    MenuItem& item = MENU_ITEMS[MENU_SETTIME];
    switch (step) {
        case 0: item.minValue = 0; item.maxValue = 1; break;
        case 1: item.minValue = 0; item.maxValue = 23; break;
        case 2:
        case 3: item.minValue = 0; item.maxValue = 59; break;
        case 4: item.minValue = 1; item.maxValue = 31; break;
        case 5: item.minValue = 1; item.maxValue = 12; break;
        case 6: item.minValue = 2024; item.maxValue = 2035; break;
    }
}

static bool editCommitSetTimeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (g_settimeStep == 0 && v == 0) {
        return false;
    }
    if (g_settimeStep == 0) {
        ClockTime t;
        ClockDate d;
        b->timeProvider.currentTime(t);
        b->timeProvider.currentDate(d);
        g_setHour  = (uint8_t)t.hours;
        g_setMin   = (uint8_t)t.minutes;
        g_setSec   = (uint8_t)t.seconds;
        g_setDay   = (uint8_t)d.date;
        g_setMonth = (uint8_t)d.month;
        g_setYear  = (uint16_t)d.year;
        g_settimeStep = 1;
        setSetTimeRange(1);
        return true;
    }
    if (g_settimeStep < 6) {
        g_settimeStep++;
        setSetTimeRange(g_settimeStep);
        return true;
    }
    struct tm tm;
    tm.tm_year = g_setYear - 1900;
    tm.tm_mon  = g_setMonth - 1;
    tm.tm_mday = g_setDay;
    tm.tm_hour = g_setHour;
    tm.tm_min  = g_setMin;
    tm.tm_sec  = g_setSec;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    b->appSettings.manualTime.enabled = true;
    b->appSettings.manualTime.epoch = (unsigned long)epoch;
    b->settingsStore.save(b->appSettings);
    b->timeProvider.setRtcFromEpoch(epoch);
    b->timeProvider.readRtc();
    g_settimeStep = 0;
    setSetTimeRange(0);
    return false;
}

static void cancelSetTimeMenu(void* ctx) {
    g_settimeStep = 0;
    setSetTimeRange(0);
}

static void previewBellForMode(void* ctx, int16_t mode) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    int h = 0, m = 0, s = 0;
    bool timeValid = b->getCurrentClockTime(h, m, s);
    ClockTime time{h, m, s};
    b->bellController.preview(clampBellMode((int)mode), time, timeValid);
}
