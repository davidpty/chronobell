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
static int16_t getAlarmMenu(void* ctx);
static void    previewAlarmMenu(void* ctx, int16_t v);
static void    commitAlarmMenu(void* ctx, int16_t v);
static bool    editCommitAlarmMenu(void* ctx, int16_t v);
static void    cancelAlarmMenu(void* ctx);
static void    setAlarmRange(uint8_t step);

uint8_t  g_settimeStep = 0;
uint8_t  g_setHour = 0;
uint8_t  g_setMin = 0;
uint8_t  g_setSec = 0;
uint8_t  g_setDay = 1;
uint8_t  g_setMonth = 1;
uint16_t g_setYear = 2025;
uint8_t g_alarmStep = 0;
uint8_t g_alarmHour = 7;
uint8_t g_alarmMin  = 0;
uint8_t g_styleStep = 0;
DisplayMode g_stylePreviewMode = DisplayMode::LargeDigitsOnly;
StyleConfig g_stylePending;
static bool g_styleEditing = false;

uint8_t styleMenuStep() {
    return g_styleStep;
}

DisplayMode styleMenuPreviewMode() {
    return g_stylePreviewMode;
}

bool styleMenuIsEditing() {
    return g_styleEditing;
}

bool styleMenuInfoPreviewActive() {
    return g_stylePreviewMode == DisplayMode::Info && g_styleStep == 1;
}

// ---------------------------------------------------------------------------
// Style-field helpers — centralise which pending/AppSettings field to
// read, write, save per (DisplayMode × style-step).
// ---------------------------------------------------------------------------

static int16_t readField(const StyleConfig& cfg, StyleField field) {
    switch (field) {
        case StyleField::InfoLine:       return (int16_t)cfg.infoLine;
        case StyleField::Separator:      return (int16_t)cfg.separator;
        case StyleField::DriftSeparator: return (int16_t)cfg.driftSeparator;
        case StyleField::DialMarks:      return (int16_t)cfg.dialMarks;
        case StyleField::BarSeconds:     return (int16_t)cfg.barSeconds;
        case StyleField::BinSeconds:     return (int16_t)cfg.binSeconds;
        case StyleField::RndInterval:    return (int16_t)cfg.rndInterval;
        default:                         return 0;
    }
}

static void writeField(StyleConfig& cfg, StyleField field, int16_t v) {
    switch (field) {
        case StyleField::InfoLine:       cfg.infoLine = clampInfoLineMode((int)v); break;
        case StyleField::Separator:      cfg.separator = clampSeparatorMode((int)v); break;
        case StyleField::DriftSeparator: cfg.driftSeparator = clampSeparatorMode((int)v); break;
        case StyleField::DialMarks:      cfg.dialMarks = clampDialMarksMode((int)v); break;
        case StyleField::BarSeconds:     cfg.barSeconds = clampBarSecondsMode((int)v); break;
        case StyleField::BinSeconds:     cfg.binSeconds = clampBinSecondsMode((int)v); break;
        case StyleField::RndInterval:    cfg.rndInterval = clampRndIntervalMode((int)v); break;
        default:                         break;
    }
}

static void commitField(AppSettings& s, StyleField field, DisplayMode mode, int16_t v) {
    switch (field) {
        case StyleField::InfoLine:       s.infoLineMode = clampInfoLineMode((int)v); break;
        case StyleField::Separator:      setSeparatorModeFor(s, mode, clampSeparatorMode((int)v)); break;
        case StyleField::DriftSeparator: s.driftSeparator = clampSeparatorMode((int)v); break;
        case StyleField::DialMarks:      s.dialMarks = clampDialMarksMode((int)v); break;
        case StyleField::BarSeconds:     s.barSeconds = clampBarSecondsMode((int)v); break;
        case StyleField::BinSeconds:     s.binSeconds = clampBinSecondsMode((int)v); break;
        case StyleField::RndInterval:    s.rndInterval = clampRndIntervalMode((int)v); break;
        default:                         break;
    }
}

