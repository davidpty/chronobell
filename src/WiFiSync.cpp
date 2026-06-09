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

    if (!_wifi->hasCredentials()) {
        LOGLN("No WiFi credentials saved");
        if (_rtcClock->available()) {
            LOGLN("Using RTC time");
        } else {
            LOGLN("No RTC available - clock may show incorrect time");
            _timeClient.begin();
        }
        _lastSyncSucceeded = false;
        _lastSyncAttemptMs = millis();
        _firstSyncPending = false;
        return;
    }

    if (_wifi->isConnected()) {
        LOGLN("WiFi already connected at boot");
        _firstSyncPending = false;
        enterNtpPhase();
    } else {
        LOGLN("WiFi credentials found, connecting...");
        _firstSyncPending = false;
        _phaseStartMs = millis();
    }
    _phase = Phase::WifiConnecting;
    _lastSyncAttemptMs = millis();
#endif
}

void WiFiSync::tick() {
#if ENABLE_WIFI_SYNC == 0
    return;
#endif

    if (_wifi->isInConfigMode() && _phase != Phase::Idle) {
        LOGLN("Sync aborted: config mode active");
        _phase = Phase::Idle;
        return;
    }

    switch (_phase) {
    case Phase::Idle:
        if (!_rtcClock->available() && _timeClient.isTimeSet()) {
            _timeClient.update();
        }
        tickPeriodic();
        break;
    case Phase::WifiConnecting:
        tickWifi();
        break;
    case Phase::NtpWaiting:
        tickNtp();
        break;
    case Phase::Teardown:
        tickTeardown();
        break;
    }
}

// -----------------------------------------------------------------------------
// State machine helpers
// -----------------------------------------------------------------------------

void WiFiSync::tickPeriodic() {
    if (TIME_SYNC_INTERVAL_MINUTES == 0) return;

    if (_firstSyncPending) {
        _firstSyncPending = false;
        LOGLN("\n=== Initial periodic sync ===");
        enterWifiPhase(2);
        return;
    }

    unsigned long interval = _lastSyncSucceeded
        ? (unsigned long)NTP_RETRY_SUCCESS_MINUTES * 60000UL
        : (unsigned long)NTP_RETRY_FAILED_MINUTES * 60000UL;

    if (millis() - _lastSyncAttemptMs >= interval) {
        LOGLN("\n=== Periodic WiFi sync ===");
        enterWifiPhase(2);
    }
}

void WiFiSync::enterWifiPhase(uint8_t timeoutMultiplier) {
    _lastSyncAttemptMs = millis();

    if (_wifi->isConnected()) {
        enterNtpPhase();
        return;
    }

    LOGLN("Connecting WiFi...");
    int timeoutMs = (int)WIFI_CONNECT_ATTEMPTS * (int)timeoutMultiplier * 500;
    if (!_wifi->reconnectSTA(timeoutMs)) {
        finishSync(false);
        return;
    }
    _phase = Phase::WifiConnecting;
    _phaseStartMs = millis();
    _phaseTimeoutMs = (unsigned long)timeoutMs + 2000;
}

void WiFiSync::enterNtpPhase() {
    LOG("WiFi connected! IP: ");
    LOGLN(_wifi->getIPAddress());

    _wifi->startNetworkServices();

    *_appSettings = _settings->load();
    _timeClient.setTimeOffset(_appSettings->timezone.offsetMinutes * 60);

    if (!_timeClient.isTimeSet()) {
        _timeClient.begin();
    }

    _phase = Phase::NtpWaiting;
    _phaseStartMs = millis();
    _phaseTimeoutMs = (unsigned long)NTP_SYNC_ATTEMPTS * 2 * 500;
}

void WiFiSync::tickWifi() {
    if (_wifi->isConnected()) {
        enterNtpPhase();
        return;
    }
    if (millis() - _phaseStartMs >= _phaseTimeoutMs) {
        LOGLN("WiFi connection timeout");
        finishSync(false);
    }
}

void WiFiSync::tickNtp() {
    _timeClient.update();
    if (_timeClient.isTimeSet()) {
        LOGLN("NTP time received");
        applySyncedTime();
        finishSync(true);
        return;
    }
    if (millis() - _phaseStartMs >= _phaseTimeoutMs) {
        LOGLN("NTP sync timeout");
        finishSync(false);
    }
}

void WiFiSync::tickTeardown() {
#if KEEP_WIFI_ALIVE == 0
    if (_wifi->isNetworkServicesActive()) {
        _wifi->stopNetworkServices();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#endif
    _phase = Phase::Idle;
}

void WiFiSync::finishSync(bool succeeded) {
    _firstSyncPending = false;
    _lastSyncSucceeded = succeeded;
    _lastSyncAttemptMs = millis();

    if (succeeded) {
        LOGLN("Time sync completed");
    } else {
        LOGLN("Time sync failed");
        if (_rtcClock->available()) {
            LOGLN("Using RTC time");
        }
    }

#if KEEP_WIFI_ALIVE == 0
    _phase = Phase::Teardown;
    _phaseStartMs = millis();
#else
    _phase = Phase::Idle;
#endif
}

void WiFiSync::applySyncedTime() {
    LOG("Current time: ");
    LOGLN(_timeClient.getFormattedTime());

    _settings->clearManualTime();
    _appSettings->manualTime.enabled = false;
    _appSettings->manualTime.epoch = 0;

    if (_rtcClock->available()) {
        time_t epoch = _timeClient.getEpochTime();
        _timeProvider->setRtcFromEpoch(epoch);
        LOGLN("RTC updated with NTP time");
        _timeProvider->readRtc();
        ClockTime rtcTime = _rtcClock->getTime();
        LOG("RTC time after sync: ");
        LOGF("%02d:%02d:%02d\n", rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
    }
}
