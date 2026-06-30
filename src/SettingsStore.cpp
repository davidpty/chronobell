#include "SettingsStore.h"

const char* SettingsStore::PREFS_NAMESPACE = "settings";
const char* SettingsStore::KEY_SSID = "ssid";
const char* SettingsStore::KEY_PASSWORD = "password";
const char* SettingsStore::KEY_BACKUP_SSID = "backup_ssid";
const char* SettingsStore::KEY_BACKUP_PASSWORD = "backup_password";
const char* SettingsStore::KEY_PENDING_SSID = "pending_ssid";
const char* SettingsStore::KEY_PENDING_PASSWORD = "pending_password";
const char* SettingsStore::KEY_HOTSPOT_ENABLED = "hotspot_enabled";
const char* SettingsStore::KEY_HOTSPOT_EXPIRY = "hotspot_expiry";
const char* SettingsStore::KEY_TIMEZONE_MINUTES = "tz_min";
const char* SettingsStore::KEY_TIMEZONE_NAME = "tz_name";
const char* SettingsStore::KEY_DISPLAY_MODE = "display";
const char* SettingsStore::KEY_DATE_STYLE = "date_style";
const char* SettingsStore::KEY_BELL_MODE = "bell";
const char* SettingsStore::KEY_TIME_FORMAT = "time_fmt";
const char* SettingsStore::KEY_NIGHT_MODE = "night";
const char* SettingsStore::KEY_INFO_LINE_MODE = "info_line";
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
const char* SettingsStore::KEY_TRANSITION_MODE = "anim";
#endif
const char* SettingsStore::KEY_SEPARATOR_BIG = "sep_big";
const char* SettingsStore::KEY_SEPARATOR_DRIFT = "sep_drift";
const char* SettingsStore::KEY_DIAL_MARKS = "dial_marks";
const char* SettingsStore::KEY_BAR_SECONDS = "bar_secs";
const char* SettingsStore::KEY_BIN_SECONDS = "bin_secs";
const char* SettingsStore::KEY_RND_INTERVAL = "rnd_intvl";
const char* SettingsStore::KEY_MANUAL_TIME_ENABLED = "manual_enabled";
const char* SettingsStore::KEY_MANUAL_EPOCH = "manual_epoch";
const char* SettingsStore::MENU_PREFS_NAMESPACE = "menu";
const char* SettingsStore::KEY_BRIGHTNESS = "bright";
const char* SettingsStore::KEY_MENU_INDEX = "last_idx";
const char* SettingsStore::TIMER_PREFS_NAMESPACE = "timer";
const char* SettingsStore::KEY_COUNTDOWN_PRESET = "cdpreset";
const char* SettingsStore::KEY_COUNTDOWN_TARGET_EPOCH = "cdtarget";
const char* SettingsStore::KEY_COUNTDOWN_REMAINING = "cd_remain";
const char* SettingsStore::KEY_COUNTDOWN_VIEW_ACTIVE = "cdview";
const char* SettingsStore::KEY_STOPWATCH_ELAPSED = "sw_elapsed";
const char* SettingsStore::KEY_STOPWATCH_START_EPOCH = "sw_start_ep";
const char* SettingsStore::KEY_STOPWATCH_VIEW_ACTIVE = "sw_view";
const char* SettingsStore::KEY_TEMP_STYLE_LABEL = "ovr_label";
const char* SettingsStore::KEY_TEMP_STYLE_EPOCH = "ovr_epoch";
const char* SettingsStore::KEY_ALARM_MODE = "alm_mode";
const char* SettingsStore::KEY_ALARM_HOUR = "alm_hour";
const char* SettingsStore::KEY_ALARM_MIN  = "alm_min";

