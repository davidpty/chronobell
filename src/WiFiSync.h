#ifndef WIFI_SYNC_H
#define WIFI_SYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <NTPClient.h>

#include "Config.h"

class WiFiManagerLite;
class TimeProvider;
class RtcClock;
class SettingsStore;
struct AppSettings;

class WiFiSync {
public:
    WiFiSync(WiFiManagerLite& wifi,
             TimeProvider& timeProvider,
             RtcClock& rtcClock,
             SettingsStore& settings,
             AppSettings& appSettings);

    // Called once from setup(): runs the boot-time NTP sync path
    // (handles disabled-sync, no-credentials, and no-RTC branches).
    void performBootSync();

    // Called from loop(): does the hourly NTP retry if it's time.
    void maybePeriodicSync();

    // Forwards to NTPClient.update() when NTP is the active time source
    // (i.e. no RTC). Otherwise no-op.
    void updateIfActive();

    // Exposed for TimeProvider to wire its NTP source at boot.
    NTPClient& getNtpClient() { return _timeClient; }

private:
    // Common sync implementation. `timeoutMultiplier` is 1 for the boot
    // path and 2 for the periodic retry (which can afford to wait longer).
    void performSyncNow(uint8_t timeoutMultiplier);

    bool waitForConnection(uint8_t timeoutMultiplier);
    bool waitForNtp(uint8_t timeoutMultiplier);
    void applySyncedTime();

    WiFiUDP _ntpUDP;
    NTPClient _timeClient;

    WiFiManagerLite* _wifi;
    TimeProvider* _timeProvider;
    RtcClock* _rtcClock;
    SettingsStore* _settings;
    AppSettings* _appSettings;

    uint32_t _lastSyncMs = 0;
    bool _retryDone = false;
};

#endif // WIFI_SYNC_H
