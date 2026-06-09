#include "WiFiSync.h"

#include "AppSettings.h"
#include "RtcClock.h"
#include "SettingsStore.h"
#include "TimeProvider.h"
#include "WiFiManagerLite.h"

WiFiSync::WiFiSync(WiFiManagerLite& wifi,
                   TimeProvider& timeProvider,
                   RtcClock& rtcClock,
                   SettingsStore& settings,
                   AppSettings& appSettings)
    : _timeClient(_ntpUDP, NTP_SERVER, 0, 60000)
    , _wifi(&wifi)
    , _timeProvider(&timeProvider)
    , _rtcClock(&rtcClock)
    , _settings(&settings)
    , _appSettings(&appSettings)
{
}

void WiFiSync::performBootSync() {
#if ENABLE_WIFI_SYNC == 0
    LOGLN("WiFi sync is disabled via configuration");
    if (_rtcClock->available()) {
        LOGLN("Using RTC time");
    }
    return;
#else
    LOGLN("Initializing WiFi...");
    _wifi->begin();

    if (!_wifi->isConnected()) {
        if (_rtcClock->available()) {
            LOGLN("WiFi not configured - using RTC time");
        } else {
            LOGLN("WiFi not configured and no RTC - clock may show incorrect time");
            _timeClient.begin();
        }
        _lastSyncSucceeded = false;
        _lastSyncAttemptMs = millis();
        _firstSyncPending = false;
        return;
    }

    LOG("WiFi connected! IP: ");
    LOGLN(_wifi->getIPAddress());

#if KEEP_WIFI_ALIVE == 1
    // Idempotent; safe to call again from the periodic sync path. Powers
    // down with the device only.
    _wifi->startNetworkServices();
#endif

    // Re-read settings in case the timezone was edited.
    *_appSettings = _settings->load();
    LOG("Timezone offset: ");
    LOG((int)_appSettings->timezone.offsetMinutes);
    LOGLN(" min");

    _timeClient.setTimeOffset(_appSettings->timezone.offsetMinutes * 60);
    _timeClient.begin();
    LOGLN("NTP client initialized");

    if (waitForNtp(1)) {
        applySyncedTime();
        _lastSyncSucceeded = true;
    } else {
        LOGLN("NTP sync failed - using RTC time");
        _lastSyncSucceeded = false;
    }

    _lastSyncAttemptMs = millis();
    _firstSyncPending = false;

#if KEEP_WIFI_ALIVE == 0
    // Power-saving: tear down LAN services and shut off WiFi after sync.
    if (_wifi->isNetworkServicesActive()) {
        _wifi->stopNetworkServices();
    }
    LOGLN("Turning off WiFi after time sync");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
#endif
}

void WiFiSync::maybePeriodicSync() {
#if ENABLE_WIFI_SYNC == 0
    return;
#else
    if (TIME_SYNC_INTERVAL_MINUTES == 0) return;

    // First periodic sync fires right after boot sync completes.
    if (_firstSyncPending) {
        _firstSyncPending = false;
        LOGLN("\n=== Initial periodic sync ===");
        performSyncNow(2);
        return;
    }

    unsigned long interval = _lastSyncSucceeded
        ? (unsigned long)NTP_RETRY_SUCCESS_MINUTES * 60000UL
        : (unsigned long)NTP_RETRY_FAILED_MINUTES * 60000UL;

    if (millis() - _lastSyncAttemptMs >= interval) {
        LOGLN("\n=== Periodic WiFi sync ===");
        performSyncNow(2);
    }
#endif
}

void WiFiSync::updateIfActive() {
    if (!_rtcClock->available() && _timeClient.isTimeSet()) {
        _timeClient.update();
    }
}

void WiFiSync::performSyncNow(uint8_t timeoutMultiplier) {
    if (_wifi->isInConfigMode()) {
        return;
    }

    _lastSyncAttemptMs = millis();

    LOGLN("\n=== WiFi time sync ===");
    if (!_wifi->isConnected()) {
#if KEEP_WIFI_ALIVE == 1
        // WiFiManagerLite handles reconnection; just wait for it.
        if (!waitForConnection(timeoutMultiplier)) {
            LOGLN("WiFi connection failed");
            _lastSyncSucceeded = false;
            return;
        }
#else
        // WiFi is powered off; explicitly reconnect.
        LOGLN("Connecting WiFi...");
        _wifi->reconnectSTA((int)WIFI_CONNECT_TIMEOUT * (int)timeoutMultiplier * 500);
        if (!_wifi->isConnected()) {
            LOGLN("WiFi connection failed");
            _lastSyncSucceeded = false;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return;
        }
#endif
    }

    LOG("WiFi connected! IP: ");
    LOGLN(_wifi->getIPAddress());

    // Idempotent: a no-op if already up (e.g. KEEP_WIFI_ALIVE=1 boot path).
    _wifi->startNetworkServices();

    if (!_timeClient.isTimeSet()) {
        _timeClient.begin();
    }

    if (waitForNtp(timeoutMultiplier)) {
        applySyncedTime();
        _lastSyncSucceeded = true;
    } else {
        LOGLN("NTP sync failed");
        _lastSyncSucceeded = false;
    }

#if KEEP_WIFI_ALIVE == 0
    if (_wifi->isNetworkServicesActive()) {
        _wifi->stopNetworkServices();
    }
    LOGLN("Turning off WiFi");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
}

bool WiFiSync::waitForConnection(uint8_t timeoutMultiplier) {
    uint8_t attempts = 0;
    uint8_t maxAttempts = WIFI_CONNECT_TIMEOUT * timeoutMultiplier;
    while (!_wifi->isConnected() && attempts < maxAttempts) {
        delay(500);
        LOG(".");
        attempts++;
    }
    LOGLN();
    return _wifi->isConnected();
}

bool WiFiSync::waitForNtp(uint8_t timeoutMultiplier) {
    LOG("Waiting for NTP sync");
    uint8_t attempts = 0;
    uint8_t maxAttempts = NTP_SYNC_TIMEOUT * timeoutMultiplier;
    while (attempts < maxAttempts) {
        if (_timeClient.update()) {
            LOGLN();
            return true;
        }
        delay(500);
        LOG(".");
        attempts++;
    }
    LOGLN();
    return false;
}

void WiFiSync::applySyncedTime() {
    LOGLN("Time synchronized!");
    LOG("Current time: ");
    LOGLN(_timeClient.getFormattedTime());

    _settings->clearManualTime();
    _appSettings->manualTime.enabled = false;
    _appSettings->manualTime.epoch = 0;

    if (_rtcClock->available()) {
        _timeProvider->setRtcFromEpoch(_timeClient.getEpochTime());
        LOGLN("RTC updated with NTP time");
        _timeProvider->readRtc();
        ClockTime rtcTime = _rtcClock->getTime();
        LOG("RTC time after sync: ");
        LOGF("%02d:%02d:%02d\n", rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
    }

}
