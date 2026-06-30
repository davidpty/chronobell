/**
 * ConfigPortal - Implementation
 */

#include "Config.h"
#include "ConfigPortal.h"
#include "ConfigPortalPage.h"
#include "AppSettings.h"
#include <WiFi.h>
#include <time.h>

static int16_t timezoneMinutesFromPortalValue(const String& value) {
    if (value == "5.5") return 330;
    if (value == "9.5") return 570;
    return (int16_t)(value.toInt() * 60);
}

static String portalTimezoneValue(int16_t offsetMinutes) {
    if (offsetMinutes % 60 == 0) {
        return String(offsetMinutes / 60);
    }
    if (offsetMinutes == 330) return "5.5";
    if (offsetMinutes == 570) return "9.5";
    return String((float)offsetMinutes / 60.0f, 1);
}

ConfigPortal::ConfigPortal(SettingsStore& settingsStore)
    : _settingsStore(settingsStore)
    , _settings()
    , _dnsServer()
    , _webServer(80)
    , _configModeStation(false)
    , _dnsActive(false)
    , _otaUpdate(false)
    , _otaExpectedSize(0)
    , _otaDisplayCb(nullptr)
    , _otaDisplayContext(nullptr)
    , _statusContext(nullptr)
    , _connectedCallback(nullptr)
    , _inConfigModeCallback(nullptr)
    , _ipAddressCallback(nullptr)
    , _reconnectActiveCallback(nullptr)
    , _reconnectFailedCallback(nullptr)
    , _saveCb(nullptr)
    , _saveContext(nullptr)
    , _previewCb(nullptr)
    , _previewContext(nullptr)
    , _timerStatusCb(nullptr)
    , _timerStatusContext(nullptr)
{
}

void ConfigPortal::setStatusProvider(void* context,
                                     BoolStatusCallback connected,
                                     BoolStatusCallback inConfigMode,
                                     StringStatusCallback ipAddress,
                                     BoolStatusCallback reconnectActive,
                                     BoolStatusCallback reconnectFailed) {
    _statusContext = context;
    _connectedCallback = connected;
    _inConfigModeCallback = inConfigMode;
    _ipAddressCallback = ipAddress;
    _reconnectActiveCallback = reconnectActive;
    _reconnectFailedCallback = reconnectFailed;
}

bool ConfigPortal::currentConnected() {
    return _connectedCallback ? _connectedCallback(_statusContext) : false;
}

bool ConfigPortal::currentInConfigMode() {
    return _inConfigModeCallback ? _inConfigModeCallback(_statusContext) : false;
}

String ConfigPortal::currentIPAddress() {
    return _ipAddressCallback ? _ipAddressCallback(_statusContext) : String("");
}

bool ConfigPortal::currentReconnectActive() {
    return _reconnectActiveCallback ? _reconnectActiveCallback(_statusContext) : false;
}

bool ConfigPortal::currentReconnectFailed() {
    return _reconnectFailedCallback ? _reconnectFailedCallback(_statusContext) : false;
}

void ConfigPortal::begin(PortalMode mode) {
    stop();
    _settings = _settingsStore.load();
    _configModeStation = (mode == PortalMode::Lan);
    _dnsActive = (mode == PortalMode::Hotspot);

    if (_dnsActive) {
        _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        _dnsServer.start(53, "*", WiFi.softAPIP());
    }

    configureWebServerRoutes();
    _webServer.begin();

    if (_dnsActive) {
        LOGLN("Config portal active on hotspot interface");
    } else {
        LOGLN("Config portal active on LAN interface");
    }
}

void ConfigPortal::stopHotspotDns() {
    _dnsActive = false;
    _dnsServer.stop();
    LOGLN("Hotspot DNS stopped");
}

bool ConfigPortal::isApActive() {
    return _dnsActive;
}

void ConfigPortal::loop() {
    if (_dnsActive) {
        _dnsServer.processNextRequest();
    }
    _webServer.handleClient();
}

void ConfigPortal::stop() {
    _dnsServer.stop();
    _webServer.stop();
    _dnsActive = false;
}

