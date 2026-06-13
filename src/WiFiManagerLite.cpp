#include "Config.h"
#include "WiFiManagerLite.h"
#include "TimeProvider.h"
#if ENABLE_OTA
#include <ArduinoOTA.h>
#endif
#if ENABLE_MDNS
#include <ESPmDNS.h>
#endif

const int WiFiManagerLite::STA_TIMEOUT_MS = 15000;
const int WiFiManagerLite::CONNECTION_RETRY_INTERVAL_MS = 30000;
const int WiFiManagerLite::CONNECTION_SLOW_RETRY_INTERVAL_MS = 300000;
const int WiFiManagerLite::CONNECTION_FAST_RETRY_LIMIT = 3;

WiFiManagerLite::WiFiManagerLite(SettingsStore& settingsStore)
    : _settingsStore(settingsStore)
    , _settings()
    , _isConnected(false)
    , _inConfigMode(false)
    , _configModeStation(false)
    , _hotspotActive(false)
    , _lastConnectionAttempt(0)
    , _connectionAttempts(0)
    , _mdnsHostname("chronobell")
    , _otaPassword("chronobell")
    , _otaUpdate(false)
    , _arduinoOtaEnabled(false)
    , _mdnsEnabled(false)
    , _networkServicesStarted(false)
    , _portalNormalMode(false)
    , _portal(settingsStore)
    , _timeProvider(nullptr)
    , _hotspotExpiryEpoch(0)
{
    _portal.setStatusProvider(this, statusConnected, statusInConfigMode, statusIPAddress);
    _portal.setHotspotCallbacks(
        [this]() -> bool { return _hotspotActive; },
        [this](bool on) { if (on) startHotspot(); else stopHotspot(); }
    );
}

void WiFiManagerLite::setNetworkServiceConfig(const char* mdnsHostname, const char* otaPassword) {
    if (mdnsHostname && mdnsHostname[0]) {
        _mdnsHostname = mdnsHostname;
    }
    if (otaPassword) {
        _otaPassword = otaPassword;
    }
}

void WiFiManagerLite::setTimeProvider(TimeProvider* timeProvider) {
    _timeProvider = timeProvider;
}

bool WiFiManagerLite::begin() {
    String ssid, password;

    loadSettings();
    restoreHotspotState();

    if (!loadCredentials(ssid, password)) {
        LOGLN("No stored Wi-Fi credentials found");
        return false;
    }

    LOG("Loaded credentials for: ");
    LOGLN(ssid);

    startConnect(ssid, password, STA_TIMEOUT_MS);
    return true;
}

bool WiFiManagerLite::reconnectSTA(int timeoutMs) {
    if (_inConfigMode || _otaUpdate) {
        return false;
    }

    String ssid, password;
    if (!loadCredentials(ssid, password)) {
        LOGLN("No stored Wi-Fi credentials found");
        _isConnected = false;
        _connectionAttempts = 0;
        return false;
    }

    if (_networkServicesStarted) {
        stopNetworkServices();
    }

    LOG("Reconnecting Wi-Fi to: ");
    LOGLN(ssid);

    startConnect(ssid, password, timeoutMs);
    return true;
}

bool WiFiManagerLite::reconnectSTAWithFallback(int timeoutMs) {
    String activeSsid;
    String activePassword;
    NetworkCredentials pending;
    bool haveActive = loadCredentials(activeSsid, activePassword);
    bool havePending = _settingsStore.loadPendingNetwork(pending);

    if (!havePending && !haveActive) {
        LOGLN("No Wi-Fi credentials available for reconnect");
        return false;
    }

    if (_networkServicesStarted) {
        stopNetworkServices();
    }

    if (havePending) {
        LOG("Trying pending Wi-Fi SSID: ");
        LOGLN(pending.ssid);
        if (connectAndWait(pending.ssid, pending.password, timeoutMs)) {
            AppSettings settings = _settingsStore.load();
            settings.network.ssid = pending.ssid;
            settings.network.password = pending.password;
            settings.manualTime.enabled = false;
            settings.manualTime.epoch = 0;
            _settingsStore.save(settings);
            _settingsStore.clearPendingNetwork();
            _settings = settings;
            loadSettings();
            return true;
        }

        LOGLN("Pending Wi-Fi credentials failed; restoring previous network");
        _settingsStore.clearPendingNetwork();
    }

    if (!haveActive) {
        return false;
    }

    if (connectAndWait(activeSsid, activePassword, timeoutMs)) {
        loadSettings();
        return true;
    }

    return false;
}

