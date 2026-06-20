#ifndef WIFI_MANAGER_LITE_H
#define WIFI_MANAGER_LITE_H

#include "Config.h"
#include <WiFi.h>
#include "ConfigPortal.h"
#include "SettingsStore.h"

class TimeProvider;

class WiFiManagerLite {
public:
    WiFiManagerLite(SettingsStore& settingsStore);

    void setNetworkServiceConfig(const char* mdnsHostname, const char* otaPassword);
    void setTimeProvider(TimeProvider* timeProvider);
    bool begin();
    bool reconnectSTA(int timeoutMs);
    bool reconnectSTAWithFallback(int timeoutMs);
    bool startPendingNetworkReconnect(int timeoutMs);
    bool startPendingNetworkReconnect(const String& ssid, const String& password, int timeoutMs);
    void loop();
    void startConfigMode();
    void startConfigModePreferStation();
    bool isConnected();
    String getIPAddress();
    bool isInConfigMode();
    void startHotspot();
    void stopHotspot();
    bool isHotspotActive();
    int16_t hotspotRemainingMenuMinutes() const;
    String hotspotMenuLabel(bool editing, int16_t value) const;
    void resetHotspotTimer();
    void suspendPendingNetworkReconnect();

    void startNetworkServices();
    void stopNetworkServices();
    bool isNetworkServicesActive();

    bool hasCredentials();

    bool isUpdating();
    typedef void (*OtaDisplayCallback)(void* context, bool active, unsigned int progress, unsigned int total);
    typedef bool (*SaveCallback)(void* context, bool wifiChanged, bool tzChanged, bool manualTimeChanged,
                                 const String& ssid, const String& password);
    typedef void (*PreviewCallback)(void* context, const String& field);
    typedef String (*TimerStatusCallback)(void* context);
    typedef bool (*HotspotStatusCallback)(void* context);
    typedef int16_t (*HotspotRemainingCallback)(void* context);
    typedef void (*HotspotToggleCallback)(void* context, bool on);
    typedef void (*ReconnectResultCallback)(void* context, bool success);

    void setOtaDisplayCallback(OtaDisplayCallback cb, void* context);
    void setSaveCallback(SaveCallback cb, void* context);
    void setPreviewCallback(PreviewCallback cb, void* context);
    void setTimerStatusCallback(TimerStatusCallback cb, void* context);
    void setHotspotCallbacks(HotspotStatusCallback status,
                             HotspotRemainingCallback remaining,
                             HotspotToggleCallback toggle,
                             void* context);
    void setReconnectResultCallback(ReconnectResultCallback cb, void* context);

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
    TimeProvider* _timeProvider;
    unsigned long _hotspotExpiryEpoch;
    bool _pendingReconnectActive;
    bool _pendingReconnectFallback;
    bool _pendingReconnectFailed;
    unsigned long _pendingReconnectStartedMs;
    int _pendingReconnectTimeoutMs;
    NetworkCredentials _pendingReconnectCredentials;
    bool _pendingReconnectCredentialsValid;

    OtaDisplayCallback _otaDisplayCb = nullptr;
    void* _otaDisplayContext = nullptr;
    SaveCallback _saveCb = nullptr;
    void* _saveContext = nullptr;
    PreviewCallback _previewCb = nullptr;
    void* _previewContext = nullptr;
    TimerStatusCallback _timerStatusCb = nullptr;
    void* _timerStatusContext = nullptr;
    ReconnectResultCallback _reconnectResultCb = nullptr;
    void* _reconnectResultContext = nullptr;

    ConnState _connState = ConnState::Idle;
    unsigned long _connStartMs = 0;
    int _connTimeoutMs = STA_TIMEOUT_MS;

    static bool statusConnected(void* context);
    static bool statusInConfigMode(void* context);
    static String statusIPAddress(void* context);
    static bool statusReconnectActive(void* context);
    static bool statusReconnectFailed(void* context);
    static bool portalHotspotStatus(void* context);
    static int16_t portalHotspotRemaining(void* context);
    static void portalHotspotToggle(void* context, bool on);
    static void portalScanPreflight(void* context);
    bool loadCredentials(String& ssid, String& password);
    void clearCredentials();
    void startConnect(const String& ssid, const String& password, int timeoutMs);
    bool connectAndWait(const String& ssid, const String& password, int timeoutMs);
    void pollConnect();
    void loadSettings();
    void restoreHotspotState();
    void startHotspotInternal(bool persistState, unsigned long expiryEpoch = 0);
#if ENABLE_MDNS
    void startMDNS();
#endif
#if ENABLE_OTA
    void startArduinoOTA();
#endif
    void stopConfigMode();
};

#endif // WIFI_MANAGER_LITE_H