AppSettings SettingsStore::load() {
    AppSettings settings;
    Preferences prefs;

    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return settings;
    }

    settings.network.ssid = prefs.getString(KEY_SSID, "");
    settings.network.password = prefs.getString(KEY_PASSWORD, "");
    settings.timezone.offsetMinutes = prefs.getShort(KEY_TIMEZONE_MINUTES, -300);
    settings.timezone.name = prefs.getString(KEY_TIMEZONE_NAME, "Eastern Time");
    settings.displayMode = clampDisplayMode(
        prefs.getUChar(KEY_DISPLAY_MODE, (uint8_t)DisplayMode::LargeDigitsOnly));
    settings.dateStyle = clampDateStyle(
        prefs.getUChar(KEY_DATE_STYLE, (uint8_t)DateStyle::Date));
    settings.bellMode = clampBellMode(prefs.getUChar(KEY_BELL_MODE, (uint8_t)BellMode::Off));
    settings.timeFormat = clampTimeFormat(prefs.getUChar(KEY_TIME_FORMAT, (uint8_t)TimeFormat::Hours24));
    settings.nightMode = clampNightMode(prefs.getUChar(KEY_NIGHT_MODE, (uint8_t)NightMode::Off));
    settings.infoLineMode = clampInfoLineMode(prefs.getUChar(KEY_INFO_LINE_MODE, (uint8_t)InfoLineMode::Seconds));
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    settings.transitionMode = clampTransitionMode(prefs.getUChar(KEY_TRANSITION_MODE, (uint8_t)TransitionMode::Morph));
#endif
    settings.bigSeparator = clampSeparatorMode(prefs.getUChar(KEY_SEPARATOR_BIG, (uint8_t)SeparatorMode::Steady));
    settings.driftSeparator = clampSeparatorMode(prefs.getUChar(KEY_SEPARATOR_DRIFT, (uint8_t)SeparatorMode::Steady));
    settings.dialMarks = clampDialMarksMode(prefs.getUChar(KEY_DIAL_MARKS, (uint8_t)DialMarksMode::On));
    settings.barSeconds = clampBarSecondsMode(prefs.getUChar(KEY_BAR_SECONDS, (uint8_t)BarSecondsMode::Off));
    settings.binSeconds = clampBinSecondsMode(prefs.getUChar(KEY_BIN_SECONDS, (uint8_t)BinSecondsMode::On));
    settings.rndInterval = clampRndIntervalMode(prefs.getUChar(KEY_RND_INTERVAL, (uint8_t)RndIntervalMode::Min15));
    settings.manualTime.enabled = prefs.getBool(KEY_MANUAL_TIME_ENABLED, false);
    settings.manualTime.epoch = prefs.getULong(KEY_MANUAL_EPOCH, 0);
    settings.alarm.mode   = prefs.getUChar(KEY_ALARM_MODE, 0);
    settings.alarm.hour   = prefs.getUChar(KEY_ALARM_HOUR, 7);
    settings.alarm.minute = prefs.getUChar(KEY_ALARM_MIN, 0);
    if (settings.network.ssid.length() == 0) {
        // No network is configured, so the clock should not stay in AUTO mode.
        settings.network.password = "";
        settings.manualTime.enabled = true;
    }

    prefs.end();
    return settings;
}

bool SettingsStore::save(const AppSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putString(KEY_SSID, settings.network.ssid);
    prefs.putString(KEY_PASSWORD, settings.network.password);
    prefs.putShort(KEY_TIMEZONE_MINUTES, settings.timezone.offsetMinutes);
    prefs.putString(KEY_TIMEZONE_NAME, settings.timezone.name);
    prefs.putUChar(KEY_DISPLAY_MODE, (uint8_t)settings.displayMode);
    prefs.putUChar(KEY_DATE_STYLE, (uint8_t)settings.dateStyle);
    prefs.putUChar(KEY_BELL_MODE, (uint8_t)settings.bellMode);
    prefs.putUChar(KEY_TIME_FORMAT, (uint8_t)settings.timeFormat);
    prefs.putUChar(KEY_NIGHT_MODE, (uint8_t)settings.nightMode);
    prefs.putUChar(KEY_INFO_LINE_MODE, (uint8_t)settings.infoLineMode);
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    prefs.putUChar(KEY_TRANSITION_MODE, (uint8_t)settings.transitionMode);
#endif
    prefs.putUChar(KEY_SEPARATOR_BIG, (uint8_t)settings.bigSeparator);
    prefs.putUChar(KEY_SEPARATOR_DRIFT, (uint8_t)settings.driftSeparator);
    prefs.putUChar(KEY_DIAL_MARKS, (uint8_t)settings.dialMarks);
    prefs.putUChar(KEY_BAR_SECONDS, (uint8_t)settings.barSeconds);
    prefs.putUChar(KEY_BIN_SECONDS, (uint8_t)settings.binSeconds);
    prefs.putUChar(KEY_RND_INTERVAL, (uint8_t)settings.rndInterval);
    prefs.putBool(KEY_MANUAL_TIME_ENABLED, settings.manualTime.enabled);
    prefs.putULong(KEY_MANUAL_EPOCH, settings.manualTime.epoch);
    prefs.putUChar(KEY_ALARM_MODE, settings.alarm.mode);
    prefs.putUChar(KEY_ALARM_HOUR, settings.alarm.hour);
    prefs.putUChar(KEY_ALARM_MIN, settings.alarm.minute);

    prefs.end();
    return true;
}

