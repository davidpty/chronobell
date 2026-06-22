#include "MenuConfig.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

#include "Config.h"
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
static bool    editCommitDisplayModeMenu(void* ctx, int16_t v);
static void    cancelDisplayModeMenu(void* ctx);
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
static int16_t getAnimMenu(void* ctx);
static void    previewAnimMenu(void* ctx, int16_t v);
static void    commitAnimMenu(void* ctx, int16_t v);
#endif
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
static void    resetStyleMenuRange();

uint8_t  g_settimeStep = 0;
uint8_t  g_setHour = 0;
uint8_t  g_setMin = 0;
uint8_t  g_setSec = 0;
uint8_t  g_setDay = 1;
uint8_t  g_setMonth = 1;
uint16_t g_setYear = 2025;
uint8_t g_styleStep = 0;
DisplayMode g_stylePreviewMode = DisplayMode::LargeDigitsOnly;
InfoLineMode g_stylePendingInfoLineMode = InfoLineMode::Seconds;
static SeparatorMode g_stylePendingSeparatorMode = SeparatorMode::Steady;
static DriftSeparatorMode g_stylePendingDriftSeparatorMode = DriftSeparatorMode::Steady;
static DialMarksMode g_stylePendingDialMarksMode = DialMarksMode::On;
static BarSecondsMode g_stylePendingBarSeconds = BarSecondsMode::Off;
static BinSecondsMode g_stylePendingBinSeconds = BinSecondsMode::On;
static bool g_styleEditing = false;

uint8_t styleMenuStep() {
    return g_styleStep;
}

DisplayMode styleMenuPreviewMode() {
    return g_stylePreviewMode;
}

InfoLineMode styleMenuPendingInfoLineMode() {
    return g_stylePendingInfoLineMode;
}

SeparatorMode styleMenuPendingSeparatorMode() {
    return g_stylePendingSeparatorMode;
}

DriftSeparatorMode styleMenuPendingDriftSeparatorMode() {
    return g_stylePendingDriftSeparatorMode;
}

DialMarksMode styleMenuPendingDialMarksMode() {
    return g_stylePendingDialMarksMode;
}

BarSecondsMode styleMenuPendingBarSeconds() {
    return g_stylePendingBarSeconds;
}

BinSecondsMode styleMenuPendingBinSeconds() {
    return g_stylePendingBinSeconds;
}

bool styleMenuIsEditing() {
    return g_styleEditing;
}

bool styleMenuInfoPreviewActive() {
    return g_stylePreviewMode == DisplayMode::Info && g_styleStep == 1;
}

enum MenuIndex : uint8_t {
    MENU_STYLE = 0,
    MENU_DATE,
    MENU_FORMAT,
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    MENU_ANIM,
#endif
    MENU_NIGHT,
    MENU_BRIGHT,
    MENU_BELL,
    MENU_SETTIME,
    MENU_HOTSPOT,
};

