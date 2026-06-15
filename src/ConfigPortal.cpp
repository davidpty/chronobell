/**
 * ConfigPortal - Implementation
 */

#include "Config.h"
#include "ConfigPortal.h"
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
    String initialDateStyle = String((int)_settings.dateStyle);
    String initialTimeFormat = String((int)_settings.timeFormat);
    String initialNightMode = String((int)_settings.nightMode);
    String initialBrightness = String(_settingsStore.loadBrightness(4));
    String initialTimezone = portalTimezoneValue(_settings.timezone.offsetMinutes);
    String initialManualMode = _settings.manualTime.enabled ? "manual" : "auto";
    String initialHotspotRemaining = String(_hotspotRemainingCb ? _hotspotRemainingCb(_hotspotContext) : 0);

    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ChronoBell Setup</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        html { font-size: clamp(16px, min(2.4vw, 2.35vh), 24px); }
        body { font-family: 'Courier New', Courier, monospace;
               background: #000000; color: #ff3333; min-height: 100vh; padding: 0.45em; overflow-x: hidden;
               background-image:
                    linear-gradient(rgba(50, 0, 0, 0.9) 1px, transparent 1px),
                    linear-gradient(90deg, rgba(50, 0, 0, 0.9) 1px, transparent 1px),
                    radial-gradient(ellipse at center, rgba(10, 0, 0, 0.6) 0%, rgba(0, 0, 0, 0.4) 100%);
               background-size: 0.25em 0.25em, 0.25em 0.25em, 100% 100%;
               text-shadow: 0 0 0.6em #ff0000, 0 0 1.2em #ff0000, 0 0 2em #ff0000; }
        .container { width: min(96vw, 42em); margin: 0 auto; }
        h1 { text-align: center; margin-bottom: 0.35em; color: #ff0000; font-size: 2.6em;
             text-shadow: 0 0 0.4em #ff0000, 0 0 0.8em #ff0000, 0 0 1.6em #ff0000; }
        .card { background: rgba(20, 0, 0, 0.7); border: 2px solid #330000; border-radius: 0.25em;
                padding: 0.55em; margin-bottom: 0.35em; box-shadow: 0 0 1.5em rgba(255, 0, 0, 0.4), 0 0 3.5em rgba(255, 0, 0, 0.15), inset 0 0 1.2em rgba(50, 0, 0, 0.5); }
        .btn { width: 100%; padding: 0.62em; border: 2px solid #ff0000; border-radius: 0.25em; cursor: pointer;
               font-size: 1em; font-weight: bold; margin-top: 0.5em; transition: all 0.2s;
               font-family: 'Courier New', Courier, monospace; text-transform: uppercase; }
        .btn:disabled { opacity: 0.55; cursor: wait; }
        .btn-primary { background: transparent; color: #ff0000; }
        .btn-primary:hover:not(:disabled) { background: #ff0000; color: #000000; box-shadow: 0 0 1.2em #ff0000; }
        .btn-secondary { background: transparent; color: #cc0000; border-color: #cc0000; }
        .btn-secondary:hover { background: #cc0000; color: #000000; box-shadow: 0 0 1.2em #cc0000; }
        select, input:not([type="file"]) { width: 100%; padding: 0.46em; border: 2px solid #330000; border-radius: 0.25em;
                        background: #0a0000; color: #ff3333; font-size: 1em;
                        font-family: 'Courier New', Courier, monospace; }
        select:focus, input:focus { outline: none; border-color: #ff0000; box-shadow: 0 0 0.6em #ff0000; }
        input[type="file"] { padding: 0.46em; border: 2px solid #330000; border-radius: 0.25em;
               background: #0a0000; color: #ff3333; font-size: 1em;
               font-family: 'Courier New', Courier, monospace; }
        input[type="file"]::file-selector-button {
               background: #330000; color: #ff3333; border: 1px solid #ff0000;
               padding: 0.42em 0.8em; border-radius: 0.25em; cursor: pointer; margin-right: 0.55em;
               font-family: 'Courier New', Courier, monospace; }
        input[type="file"]::file-selector-button:hover {
               background: #ff0000; color: #000000; }
        label { font-size: 0.95em; color: #ff0000; text-transform: uppercase; letter-spacing: 0.12em; display: block; margin-top: 0.4em; text-shadow: none; }
        .setting-row { display: flex; align-items: center; gap: 0.6em; margin-top: 0.42em; }
        .setting-row .setting-label { font-size: 0.95em; color: #ff0000; text-transform: uppercase;
                                      letter-spacing: 0.12em; text-shadow: none; white-space: nowrap; min-width: 6.7em; }
        .setting-row select { flex: 1; min-width: 0; }
        .toggle-group { display: flex; gap: 0.4em; flex: 1; }
        .toggle-btn { flex: 1; padding: 0.46em 0.8em; border: 2px solid #ff0000; border-radius: 0.25em;
                      cursor: pointer; font-family: 'Courier New', monospace; font-size: 0.95em;
                      text-transform: uppercase; font-weight: bold; transition: all 0.2s;
                      background: transparent; color: #ff0000; }
        .toggle-btn.active { background: #ff0000; color: #000000; box-shadow: 0 0 0.6em #ff0000; }
        .panel-row { display: flex; gap: 0.6em; margin-top: 0.42em; }
        .panel-row.hidden, .hidden { display: none; }
        .setup-spacer { min-width: 6.7em; }
        .network-item { display: flex; justify-content: space-between; align-items: center;
                        padding: 0.65em; background: rgba(10, 0, 0, 0.8); border-radius: 0.25em; margin-bottom: 0.4em;
                        cursor: pointer; border: 1px solid #330000; transition: all 0.2s; }
        .network-item:hover { border-color: #ff0000; background: rgba(30, 0, 0, 0.8); }
        .network-item.selected { border-color: #ff0000; background: rgba(40, 0, 0, 0.8);
                                box-shadow: 0 0 1em rgba(255, 0, 0, 0.3); }
        .network-item.selected::after { content: '[X]'; color: #ff0000; font-size: 0.85em; text-shadow: 0 0 0.6em #ff0000; margin-left: 0.4em; }
        .btn-scan { padding: 0.46em 0.5em; border: 2px solid #cc0000; border-radius: 0.25em;
                     background: transparent; color: #cc0000; font-family: 'Courier New',monospace;
                     font-size: 0.95em; font-weight: bold; text-transform: uppercase; cursor: pointer;
                     white-space: nowrap; transition: all 0.2s; width: 5.2em; text-align: center; }
        .btn-scan:hover:not(:disabled) { background: #cc0000; color: #000000; box-shadow: 0 0 1em #cc0000; }
        .btn-scan:disabled { opacity: 0.9; cursor: default; }
        .row { display: flex; gap: 0.55em; margin-top: 0.15em; }
        .row input, .row .btn { flex: 1; min-width: 0; }
        .row .btn { margin-top: 0; }
        .network-name { font-weight: bold; color: #ff3333; font-size: 1em; }
        .network-signal { font-size: 0.9em; color: #880000; }
        .network-sec { font-size: 0.9em; color: #aa5500; }
        @media (min-width: 900px) and (min-height: 700px) {
            html { font-size: clamp(17px, min(2.4vw, 2.25vh), 25px); }
        }
        @media (min-width: 1400px) and (min-height: 850px) {
            html { font-size: clamp(18px, min(2.25vw, 2.15vh), 26px); }
        }
        input[type="range"] { -webkit-appearance: none; appearance: none; width: 100%; height: 0.5em; background: #330000; border-radius: 0.25em; outline: none; border: none; margin: 0; }
        input[type="range"]::-webkit-slider-thumb { -webkit-appearance: none; width: 1.2em; height: 1.2em; background: #ff0000; border-radius: 50%; cursor: pointer; box-shadow: 0 0 0.6em #ff0000; }
        input[type="range"]::-moz-range-thumb { width: 1.2em; height: 1.2em; background: #ff0000; border-radius: 50%; border: none; cursor: pointer; }
        .brightness-display { min-width: 2em; text-align: center; color: #ff3333; font-weight: bold; text-shadow: 0 0 0.4em #ff0000; }
        @media (max-width: 520px) {
            html { font-size: 15px; }
            body { padding: 0.6em; }
            .container { width: 100%; }
            .setting-row { gap: 0.4em; }
            .setting-row .setting-label, .setup-spacer { min-width: 5.5em; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ChronoBell</h1>

        <div class="card">
            <div class="setting-row">
                <div class="setting-label">Style</div>
                <select id="style" onchange="applySetting('style', this.value)">
                    <option value="0" selected>RND - Daily random style</option>
                    <option value="1">BIG - Large HH:MM, no seconds</option>
                    <option value="2">SEC - HH:MM with seconds below</option>
                    <option value="3">DECI - HH:MM with SS.d below</option>
                    <option value="4">DATE - HH:MM with date below</option>
                    <option value="5">WORD - Mixed-size word clock display</option>
                    <option value="6">ROMA - Roman numeral clock</option>
                    <option value="7">BIN - Binary clock</option>
                    <option value="8">DRIFT - Irregular BIG-style clock</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Date</div>
                <select id="datestyle" onchange="applySetting('datestyle', this.value)">
                    <option value="0" selected>DATE - Weekday and month/day</option>
                    <option value="1">YEAR - Year and day-of-year</option>
                    <option value="2">MOON - Lunar phase preview</option>
                    <option value="3">ZOD - Western zodiac</option>
                    <option value="4">CZOD - Chinese zodiac</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Format</div>
                <select id="timefmt" onchange="applySetting('timefmt', this.value)">
                    <option value="0" selected>24-HOUR - 00:00 to 23:59</option>
                    <option value="1">AM/PM - 12:00 to 11:59</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Night</div>
                <select id="nightmode" onchange="applySetting('nightmode', this.value)">
                    <option value="0" selected>OFF - No night mode</option>
                    <option value="1">LOW - Dim display 18:00-06:00</option>
                    <option value="2">LOW+MUTE - Dim display 18:00-06:00, bell muted 22:00-06:00</option>
                    <option value="3">DARK - Dim 18:00-22:00, off 22:00-06:00</option>
                    <option value="4">DARK+MUTE - Dim 18:00-22:00, off + bell muted 22:00-06:00</option>
                    <option value="5">MUTE - Bell muted 22:00-06:00 only</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Brightness</div>
                <div style="flex:1; display:flex; align-items:center; gap:0.5em;">
                    <input type="range" id="brightness" min="0" max="15" value="4" oninput="updateBrightness(this.value)">
                    <span class="brightness-display" id="brightnessVal">4</span>
                </div>
            </div>

            <div class="setting-row">
                <div class="setting-label">Bell</div>
                <select id="bellmode" onchange="applySetting('bellmode', this.value)">
                    <option value="0" selected>SILENT - No bell</option>
                    <option value="1">HOUR - One bell each hour</option>
                    <option value="2">STRIKE - Hour count strikes</option>
                    <option value="3">STRIKE+HALF - Hour + half-hour strikes</option>
                    <option value="4">PAIRS - Strikes in pairs</option>
                    <option value="5">TRIPLES - Strikes in triples</option>
                    <option value="6">SHIP'S BELL - Traditional watch cycle</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Timezone</div>
                <select id="timezone" onchange="applySetting('timezone', this.value)">
                    <option value="-12">UTC-12:00 (Baker Island)</option>
                    <option value="-11">UTC-11:00 (Samoa)</option>
                    <option value="-10">UTC-10:00 (Hawaii)</option>
                    <option value="-9">UTC-09:00 (Alaska)</option>
                    <option value="-8">UTC-08:00 (Pacific Time)</option>
                    <option value="-7">UTC-07:00 (Mountain Time)</option>
                    <option value="-6">UTC-06:00 (Central Time)</option>
                    <option value="-5" selected>UTC-05:00 (Eastern Time)</option>
                    <option value="-4">UTC-04:00 (Atlantic Time)</option>
                    <option value="-3">UTC-03:00 (Brazil)</option>
                    <option value="-2">UTC-02:00 (Mid-Atlantic)</option>
                    <option value="-1">UTC-01:00 (Azores)</option>
                    <option value="0">UTC+00:00 (London, Lisbon)</option>
                    <option value="1">UTC+01:00 (Paris, Berlin, Rome)</option>
                    <option value="2">UTC+02:00 (Cairo, Jerusalem)</option>
                    <option value="3">UTC+03:00 (Moscow, Istanbul)</option>
                    <option value="4">UTC+04:00 (Dubai)</option>
                    <option value="5">UTC+05:00 (Karachi)</option>
                    <option value="5.5">UTC+05:30 (Mumbai, Delhi)</option>
                    <option value="6">UTC+06:00 (Dhaka)</option>
                    <option value="7">UTC+07:00 (Bangkok)</option>
                    <option value="8">UTC+08:00 (Beijing, Singapore)</option>
                    <option value="9">UTC+09:00 (Tokyo, Seoul)</option>
                    <option value="9.5">UTC+09:30 (Adelaide)</option>
                    <option value="10">UTC+10:00 (Sydney)</option>
                    <option value="11">UTC+11:00 (Solomon Islands)</option>
                    <option value="12">UTC+12:00 (Auckland)</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Time</div>
                <div class="toggle-group">
                    <button class="toggle-btn active" id="wifiAuto" onclick="setWifiMode('auto'); applySetting('timeMode', 'auto')">AUTO</button>
                    <button class="toggle-btn" id="wifiManual" onclick="setWifiMode('manual'); applySetting('timeMode', 'manual')">MANUAL</button>
                </div>
            </div>

            <div id="autoWifiPanel" class="panel-row">
                <div class="setup-spacer"></div>
                <div style="flex:1; min-width:0;">
                    <div class="row">
                        <input type="text" id="ssidInput" value="__SSID_VALUE__" placeholder="Enter network name" oninput="onWifiInput()">
                        <button class="btn-scan" id="scanBtn" onclick="scanNetworks()">SCAN</button>
                    </div>
                    <div id="networkList" style="margin-top: 0.25em;"></div>
                    <input type="password" id="password" value="__PASSWORD_VALUE__" placeholder="Enter password" style="margin-top: 0.25em;" oninput="onWifiInput()">
                    <button class="btn btn-primary" id="connectBtn" onclick="connectWifi()" style="margin-top: 0.25em;">Connect</button>
                </div>
            </div>

            <div id="manualPanel" class="panel-row hidden">
                <div class="setup-spacer"></div>
                <div style="flex:1; min-width:0;">
                    <div class="row">
                        <input type="date" id="manualDate" onchange="applyManualTime()" style="margin-top: 0;">
                        <input type="time" id="manualTime" onchange="applyManualTime()" style="flex:0 0 auto; width:auto; margin-top:0;">
                        <input type="text" id="manualSec" maxlength="2" inputmode="numeric" pattern="[0-9]*" value="0" onchange="applyManualTime()" style="flex:0 0 auto; width:2.8em; margin-top:0; text-align:center;" placeholder="ss">
                    </div>
                </div>
            </div>

            <div class="setting-row">
                <div class="setting-label">Hotspot</div>
                <div class="toggle-group">
                    <button class="toggle-btn __HOTSPOT_OFF_CLASS__" id="hotspotOff" onclick="setHotspot(0)">OFF</button>
                    <button class="toggle-btn __HOTSPOT_ON_CLASS__" id="hotspotOn" onclick="setHotspot(1)">__HOTSPOT_ON_TEXT_HTML__</button>
                </div>
            </div>

            <div class="setting-row">
                <div class="setting-label">Firmware</div>
                <div style="flex:1; min-width:0;">
                    <div class="row">
                        <input type="file" id="firmware" accept=".bin">
                        <button class="btn btn-primary" id="updateBtn" onclick="uploadFirmware()" style="margin-top:0;">UPDATE</button>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        let activeSSID = "__ACTIVE_SSID__";
        let activePassword = "__ACTIVE_PASSWORD__";
        let pendingSSID = "__PENDING_SSID__";
        let pendingPassword = "__PENDING_PASSWORD__";
        let hotspotActive = __HOTSPOT_ACTIVE__;
        let hotspotRemaining = __HOTSPOT_REMAINING__;
        let hotspotTimeout = __HOTSPOT_TIMEOUT__;
        let hotspotOnText = "__HOTSPOT_ON_TEXT__";
        let __debug = __DEBUG__;
        let selectedSSID = '';
        let selectedSecured = false;
        let isScanning = false;
        let wifiMode = 'auto';
        let pendingPollTimer = null;
        let statusPollTimer = null;
        let wifiFieldsDirty = false;

        function htmlEscape(value) {
            return String(value).replace(/[&<>"']/g, function(ch) {
                return {'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'}[ch];
            });
        }

        function setWifiMode(mode) {
            wifiMode = mode;
            document.getElementById('wifiAuto').classList.toggle('active', mode === 'auto');
            document.getElementById('wifiManual').classList.toggle('active', mode === 'manual');
            document.getElementById('autoWifiPanel').classList.toggle('hidden', mode !== 'auto');
            document.getElementById('manualPanel').classList.toggle('hidden', mode !== 'manual');
        }

        function syncWifiFields() {
            if (wifiFieldsDirty) {
                return;
            }
            document.getElementById('ssidInput').value = activeSSID;
            document.getElementById('password').value = activePassword;
            selectedSSID = activeSSID;
        }

        function onWifiInput() {
            wifiFieldsDirty = true;
            selectedSSID = '';
            selectedSecured = false;
            document.querySelectorAll('.network-item').forEach(el => {
                el.classList.remove('selected');
            });
            document.getElementById('password').disabled = false;
        }

        function syncHotspotToggle() {
            const offBtn = document.getElementById('hotspotOff');
            const onBtn = document.getElementById('hotspotOn');
            offBtn.classList.toggle('active', !hotspotActive);
            onBtn.classList.toggle('active', hotspotActive);
            onBtn.textContent = hotspotOnText;
            if (__debug) console.log('DBG sync: active=' + hotspotActive + ' off=["' + offBtn.className + '"] on=["' + onBtn.className + '"] text="' + hotspotOnText + '"');
        }

        function updateHotspotFromStatus(data) {
            hotspotActive = data.hotspotActive === true;
            hotspotRemaining = Number(data.hotspotRemaining || 0);
            hotspotTimeout = Number(data.hotspotTimeout || 0);
            hotspotOnText = String(data.hotspotOnText || 'ON');
            if (__debug) console.log('DBG fromStatus: data.hotspotActive=' + data.hotspotActive + ' data.hotspotOnText="' + data.hotspotOnText + '"');
            syncHotspotToggle();
        }

        function applyInitialState() {
            const initial = {
                style: "__INITIAL_STYLE__",
                datestyle: "__INITIAL_DATESTYLE__",
                timefmt: "__INITIAL_TIMEFMT__",
                nightmode: "__INITIAL_NIGHTMODE__",
                bellmode: "__INITIAL_BELLMODE__",
                brightness: "__INITIAL_BRIGHTNESS__",
                timezone: "__INITIAL_TIMEZONE__",
                manualMode: "__INITIAL_MANUAL_MODE__"
            };

            const pairs = [
                ['style', initial.style],
                ['datestyle', initial.datestyle],
                ['timefmt', initial.timefmt],
                ['nightmode', initial.nightmode],
                ['bellmode', initial.bellmode],
                ['timezone', initial.timezone]
            ];
            pairs.forEach(function(item) {
                const el = document.getElementById(item[0]);
                if (!el || item[1] === undefined) return;
                const value = String(item[1]);
                for (let i = 0; i < el.options.length; i++) {
                    if (el.options[i].value === value) {
                        el.selectedIndex = i;
                        break;
                    }
                }
            });

            document.getElementById('brightness').value = initial.brightness;
            document.getElementById('brightnessVal').textContent = initial.brightness;
            setWifiMode(initial.manualMode === 'manual' ? 'manual' : 'auto');
            syncHotspotToggle();
        }

        function stopPendingPoll() {
            if (pendingPollTimer) {
                clearInterval(pendingPollTimer);
                pendingPollTimer = null;
            }
        }

        function startPendingPoll() {
            if (pendingPollTimer) return;
            pendingPollTimer = setInterval(function() {
                loadSettings(true);
            }, 2000);
        }

        function startStatusPoll() {
            if (statusPollTimer) return;
            statusPollTimer = setInterval(function() {
                loadSettings(false);
            }, 15000);
        }

        function updateConnectButton(state) {
            const btn = document.getElementById('connectBtn');
            if (!btn) return;
            if (state === 'connecting') {
                btn.textContent = 'Connecting...';
                btn.disabled = true;
            } else if (state === 'connected') {
                btn.textContent = 'Connect';
                btn.disabled = false;
            } else if (state === 'failed') {
                btn.textContent = 'FAILED';
                btn.disabled = false;
            } else {
                btn.textContent = 'Connect';
                btn.disabled = false;
            }
        }

        function applyManualTime() {
            const date = document.getElementById('manualDate').value;
            const time = document.getElementById('manualTime').value;
            const sec = document.getElementById('manualSec').value || '0';
            if (date && time) {
                fetch('/apply?field=manualtime&date=' + encodeURIComponent(date) + '&time=' + encodeURIComponent(time) + '&sec=' + encodeURIComponent(sec));
            }
        }

        async function loadSettings(fromPoll) {
            try {
                const response = await fetch('/status');
                const data = await response.json();

                const selects = [
                    ['style', data.style],
                    ['datestyle', data.datestyle],
                    ['timefmt', data.timefmt],
                    ['nightmode', data.nightmode],
                    ['bellmode', data.bellmode],
                    ['timezone', data.timezone]
                ];
                selects.forEach(function(item) {
                    const el = document.getElementById(item[0]);
                    if (!el || item[1] === undefined) return;
                    const value = String(item[1]);
                    for (let i = 0; i < el.options.length; i++) {
                        if (el.options[i].value === value) {
                            el.selectedIndex = i;
                            break;
                        }
                    }
                });

                const b = data.brightness;
                if (b !== undefined) {
                    document.getElementById('brightness').value = b;
                    document.getElementById('brightnessVal').textContent = b;
                }

                updateHotspotFromStatus(data);

                const now = new Date();
                document.getElementById('manualDate').value = now.getFullYear() + '-' +
                    String(now.getMonth() + 1).padStart(2, '0') + '-' +
                    String(now.getDate()).padStart(2, '0');
                document.getElementById('manualTime').value = String(now.getHours()).padStart(2, '0') + ':' +
                    String(now.getMinutes()).padStart(2, '0');
                document.getElementById('manualSec').value = String(now.getSeconds()).padStart(2, '0');

                activeSSID = data.storedSsid || '';
                activePassword = data.storedPassword || '';
                syncWifiFields();
                if (data.wifiState === 'connected') {
                    pendingSSID = '';
                    pendingPassword = '';
                    stopPendingPoll();
                    updateConnectButton('connected');
                } else if (data.wifiState === 'failed') {
                    pendingSSID = '';
                    pendingPassword = '';
                    stopPendingPoll();
                    updateConnectButton('failed');
                } else if (data.wifiState === 'connecting') {
                    startPendingPoll();
                    updateConnectButton('connecting');
                } else {
                    stopPendingPoll();
                    updateConnectButton(data.wifiState || 'idle');
                }
                setWifiMode(data.manualTime ? 'manual' : 'auto');
            } catch (e) {
                console.log('Error loading settings:', e);
                setWifiMode('auto');
            }
        }

        function setScanButton(text, disabled) {
            const btn = document.getElementById('scanBtn');
            if (!btn) return;
            btn.textContent = text;
            btn.disabled = !!disabled;
        }

        function resetScanButtonSoon() {
            setTimeout(function() {
                if (!isScanning) setScanButton('SCAN', false);
            }, 1800);
        }

        function scanNetworks() {
            if (isScanning) return;
            isScanning = true;
            setScanButton('WAIT', true);
            document.getElementById('ssidInput').disabled = true;
            document.getElementById('networkList').innerHTML = '';

            fetch('/scan').then(r => r.json()).then(data => {
                isScanning = false;
                document.getElementById('ssidInput').disabled = false;
                const networks = data.networks || [];
                setScanButton(networks.length + ' NETS', false);
                resetScanButtonSoon();
                if (networks.length === 0) {
                    document.getElementById('networkList').innerHTML = '';
                    window.scannedNetworks = [];
                    return;
                }
                const list = networks.map(function(n, idx) {
                    const ssid = String(n.ssid || '');
                    const signal = String(n.signal || '');
                    return '<div class="network-item" onclick="selectNetworkByIndex(' + idx + ')" data-ssid="' + htmlEscape(ssid) + '">' +
                        '<div><div class="network-name">' + htmlEscape(ssid) + '</div>' +
                        '<div class="network-signal">' + htmlEscape(signal) + '</div></div>' +
                        (n.secured ? '<div class="network-sec">&#128274;</div>' : '<div class="network-sec">&#128275;</div>') +
                        '</div>';
                }).join('');
                document.getElementById('networkList').innerHTML = list;
                window.scannedNetworks = networks;
            }).catch(() => {
                isScanning = false;
                document.getElementById('ssidInput').disabled = false;
                document.getElementById('networkList').innerHTML = '';
                window.scannedNetworks = [];
                setScanButton('ERROR', false);
                resetScanButtonSoon();
            });
        }

        function selectNetworkByIndex(index) {
            const n = (window.scannedNetworks || [])[index];
            if (!n) return;
            selectNetwork(n.ssid, !!n.secured);
        }

        function selectNetwork(ssid, secured) {
            document.querySelectorAll('.network-item').forEach(el => {
                el.classList.toggle('selected', el.getAttribute('data-ssid') === ssid);
            });
            selectedSSID = ssid;
            selectedSecured = !!secured;
            wifiFieldsDirty = true;
            document.getElementById('ssidInput').value = ssid;
            document.getElementById('password').disabled = !secured;
            document.getElementById('password').value = '';
            document.getElementById('password').focus();
        }

        function applySetting(field, value) {
            let url = '/apply?field=' + encodeURIComponent(field) +
                      '&value=' + encodeURIComponent(value);
            if (field === 'timezone') {
                const tz = document.getElementById('timezone');
                url += '&tzname=' + encodeURIComponent(tz.options[tz.selectedIndex].text);
            }
            return fetch(url);
        }

        function setHotspot(value) {
            hotspotActive = value === 1;
            if (__debug) console.log('DBG setHotspot: value=' + value + ' hotspotActive=' + hotspotActive);
            syncHotspotToggle();
            applySetting('hotspot', value)
                .then(function() {
                    loadSettings(false);
                })
                .catch(function() {
                    loadSettings(false);
                });
        }

        function updateBrightness(value) {
            document.getElementById('brightnessVal').textContent = value;
            applySetting('brightness', value);
        }

        function connectWifi() {
            const btn = document.getElementById('connectBtn');
            const ssid = document.getElementById('ssidInput').value || '';
            let password = document.getElementById('password').value;
            if (selectedSecured && !password) {
                btn.textContent = 'PASSWORD';
                setTimeout(function() { btn.textContent = 'Connect'; }, 1500);
                return;
            }
            wifiFieldsDirty = false;
            btn.disabled = true;
            btn.textContent = 'Connecting...';
            fetch('/save?ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password))
                .then(r => r.json())
                .then(data => {
                    btn.disabled = false;
                    if (data.success) {
                        if (data.pending) {
                            pendingSSID = ssid;
                            pendingPassword = password;
                            syncWifiFields();
                            selectedSSID = '';
                            selectedSecured = false;
                            updateConnectButton('connecting');
                            startPendingPoll();
                        } else {
                            activeSSID = ssid;
                            activePassword = password;
                            pendingSSID = '';
                            pendingPassword = '';
                            syncWifiFields();
                            selectedSSID = '';
                            selectedSecured = false;
                            stopPendingPoll();
                            btn.textContent = 'Connect';
                        }
                    } else {
                        stopPendingPoll();
                        wifiFieldsDirty = false;
                        selectedSSID = '';
                        selectedSecured = false;
                        updateConnectButton('failed');
                    }
                })
                .catch(function() {
                    stopPendingPoll();
                    wifiFieldsDirty = false;
                    selectedSSID = '';
                    selectedSecured = false;
                    updateConnectButton('failed');
                });
        }

        function uploadFirmware() {
            const fileInput = document.getElementById('firmware');
            if (!fileInput.files.length) return;
            const file = fileInput.files[0];
            const btn = document.getElementById('updateBtn');
            btn.disabled = true;
            btn.textContent = '0%';
            const formData = new FormData();
            formData.append('firmware', file);
            const xhr = new XMLHttpRequest();
            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    btn.textContent = Math.round((e.loaded / e.total) * 100) + '%';
                }
            };
            xhr.onload = function() {
                if (xhr.status === 200) {
                    btn.textContent = '100%';
                    setTimeout(function() { btn.textContent = 'Restarting...'; }, 500);
                    setTimeout(function() { btn.textContent = 'UPDATE'; btn.disabled = false; }, 12000);
                } else {
                    btn.textContent = 'UPDATE';
                    btn.disabled = false;
                }
            };
            xhr.onerror = function() {
                btn.textContent = 'UPDATE';
                btn.disabled = false;
            };
            xhr.open('POST', '/update?size=' + encodeURIComponent(file.size));
            xhr.send(formData);
        }

    syncHotspotToggle();
    syncWifiFields();
    applyInitialState();
    window.onload = function() {
        loadSettings(false);
        startStatusPoll();
    };
    </script>
</body>
</html>
)rawliteral";

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
    html.replace("__INITIAL_STYLE__", initialStyle);
    html.replace("__INITIAL_DATESTYLE__", initialDateStyle);
    html.replace("__INITIAL_TIMEFMT__", initialTimeFormat);
    html.replace("__INITIAL_NIGHTMODE__", initialNightMode);
    html.replace("__INITIAL_BELLMODE__", initialBellMode);
    html.replace("__INITIAL_BRIGHTNESS__", initialBrightness);
    html.replace("__INITIAL_TIMEZONE__", initialTimezone);
    html.replace("__INITIAL_MANUAL_MODE__", initialManualMode);

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

void ConfigPortal::handleApply() {
    String field = _webServer.arg("field");
    String value = _webServer.arg("value");

    AppSettings settings = _settingsStore.load();
    bool tzChanged = false;
    bool manualTimeChanged = false;

    if (field == "style") {
        settings.displayMode = clampDisplayMode(value.toInt());
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

    _settingsStore.save(settings);
    _settings = settings;

    if (_saveCb) {
        (void)_saveCb(_saveContext, false, tzChanged, manualTimeChanged, "", "");
    }

    if (_previewCb && (field == "style" || field == "datestyle" || field == "timefmt" || field == "timezone" || field == "timeMode" || field == "manualtime" || field == "bellmode" || field == "brightness")) {
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