bool SettingsStore::saveNetworkBackup(const NetworkCredentials& network) {
    if (network.ssid.length() == 0) {
        return clearNetworkBackup();
    }

    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putString(KEY_BACKUP_SSID, network.ssid);
    prefs.putString(KEY_BACKUP_PASSWORD, network.password);
    prefs.end();
    return true;
}

bool SettingsStore::loadNetworkBackup(NetworkCredentials& network) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return false;
    }

    network.ssid = prefs.getString(KEY_BACKUP_SSID, "");
    network.password = prefs.getString(KEY_BACKUP_PASSWORD, "");
    prefs.end();
    return network.ssid.length() > 0;
}

bool SettingsStore::clearNetworkBackup() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.remove(KEY_BACKUP_SSID);
    prefs.remove(KEY_BACKUP_PASSWORD);
    prefs.end();
    return true;
}

bool SettingsStore::savePendingNetwork(const NetworkCredentials& network) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putString(KEY_PENDING_SSID, network.ssid);
    prefs.putString(KEY_PENDING_PASSWORD, network.password);
    prefs.end();
    return true;
}

bool SettingsStore::loadPendingNetwork(NetworkCredentials& network) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        return false;
    }

    network.ssid = prefs.getString(KEY_PENDING_SSID, "");
    network.password = prefs.getString(KEY_PENDING_PASSWORD, "");
    prefs.end();
    return network.ssid.length() > 0;
}

bool SettingsStore::clearPendingNetwork() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.remove(KEY_PENDING_SSID);
    prefs.remove(KEY_PENDING_PASSWORD);
    prefs.end();
    return true;
}

bool SettingsStore::saveHotspotState(bool enabled, unsigned long expiryEpoch) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putBool(KEY_HOTSPOT_ENABLED, enabled);
    if (enabled) {
        prefs.putULong(KEY_HOTSPOT_EXPIRY, expiryEpoch);
    } else {
        prefs.remove(KEY_HOTSPOT_EXPIRY);
    }
    prefs.end();
    return true;
}

bool SettingsStore::loadHotspotState(bool& enabled, unsigned long& expiryEpoch) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        enabled = false;
        expiryEpoch = 0;
        return false;
    }

    enabled = prefs.getBool(KEY_HOTSPOT_ENABLED, false);
    expiryEpoch = prefs.getULong(KEY_HOTSPOT_EXPIRY, 0);
    prefs.end();
    return enabled;
}

bool SettingsStore::saveTemporaryStyle(DisplayMode mode, time_t epoch) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_TEMP_STYLE_LABEL, (uint8_t)mode);
    prefs.putULong(KEY_TEMP_STYLE_EPOCH, (unsigned long)epoch);
    prefs.end();
    return true;
}

bool SettingsStore::loadTemporaryStyle(DisplayMode& mode, time_t& epoch) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true)) {
        mode = DisplayMode::LargeDigitsOnly;
        epoch = 0;
        return false;
    }
    epoch = (time_t)prefs.getULong(KEY_TEMP_STYLE_EPOCH, 0);
    mode = (DisplayMode)prefs.getUChar(KEY_TEMP_STYLE_LABEL, (uint8_t)DisplayMode::LargeDigitsOnly);
    prefs.end();
    return epoch > 0;
}

bool SettingsStore::clearTemporaryStyle() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.remove(KEY_TEMP_STYLE_LABEL);
    prefs.remove(KEY_TEMP_STYLE_EPOCH);
    prefs.end();
    return true;
}

bool SettingsStore::saveDisplayMode(DisplayMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_DISPLAY_MODE, (uint8_t)clampDisplayMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveDateStyle(DateStyle style) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_DATE_STYLE, (uint8_t)clampDateStyle((int)style));
    prefs.end();
    return true;
}

