#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <Arduino.h>
#include "Config.h"

enum class DisplayMode : uint8_t {
    Rnd = 0,
    LargeDigitsOnly = 1,
    Info = 2,
    Word = 3,
    Roma = 4,
    Dial = 5,
    Bar = 6,
    Bin = 7,
    Pong = 8,
    Drift = 9
};

enum class InfoLineMode : uint8_t {
    Seconds = 0,
    Deciseconds = 1,
    Date = 2,
    Weekday = 3,
    Alt = 4
};

enum class DateStyle : uint8_t {
    Date = 0,
    Year = 1,
    Moon = 2,
    Zod = 3,
    Czod = 4
};

enum class BellMode : uint8_t {
    Off = 0,
    SingleHour = 1,
    HourCount = 2,
    HourCountHalf = 3,
    Pair = 4,
    Triple = 5,
    Ships = 6
};

enum class TimeFormat : uint8_t {
    AmPm = 0,
    Hours24 = 1
};

enum class NightMode : uint8_t {
    Off      = 0,
    Dim      = 1,
    DimMute  = 2,
    Dark     = 3,
    DarkMute = 4,
    Mute     = 5
};

enum class SeparatorMode : uint8_t {
    Steady = 0,
    Pulse = 1
};

enum class DialMarksMode : uint8_t {
    Off = 0,
    On = 1
};

enum class BarSecondsMode : uint8_t {
    Off = 0,
    On = 1
};

enum class BinSecondsMode : uint8_t {
    Off = 0,
    On = 1
};

enum class RndIntervalMode : uint8_t {
    Min1 = 0,
    Min5 = 1,
    Min10 = 2,
    Min15 = 3,
    Min30 = 4,
    Min60 = 5,
    Min90 = 6,
    Min120 = 7,
    H4 = 8,
    H6 = 9,
    H12 = 10,
    H24 = 11
};

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
enum class TransitionMode : uint8_t {
    Off = 0,
    Morph = 1
};
#endif

struct NetworkCredentials {
    String ssid;
    String password;
};

struct TimezoneSetting {
    int16_t offsetMinutes = -300;
    String name = "Eastern Time";
};

struct ManualTimeSetting {
    bool enabled = false;
    unsigned long epoch = 0;
};

struct AlarmSettings {
    uint8_t mode = 0;   // 0=Off, 1=Once, 2=Daily, 3=Weekday, 4=Weekend
    uint8_t hour = 7;
    uint8_t minute = 0;
};

struct AppSettings {
    NetworkCredentials network;
    TimezoneSetting timezone;
    DisplayMode displayMode = DisplayMode::LargeDigitsOnly;
    DateStyle dateStyle = DateStyle::Date;
    BellMode bellMode = BellMode::Off;
    TimeFormat timeFormat = TimeFormat::Hours24;
    NightMode nightMode = NightMode::Off;
    SeparatorMode bigSeparator = SeparatorMode::Steady;
    InfoLineMode infoLineMode = InfoLineMode::Seconds;
    SeparatorMode driftSeparator = SeparatorMode::Steady;
    DialMarksMode dialMarks = DialMarksMode::On;
    BarSecondsMode barSeconds = BarSecondsMode::Off;
    BinSecondsMode binSeconds = BinSecondsMode::On;
    RndIntervalMode rndInterval = RndIntervalMode::Min15;
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    TransitionMode transitionMode = TransitionMode::Morph;
#endif
    ManualTimeSetting manualTime;
    AlarmSettings alarm;
};

inline bool hasConfigurableSeparator(DisplayMode mode) {
    return mode == DisplayMode::LargeDigitsOnly ||
           mode == DisplayMode::Info ||
           mode == DisplayMode::Drift;
}

inline SeparatorMode clampSeparatorMode(int mode) {
    return mode <= (int)SeparatorMode::Steady ? SeparatorMode::Steady : SeparatorMode::Pulse;
}

inline DialMarksMode clampDialMarksMode(int mode) {
    return mode <= (int)DialMarksMode::Off ? DialMarksMode::Off : DialMarksMode::On;
}

inline BarSecondsMode clampBarSecondsMode(int mode) {
    return mode <= (int)BarSecondsMode::Off ? BarSecondsMode::Off : BarSecondsMode::On;
}

inline BinSecondsMode clampBinSecondsMode(int mode) {
    return mode <= (int)BinSecondsMode::Off ? BinSecondsMode::Off : BinSecondsMode::On;
}