void WiFiManagerLite::loadSettings() {
    _settings = _settingsStore.load();

    LOG("Clock style loaded: ");
    LOGLN(displayModeLabel(_settings.displayMode));

    LOG("Timezone loaded: ");
    LOG(_settings.timezone.offsetMinutes);
    LOG(" min");
    LOG(" (");
    LOG(_settings.timezone.name);
    LOGLN(")");

    LOG("Bell mode loaded: ");
    LOGLN((int)_settings.bellMode);

    LOG("Hour format loaded: ");
    LOGLN(timeFormatLabel(_settings.timeFormat));
}

void WiFiManagerLite::restoreHotspotState() {
    bool enabled = false;
    unsigned long expiryEpoch = 0;
    if (!_settingsStore.loadHotspotState(enabled, expiryEpoch) || !enabled) {
        return;
    }

    if (_hotspotActive) {
        return;
    }

    if (HOTSPOT_TIMEOUT_MINUTES == 0) {
        LOGLN("Restoring persistent hotspot (forever)");
        startHotspotInternal(false, 0);
        return;
    }

    time_t nowEpoch = 0;
    if (_timeProvider && _timeProvider->currentEpoch(nowEpoch) && expiryEpoch > 0) {
        if ((time_t)expiryEpoch > nowEpoch) {
            LOGLN("Restoring persistent hotspot until saved expiry");
            startHotspotInternal(false, expiryEpoch);
            return;
        }

        LOGLN("Saved hotspot expiry already passed - clearing state");
        _settingsStore.saveHotspotState(false, 0);
        return;
    }

    LOGLN("No valid time available to restore hotspot state - clearing saved state");
    _settingsStore.saveHotspotState(false, 0);
}

void WiFiManagerLite::startHotspotInternal(bool persistState, unsigned long expiryEpoch) {
    if (_hotspotActive) return;

    LOGLN("Starting hotspot AP alongside station...");

    WiFi.mode(WIFI_AP_STA);
    IPAddress apIP(192, 168, 4, 1);
    IPAddress apGateway(192, 168, 4, 1);
    IPAddress apSubnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);

    _portal.beginApOnly();

    _hotspotActive = true;
    _hotspotExpiryEpoch = 0;
    if (persistState) {
        if (HOTSPOT_TIMEOUT_MINUTES > 0) {
            time_t nowEpoch = 0;
            if (_timeProvider && _timeProvider->currentEpoch(nowEpoch)) {
                expiryEpoch = (unsigned long)(nowEpoch + (time_t)HOTSPOT_TIMEOUT_MINUTES * 60);
            }
        }
        _hotspotExpiryEpoch = expiryEpoch;
        _settingsStore.saveHotspotState(true, expiryEpoch);
    } else if (expiryEpoch > 0) {
        _hotspotExpiryEpoch = expiryEpoch;
    }
    LOG("Hotspot active on SSID: ");
    LOGLN(AP_SSID);
}