bool SettingsStore::saveBellMode(BellMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_BELL_MODE, (uint8_t)clampBellMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveTimeFormat(TimeFormat format) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_TIME_FORMAT, (uint8_t)clampTimeFormat((int)format));
    prefs.end();
    return true;
}

bool SettingsStore::saveNightMode(NightMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_NIGHT_MODE, (uint8_t)clampNightMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveInfoLineMode(InfoLineMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_INFO_LINE_MODE, (uint8_t)clampInfoLineMode((int)mode));
    prefs.end();
    return true;
}

#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
bool SettingsStore::saveTransitionMode(TransitionMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_TRANSITION_MODE, (uint8_t)clampTransitionMode((int)mode));
    prefs.end();
    return true;
}
#endif

bool SettingsStore::saveSeparatorMode(DisplayMode displayMode, SeparatorMode mode) {
    const char* key = nullptr;
    switch (displayMode) {
        case DisplayMode::LargeDigitsOnly: key = KEY_SEPARATOR_BIG; break;
        case DisplayMode::Info: key = KEY_SEPARATOR_BIG; break;
        case DisplayMode::Drift: key = KEY_SEPARATOR_DRIFT; break;
        default: return false;
    }
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(key, (uint8_t)clampSeparatorMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveDialMarksMode(DialMarksMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_DIAL_MARKS, (uint8_t)clampDialMarksMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveBarSeconds(BarSecondsMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_BAR_SECONDS, (uint8_t)clampBarSecondsMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveBinSeconds(BinSecondsMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_BIN_SECONDS, (uint8_t)clampBinSecondsMode((int)mode));
    prefs.end();
    return true;
}

bool SettingsStore::saveRndInterval(RndIntervalMode mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_RND_INTERVAL, (uint8_t)clampRndIntervalMode((int)mode));
    prefs.end();
    return true;
}

int8_t SettingsStore::loadBrightness(int8_t defaultBrightness) {
    Preferences prefs;
    if (!prefs.begin(MENU_PREFS_NAMESPACE, true)) {
        return defaultBrightness;
    }

    int8_t brightness = (int8_t)prefs.getChar(KEY_BRIGHTNESS, defaultBrightness);
    prefs.end();
    if (brightness < 0) brightness = 0;
    if (brightness > 15) brightness = 15;
    return brightness;
}

bool SettingsStore::saveBrightness(int8_t brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 15) brightness = 15;

    Preferences prefs;
    if (!prefs.begin(MENU_PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putChar(KEY_BRIGHTNESS, brightness);
    prefs.end();
    return true;
}

uint8_t SettingsStore::loadMenuIndex(uint8_t itemCount, uint8_t defaultIndex) {
    if (itemCount == 0) {
        return 0;
    }
    if (defaultIndex >= itemCount) {
        defaultIndex = 0;
    }

    Preferences prefs;
    if (!prefs.begin(MENU_PREFS_NAMESPACE, true)) {
        return defaultIndex;
    }

    uint8_t index = prefs.getUChar(KEY_MENU_INDEX, defaultIndex);
    prefs.end();
    return index < itemCount ? index : defaultIndex;
}

bool SettingsStore::saveMenuIndex(uint8_t index) {
    Preferences prefs;
    if (!prefs.begin(MENU_PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_MENU_INDEX, index);
    prefs.end();
    return true;
}

uint8_t SettingsStore::loadCountdownPreset(uint8_t presetCount, uint8_t defaultPresetIndex) {
    if (presetCount == 0) {
        return 0;
    }
    if (defaultPresetIndex >= presetCount) {
        defaultPresetIndex = 0;
    }

    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return defaultPresetIndex;
    }

    uint8_t presetIndex = prefs.getUChar(KEY_COUNTDOWN_PRESET, defaultPresetIndex);
    prefs.end();
    return presetIndex < presetCount ? presetIndex : defaultPresetIndex;
}

bool SettingsStore::saveCountdownPreset(uint8_t presetIndex) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putUChar(KEY_COUNTDOWN_PRESET, presetIndex);
    prefs.end();
    return true;
}

time_t SettingsStore::loadCountdownTargetEpoch() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return 0;
    }

    unsigned long targetEpoch = prefs.getULong(KEY_COUNTDOWN_TARGET_EPOCH, 0);
    prefs.end();
    return (time_t)targetEpoch;
}