static const uint16_t RND_INTERVAL_VALUES[] = {
    1, 5, 10, 15, 30, 60, 90, 120, 240, 360, 720, 1440
};
enum { RND_INTERVAL_COUNT = 12 };

inline uint16_t rndIntervalMinutes(RndIntervalMode mode) {
    uint8_t idx = (uint8_t)mode;
    if (idx >= RND_INTERVAL_COUNT) idx = 3;
    return RND_INTERVAL_VALUES[idx];
}

inline RndIntervalMode clampRndIntervalMode(int mode) {
    if (mode < 0) return RndIntervalMode::Min1;
    if (mode >= RND_INTERVAL_COUNT) return RndIntervalMode::H24;
    return static_cast<RndIntervalMode>(mode);
}

inline SeparatorMode separatorModeFor(const AppSettings& settings, DisplayMode mode) {
    switch (mode) {
        case DisplayMode::Drift:
            return settings.driftSeparator;
        case DisplayMode::Info:
        case DisplayMode::LargeDigitsOnly:
        default: return settings.bigSeparator;
    }
}

inline void setSeparatorModeFor(AppSettings& settings, DisplayMode mode, SeparatorMode separator) {
    switch (mode) {
        case DisplayMode::Drift: settings.driftSeparator = separator; break;
        case DisplayMode::Info:
        case DisplayMode::LargeDigitsOnly: settings.bigSeparator = separator; break;
        default: break;
    }
}

inline const char* displayModeLabel(DisplayMode mode) {
    switch (mode) {
        case DisplayMode::Rnd:
            return "RND";
        case DisplayMode::LargeDigitsOnly:
            return "BIG";
        case DisplayMode::Info:
            return "INFO";
        case DisplayMode::Word:
            return "WORD";
        case DisplayMode::Roma:
            return "ROMA";
        case DisplayMode::Dial:
            return "DIAL";
        case DisplayMode::Bar:
            return "BAR";
        case DisplayMode::Bin:
            return "BIN";
        case DisplayMode::Pong:
            return "PONG";
        case DisplayMode::Drift:
            return "DRIFT";
        default:
            return "?";
    }
}

inline DisplayMode clampDisplayMode(int mode) {
    if (mode < (int)DisplayMode::Rnd) {
        return DisplayMode::Rnd;
    }
    if (mode > (int)DisplayMode::Drift) {
        return DisplayMode::Drift;
    }
    return static_cast<DisplayMode>(mode);
}

inline bool isRandomDisplayMode(DisplayMode mode) {
    return mode == DisplayMode::Rnd;
}

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
inline const char* transitionModeLabel(TransitionMode mode) {
    switch (mode) {
        case TransitionMode::Off:   return "OFF";
        case TransitionMode::Morph:  return "MORPH";
        default:                     return "?";
    }
}

inline TransitionMode clampTransitionMode(int mode) {
    if (mode <= (int)TransitionMode::Off) {
        return TransitionMode::Off;
    }
    if (mode >= (int)TransitionMode::Morph) {
        return TransitionMode::Morph;
    }
    return TransitionMode::Morph;
}
#endif

inline const char* infoLineLabel(InfoLineMode mode) {
    switch (mode) {
        case InfoLineMode::Seconds:     return "SEC";
        case InfoLineMode::Deciseconds: return "DECI";
        case InfoLineMode::Date:        return "DATE";
        case InfoLineMode::Weekday:     return "WDAY";
        case InfoLineMode::Alt:         return "ALT";
        default:                        return "?";
    }
}

inline InfoLineMode clampInfoLineMode(int mode) {
    if (mode < (int)InfoLineMode::Seconds) {
        return InfoLineMode::Seconds;
    }
    if (mode > (int)InfoLineMode::Alt) {
        return InfoLineMode::Alt;
    }
    return static_cast<InfoLineMode>(mode);
}

inline const char* dateStyleLabel(DateStyle style) {
    switch (style) {
        case DateStyle::Year: return "YEAR";
        case DateStyle::Moon:  return "MOON";
        case DateStyle::Zod:   return "ZOD";
        case DateStyle::Czod:  return "CZOD";
        case DateStyle::Date:
        default:               return "DATE";
    }
}

