#ifndef MENU_BINDINGS_H
#define MENU_BINDINGS_H

#include <Arduino.h>
#include "AppSettings.h"

class BellController;
class Display;
class SettingsStore;
class TimeProvider;
class WiFiManagerLite;

struct MenuBindings {
    AppSettings&    appSettings;
    SettingsStore&  settingsStore;
    Display&        display;
    BellController& bellController;
    TimeProvider&   timeProvider;
    WiFiManagerLite& wifiManager;
    BellMode&       bellMode;
    DisplayMode&    displayMode;
    TimeFormat&     timeFormat;
    NightMode&      nightMode;

    bool getCurrentClockTime(int& h, int& m, int& s) const;
};

const char* menuValueName(uint8_t index, int16_t value, void* ctx);
const char* bellValueName(int16_t value);
const char* styleValueName(int16_t value);
const char* hourValueName(int16_t value);
const char* nightValueName(int16_t value);

#endif // MENU_BINDINGS_H
