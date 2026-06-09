/**
 * WiFiManagerLite - Minimal Wi-Fi Configuration for ESP32
 * Handles credential storage, AP mode, and web configuration portal
 */

#ifndef WIFI_MANAGER_LITE_H
#define WIFI_MANAGER_LITE_H

#include "Config.h"
#include <WiFi.h>
#include "ConfigPortal.h"
#include "SettingsStore.h"

class WiFiManagerLite {
public:
    WiFiManagerLite(SettingsStore& settingsStore);

    // Public API
    void setNetworkServiceConfig(const char* mdnsHostname, const char* otaPassword);
    bool begin();
    bool reconnectSTA(int timeoutMs);
    void loop();
    void startConfigMode();  // Force entry into configuration mode
    void startConfigModePreferStation();  // Use saved WiFi first, then fall back to AP mode
    bool isConnected();
    String getIPAddress();
    bool isInConfigMode();

    // Always-on LAN services (mDNS, ArduinoOTA). Idempotent. No-op if
    // STA is not currently connected. Call after a successful connect to
    // make the device reachable as <MDNS_HOSTNAME>.local and via
    // ArduinoIDE's network port.
    void startNetworkServices();
    void stopNetworkServices();
    bool isNetworkServicesActive();

    // Firmware update methods
    void startOTAUpdate();
    bool isUpdating();

private:
    // Configuration constants
    static const int STA_TIMEOUT_MS;
    static const int CONNECTION_RETRY_INTERVAL_MS;
    static const int CONNECTION_SLOW_RETRY_INTERVAL_MS;
    static const int CONNECTION_FAST_RETRY_LIMIT;

    // Internal state
    SettingsStore& _settingsStore;
    AppSettings _settings;
    bool _isConnected;
    bool _inConfigMode;
    bool _configModeStation;
    unsigned long _lastConnectionAttempt;
    int _connectionAttempts;
    String _mdnsHostname;
    String _otaPassword;
    bool _otaUpdate;
    bool _arduinoOtaEnabled;
    bool _mdnsEnabled;
    bool _networkServicesStarted;
    ConfigPortal _portal;

    // Private methods
    static bool statusConnected(void* context);
    static bool statusInConfigMode(void* context);
    static String statusIPAddress(void* context);
    bool loadCredentials(String& ssid, String& password);
    void clearCredentials();
    bool connectSTA(const String& ssid, const String& password, int timeoutMs);
    void loadSettings();
#if ENABLE_MDNS
    void startMDNS();
#endif
#if ENABLE_OTA
    void startArduinoOTA();
#endif
    void stopConfigMode();
};

#endif // WIFI_MANAGER_LITE_H
