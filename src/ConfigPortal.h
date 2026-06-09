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
    typedef bool (*BoolStatusCallback)(void* context);
    typedef String (*StringStatusCallback)(void* context);

    ConfigPortal(SettingsStore& settingsStore);

    void setStatusProvider(void* context,
                           BoolStatusCallback connected,
                           BoolStatusCallback inConfigMode,
                           StringStatusCallback ipAddress);

    void beginAPMode();
    void beginStationMode();
    void loop();
    void stop();

    void startOTAUpdate();
    bool isUpdating();
    void setOtaDisplayCallback(std::function<void(bool, unsigned int, unsigned int)> cb);

#if ENABLE_OTA
    void handleUpdateForm();
    void handleUpdateUpload();
    void handleUpdateStatus();
#endif

private:
    SettingsStore& _settingsStore;
    AppSettings _settings;
    DNSServer _dnsServer;
    WebServer _webServer;
    bool _configModeStation;
    bool _otaUpdate;
    std::function<void(bool, unsigned int, unsigned int)> _otaDisplayCb;
    void* _statusContext;
    BoolStatusCallback _connectedCallback;
    BoolStatusCallback _inConfigModeCallback;
    StringStatusCallback _ipAddressCallback;

    bool currentConnected();
    bool currentInConfigMode();
    String currentIPAddress();

    void configureWebServerRoutes();
    void handleRoot();
    void handleScan();
    void handleSave();
    void handleStatus();
    void handleCaptivePortal();
    void handleNotFound();
    String encodeHTML(const String& str);
    String encodeJSON(const String& str);
    int getRSSIPercentage(int rssi);
};

#endif // CONFIG_PORTAL_H