void WiFiManagerLite::loop() {
    pollConnect();

    if (_hotspotActive && HOTSPOT_TIMEOUT_MINUTES > 0 && _hotspotExpiryEpoch > 0) {
        time_t nowEpoch = 0;
        if (_timeProvider && _timeProvider->currentEpoch(nowEpoch) &&
            (time_t)_hotspotExpiryEpoch <= nowEpoch) {
            LOG("Hotspot expiry reached at ");
            LOG(_hotspotExpiryEpoch);
            LOGLN(", stopping hotspot");
            stopHotspot();
        }
    }

    if (!_inConfigMode && _connState == ConnState::Connected && !_portalNormalMode) {
        _portal.beginNormalMode();
        _portalNormalMode = true;
    }

    if (_inConfigMode || _hotspotActive) {
        _portal.loop();
    } else if (_portalNormalMode) {
        _portal.loop();
    }
#if ENABLE_OTA
    if (_arduinoOtaEnabled) {
        ArduinoOTA.handle();
    }
#endif
#if KEEP_WIFI_ALIVE == 1
    if (!_inConfigMode && !_otaUpdate && _connState != ConnState::Connecting && WiFi.status() != WL_CONNECTED) {
        if (!hasCredentials()) return;

        uint32_t now = millis();
        uint32_t retryInterval;
        if (_connectionAttempts < CONNECTION_FAST_RETRY_LIMIT) {
            retryInterval = CONNECTION_RETRY_INTERVAL_MS;
        } else if (_connectionAttempts < CONNECTION_SLOW_RETRY_LIMIT) {
            retryInterval = CONNECTION_SLOW_RETRY_INTERVAL_MS;
        } else {
            retryInterval = CONNECTION_DEEP_BACKOFF_INTERVAL_MINUTES * 60000UL;
        }
        if (now - _lastConnectionAttempt >= retryInterval) {
            _lastConnectionAttempt = now;
            reconnectSTA(STA_TIMEOUT_MS);
        }
    }
#endif
}

bool WiFiManagerLite::isConnected() {
    return _connState == ConnState::Connected && !_inConfigMode && WiFi.status() == WL_CONNECTED;
}

