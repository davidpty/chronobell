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

    void performBootSync();

    void tick();

    NTPClient& getNtpClient() { return _timeClient; }

private:
    enum class Phase { Idle, WifiConnecting, NtpWaiting, Teardown };

    void enterWifiPhase(uint8_t timeoutMultiplier);
    void enterNtpPhase();
    void tickPeriodic();
    void tickWifi();
    void tickNtp();
    void tickTeardown();
    void finishSync(bool succeeded);
    void applySyncedTime();

    WiFiUDP _ntpUDP;
    NTPClient _timeClient;

    WiFiManagerLite* _wifi;
    TimeProvider* _timeProvider;
    RtcClock* _rtcClock;
    SettingsStore* _settings;
    AppSettings* _appSettings;

    unsigned long _lastSyncAttemptMs = 0;
    bool _lastSyncSucceeded = false;
    bool _firstSyncPending = true;

    Phase _phase = Phase::Idle;
    unsigned long _phaseStartMs = 0;
    unsigned long _phaseTimeoutMs = 0;
};

#endif // WIFI_SYNC_H