void ConfigPortal::configureWebServerRoutes() {
    _webServer.on("/", HTTP_GET, [this]() { handleRoot(); });
    _webServer.on("/generate_204", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/gen_204", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/library/test/success.html", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/connecttest.txt", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/redirect", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/ncsi.txt", HTTP_GET, [this]() { handleCaptivePortal(); });
    _webServer.on("/scan", HTTP_GET, [this]() { handleScan(); });
    _webServer.on("/save", HTTP_GET, [this]() { handleSave(); });
    _webServer.on("/apply", HTTP_GET, [this]() { handleApply(); });
    _webServer.on("/timer", HTTP_GET, [this]() { handleTimer(); });
    _webServer.on("/timerstatus", HTTP_GET, [this]() { handleTimerStatus(); });
    _webServer.on("/status", HTTP_GET, [this]() { handleStatus(); });
    _webServer.on("/update", HTTP_POST, [this]() {
        _webServer.send(200, "text/plain", "Update processing...");
        if (Update.hasError()) {
            _webServer.send(500, "text/plain", "Update failed: " + String(Update.errorString()));
        } else {
            LOGLN("Update complete, rebooting...");
            delay(1000);
            ESP.restart();
        }
    }, [this]() { handleUpdateUpload(); });
    _webServer.onNotFound([this]() { handleNotFound(); });
}

void ConfigPortal::handleRoot() {
    _settings = _settingsStore.load();
    String displaySsid = _settings.network.ssid;
    String displayPassword = _settings.network.password;
    bool hotspotActive = _hotspotStatusCb ? _hotspotStatusCb(_hotspotContext) : false;
    String hotspotOffClass = hotspotActive ? String("") : String("active");
    String hotspotOnClass = hotspotActive ? String("active") : String("");
    String initialHotspotOnText = hotspotOnText(hotspotActive);
    String initialBellMode = String((int)_settings.bellMode);
    String initialStyle = String((int)_settings.displayMode);
    String initialDialMarks = String((int)_settings.dialMarks);
    String initialBarSeconds = String((int)_settings.barSeconds);
    String initialBinSeconds = String((int)_settings.binSeconds);
    String initialRndInterval = String((int)_settings.rndInterval);
    String initialDateStyle = String((int)_settings.dateStyle);
    String initialTimeFormat = String((int)_settings.timeFormat);
    String initialNightMode = String((int)_settings.nightMode);
    String initialBrightness = String(_settingsStore.loadBrightness(4));
    String initialTimezone = portalTimezoneValue(_settings.timezone.offsetMinutes);
    String initialManualMode = _settings.manualTime.enabled ? "manual" : "auto";
    String initialHotspotRemaining = String(_hotspotRemainingCb ? _hotspotRemainingCb(_hotspotContext) : 0);

    String html;
    html.reserve(48000);
    html = CONFIG_PORTAL_PAGE_TEMPLATE;


    html.replace("__SSID_VALUE__", encodeHTML(displaySsid));
    html.replace("__PASSWORD_VALUE__", encodeHTML(displayPassword));
    html.replace("__ACTIVE_SSID__", encodeJSON(_settings.network.ssid));
    html.replace("__ACTIVE_PASSWORD__", encodeJSON(_settings.network.password));
    html.replace("__PENDING_SSID__", "");
    html.replace("__PENDING_PASSWORD__", "");
    html.replace("__HOTSPOT_ACTIVE__", hotspotActive ? "true" : "false");
    html.replace("__HOTSPOT_REMAINING__", initialHotspotRemaining);
    html.replace("__HOTSPOT_TIMEOUT__", String(HOTSPOT_TIMEOUT_MINUTES));
    html.replace("__HOTSPOT_ON_TEXT_HTML__", encodeHTML(initialHotspotOnText));
    html.replace("__HOTSPOT_ON_TEXT__", encodeJSON(initialHotspotOnText));
#if DEBUG
    html.replace("__DEBUG__", "1");
#else
    html.replace("__DEBUG__", "0");
#endif
    html.replace("__HOTSPOT_OFF_CLASS__", hotspotOffClass);
    html.replace("__HOTSPOT_ON_CLASS__", hotspotOnClass);
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    html.replace("__ANIM_ROW__", CONFIG_PORTAL_ANIM_ROW_TEMPLATE);
#else
    html.replace("__ANIM_ROW__", "");
#endif
    html.replace("__INITIAL_STYLE__", initialStyle);
    html.replace("__INITIAL_DIALMARKS__", initialDialMarks);
    html.replace("__INITIAL_BARSECONDS__", initialBarSeconds);
    html.replace("__INITIAL_BINSECONDS__", initialBinSeconds);
    html.replace("__INITIAL_RNDINTERVAL__", initialRndInterval);
    html.replace("__INITIAL_INFOLINE__", String((int)_settings.infoLineMode));
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    html.replace("__INITIAL_ANIM__", String((int)_settings.transitionMode));
#else
    html.replace("__INITIAL_ANIM__", "0");
#endif
    html.replace("__INITIAL_DATESTYLE__", initialDateStyle);
    html.replace("__INITIAL_TIMEFMT__", initialTimeFormat);
    html.replace("__INITIAL_NIGHTMODE__", initialNightMode);
    html.replace("__INITIAL_BELLMODE__", initialBellMode);
    html.replace("__INITIAL_BRIGHTNESS__", initialBrightness);
    html.replace("__INITIAL_ALARM_MODE__", String((int)_settings.alarm.mode));
    {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", _settings.alarm.hour, _settings.alarm.minute);
        html.replace("__INITIAL_ALARM_TIME__", buf);
    }
    html.replace("__INITIAL_TIMEZONE__", initialTimezone);
    html.replace("__INITIAL_MANUAL_MODE__", initialManualMode);

    {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:00", NIGHT_DIM_START_HOUR);
        html.replace("__NIGHT_DIM_START__", buf);
        snprintf(buf, sizeof(buf), "%02d:00", NIGHT_DIM_END_HOUR);
        html.replace("__NIGHT_DIM_END__", buf);
        snprintf(buf, sizeof(buf), "%02d:00", NIGHT_DARK_START_HOUR);
        html.replace("__NIGHT_DARK_START__", buf);
        snprintf(buf, sizeof(buf), "%02d:00", NIGHT_DARK_END_HOUR);
        html.replace("__NIGHT_DARK_END__", buf);
        snprintf(buf, sizeof(buf), "%02d:00", NIGHT_MUTE_START_HOUR);
        html.replace("__NIGHT_MUTE_START__", buf);
        snprintf(buf, sizeof(buf), "%02d:00", NIGHT_MUTE_END_HOUR);
        html.replace("__NIGHT_MUTE_END__", buf);
    }

    _webServer.send(200, "text/html", html);
}

void ConfigPortal::handleScan() {
    if (_scanPreflightCb) {
        _scanPreflightCb(_scanPreflightContext);
    }

    delay(100);
    WiFi.scanDelete();
    int n = WiFi.scanNetworks();

    if (n == WIFI_SCAN_FAILED) {
        _webServer.send(500, "application/json", "{\"error\":\"scan_failed\"}");
        return;
    }
    if (n == WIFI_SCAN_RUNNING) {
        _webServer.send(500, "application/json", "{\"error\":\"scan_running\"}");
        return;
    }

    // Store networks with their info
    struct NetworkInfo {
        String ssid;
        int rssi;
        bool secured;
    };
    NetworkInfo networks[50];
    int networkCount = 0;

    for (int i = 0; i < n && networkCount < 50; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;  // Skip hidden networks

        // Check for duplicates
        bool isDuplicate = false;
        for (int j = 0; j < networkCount; j++) {
            if (networks[j].ssid == ssid) {
                // Keep the stronger signal
                if (WiFi.RSSI(i) > networks[j].rssi) {
                    networks[j].rssi = WiFi.RSSI(i);
                    networks[j].secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                }
                isDuplicate = true;
                break;
            }
        }

        if (!isDuplicate) {
            networks[networkCount].ssid = ssid;
            networks[networkCount].rssi = WiFi.RSSI(i);
            networks[networkCount].secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            networkCount++;
        }
    }

    // Sort by RSSI (descending)
    for (int i = 0; i < networkCount - 1; i++) {
        for (int j = i + 1; j < networkCount; j++) {
            if (networks[j].rssi > networks[i].rssi) {
                NetworkInfo temp = networks[i];
                networks[i] = networks[j];
                networks[j] = temp;
            }
        }
    }

    WiFi.scanDelete();

    // Build JSON response
    String json = "{\"networks\":[";
    for (int i = 0; i < networkCount; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"";
        json += encodeJSON(networks[i].ssid);
        json += "\",\"signal\":\"";
        json += String(getRSSIPercentage(networks[i].rssi));
        json += "%\",\"secured\":";
        json += networks[i].secured ? "true" : "false";
        json += "}";
    }
    json += "]}";

    _webServer.send(200, "application/json", json);
}

void ConfigPortal::handleSave() {
    String ssid = _webServer.arg("ssid");
    String password = _webServer.arg("password");

    if (ssid.length() == 0) {
        _webServer.send(400, "application/json", "{\"success\":false}");
        return;
    }
    if (ssid.length() > 32) {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"SSID too long\"}");
        return;
    }
    if (password.length() > 64) {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Password too long\"}");
        return;
    }

    AppSettings current = _settingsStore.load();
    bool wifiChanged = (ssid != current.network.ssid || password != current.network.password);
    LOG("Portal Wi-Fi save: SSID=\"");
    LOG(ssid);
    LOG("\" password=\"");
    LOG(password);
    LOGLN("\"");
    if (wifiChanged) {
        _settingsStore.clearPendingNetwork();
        _settingsStore.clearNetworkBackup();
        if (!_settingsStore.saveNetworkBackup(current.network)) {
            _webServer.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to back up Wi-Fi credentials\"}");
            return;
        }
        NetworkCredentials pending;
        pending.ssid = ssid;
        pending.password = password;
        if (!_settingsStore.savePendingNetwork(pending)) {
            _settingsStore.clearNetworkBackup();
            _webServer.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to stage Wi-Fi credentials\"}");
            return;
        }
        _settings = current;
    } else {
        _settingsStore.clearPendingNetwork();
        _settingsStore.clearNetworkBackup();
    }

    bool success = true;
    if (_saveCb) {
        success = _saveCb(_saveContext, wifiChanged, false, false, ssid, password);
    }

    if (!success) {
        _settingsStore.clearPendingNetwork();
        NetworkCredentials backup;
        if (_settingsStore.loadNetworkBackup(backup)) {
            AppSettings restored = current;
            restored.network = backup;
            _settingsStore.save(restored);
            _settings = restored;
        }
        _settingsStore.clearNetworkBackup();
        _webServer.send(200, "application/json", "{\"success\":false,\"message\":\"Wi-Fi connection failed\"}");
        return;
    }

    String json = "{\"success\":true,\"pending\":";
    json += wifiChanged ? "true" : "false";
    json += "}";
    _webServer.send(200, "application/json", json);
}