bool SettingsStore::saveCountdownTargetEpoch(time_t targetEpoch) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }

    if (targetEpoch > 0) {
        prefs.putULong(KEY_COUNTDOWN_TARGET_EPOCH, (unsigned long)targetEpoch);
    } else {
        prefs.remove(KEY_COUNTDOWN_TARGET_EPOCH);
    }
    prefs.end();
    return true;
}

bool SettingsStore::clearCountdownTargetEpoch() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.remove(KEY_COUNTDOWN_TARGET_EPOCH);
    prefs.end();
    return true;
}

uint32_t SettingsStore::loadCountdownRemainingMs() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return 0;
    }

    uint32_t remaining = prefs.getULong(KEY_COUNTDOWN_REMAINING, 0);
    prefs.end();
    return remaining;
}

bool SettingsStore::saveCountdownRemainingMs(uint32_t remainingMs) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }

    if (remainingMs > 0) {
        prefs.putULong(KEY_COUNTDOWN_REMAINING, remainingMs);
    } else {
        prefs.remove(KEY_COUNTDOWN_REMAINING);
    }
    prefs.end();
    return true;
}

bool SettingsStore::clearCountdownRemainingMs() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.remove(KEY_COUNTDOWN_REMAINING);
    prefs.end();
    return true;
}

bool SettingsStore::loadCountdownViewActive() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return false;
    }

    bool active = prefs.getBool(KEY_COUNTDOWN_VIEW_ACTIVE, false);
    prefs.end();
    return active;
}

bool SettingsStore::saveCountdownViewActive(bool active) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.putBool(KEY_COUNTDOWN_VIEW_ACTIVE, active);
    prefs.end();
    return true;
}

uint64_t SettingsStore::loadStopwatchElapsed() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return 0;
    }
    uint64_t elapsed = prefs.getULong64(KEY_STOPWATCH_ELAPSED, 0);
    prefs.end();
    return elapsed;
}

bool SettingsStore::saveStopwatchElapsed(uint64_t elapsedMs) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }
    prefs.putULong64(KEY_STOPWATCH_ELAPSED, elapsedMs);
    prefs.end();
    return true;
}

bool SettingsStore::clearStopwatchElapsed() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }
    prefs.remove(KEY_STOPWATCH_ELAPSED);
    prefs.end();
    return true;
}

time_t SettingsStore::loadStopwatchStartEpoch() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return 0;
    }
    time_t epoch = (time_t)prefs.getLong(KEY_STOPWATCH_START_EPOCH, 0);
    prefs.end();
    return epoch;
}

bool SettingsStore::saveStopwatchStartEpoch(time_t epoch) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }
    prefs.putLong(KEY_STOPWATCH_START_EPOCH, (int32_t)epoch);
    prefs.end();
    return true;
}

bool SettingsStore::clearStopwatchStartEpoch() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }
    prefs.remove(KEY_STOPWATCH_START_EPOCH);
    prefs.end();
    return true;
}

bool SettingsStore::loadStopwatchViewActive() {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, true)) {
        return false;
    }
    bool active = prefs.getBool(KEY_STOPWATCH_VIEW_ACTIVE, false);
    prefs.end();
    return active;
}

bool SettingsStore::saveStopwatchViewActive(bool active) {
    Preferences prefs;
    if (!prefs.begin(TIMER_PREFS_NAMESPACE, false)) {
        return false;
    }
    prefs.putBool(KEY_STOPWATCH_VIEW_ACTIVE, active);
    prefs.end();
    return true;
}

bool SettingsStore::saveAlarmMode(uint8_t mode) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_ALARM_MODE, mode);
    prefs.end();
    return true;
}

bool SettingsStore::saveAlarmHour(uint8_t hour) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_ALARM_HOUR, hour);
    prefs.end();
    return true;
}

bool SettingsStore::saveAlarmMin(uint8_t min) {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) return false;
    prefs.putUChar(KEY_ALARM_MIN, min);
    prefs.end();
    return true;
}

bool SettingsStore::clearManualTime() {
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false)) {
        return false;
    }

    prefs.remove(KEY_MANUAL_TIME_ENABLED);
    prefs.remove(KEY_MANUAL_EPOCH);
    prefs.end();
    return true;
}