MenuItem MENU_ITEMS[] = {
  {"STYLE",   (int16_t)DisplayMode::Rnd,  (int16_t)DisplayMode::Drift,
              getDisplayModeMenu, previewDisplayModeMenu, commitDisplayModeMenu, nullptr,
              editCommitDisplayModeMenu, cancelDisplayModeMenu},
  {"DATE",    (int16_t)DateStyle::Date,    (int16_t)DateStyle::Czod,
              getDateStyleMenu, previewDateStyleMenu, commitDateStyleMenu, nullptr},
  {"FORMAT",  (int16_t)TimeFormat::AmPm, (int16_t)TimeFormat::Hours24,
             getTimeFormatMenu, previewTimeFormatMenu, commitTimeFormatMenu, nullptr},
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
  {"ANIM",    (int16_t)TransitionMode::Off, (int16_t)TransitionMode::Morph,
              getAnimMenu, previewAnimMenu, commitAnimMenu, nullptr},
#endif
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
        "OFF", "HOUR", "STRIKE", "HALF", "PAIRS", "TRIPLES", "SHIP", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

const char* styleValueName(int16_t value) {
    static const char* const NAMES[] = {
        "RND", "BIG", "DATA", "WORD", "ROMA", "DIAL", "BAR", "BIN", "PONG", "DRIFT", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
static const char* animValueName(int16_t value) {
    static const char* const NAMES[] = {
        "OFF", "ON", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}
#endif

static const char* infoLineValueName(int16_t value) {
    static const char* const NAMES[] = {
        "SEC", "DECI", "DATE", "WDAY", "ALT", nullptr
    };
    for (uint8_t i = 0; NAMES[i]; i++) {
        if ((int16_t)i == value) return NAMES[i];
    }
    return "?";
}

static const char* separatorValueName(int16_t value, bool drift) {
    static const char* const STANDARD[] = {"SOLID", "BLINK"};
    static const char* const DRIFT[] = {"SOLID", "BLINK"};
    if (drift) return (value >= 0 && value <= 1) ? DRIFT[value] : "?";
    return (value >= 0 && value <= 1) ? STANDARD[value] : "?";
}

static const char* dialMarksValueName(int16_t value) {
    static const char* const NAMES[] = {"OFF", "ON"};
    return (value >= 0 && value <= 1) ? NAMES[value] : "?";
}

static const char* barSecondsValueName(int16_t value) {
    static const char* const NAMES[] = {"OFF", "ON"};
    if (value >= 0 && value <= 1) return NAMES[value];
    return "?";
}

static const char* binSecondsValueName(int16_t value) {
    static const char* const NAMES[] = {"OFF", "ON"};
    if (value >= 0 && value <= 1) return NAMES[value];
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
        "12H", "24H", nullptr
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

static const char* hotspotValueName(void* ctx, int16_t value) {
    if (value == 0) {
        return "OFF";
    }
#if HOTSPOT_TIMEOUT_MINUTES == 0
    return "ON";
#else
    if (!ctx) {
        return "ON";
    }
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (!b->wifiManager.isHotspotActive()) {
        return "ON";
    }
    static char label[12];
    int16_t minutes = b->wifiManager.hotspotRemainingMenuMinutes();
    if (minutes <= 0) {
        minutes = 5;
    }
    snprintf(label, sizeof(label), "%d MIN", minutes);
    return label;
#endif
}

static const char* setTimeValueName(int16_t value) {
    return value == 0 ? "AUTO" : "MANUAL";
}

static void seedSetTimeGlobals(MenuBindings* b) {
    ClockTime t;
    ClockDate d;
    if (b->timeProvider.currentTime(t)) {
        g_setHour = (uint8_t)t.hours;
        g_setMin = (uint8_t)t.minutes;
        g_setSec = (uint8_t)t.seconds;
    }
    if (b->timeProvider.currentDate(d)) {
        g_setDay = (uint8_t)d.date;
        g_setMonth = (uint8_t)d.month;
        g_setYear = (uint16_t)d.year;
    }
}

static time_t manualTimeEpochFromGlobals() {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = g_setYear - 1900;
    tm.tm_mon = g_setMonth - 1;
    tm.tm_mday = g_setDay;
    tm.tm_hour = g_setHour;
    tm.tm_min = g_setMin;
    tm.tm_sec = g_setSec;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

const char* menuValueName(uint8_t index, int16_t value, void* ctx) {
    if (index == MENU_STYLE) {
        if (g_styleStep == 0) return styleValueName(value);
        if (g_stylePreviewMode == DisplayMode::Dial) return dialMarksValueName(value);
        if (g_stylePreviewMode == DisplayMode::Bar) return barSecondsValueName(value);
        if (g_stylePreviewMode == DisplayMode::Bin) return binSecondsValueName(value);
        if (g_stylePreviewMode == DisplayMode::Info && g_styleStep == 1) {
            return infoLineValueName(value);
        }
        return separatorValueName(value, g_stylePreviewMode == DisplayMode::Drift);
    }
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    if (index == MENU_ANIM) return animValueName(value);
#endif
    if (index == MENU_DATE) return dateStyleValueName(value);
    if (index == MENU_FORMAT) return formatValueName(value);
    if (index == MENU_NIGHT) return nightValueName(value);
    if (index == MENU_BELL) return bellValueName(value);
    if (index == MENU_SETTIME) return setTimeValueName(value);
    if (index == MENU_HOTSPOT) return hotspotValueName(ctx, value);
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
    if (g_styleStep == 1) {
        if (styleMenuInfoPreviewActive()) return (int16_t)g_stylePendingInfoLineMode;
        if (g_stylePreviewMode == DisplayMode::Drift) return (int16_t)g_stylePendingDriftSeparatorMode;
        if (g_stylePreviewMode == DisplayMode::Dial) return (int16_t)g_stylePendingDialMarksMode;
        if (g_stylePreviewMode == DisplayMode::Bar) return (int16_t)g_stylePendingBarSeconds;
        if (g_stylePreviewMode == DisplayMode::Bin) return (int16_t)g_stylePendingBinSeconds;
        return (int16_t)g_stylePendingSeparatorMode;
    }
    if (g_styleStep == 2) {
        return (int16_t)g_stylePendingSeparatorMode;
    }
    return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.displayMode;
}
static void previewDisplayModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (!g_styleEditing) {
        g_styleEditing = true;
        g_stylePendingInfoLineMode = b->appSettings.infoLineMode;
        g_stylePendingSeparatorMode = b->appSettings.bigSeparator;
        g_stylePendingDriftSeparatorMode = b->appSettings.driftSeparator;
        g_stylePendingDialMarksMode = b->appSettings.dialMarks;
        g_stylePendingBarSeconds = b->appSettings.barSeconds;
        g_stylePendingBinSeconds = b->appSettings.binSeconds;
    }
    if (g_styleStep == 0) {
        g_stylePreviewMode = clampDisplayMode((int)v);
        b->displayMode = g_stylePreviewMode;
    } else if (styleMenuInfoPreviewActive()) {
        g_stylePendingInfoLineMode = clampInfoLineMode((int)v);
    } else if (g_stylePreviewMode == DisplayMode::Info && g_styleStep == 2) {
        g_stylePendingSeparatorMode = clampSeparatorMode((int)v);
    } else if (g_stylePreviewMode == DisplayMode::Drift && g_styleStep == 1) {
        g_stylePendingDriftSeparatorMode = clampDriftSeparatorMode((int)v);
    } else if (g_stylePreviewMode == DisplayMode::Dial && g_styleStep == 1) {
        g_stylePendingDialMarksMode = clampDialMarksMode((int)v);
    } else if (g_stylePreviewMode == DisplayMode::Bar && g_styleStep == 1) {
        g_stylePendingBarSeconds = clampBarSecondsMode((int)v);
    } else if (g_stylePreviewMode == DisplayMode::Bin && g_styleStep == 1) {
        g_stylePendingBinSeconds = clampBinSecondsMode((int)v);
    } else if (g_styleStep == 1) {
        g_stylePendingSeparatorMode = clampSeparatorMode((int)v);
    }
}
static void commitDisplayModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (g_styleStep == 0) {
        DisplayMode mode = clampDisplayMode((int)v);
        g_stylePreviewMode = mode;
        b->appSettings.displayMode = mode;
        b->displayMode = mode;
        b->settingsStore.saveDisplayMode(mode);
        return;
    }

    if (styleMenuInfoPreviewActive()) {
        InfoLineMode info = clampInfoLineMode((int)v);
        g_stylePendingInfoLineMode = info;
        b->appSettings.infoLineMode = info;
        b->settingsStore.saveInfoLineMode(info);
        return;
    }

    if (g_stylePreviewMode == DisplayMode::Info && g_styleStep == 2) {
        SeparatorMode separator = clampSeparatorMode((int)v);
        g_stylePendingSeparatorMode = separator;
        setSeparatorModeFor(b->appSettings, g_stylePreviewMode, separator);
        b->settingsStore.saveSeparatorMode(DisplayMode::Info, separator);
        return;
    }

    if (g_stylePreviewMode == DisplayMode::Drift && g_styleStep == 1) {
        DriftSeparatorMode separator = clampDriftSeparatorMode((int)v);
        g_stylePendingDriftSeparatorMode = separator;
        b->appSettings.driftSeparator = separator;
        b->settingsStore.saveDriftSeparatorMode(separator);
        return;
    }

    if (g_stylePreviewMode == DisplayMode::Dial && g_styleStep == 1) {
        DialMarksMode marks = clampDialMarksMode((int)v);
        g_stylePendingDialMarksMode = marks;
        b->appSettings.dialMarks = marks;
        b->settingsStore.saveDialMarksMode(marks);
        return;
    }

    if (g_stylePreviewMode == DisplayMode::Bar && g_styleStep == 1) {
        BarSecondsMode secs = clampBarSecondsMode((int)v);
        g_stylePendingBarSeconds = secs;
        b->appSettings.barSeconds = secs;
        b->settingsStore.saveBarSeconds(secs);
        return;
    }

    if (g_stylePreviewMode == DisplayMode::Bin && g_styleStep == 1) {
        BinSecondsMode secs = clampBinSecondsMode((int)v);
        g_stylePendingBinSeconds = secs;
        b->appSettings.binSeconds = secs;
        b->settingsStore.saveBinSeconds(secs);
        return;
    }

    if (g_styleStep == 1) {
        SeparatorMode separator = clampSeparatorMode((int)v);
        g_stylePendingSeparatorMode = separator;
        setSeparatorModeFor(b->appSettings, g_stylePreviewMode, separator);
        b->settingsStore.saveSeparatorMode(g_stylePreviewMode, separator);
        return;
    }
}

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
static int16_t getAnimMenu(void* ctx) {
    return (int16_t)static_cast<MenuBindings*>(ctx)->transitionMode;
}
static void previewAnimMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    TransitionMode mode = clampTransitionMode((int)v);
    b->appSettings.transitionMode = mode;
    b->transitionMode = mode;
}
static void commitAnimMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    TransitionMode mode = clampTransitionMode((int)v);
    b->appSettings.transitionMode = mode;
    b->transitionMode = mode;
    b->settingsStore.saveTransitionMode(mode);
}
#endif
static void resetStyleMenuRange() {
    MENU_ITEMS[MENU_STYLE].minValue = (int16_t)DisplayMode::Rnd;
    MENU_ITEMS[MENU_STYLE].maxValue = (int16_t)DisplayMode::Drift;
}
static bool editCommitDisplayModeMenu(void* ctx, int16_t v) {
    (void)ctx;
    if (g_styleStep == 0) {
        g_stylePreviewMode = clampDisplayMode((int)v);
        if (g_stylePreviewMode == DisplayMode::Info) {
            g_styleStep = 1;
            MENU_ITEMS[MENU_STYLE].minValue = 0;
            MENU_ITEMS[MENU_STYLE].maxValue = (int16_t)InfoLineMode::Alt;
            return true;
        }
        if (g_stylePreviewMode == DisplayMode::Bar) {
            g_styleStep = 1;
            MENU_ITEMS[MENU_STYLE].minValue = 0;
            MENU_ITEMS[MENU_STYLE].maxValue = (int16_t)BarSecondsMode::On;
            return true;
        }
        if (g_stylePreviewMode == DisplayMode::Bin) {
            g_styleStep = 1;
            MENU_ITEMS[MENU_STYLE].minValue = 0;
            MENU_ITEMS[MENU_STYLE].maxValue = (int16_t)BinSecondsMode::On;
            return true;
        }
        if (g_stylePreviewMode == DisplayMode::LargeDigitsOnly ||
            g_stylePreviewMode == DisplayMode::Drift ||
            g_stylePreviewMode == DisplayMode::Dial) {
            g_styleStep = 1;
            MENU_ITEMS[MENU_STYLE].minValue = 0;
            MENU_ITEMS[MENU_STYLE].maxValue = (int16_t)SeparatorMode::Pulse;
            return true;
        }
        g_styleStep = 0;
        g_styleEditing = false;
        resetStyleMenuRange();
        return false;
    } else if (styleMenuInfoPreviewActive()) {
        g_stylePendingInfoLineMode = clampInfoLineMode((int)v);
        g_styleStep = 2;
        MENU_ITEMS[MENU_STYLE].minValue = 0;
        MENU_ITEMS[MENU_STYLE].maxValue = (int16_t)SeparatorMode::Pulse;
        return true;
    } else if (g_stylePreviewMode == DisplayMode::Info && g_styleStep == 2) {
        g_styleStep = 0;
        g_styleEditing = false;
        resetStyleMenuRange();
    } else if (g_stylePreviewMode == DisplayMode::Drift && g_styleStep == 1) {
        g_styleStep = 0;
        g_styleEditing = false;
        resetStyleMenuRange();
    } else if (g_styleStep == 1) {
        g_styleStep = 0;
        g_styleEditing = false;
        resetStyleMenuRange();
    }
    return false;
}
static void cancelDisplayModeMenu(void* ctx) {
    if (!g_styleEditing) return;
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->displayMode = b->appSettings.displayMode;
    g_stylePendingInfoLineMode = b->appSettings.infoLineMode;
    g_stylePendingSeparatorMode = b->appSettings.bigSeparator;
    g_stylePendingDriftSeparatorMode = b->appSettings.driftSeparator;
    g_stylePendingDialMarksMode = b->appSettings.dialMarks;
    g_stylePendingBarSeconds = b->appSettings.barSeconds;
    g_stylePendingBinSeconds = b->appSettings.binSeconds;
    g_styleStep = 0;
    g_styleEditing = false;
    resetStyleMenuRange();
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
        b->wifiManager.resetHotspotTimer();
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
        if (manual) {
            seedSetTimeGlobals(b);
            time_t epoch = manualTimeEpochFromGlobals();
            if (epoch > 0) {
                b->appSettings.manualTime.epoch = (unsigned long)epoch;
            }
        }
        b->settingsStore.save(b->appSettings);
        return;
    }
    previewSetTimeMenu(ctx, v);
    b->appSettings.manualTime.enabled = true;
    time_t epoch = manualTimeEpochFromGlobals();
    if (epoch > 0) {
        b->appSettings.manualTime.epoch = (unsigned long)epoch;
    }
    b->settingsStore.save(b->appSettings);
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
    ClockTime time;
    time.hours = h;
    time.minutes = m;
    time.seconds = s;
    b->bellController.preview(clampBellMode((int)mode), time, timeValid);
}
