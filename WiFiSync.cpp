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
    Serial.println("WiFi sync is disabled via configuration");
    if (_rtcClock->available()) {
        Serial.println("Using RTC time");
    }
    return;
#else
    Serial.println("Initializing WiFi...");
    _wifi->begin();

    if (!_wifi->isConnected()) {
        if (_rtcClock->available()) {
            Serial.println("WiFi not configured - using RTC time");
        } else {
            Serial.println("WiFi not configured and no RTC - clock may show incorrect time");
            _timeClient.begin();
        }
        return;
    }

    Serial.print("WiFi connected! IP: ");
    Serial.println(_wifi->getIPAddress());

#if KEEP_WIFI_ALIVE == 1
    // Idempotent; safe to call again from the periodic sync path. Powers
    // down with the device only.
    _wifi->startNetworkServices();
#endif

    // Re-read settings in case the timezone was edited.
    *_appSettings = _settings->load();
    Serial.print("Timezone offset: ");
    Serial.print((int)_appSettings->timezone.offsetMinutes);
    Serial.println(" min");

    _timeClient.setTimeOffset(_appSettings->timezone.offsetMinutes * 60);
    _timeClient.begin();
    Serial.println("NTP client initialized");

    if (waitForNtp(1)) {
        applySyncedTime();
        _retryDone = true;  // Don't retry this hour
    } else {
        Serial.println("NTP sync failed - using RTC time");
    }

#if KEEP_WIFI_ALIVE == 0
    // Power-saving: tear down LAN services and shut off WiFi after sync.
    if (_wifi->isNetworkServicesActive()) {
        _wifi->stopNetworkServices();
    }
    Serial.println("Turning off WiFi after time sync");
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
    if (_retryDone) return;
    if (millis() - _lastSyncMs < (TIME_SYNC_INTERVAL_MINUTES * 60UL * 1000UL)) return;

    Serial.println("\n=== Hourly WiFi retry ===");
    performSyncNow(2);
    _retryDone = true;  // Only one retry per hour
#endif
}

void WiFiSync::updateIfActive() {
    if (!_rtcClock->available() && _timeClient.isTimeSet()) {
        _timeClient.update();
    }
}

void WiFiSync::performSyncNow(uint8_t timeoutMultiplier) {
    // Skip the hourly sync if the user has the config portal open. The AP
    // path will see isConnected()==false, but the wait loop would still
    // spin for ~5 s calling WiFi APIs that fight the portal.
    if (_wifi->isInConfigMode()) {
        return;
    }

    Serial.println("\n=== WiFi time sync ===");
    if (!_wifi->isConnected()) {
        Serial.println("Connecting WiFi...");
        _wifi->reconnectSTA((int)WIFI_CONNECT_TIMEOUT * (int)timeoutMultiplier * 500);
    }

    if (!waitForConnection(timeoutMultiplier)) {
        Serial.println("WiFi connection failed");
#if KEEP_WIFI_ALIVE == 0
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
#endif
        return;
    }

    Serial.print("WiFi connected! IP: ");
    Serial.println(_wifi->getIPAddress());

    // Idempotent: a no-op if already up (e.g. KEEP_WIFI_ALIVE=1 boot path).
    _wifi->startNetworkServices();

    if (!_timeClient.isTimeSet()) {
        _timeClient.begin();
    }

    if (waitForNtp(timeoutMultiplier)) {
        applySyncedTime();
    } else {
        Serial.println("NTP sync failed");
    }

#if KEEP_WIFI_ALIVE == 0
    if (_wifi->isNetworkServicesActive()) {
        _wifi->stopNetworkServices();
    }
    Serial.println("Turning off WiFi");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
}

bool WiFiSync::waitForConnection(uint8_t timeoutMultiplier) {
    uint8_t attempts = 0;
    uint8_t maxAttempts = WIFI_CONNECT_TIMEOUT * timeoutMultiplier;
    while (!_wifi->isConnected() && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    return _wifi->isConnected();
}

bool WiFiSync::waitForNtp(uint8_t timeoutMultiplier) {
    Serial.print("Waiting for NTP sync");
    uint8_t attempts = 0;
    uint8_t maxAttempts = NTP_SYNC_TIMEOUT * timeoutMultiplier;
    while (attempts < maxAttempts) {
        if (_timeClient.update()) {
            Serial.println();
            return true;
        }
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    return false;
}

void WiFiSync::applySyncedTime() {
    Serial.println("Time synchronized!");
    Serial.print("Current time: ");
    Serial.println(_timeClient.getFormattedTime());

    _settings->clearManualTime();
    _appSettings->manualTime.enabled = false;
    _appSettings->manualTime.epoch = 0;

    if (_rtcClock->available()) {
        _timeProvider->setRtcFromEpoch(_timeClient.getEpochTime());
        Serial.println("RTC updated with NTP time");
        _timeProvider->readRtc();
        ClockTime rtcTime = _rtcClock->getTime();
        Serial.print("RTC time after sync: ");
        Serial.printf("%02d:%02d:%02d\n", rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
    }

    _lastSyncMs = millis();
}