void ConfigPortal::handleTimer() {
    String action = _webServer.arg("action");
    if (action.length() == 0) {
        action = _webServer.arg("value");
    }

    if (_previewCb) {
        if (action == "prev" || action == "left") {
            _previewCb(_previewContext, "timer:left");
        } else if (action == "mode" || action == "middle") {
            _previewCb(_previewContext, "timer:middle");
        } else if (action == "middle-long" || action == "long" || action == "hold") {
            _previewCb(_previewContext, "timer:middle-long");
        } else if (action == "next" || action == "right") {
            _previewCb(_previewContext, "timer:right");
        }
    }

    handleTimerStatus();
}

void ConfigPortal::handleTimerStatus() {
    String json = "{\"success\":true";
    if (_timerStatusCb) {
        json += ",\"timer\":";
        json += _timerStatusCb(_timerStatusContext);
    }
    json += "}";
    _webServer.send(200, "application/json", json);
}

void ConfigPortal::handleApply() {
    String field = _webServer.arg("field");
    String value = _webServer.arg("value");

    AppSettings settings = _settingsStore.load();
    bool tzChanged = false;
    bool manualTimeChanged = false;

    if (field == "style") {
        settings.displayMode = clampDisplayMode(value.toInt());
    } else if (field == "infoline") {
        settings.infoLineMode = clampInfoLineMode(value.toInt());
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    } else if (field == "anim") {
        settings.transitionMode = clampTransitionMode(value.toInt());
#endif
    } else if (field == "separator") {
        DisplayMode style = clampDisplayMode(_webServer.arg("style").toInt());
        if (style == DisplayMode::Drift) {
            settings.driftSeparator = clampSeparatorMode(value.toInt());
        } else if (style == DisplayMode::LargeDigitsOnly || style == DisplayMode::Info) {
            setSeparatorModeFor(settings, style, clampSeparatorMode(value.toInt()));
        } else {
            _webServer.send(400, "application/json", "{\"success\":false}");
            return;
        }
    } else if (field == "dialmarks") {
        settings.dialMarks = clampDialMarksMode(value.toInt());
    } else if (field == "barseconds") {
        settings.barSeconds = clampBarSecondsMode(value.toInt());
    } else if (field == "binseconds") {
        settings.binSeconds = clampBinSecondsMode(value.toInt());
    } else if (field == "rndinterval") {
        settings.rndInterval = clampRndIntervalMode(value.toInt());
    } else if (field == "datestyle") {
        settings.dateStyle = clampDateStyle(value.toInt());
    } else if (field == "timefmt") {
        settings.timeFormat = clampTimeFormat(value.toInt());
    } else if (field == "bellmode") {
        settings.bellMode = clampBellMode(value.toInt());
    } else if (field == "nightmode") {
        settings.nightMode = clampNightMode(value.toInt());
    } else if (field == "brightness") {
        int8_t b = constrain(value.toInt(), 0, 15);
        _settingsStore.saveBrightness(b);
    } else if (field == "hotspot") {
        if (_hotspotToggleCb) _hotspotToggleCb(_hotspotContext, value.toInt() != 0);
    } else if (field == "timezone") {
        settings.timezone.offsetMinutes = timezoneMinutesFromPortalValue(value);
        settings.timezone.name = _webServer.arg("tzname");
        tzChanged = true;
    } else if (field == "timeMode") {
        settings.manualTime.enabled = (value == "manual");
    } else if (field == "alarm_mode") {
        settings.alarm.mode = (uint8_t)constrain(value.toInt(), 0, 4);
    } else if (field == "alarm_time") {
        int colon = value.indexOf(':');
        if (colon > 0) {
            settings.alarm.hour   = (uint8_t)constrain(value.substring(0, colon).toInt(), 0, 23);
            settings.alarm.minute = (uint8_t)constrain(value.substring(colon + 1).toInt(), 0, 59);
        }
    } else if (field == "manualtime") {
        String dateStr = _webServer.arg("date");
        String timeStr = _webServer.arg("time");
        String secStr = _webServer.arg("sec");
        if (dateStr.length() > 0 && timeStr.length() > 0) {
            int year = dateStr.substring(0, 4).toInt();
            int month = dateStr.substring(5, 7).toInt();
            int day = dateStr.substring(8, 10).toInt();
            int hour = timeStr.substring(0, 2).toInt();
            int minute = timeStr.substring(3, 5).toInt();
            int second = secStr.length() > 0 ? constrain(secStr.toInt(), 0, 59) : 0;
            struct tm tm;
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = minute;
            tm.tm_sec = second;
            tm.tm_isdst = -1;
            time_t epoch = mktime(&tm);
            if (epoch > 0) {
                settings.manualTime.enabled = true;
                settings.manualTime.epoch = (unsigned long)epoch;
                manualTimeChanged = true;
            }
        }
    } else {
        _webServer.send(400, "application/json", "{\"success\":false}");
        return;
    }

    bool saved = _settingsStore.save(settings);
    if (saved) {
        _settings = settings;
        if (field == "style") {
            _settingsStore.clearTemporaryStyle();
        }
    }

    if (_saveCb) {
        (void)_saveCb(_saveContext, false, tzChanged, manualTimeChanged, "", "");
    }

    if (_previewCb && (field == "style" || field == "infoline"
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
        || field == "anim"
#endif
        || field == "separator" || field == "dialmarks" || field == "barseconds" || field == "binseconds" || field == "rndinterval" || field == "datestyle" || field == "timefmt" || field == "timezone" || field == "timeMode" || field == "manualtime" || field == "bellmode" || field == "brightness")) {
        _previewCb(_previewContext, field);
    }

    _webServer.send(200, "application/json", "{\"success\":true}");
}

