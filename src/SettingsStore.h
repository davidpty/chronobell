#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <time.h>

#include <Preferences.h>
#include "AppSettings.h"

class SettingsStore {
public:
    AppSettings load();
    bool save(const AppSettings& settings);
    bool saveDisplayMode(DisplayMode mode);
    bool saveDateStyle(DateStyle style);
    bool saveBellMode(BellMode mode);
    bool saveTimeFormat(TimeFormat format);
    bool saveNightMode(NightMode mode);
    bool saveInfoLineMode(InfoLineMode mode);
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    bool saveTransitionMode(TransitionMode mode);
#endif
    bool saveSeparatorMode(DisplayMode displayMode, SeparatorMode mode);
    bool saveDriftSeparatorMode(DriftSeparatorMode mode);
    bool saveDialMarksMode(DialMarksMode mode);
    int8_t loadBrightness(int8_t defaultBrightness);
    bool saveBrightness(int8_t brightness);
    uint8_t loadMenuIndex(uint8_t itemCount, uint8_t defaultIndex = 0);
    bool saveMenuIndex(uint8_t index);
    uint8_t loadCountdownPreset(uint8_t presetCount, uint8_t defaultPresetIndex = 0);
    bool saveCountdownPreset(uint8_t presetIndex);
    time_t loadCountdownTargetEpoch();
    bool saveCountdownTargetEpoch(time_t targetEpoch);
    bool clearCountdownTargetEpoch();
    uint32_t loadCountdownRemainingMs();
    bool saveCountdownRemainingMs(uint32_t remainingMs);
    bool clearCountdownRemainingMs();
    bool loadCountdownViewActive();
    bool saveCountdownViewActive(bool active);
    uint64_t loadStopwatchElapsed();
    bool saveStopwatchElapsed(uint64_t elapsedMs);
    bool clearStopwatchElapsed();
    time_t loadStopwatchStartEpoch();
    bool saveStopwatchStartEpoch(time_t epoch);
    bool clearStopwatchStartEpoch();
    bool loadStopwatchViewActive();
    bool saveStopwatchViewActive(bool active);
    bool clearManualTime();
    bool saveNetworkBackup(const NetworkCredentials& network);
    bool loadNetworkBackup(NetworkCredentials& network);
    bool clearNetworkBackup();
    bool savePendingNetwork(const NetworkCredentials& network);
    bool loadPendingNetwork(NetworkCredentials& network);
    bool clearPendingNetwork();
    bool saveHotspotState(bool enabled, unsigned long expiryEpoch);
    bool loadHotspotState(bool& enabled, unsigned long& expiryEpoch);

private:
    static const char* PREFS_NAMESPACE;
    static const char* KEY_SSID;
    static const char* KEY_PASSWORD;
    static const char* KEY_BACKUP_SSID;
    static const char* KEY_BACKUP_PASSWORD;
    static const char* KEY_PENDING_SSID;
    static const char* KEY_PENDING_PASSWORD;
    static const char* KEY_HOTSPOT_ENABLED;
    static const char* KEY_HOTSPOT_EXPIRY;
    static const char* KEY_TIMEZONE_MINUTES;
    static const char* KEY_TIMEZONE_NAME;
    static const char* KEY_DISPLAY_MODE;
    static const char* KEY_DATE_STYLE;
    static const char* KEY_BELL_MODE;
    static const char* KEY_TIME_FORMAT;
    static const char* KEY_NIGHT_MODE;
    static const char* KEY_INFO_LINE_MODE;
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    static const char* KEY_TRANSITION_MODE;
#endif
    static const char* KEY_SEPARATOR_BIG;
    static const char* KEY_SEPARATOR_SECONDS;
    static const char* KEY_SEPARATOR_DECISECONDS;
    static const char* KEY_SEPARATOR_DATE;
    static const char* KEY_SEPARATOR_WEEKDAY;
    static const char* KEY_SEPARATOR_DRIFT;
    static const char* KEY_DIAL_MARKS;
    static const char* KEY_MANUAL_TIME_ENABLED;
    static const char* KEY_MANUAL_EPOCH;
    static const char* MENU_PREFS_NAMESPACE;
    static const char* KEY_BRIGHTNESS;
    static const char* KEY_MENU_INDEX;
    static const char* TIMER_PREFS_NAMESPACE;
    static const char* KEY_COUNTDOWN_PRESET;
    static const char* KEY_COUNTDOWN_TARGET_EPOCH;
    static const char* KEY_COUNTDOWN_REMAINING;
    static const char* KEY_COUNTDOWN_VIEW_ACTIVE;
    static const char* KEY_STOPWATCH_ELAPSED;
    static const char* KEY_STOPWATCH_START_EPOCH;
    static const char* KEY_STOPWATCH_VIEW_ACTIVE;
};

#endif // SETTINGS_STORE_H