String WiFiManagerLite::getIPAddress() {
    if (_isConnected && WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    if (_inConfigMode && !_configModeStation) {
        return WiFi.softAPIP().toString();
    }
    return "";
}

bool WiFiManagerLite::isInConfigMode() {
    return _inConfigMode;
}

bool WiFiManagerLite::statusConnected(void* context) {
    WiFiManagerLite* manager = static_cast<WiFiManagerLite*>(context);
    return manager->_connState == ConnState::Connected;
}

bool WiFiManagerLite::statusInConfigMode(void* context) {
    return static_cast<WiFiManagerLite*>(context)->isInConfigMode();
}

String WiFiManagerLite::statusIPAddress(void* context) {
    return static_cast<WiFiManagerLite*>(context)->getIPAddress();
}

bool WiFiManagerLite::hasCredentials() {
    String ssid, password;
    return loadCredentials(ssid, password);
}

bool WiFiManagerLite::loadCredentials(String& ssid, String& password) {
    _settings = _settingsStore.load();
    ssid = _settings.network.ssid;
    password = _settings.network.password;

    return ssid.length() > 0;
}

void WiFiManagerLite::clearCredentials() {
    AppSettings settings = _settingsStore.load();
    settings.network.ssid = "";
    settings.network.password = "";
    _settingsStore.save(settings);
    _settings = settings;
}

void WiFiManagerLite::startConnect(const String& ssid, const String& password, int timeoutMs) {
    if (_hotspotActive) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(ssid.c_str(), password.c_str());
    _connState = ConnState::Connecting;
    _connStartMs = millis();
    _connTimeoutMs = timeoutMs;
    _isConnected = false;
}

bool WiFiManagerLite::connectAndWait(const String& ssid, const String& password, int timeoutMs) {
    startConnect(ssid, password, timeoutMs);

    unsigned long startedAt = millis();
    while (true) {
        pollConnect();
        if (_connState == ConnState::Connected) {
            return true;
        }
        if (_connState == ConnState::Idle) {
            return false;
        }
        if (millis() - startedAt >= (unsigned long)timeoutMs + 2000UL) {
            return false;
        }
        delay(100);
    }
}

void WiFiManagerLite::pollConnect() {
    if (_connState == ConnState::Connected && WiFi.status() != WL_CONNECTED) {
        _connState = ConnState::Idle;
        _isConnected = false;
        LOGLN("Wi-Fi connection lost");
        return;
    }
    if (_connState != ConnState::Connecting) return;

    if (WiFi.status() == WL_CONNECTED) {
        _connState = ConnState::Connected;
        _isConnected = true;
        _connectionAttempts = 0;
        LOG("Wi-Fi connected! IP: ");
        LOGLN(WiFi.localIP());
#if KEEP_WIFI_ALIVE == 1
        startNetworkServices();
#endif
        return;
    }

    if (millis() - _connStartMs >= (unsigned long)_connTimeoutMs) {
        LOGLN("Wi-Fi connection timeout");
        WiFi.disconnect(!_hotspotActive);
        _connState = ConnState::Idle;
        _isConnected = false;
        if (_connectionAttempts < 255) {
            _connectionAttempts++;
        }
    }
}

#if ENABLE_OTA
void WiFiManagerLite::startArduinoOTA() {
    if (_arduinoOtaEnabled) return;

    ArduinoOTA.setHostname(_mdnsHostname.c_str());
    ArduinoOTA.setPassword(_otaPassword.c_str());
    ArduinoOTA.onStart([this]() {
        _otaUpdate = true;
        if (_otaDisplayCb) _otaDisplayCb(true, 0, 0);
        LOGLN("Arduino IDE OTA update started");
    });
    ArduinoOTA.onEnd([this]() {
        LOGLN("\nArduino IDE OTA update complete");
        _otaUpdate = false;
        if (_otaDisplayCb) _otaDisplayCb(false, 100, 100);
    });
    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        if (_otaDisplayCb) _otaDisplayCb(true, progress, total);
        if (total > 0) {
            LOGF("Arduino IDE OTA progress: %u%%\r", (progress * 100) / total);
        }
    });
    ArduinoOTA.onError([this](ota_error_t error) {
        _otaUpdate = false;
        if (_otaDisplayCb) _otaDisplayCb(false, 0, 0);
        LOGF("Arduino IDE OTA error[%u]\n", error);
    });
    ArduinoOTA.begin();
    _arduinoOtaEnabled = true;
    LOGLN("Arduino IDE OTA ready");
}
#endif

#if ENABLE_MDNS
void WiFiManagerLite::startMDNS() {
    if (_mdnsEnabled) return;
    if (MDNS.begin(_mdnsHostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        _mdnsEnabled = true;
        LOG("mDNS ready: http://");
        LOG(_mdnsHostname);
        LOGLN(".local/");
    } else {
        _mdnsEnabled = false;
        LOGLN("mDNS start failed");
    }
}
#endif

void WiFiManagerLite::startNetworkServices() {
    if (_networkServicesStarted) return;
    if (_connState != ConnState::Connected) {
        return;
    }
#if ENABLE_MDNS
    startMDNS();
#endif
#if ENABLE_OTA
    startArduinoOTA();
#endif
    _networkServicesStarted = true;
}

void WiFiManagerLite::stopNetworkServices() {
    if (!_networkServicesStarted) return;
#if ENABLE_OTA
    if (_arduinoOtaEnabled) {
        ArduinoOTA.end();
        _arduinoOtaEnabled = false;
    }
#endif
#if ENABLE_MDNS
    if (_mdnsEnabled) {
        MDNS.end();
        _mdnsEnabled = false;
    }
#endif
    _networkServicesStarted = false;
}

bool WiFiManagerLite::isNetworkServicesActive() {
    return _networkServicesStarted;
}

void WiFiManagerLite::startConfigMode() {
    if (_portalNormalMode) {
        _portal.stop();
        _portalNormalMode = false;
    }
    stopNetworkServices();

    _inConfigMode = true;
    _configModeStation = false;
    _isConnected = false;
    _connState = ConnState::Idle;

    LOGLN("Starting Configuration Mode...");
    LOG("AP SSID: ");
    LOGLN(AP_SSID);

    String ssid, password;
    if (loadCredentials(ssid, password)) {
        LOG("Loaded stored SSID: ");
        LOGLN(ssid);
        LOG("Clock style: ");
        LOGLN(displayModeLabel(_settings.displayMode));
        LOG("Hour format: ");
        LOGLN(timeFormatLabel(_settings.timeFormat));
    }

    IPAddress apIP(192, 168, 4, 1);
    IPAddress apGateway(192, 168, 4, 1);
    IPAddress apSubnet(255, 255, 255, 0);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL);

    _portal.beginAPMode();

    LOG("Configuration portal available at: http://");
    LOGLN(WiFi.softAPIP());
}