static void saveField(SettingsStore& store, StyleField field, DisplayMode mode, int16_t v) {
    switch (field) {
        case StyleField::InfoLine:       store.saveInfoLineMode(clampInfoLineMode((int)v)); break;
        case StyleField::Separator:      store.saveSeparatorMode(mode, clampSeparatorMode((int)v)); break;
        case StyleField::DriftSeparator: store.saveSeparatorMode(DisplayMode::Drift, clampSeparatorMode((int)v)); break;
        case StyleField::DialMarks:      store.saveDialMarksMode(clampDialMarksMode((int)v)); break;
        case StyleField::BarSeconds:     store.saveBarSeconds(clampBarSecondsMode((int)v)); break;
        case StyleField::BinSeconds:     store.saveBinSeconds(clampBinSecondsMode((int)v)); break;
        case StyleField::RndInterval:    store.saveRndInterval(clampRndIntervalMode((int)v)); break;
        default:                         break;
    }
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
    MENU_ALARM,
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
  {"ALARM", 0, 4,
               getAlarmMenu, previewAlarmMenu, commitAlarmMenu, nullptr, editCommitAlarmMenu, cancelAlarmMenu},
  {"SETTIME", 0, 1,
               getSetTimeMenu, previewSetTimeMenu, commitSetTimeMenu, nullptr, editCommitSetTimeMenu, cancelSetTimeMenu},
  {"HOTSPOT", 0, 1,
               getHotspotMenu, previewHotspotMenu, commitHotspotMenu, nullptr},
};
const uint8_t MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

const char* alarmValueName(int16_t value) {
    switch (value) {
        case 0: return "OFF";
        case 1: return "ONCE";
        case 2: return "DAILY";
        case 3: return "WDAY";
        case 4: return "WEND";
        default: return "?";
    }
}

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
        "RND", "BIG", "INFO", "WORD", "ROMA", "DIAL", "BAR", "BIN", "PONG", "DRIFT", nullptr
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

static const char* rndIntervalValueName(int16_t value) {
    static const char* const NAMES[] = {
        "1", "5", "10", "15", "30", "60", "90", "2H",
        "4H", "6H", "12H", "24H"
    };
    if (value >= 0 && value < (int16_t)RND_INTERVAL_COUNT) return NAMES[value];
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

static const char* fieldValueName(StyleField field, int16_t value, bool drift) {
    switch (field) {
        case StyleField::InfoLine:       return infoLineValueName(value);
        case StyleField::Separator:      return separatorValueName(value, false);
        case StyleField::DriftSeparator: return separatorValueName(value, true);
        case StyleField::DialMarks:      return dialMarksValueName(value);
        case StyleField::BarSeconds:     return barSecondsValueName(value);
        case StyleField::BinSeconds:     return binSecondsValueName(value);
        case StyleField::RndInterval:    return rndIntervalValueName(value);
        default:                         return "?";
    }
}

const char* menuValueName(uint8_t index, int16_t value, void* ctx) {
    if (index == MENU_STYLE) {
        if (g_styleStep == 0) return styleValueName(value);
        const StyleTrait& t = styleTraitFor(g_stylePreviewMode);
        const StyleStepConfig* s = (g_styleStep == 1) ? &t.step1 : &t.step2;
        return fieldValueName(s->field, value, g_stylePreviewMode == DisplayMode::Drift);
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
    if (index == MENU_ALARM) return alarmValueName(value);
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
    if (g_styleStep == 0) {
        return (int16_t)static_cast<MenuBindings*>(ctx)->appSettings.displayMode;
    }
    const StyleTrait& t = styleTraitFor(g_stylePreviewMode);
    const StyleStepConfig* s = (g_styleStep == 1) ? &t.step1 : &t.step2;
    return readField(g_stylePending, s->field);
}
static void previewDisplayModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (!g_styleEditing) {
        g_styleEditing = true;
        g_stylePending.infoLine = b->appSettings.infoLineMode;
        g_stylePending.separator = b->appSettings.bigSeparator;
        g_stylePending.driftSeparator = b->appSettings.driftSeparator;
        g_stylePending.dialMarks = b->appSettings.dialMarks;
        g_stylePending.barSeconds = b->appSettings.barSeconds;
        g_stylePending.binSeconds = b->appSettings.binSeconds;
        g_stylePending.rndInterval = b->appSettings.rndInterval;
    }
    if (g_styleStep == 0) {
        g_stylePreviewMode = clampDisplayMode((int)v);
        b->displayMode = g_stylePreviewMode;
    } else {
        const StyleTrait& t = styleTraitFor(g_stylePreviewMode);
        const StyleStepConfig* s = (g_styleStep == 1) ? &t.step1 : &t.step2;
        writeField(g_stylePending, s->field, v);
    }
}
static void commitDisplayModeMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (g_styleStep == 0) {
        DisplayMode mode = clampDisplayMode((int)v);
        g_stylePreviewMode = mode;
        b->appSettings.displayMode = mode;
        b->displayMode = mode;
        if (b->settingsStore.saveDisplayMode(mode)) {
            b->settingsStore.clearTemporaryStyle();
        }
        return;
    }
    const StyleTrait& t = styleTraitFor(g_stylePreviewMode);
    const StyleStepConfig* s = (g_styleStep == 1) ? &t.step1 : &t.step2;
    writeField(g_stylePending, s->field, v);
    commitField(b->appSettings, s->field, g_stylePreviewMode, v);
    saveField(b->settingsStore, s->field, g_stylePreviewMode, v);
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
    const StyleTrait& t = styleTraitFor(g_stylePreviewMode);
    if (g_styleStep == 0) {
        g_stylePreviewMode = clampDisplayMode((int)v);
        const StyleTrait& t0 = styleTraitFor(g_stylePreviewMode);
        if (t0.step1.label) {
            g_styleStep = 1;
            MENU_ITEMS[MENU_STYLE].minValue = t0.step1.min;
            MENU_ITEMS[MENU_STYLE].maxValue = t0.step1.max;
            return true;
        }
        g_styleStep = 0;
        g_styleEditing = false;
        resetStyleMenuRange();
        return false;
    }
    if (t.step2.label && g_styleStep == 1) {
        writeField(g_stylePending, t.step1.field, v);
        g_styleStep = 2;
        MENU_ITEMS[MENU_STYLE].minValue = t.step2.min;
        MENU_ITEMS[MENU_STYLE].maxValue = t.step2.max;
        return true;
    }
    g_styleStep = 0;
    g_styleEditing = false;
    resetStyleMenuRange();
    return false;
}
static void cancelDisplayModeMenu(void* ctx) {
    if (!g_styleEditing) return;
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    b->displayMode = b->appSettings.displayMode;
    g_stylePending.infoLine = b->appSettings.infoLineMode;
    g_stylePending.separator = b->appSettings.bigSeparator;
    g_stylePending.driftSeparator = b->appSettings.driftSeparator;
    g_stylePending.dialMarks = b->appSettings.dialMarks;
    g_stylePending.barSeconds = b->appSettings.barSeconds;
    g_stylePending.binSeconds = b->appSettings.binSeconds;
    g_stylePending.rndInterval = b->appSettings.rndInterval;
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

static int16_t getAlarmMenu(void* ctx) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    switch (g_alarmStep) {
        case 0: return (int16_t)b->alarmMode;
        case 1: return (int16_t)g_alarmHour;
        case 2: return (int16_t)g_alarmMin;
    }
    return 0;
}