void ConfigPortal::handleStatus() {
    _settings = _settingsStore.load();
    bool hotspotActive = _hotspotStatusCb ? _hotspotStatusCb(_hotspotContext) : false;

    String json = "{\"connected\":";
    json += currentConnected() ? "true" : "false";
    json += ",\"configMode\":";
    json += currentInConfigMode() ? "true" : "false";
    json += ",\"ip\":\"";
    json += encodeJSON(currentIPAddress());
    json += "\",\"timezone\":\"";
    json += encodeJSON(portalTimezoneValue(_settings.timezone.offsetMinutes));
    json += "\"";
    json += ",\"tzname\":\"";
    json += encodeJSON(_settings.timezone.name);
    json += "\"";
    json += ",\"style\":";
    json += (int)_settings.displayMode;
    json += ",\"infoLineMode\":";
    json += (int)_settings.infoLineMode;
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    json += ",\"anim\":";
    json += (int)_settings.transitionMode;
#endif
    json += ",\"datestyle\":";
    json += (int)_settings.dateStyle;
    json += ",\"timefmt\":";
    json += (int)_settings.timeFormat;
    json += ",\"bellmode\":";
    json += (int)_settings.bellMode;
    json += ",\"nightmode\":";
    json += (int)_settings.nightMode;
    json += ",\"brightness\":";
    json += (int)_settingsStore.loadBrightness(4);
    json += ",\"separatorBig\":";
    json += (int)_settings.bigSeparator;
    json += ",\"separatorDrift\":";
    json += (int)_settings.driftSeparator;
    json += ",\"dialMarks\":";
    json += (int)_settings.dialMarks;
    json += ",\"barSeconds\":";
    json += (int)_settings.barSeconds;
    json += ",\"binSeconds\":";
    json += (int)_settings.binSeconds;
    json += ",\"rndInterval\":";
    json += (int)_settings.rndInterval;
    json += ",\"hotspotActive\":";
    json += hotspotActive ? "true" : "false";
    json += ",\"hotspotRemaining\":";
    json += _hotspotRemainingCb ? _hotspotRemainingCb(_hotspotContext) : 0;
    json += ",\"hotspotTimeout\":";
    json += HOTSPOT_TIMEOUT_MINUTES;
    json += ",\"hotspotOnText\":\"";
    {
        String onText = hotspotOnText(hotspotActive);
        json += encodeJSON(onText);
#if DEBUG
        int16_t remaining = _hotspotRemainingCb ? _hotspotRemainingCb(_hotspotContext) : 0;
        LOGF("DBG status: active=%d remaining=%d text=\"%s\"\n",
              hotspotActive, remaining, onText.c_str());
#endif
    }
    json += "\"";

    json += ",\"alarm_mode\":";
    json += (int)_settings.alarm.mode;
    json += ",\"alarm_time\":\"";
    {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", _settings.alarm.hour, _settings.alarm.minute);
        json += encodeJSON(String(buf));
    }
    json += "\"";

    json += ",\"manualTime\":";
    json += _settings.manualTime.enabled ? "true" : "false";

    json += ",\"storedSsid\":\"";
    json += encodeJSON(_settings.network.ssid);
    json += "\",\"storedPassword\":\"";
    json += encodeJSON(_settings.network.password);
    json += "\"";

    json += ",\"pendingSsid\":\"";
    json += "\",\"pendingPassword\":\"";
    json += "\"";
    json += ",\"wifiState\":\"";
    if (currentReconnectFailed()) {
        json += "failed";
    } else if (currentReconnectActive()) {
        json += "connecting";
    } else if (currentConnected()) {
        json += "connected";
    } else {
        json += "idle";
    }
    json += "\"";

    if (_timerStatusCb) {
        json += ",\"timer\":";
        json += _timerStatusCb(_timerStatusContext);
    }

    json += "}";
    _webServer.send(200, "application/json", json);
}