void WiFiManagerLite::startConfigModePreferStation() {
    if (_portalNormalMode) {
        _portal.stop();
        _portalNormalMode = false;
    }
    LOGLN("Starting Configuration Mode (saved WiFi preferred)...");

    String ssid, password;
    if (loadCredentials(ssid, password)) {
        LOG("Trying stored WiFi SSID: ");
        LOGLN(ssid);

        startConnect(ssid, password, STA_TIMEOUT_MS);
        for (int i = 0; i < STA_TIMEOUT_MS / 100; i++) {
            pollConnect();
            if (_connState == ConnState::Connected || _connState == ConnState::Idle) break;
            delay(100);
        }

        if (_connState == ConnState::Connected) {
            _inConfigMode = true;
            _configModeStation = true;
            _isConnected = true;

            _portal.beginStationMode();
            startNetworkServices();

            LOG("LAN configuration portal available at: http://");
            LOGLN(WiFi.localIP());
            LOG("LAN configuration hostname: http://");
            LOG(_mdnsHostname);
            LOGLN(".local/");
            LOG("Arduino IDE OTA target: ");
            LOGLN(_mdnsHostname);
            return;
        }

        LOGLN("Stored WiFi connection failed; falling back to AP config mode");
    } else {
        LOGLN("No stored WiFi credentials; falling back to AP config mode");
    }

    startConfigMode();
}

void WiFiManagerLite::stopConfigMode() {
    _inConfigMode = false;
    stopNetworkServices();
    _portal.stop();
    if (_configModeStation) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    } else {
        WiFi.softAPdisconnect(true);
    }
    _configModeStation = false;
    _isConnected = false;
    _connState = ConnState::Idle;
}

void WiFiManagerLite::startHotspot() {
    startHotspotInternal(true);
}

void WiFiManagerLite::stopHotspot() {
    if (!_hotspotActive) return;

    LOGLN("Stopping hotspot...");
    _portal.stopApOnly();
    WiFi.softAPdisconnect(true);
    _hotspotActive = false;
    _hotspotExpiryEpoch = 0;
    _settingsStore.saveHotspotState(false, 0);
    LOGLN("Hotspot stopped - station WiFi continues normally");
}

bool WiFiManagerLite::isHotspotActive() {
    return _hotspotActive;
}

void WiFiManagerLite::setOtaDisplayCallback(std::function<void(bool, unsigned int, unsigned int)> cb) {
    _otaDisplayCb = cb;
    _portal.setOtaDisplayCallback(cb);
}

void WiFiManagerLite::setSaveCallback(std::function<bool(bool, bool, bool)> cb) {
    _saveCb = cb;
    _portal.setSaveCallback([this](bool w, bool t, bool m) -> bool {
        return _saveCb ? _saveCb(w, t, m) : true;
    });
}

void WiFiManagerLite::setPreviewCallback(std::function<void(const String&)> cb) {
    _previewCb = cb;
    _portal.setPreviewCallback(cb);
}

void WiFiManagerLite::setHotspotCallbacks(std::function<bool()> status, std::function<void(bool)> toggle) {
    _portal.setHotspotCallbacks(status, toggle);
}

bool WiFiManagerLite::isUpdating() {
    return _otaUpdate || _portal.isUpdating();
}
