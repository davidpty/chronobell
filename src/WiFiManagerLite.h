#ifndef WIFI_MANAGER_LITE_H
#define WIFI_MANAGER_LITE_H

#include <functional>
#include "Config.h"
#include <WiFi.h>
#include "ConfigPortal.h"
#include "SettingsStore.h"

class WiFiManagerLite {
public:
    WiFiManagerLite(SettingsStore& settingsStore);

    void setNetworkServiceConfig(const char* mdnsHostname, const char* otaPassword);
    bool begin();
    bool reconnectSTA(int timeoutMs);
    void loop();
    void startConfigMode();
    void startConfigModePreferStation();
    bool isConnected();
    String getIPAddress();
    bool isInConfigMode();
    void startHotspot();
    void stopHotspot();
    bool isHotspotActive();

    void startNetworkServices();
    void stopNetworkServices();
    bool isNetworkServicesActive();

    bool hasCredentials();

    bool isUpdating();
    void setOtaDisplayCallback(std::function<void(bool, unsigned int, unsigned int)> cb);
    void setSaveCallback(std::function<void(bool, bool, bool)> cb);
    void setPreviewCallback(std::function<void(const String&)> cb);

private:
    enum class ConnState { Idle, Connecting, Connected };

    static const int STA_TIMEOUT_MS;
    static const int CONNECTION_RETRY_INTERVAL_MS;
    static const int CONNECTION_SLOW_RETRY_INTERVAL_MS;
    static const int CONNECTION_FAST_RETRY_LIMIT;

    SettingsStore& _settingsStore;
    AppSettings _settings;
    bool _isConnected;
    bool _inConfigMode;
    bool _configModeStation;
    bool _hotspotActive;
    unsigned long _lastConnectionAttempt;
    int _connectionAttempts;
    String _mdnsHostname;
    String _otaPassword;
    bool _otaUpdate;
    bool _arduinoOtaEnabled;
    bool _mdnsEnabled;
    bool _networkServicesStarted;
    bool _portalNormalMode;
    ConfigPortal _portal;

    std::function<void(bool, unsigned int, unsigned int)> _otaDisplayCb;
    std::function<void(bool, bool, bool)> _saveCb;
    std::function<void(const String&)> _previewCb;

    ConnState _connState = ConnState::Idle;
    unsigned long _connStartMs = 0;
    int _connTimeoutMs = STA_TIMEOUT_MS;

    static bool statusConnected(void* context);
    static bool statusInConfigMode(void* context);
    static String statusIPAddress(void* context);
    bool loadCredentials(String& ssid, String& password);
    void clearCredentials();
    void startConnect(const String& ssid, const String& password, int timeoutMs);
    void pollConnect();
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
