#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

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
    typedef void (*OtaDisplayCallback)(void* context, bool active, unsigned int progress, unsigned int total);
    typedef bool (*SaveCallback)(void* context, bool wifiChanged, bool tzChanged, bool manualTimeChanged,
                                 const String& ssid, const String& password);
    typedef void (*PreviewCallback)(void* context, const String& field);
    typedef bool (*HotspotStatusCallback)(void* context);
    typedef int16_t (*HotspotRemainingCallback)(void* context);
    typedef void (*HotspotToggleCallback)(void* context, bool on);
    typedef void (*ScanPreflightCallback)(void* context);

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
    void setOtaDisplayCallback(OtaDisplayCallback cb, void* context);
    void setSaveCallback(SaveCallback cb, void* context);
    void setPreviewCallback(PreviewCallback cb, void* context);
    void setHotspotCallbacks(HotspotStatusCallback status,
                             HotspotRemainingCallback remaining,
                             HotspotToggleCallback toggle,
                             void* context);
    void setScanPreflightCallback(ScanPreflightCallback cb, void* context);

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
    OtaDisplayCallback _otaDisplayCb;
    void* _otaDisplayContext;
    void* _statusContext;
    BoolStatusCallback _connectedCallback;
    BoolStatusCallback _inConfigModeCallback;
    StringStatusCallback _ipAddressCallback;
    BoolStatusCallback _reconnectActiveCallback;
    BoolStatusCallback _reconnectFailedCallback;
    SaveCallback _saveCb;
    void* _saveContext;
    PreviewCallback _previewCb;
    void* _previewContext;
    HotspotStatusCallback _hotspotStatusCb = nullptr;
    HotspotRemainingCallback _hotspotRemainingCb = nullptr;
    HotspotToggleCallback _hotspotToggleCb = nullptr;
    void* _hotspotContext = nullptr;
    ScanPreflightCallback _scanPreflightCb = nullptr;
    void* _scanPreflightContext = nullptr;

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
    String hotspotOnText(bool hotspotActive) const;
    String encodeHTML(const String& str);
    String encodeJSON(const String& str);
    int getRSSIPercentage(int rssi);
};

#endif // CONFIG_PORTAL_H