void ConfigPortal::handleCaptivePortal() {
    if (_configModeStation) {
        _webServer.sendHeader("Location", "/", true);
        _webServer.send(302, "text/plain", "");
        return;
    }

    _webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    _webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _webServer.sendHeader("Pragma", "no-cache");
    _webServer.sendHeader("Expires", "-1");
    _webServer.send(302, "text/plain", "");
}

void ConfigPortal::handleNotFound() {
    if (_configModeStation) {
        _webServer.sendHeader("Location", "/", true);
        _webServer.send(302, "text/plain", "");
        return;
    }

    // Redirect all AP-mode requests to the root page (captive portal)
    if (_webServer.hostHeader() != WiFi.softAPIP().toString()) {
        handleCaptivePortal();
    } else {
        handleRoot();
    }
}

String ConfigPortal::hotspotOnText(bool hotspotActive) const {
#if HOTSPOT_TIMEOUT_MINUTES == 0
    return String("ON");
#else
    int16_t minutes = HOTSPOT_TIMEOUT_MINUTES;
    if (hotspotActive && _hotspotRemainingCb) {
        minutes = _hotspotRemainingCb(_hotspotContext);
        if (minutes <= 0) {
            minutes = 5;
        }
    }

    String label = "ON - ";
    label += minutes;
    label += " min";
#if DEBUG
    LOGF("DBG onText: active=%d cb=%s min=%d result=\"%s\"\n",
          hotspotActive, _hotspotRemainingCb ? "yes" : "no", minutes, label.c_str());
#endif
    return label;
#endif
}

