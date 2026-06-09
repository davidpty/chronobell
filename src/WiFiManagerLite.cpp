/**
 * WiFiManagerLite - Implementation
 */

#include "Config.h"
#include "WiFiManagerLite.h"
#if ENABLE_OTA
#include <ArduinoOTA.h>
#endif
#if ENABLE_MDNS
#include <ESPmDNS.h>
#endif

// Configuration constants
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
    , _lastConnectionAttempt(0)
    , _connectionAttempts(0)
    , _mdnsHostname("chronobell")
    , _otaPassword("chronobell")
    , _otaUpdate(false)
    , _arduinoOtaEnabled(false)
    , _mdnsEnabled(false)
    , _networkServicesStarted(false)
    , _portal(settingsStore)
{
    _portal.setStatusProvider(this, statusConnected, statusInConfigMode, statusIPAddress);
}

void WiFiManagerLite::setNetworkServiceConfig(const char* mdnsHostname, const char* otaPassword) {
    if (mdnsHostname && mdnsHostname[0]) {
        _mdnsHostname = mdnsHostname;
    }
    if (otaPassword) {
        _otaPassword = otaPassword;
    }
}

bool WiFiManagerLite::begin() {
    String ssid, password;

    loadSettings();

    // Now check for WiFi credentials
    if (!loadCredentials(ssid, password)) {
        LOGLN("No stored Wi-Fi credentials found");
        return false;  // Don't enter AP mode automatically
    }

    LOG("Loaded credentials for: ");
    LOGLN(ssid);

    // Attempt to connect
    if (connectSTA(ssid, password, STA_TIMEOUT_MS)) {
        _isConnected = true;
        _connectionAttempts = 0;
        LOG("Wi-Fi connected! IP: ");
        LOGLN(WiFi.localIP());
        return true;
    }

    LOGLN("Failed to connect with stored credentials");
    _isConnected = false;
    return false;  // Don't enter AP mode automatically
}

bool WiFiManagerLite::reconnectSTA(int timeoutMs) {
    if (_inConfigMode || _otaUpdate) {
        return false;
    }

    bool shouldKeepTrying = _isConnected;
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

    if (!connectSTA(ssid, password, timeoutMs)) {
        LOGLN("Wi-Fi reconnect failed");
        _isConnected = shouldKeepTrying;
        if (_connectionAttempts < 255) {
            _connectionAttempts++;
        }
        return false;
    }

    _isConnected = true;
    _connectionAttempts = 0;
    LOG("Wi-Fi reconnected! IP: ");
    LOGLN(WiFi.localIP());

#if KEEP_WIFI_ALIVE == 1
    startNetworkServices();
#endif

    return true;
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

void WiFiManagerLite::loop() {
    if (_inConfigMode) {
        _portal.loop();
    }
    // ArduinoOTA.handle() is pumped whenever the service is up, regardless
    // of whether we are in config mode. With KEEP_WIFI_ALIVE=1 it stays
    // up permanently after the first successful STA connect.
#if ENABLE_OTA
    if (_arduinoOtaEnabled) {
        ArduinoOTA.handle();
    }
#endif
#if KEEP_WIFI_ALIVE == 1
    if (!_inConfigMode && !_otaUpdate && _isConnected && WiFi.status() != WL_CONNECTED) {
        uint32_t now = millis();
        uint32_t retryInterval = (_connectionAttempts < CONNECTION_FAST_RETRY_LIMIT)
            ? CONNECTION_RETRY_INTERVAL_MS
            : CONNECTION_SLOW_RETRY_INTERVAL_MS;
        if (now - _lastConnectionAttempt >= retryInterval) {
            _lastConnectionAttempt = now;
            reconnectSTA(STA_TIMEOUT_MS);
        }
    }
#endif
}

bool WiFiManagerLite::isConnected() {
    return _isConnected && WiFi.status() == WL_CONNECTED && !_inConfigMode;
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
    return manager->_isConnected && WiFi.status() == WL_CONNECTED;
}

bool WiFiManagerLite::statusInConfigMode(void* context) {
    return static_cast<WiFiManagerLite*>(context)->isInConfigMode();
}

String WiFiManagerLite::statusIPAddress(void* context) {
    return static_cast<WiFiManagerLite*>(context)->getIPAddress();
}

// ============================================================================
// Private Methods
// ============================================================================

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

bool WiFiManagerLite::connectSTA(const String& ssid, const String& password, int timeoutMs) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        if (millis() - startTime > timeoutMs) {
            WiFi.disconnect(true);
            return false;
        }
    }
    return true;
}

#if ENABLE_OTA
void WiFiManagerLite::startArduinoOTA() {
    if (_arduinoOtaEnabled) return;

    ArduinoOTA.setHostname(_mdnsHostname.c_str());
    ArduinoOTA.setPassword(_otaPassword.c_str());
    ArduinoOTA.onStart([this]() {
        _otaUpdate = true;
        LOGLN("Arduino IDE OTA update started");
    });
    ArduinoOTA.onEnd([this]() {
        LOGLN("\nArduino IDE OTA update complete");
        _otaUpdate = false;
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (total > 0) {
            LOGF("Arduino IDE OTA progress: %u%%\r", (progress * 100) / total);
        }
    });
    ArduinoOTA.onError([this](ota_error_t error) {
        _otaUpdate = false;
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
    if (!_isConnected || WiFi.status() != WL_CONNECTED) {
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
    // Tear down any always-on LAN services first so they do not hold a
    // UDP/HTTP listener alive while the AP-mode portal is running. They
    // will be re-started on the next boot (or next sync) if needed.
    stopNetworkServices();

    _inConfigMode = true;
    _configModeStation = false;
    _isConnected = false;

    LOGLN("Starting Configuration Mode...");
    LOG("AP SSID: ");
    LOGLN(AP_SSID);

    // Load stored settings (credentials, timezone, clock style, hour format)
    String ssid, password;
    if (loadCredentials(ssid, password)) {
        LOG("Loaded stored SSID: ");
        LOGLN(ssid);
        LOG("Clock style: ");
        LOGLN(displayModeLabel(_settings.displayMode));
        LOG("Hour format: ");
        LOGLN(timeFormatLabel(_settings.timeFormat));
    }

    // Configure AP
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
    LOGLN("Starting Configuration Mode (saved WiFi preferred)...");

    String ssid, password;
    if (loadCredentials(ssid, password)) {
        LOG("Trying stored WiFi SSID: ");
        LOGLN(ssid);

        if (connectSTA(ssid, password, STA_TIMEOUT_MS)) {
            _inConfigMode = true;
            _configModeStation = true;
            _isConnected = true;

            _portal.beginStationMode();
            // Re-use the always-on services path: same mDNS + ArduinoOTA
            // are needed for the LAN portal. startNetworkServices() is
            // idempotent and a no-op if already up.
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
    // Network services (ArduinoOTA, mDNS) are owned by the always-on
    // lifecycle now. stopNetworkServices() is a no-op if they were never
    // started or were already cleaned up.
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
}

bool WiFiManagerLite::isUpdating() {
    return _otaUpdate || _portal.isUpdating();
}
