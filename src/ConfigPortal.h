#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <functional>
#include "Config.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#if ENABLE_OTA
#include <Update.h>
#endif
#include "SettingsStore.h"

class ConfigPortal {
public:
    enum class PortalMode {
        Lan,
        Hotspot,
    };

    typedef bool (*BoolStatusCallback)(void* context);
    typedef String (*StringStatusCallback)(void* context);

    ConfigPortal(SettingsStore& settingsStore);

    void setStatusProvider(void* context,
                           BoolStatusCallback connected,
                           BoolStatusCallback inConfigMode,
                           StringStatusCallback ipAddress,
                           BoolStatusCallback reconnectActive,
                           BoolStatusCallback reconnectFailed);

    void begin(PortalMode mode);
    void stopHotspotDns();
    bool isApActive();
    void loop();
    void stop();

    void startOTAUpdate();
    bool isUpdating();
    void setOtaDisplayCallback(std::function<void(bool, unsigned int, unsigned int)> cb);
    void setSaveCallback(std::function<bool(bool, bool, bool, const String&, const String&)> cb);
    void setPreviewCallback(std::function<void(const String&)> cb);
    void setHotspotCallbacks(std::function<bool()> status, std::function<void(bool)> toggle);
    void setScanPreflightCallback(std::function<void()> cb);

#if ENABLE_OTA
    void handleUpdateUpload();
#endif

private:
    SettingsStore& _settingsStore;
    AppSettings _settings;
    DNSServer _dnsServer;
    WebServer _webServer;
    bool _configModeStation;
    bool _dnsActive;
    bool _otaUpdate;
    size_t _otaExpectedSize;
    std::function<void(bool, unsigned int, unsigned int)> _otaDisplayCb;
    void* _statusContext;
    BoolStatusCallback _connectedCallback;
    BoolStatusCallback _inConfigModeCallback;
    StringStatusCallback _ipAddressCallback;
    BoolStatusCallback _reconnectActiveCallback;
    BoolStatusCallback _reconnectFailedCallback;
    std::function<bool(bool, bool, bool, const String&, const String&)> _saveCb;
    std::function<void(const String&)> _previewCb;
    std::function<bool()> _hotspotStatusCb = nullptr;
    std::function<void(bool)> _hotspotToggleCb = nullptr;
    std::function<void()> _scanPreflightCb = nullptr;

    bool currentConnected();
    bool currentInConfigMode();
    String currentIPAddress();
    bool currentReconnectActive();
    bool currentReconnectFailed();

    void configureWebServerRoutes();
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleApply();
    void handleStatus();
    void handleCaptivePortal();
    void handleNotFound();
    String encodeHTML(const String& str);
    String encodeJSON(const String& str);
    int getRSSIPercentage(int rssi);
};

#endif // CONFIG_PORTAL_H
