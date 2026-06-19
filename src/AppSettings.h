#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <Arduino.h>

enum class DisplayMode : uint8_t {
    Rnd = 0,
    LargeDigitsOnly = 1,
    TimeWithSeconds = 2,
    TimeWithDeciseconds = 3,
    TimeWithDate = 4,
    TimeWithWeekday = 5,
    Word = 6,
    Roma = 7,
    Bin = 8,
    Drift = 9
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
    Hours24 = 0,
    AmPm = 1
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

enum class DriftSeparatorMode : uint8_t {
    Steady = 0,
    Pulse = 1
};

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

struct AppSettings {
    NetworkCredentials network;
    TimezoneSetting timezone;
    DisplayMode displayMode = DisplayMode::LargeDigitsOnly;
    DateStyle dateStyle = DateStyle::Date;
    BellMode bellMode = BellMode::Off;
    TimeFormat timeFormat = TimeFormat::Hours24;
    NightMode nightMode = NightMode::Off;
    SeparatorMode bigSeparator = SeparatorMode::Steady;
    SeparatorMode secondsSeparator = SeparatorMode::Steady;
    SeparatorMode decisecondsSeparator = SeparatorMode::Steady;
    SeparatorMode dateSeparator = SeparatorMode::Steady;
    SeparatorMode weekdaySeparator = SeparatorMode::Steady;
    DriftSeparatorMode driftSeparator = DriftSeparatorMode::Steady;
    ManualTimeSetting manualTime;
};

inline bool hasConfigurableSeparator(DisplayMode mode) {
    return mode == DisplayMode::LargeDigitsOnly ||
           mode == DisplayMode::TimeWithSeconds ||
           mode == DisplayMode::TimeWithDeciseconds ||
           mode == DisplayMode::TimeWithDate ||
           mode == DisplayMode::TimeWithWeekday ||
           mode == DisplayMode::Drift;
}

inline SeparatorMode clampSeparatorMode(int mode) {
    return mode <= (int)SeparatorMode::Steady ? SeparatorMode::Steady : SeparatorMode::Pulse;
}

inline DriftSeparatorMode clampDriftSeparatorMode(int mode) {
    if (mode < (int)DriftSeparatorMode::Steady) return DriftSeparatorMode::Steady;
    if (mode > (int)DriftSeparatorMode::Pulse) return DriftSeparatorMode::Pulse;
    return static_cast<DriftSeparatorMode>(mode);
}

inline SeparatorMode separatorModeFor(const AppSettings& settings, DisplayMode mode) {
    switch (mode) {
        case DisplayMode::TimeWithSeconds: return settings.secondsSeparator;
        case DisplayMode::TimeWithDeciseconds: return settings.decisecondsSeparator;
        case DisplayMode::TimeWithDate: return settings.dateSeparator;
        case DisplayMode::TimeWithWeekday: return settings.weekdaySeparator;
        case DisplayMode::LargeDigitsOnly:
        default: return settings.bigSeparator;
    }
}

inline void setSeparatorModeFor(AppSettings& settings, DisplayMode mode, SeparatorMode separator) {
    switch (mode) {
        case DisplayMode::TimeWithSeconds: settings.secondsSeparator = separator; break;
        case DisplayMode::TimeWithDeciseconds: settings.decisecondsSeparator = separator; break;
        case DisplayMode::TimeWithDate: settings.dateSeparator = separator; break;
        case DisplayMode::TimeWithWeekday: settings.weekdaySeparator = separator; break;
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
        case DisplayMode::TimeWithSeconds:
            return "SEC";
        case DisplayMode::TimeWithDeciseconds:
            return "DECI";
        case DisplayMode::TimeWithDate:
            return "DATE";
        case DisplayMode::TimeWithWeekday:
            return "WDAY";
        case DisplayMode::Word:
            return "WORD";
        case DisplayMode::Roma:
            return "ROMA";
        case DisplayMode::Bin:
            return "BIN";
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
    if (format < (int)TimeFormat::Hours24) {
        return TimeFormat::Hours24;
    }
    if (format > (int)TimeFormat::AmPm) {
        return TimeFormat::AmPm;
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

#endif // APP_SETTINGS_H