String ConfigPortal::encodeHTML(const String& str) {
    String result = str;
    result.replace("&",  "&amp;");
    result.replace("<",  "&lt;");
    result.replace(">",  "&gt;");
    result.replace("\"", "&quot;");
    result.replace("'",  "&#39;");
    return result;
}

String ConfigPortal::encodeJSON(const String& str) {
    String result = str;

    // Order is critical
    result.replace("\\", "\\\\");
    result.replace("\"", "\\\"");
    result.replace("\b", "\\b");
    result.replace("\f", "\\f");
    result.replace("\n", "\\n");
    result.replace("\r", "\\r");
    result.replace("\t", "\\t");
    result.replace("/",  "\\/");   // optional but safe

    return result;
}


int ConfigPortal::getRSSIPercentage(int rssi) {
    // Convert RSSI to percentage (approximate)
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return 2 * (rssi + 100);
}

// =============================================================================
// FIRMWARE UPDATE METHODS
// =============================================================================

void ConfigPortal::startOTAUpdate() {
    _otaUpdate = true;
    if (_otaDisplayCb) _otaDisplayCb(_otaDisplayContext, true, 0, 0);
    LOGLN("OTA update started");
}

void ConfigPortal::setOtaDisplayCallback(OtaDisplayCallback cb, void* context) {
    _otaDisplayCb = cb;
    _otaDisplayContext = context;
}