inline DateStyle clampDateStyle(int style) {
    if (style < (int)DateStyle::Date) {
        return DateStyle::Date;
    }
    if (style > (int)DateStyle::Czod) {
        return DateStyle::Czod;
    }
    return static_cast<DateStyle>(style);
}

inline BellMode clampBellMode(int mode) {
    if (mode < (int)BellMode::Off) {
        return BellMode::Off;
    }
    if (mode > (int)BellMode::Ships) {
        return BellMode::Ships;
    }
    return static_cast<BellMode>(mode);
}

inline const char* timeFormatLabel(TimeFormat format) {
    switch (format) {
        case TimeFormat::AmPm:
            return "AM/PM";
        case TimeFormat::Hours24:
        default:
            return "24-HOUR";
    }
}

inline TimeFormat clampTimeFormat(int format) {
    if (format < (int)TimeFormat::AmPm) {
        return TimeFormat::AmPm;
    }
    if (format > (int)TimeFormat::Hours24) {
        return TimeFormat::Hours24;
    }
    return static_cast<TimeFormat>(format);
}

inline const char* nightModeLabel(NightMode mode) {
    switch (mode) {
        case NightMode::Dim:      return "LOW";
        case NightMode::DimMute:  return "LOW+MUTE";
        case NightMode::Dark:     return "DARK";
        case NightMode::DarkMute: return "DARK+MUTE";
        case NightMode::Mute:     return "MUTE";
        case NightMode::Off:
        default:                  return "OFF";
    }
}

inline NightMode clampNightMode(int mode) {
    if (mode < (int)NightMode::Off) {
        return NightMode::Off;
    }
    if (mode > (int)NightMode::Mute) {
        return NightMode::Mute;
    }
    return static_cast<NightMode>(mode);
}

// =============================================================================
// Style traits — menu metadata for each display mode's sub-config(s)
// =============================================================================

enum class StyleField : uint8_t {
    None,
    InfoLine,
    Separator,
    DriftSeparator,
    DialMarks,
    BarSeconds,
    BinSeconds,
    RndInterval
};

struct StyleStepConfig {
    const char* label;   // menu label ("INFO", "MARKS", "COLON"); null means no step
    int8_t min;
    int8_t max;
    StyleField field;
};

struct StyleTrait {
    StyleStepConfig step1;
    StyleStepConfig step2;   // only Info has two steps
};

struct StyleConfig {
    InfoLineMode infoLine = InfoLineMode::Seconds;
    SeparatorMode separator = SeparatorMode::Steady;
    SeparatorMode driftSeparator = SeparatorMode::Steady;
    DialMarksMode dialMarks = DialMarksMode::On;
    BarSecondsMode barSeconds = BarSecondsMode::Off;
    BinSecondsMode binSeconds = BinSecondsMode::On;
    RndIntervalMode rndInterval = RndIntervalMode::Min15;
};

inline const StyleTrait& styleTraitFor(DisplayMode mode) {
    static const StyleTrait TRAITS[] = {
        /* Rnd            */ { {"CYCLE",         0, 11, StyleField::RndInterval}, {nullptr, 0, 0, StyleField::None} },
        /* LargeDigitsOnly */ { {"COLON",         0, 1, StyleField::Separator}, {nullptr, 0, 0, StyleField::None} },
        /* Info           */ { {"INFO",          0, 4, StyleField::InfoLine}, {"COLON", 0, 1, StyleField::Separator} },
        /* Word           */ { {nullptr,         0, 0, StyleField::None}, {nullptr, 0, 0, StyleField::None} },
        /* Roma           */ { {nullptr,         0, 0, StyleField::None}, {nullptr, 0, 0, StyleField::None} },
        /* Dial           */ { {"MARKS",         0, 1, StyleField::DialMarks}, {nullptr, 0, 0, StyleField::None} },
        /* Bar            */ { {"BAR",           0, 1, StyleField::BarSeconds}, {nullptr, 0, 0, StyleField::None} },
        /* Bin            */ { {"BIN",           0, 1, StyleField::BinSeconds}, {nullptr, 0, 0, StyleField::None} },
        /* Pong           */ { {nullptr,         0, 0, StyleField::None}, {nullptr, 0, 0, StyleField::None} },
        /* Drift          */ { {"COLON",         0, 1, StyleField::DriftSeparator}, {nullptr, 0, 0, StyleField::None} },
    };
    return TRAITS[(uint8_t)mode];
}

#endif // APP_SETTINGS_H