static void previewAlarmMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    switch (g_alarmStep) {
        case 0: b->alarmMode = (uint8_t)constrain(v, 0, 4); break;
        case 1: g_alarmHour = (uint8_t)constrain(v, 0, 23); break;
        case 2: g_alarmMin  = (uint8_t)constrain(v, 0, 59); break;
    }
}

static void commitAlarmMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    switch (g_alarmStep) {
        case 0:
            b->alarmMode = (uint8_t)constrain(v, 0, 4);
            b->appSettings.alarm.mode = b->alarmMode;
            b->settingsStore.saveAlarmMode(b->alarmMode);
            break;
        case 1:
            g_alarmHour = (uint8_t)constrain(v, 0, 23);
            b->appSettings.alarm.hour = g_alarmHour;
            b->alarmHour = g_alarmHour;
            b->settingsStore.saveAlarmHour(g_alarmHour);
            break;
        case 2:
            g_alarmMin = (uint8_t)constrain(v, 0, 59);
            b->appSettings.alarm.minute = g_alarmMin;
            b->alarmMin = g_alarmMin;
            b->settingsStore.saveAlarmMin(g_alarmMin);
            break;
    }
}

static void setAlarmRange(uint8_t step) {
    MenuItem& item = MENU_ITEMS[MENU_ALARM];
    switch (step) {
        case 0: item.minValue = 0; item.maxValue = 4; break;
        case 1: item.minValue = 0; item.maxValue = 23; break;
        case 2: item.minValue = 0; item.maxValue = 59; break;
    }
}

static bool editCommitAlarmMenu(void* ctx, int16_t v) {
    MenuBindings* b = static_cast<MenuBindings*>(ctx);
    if (g_alarmStep == 0) {
        if (b->alarmMode == 0) {
            g_alarmStep = 0;
            setAlarmRange(0);
            return false;
        }
        g_alarmStep = 1;
        g_alarmHour = b->alarmHour;
        g_alarmMin  = b->alarmMin;
        setAlarmRange(1);
        return true;
    }
    if (g_alarmStep == 1) {
        g_alarmStep = 2;
        setAlarmRange(2);
        return true;
    }
    if (g_alarmStep == 2) {
        g_alarmStep = 0;
        setAlarmRange(0);
        return false;
    }
    return false;
}

static void cancelAlarmMenu(void* ctx) {
    if (ctx) {
        MenuBindings* b = static_cast<MenuBindings*>(ctx);
        g_alarmHour = b->alarmHour;
        g_alarmMin  = b->alarmMin;
    }
    g_alarmStep = 0;
    setAlarmRange(0);
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