void ConfigPortal::setSaveCallback(SaveCallback cb, void* context) {
    _saveCb = cb;
    _saveContext = context;
}

void ConfigPortal::setPreviewCallback(PreviewCallback cb, void* context) {
    _previewCb = cb;
    _previewContext = context;
}

void ConfigPortal::setTimerStatusCallback(TimerStatusCallback cb, void* context) {
    _timerStatusCb = cb;
    _timerStatusContext = context;
}

void ConfigPortal::setHotspotCallbacks(HotspotStatusCallback status,
                                       HotspotRemainingCallback remaining,
                                       HotspotToggleCallback toggle,
                                       void* context) {
    _hotspotStatusCb = status;
    _hotspotRemainingCb = remaining;
    _hotspotToggleCb = toggle;
    _hotspotContext = context;
}

void ConfigPortal::setScanPreflightCallback(ScanPreflightCallback cb, void* context) {
    _scanPreflightCb = cb;
    _scanPreflightContext = context;
}

bool ConfigPortal::isUpdating() {
    return _otaUpdate;
}

void ConfigPortal::handleUpdateUpload() {
    HTTPUpload& upload = _webServer.upload();

    if (upload.status == UPLOAD_FILE_START) {
        _otaUpdate = true;
        _otaExpectedSize = 0;

        if (_webServer.hasArg("size")) {
            _otaExpectedSize = (size_t)_webServer.arg("size").toInt();
        }

        if (_otaDisplayCb) _otaDisplayCb(_otaDisplayContext, true, 0, (unsigned int)_otaExpectedSize);
        LOGF("Firmware update: %s (%u B)\n", upload.filename.c_str(), (unsigned int)_otaExpectedSize);

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOGLN("Update begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Writing firmware to flash
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            LOGLN("Update write failed");
        }

        if (_otaDisplayCb) {
            size_t written = Update.progress();
            size_t total = _otaExpectedSize > 0 ? _otaExpectedSize : upload.totalSize;
            if (total > 0 && written > total) written = total;
            _otaDisplayCb(_otaDisplayContext, true, (unsigned int)written, (unsigned int)total);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        // Update finished
        if (_otaDisplayCb) {
            size_t total = _otaExpectedSize > 0 ? _otaExpectedSize : upload.totalSize;
            _otaDisplayCb(_otaDisplayContext, true, (unsigned int)total, (unsigned int)total);
        }
        if (Update.end(true)) {
            LOGF("Update OK: %u B\n", upload.totalSize);
        } else {
            LOGF("Update failed: %s\n", Update.errorString());
            _otaUpdate = false;
            if (_otaDisplayCb) _otaDisplayCb(_otaDisplayContext, false, 0, 0);
        }
        _otaExpectedSize = 0;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        _otaUpdate = false;
        _otaExpectedSize = 0;
        if (_otaDisplayCb) _otaDisplayCb(_otaDisplayContext, false, 0, 0);
        LOGLN("Update aborted");
    }
}
