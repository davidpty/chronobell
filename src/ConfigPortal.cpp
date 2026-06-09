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
    , _otaUpdate(false)
    , _otaExpectedSize(0)
    , _statusContext(nullptr)
    , _connectedCallback(nullptr)
    , _inConfigModeCallback(nullptr)
    , _ipAddressCallback(nullptr)
{
}

void ConfigPortal::setStatusProvider(void* context,
                                     BoolStatusCallback connected,
                                     BoolStatusCallback inConfigMode,
                                     StringStatusCallback ipAddress) {
    _statusContext = context;
    _connectedCallback = connected;
    _inConfigModeCallback = inConfigMode;
    _ipAddressCallback = ipAddress;
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

void ConfigPortal::beginAPMode() {
    _configModeStation = false;
    _settings = _settingsStore.load();
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.start(53, "*", WiFi.softAPIP());
    configureWebServerRoutes();
    _webServer.begin();
}

void ConfigPortal::beginStationMode() {
    _configModeStation = true;
    _settings = _settingsStore.load();
    configureWebServerRoutes();
    _webServer.begin();
}

void ConfigPortal::loop() {
    if (!_configModeStation) {
        _dnsServer.processNextRequest();
    }
    _webServer.handleClient();
}

void ConfigPortal::stop() {
    _dnsServer.stop();
    _webServer.stop();
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
    _webServer.on("/status", HTTP_GET, [this]() { handleStatus(); });
    _webServer.on("/update", HTTP_GET, [this]() { handleUpdateForm(); });
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
    _webServer.on("/update/status", HTTP_GET, [this]() { handleUpdateStatus(); });
    _webServer.onNotFound([this]() { handleNotFound(); });
}

void ConfigPortal::handleRoot() {
    const char* html = R"rawliteral(
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
        .row input { flex: 1; min-width: 0; }
        .network-name { font-weight: bold; color: #ff3333; font-size: 1em; }
        .network-signal { font-size: 0.9em; color: #880000; }
        .network-sec { font-size: 0.9em; color: #aa5500; }
        #status { text-align: center; padding: 0.46em; border-radius: 0.25em; margin-top: 0.42em;
                  font-size: 0.85em; display: none; border: 2px solid; text-shadow: none; }
        #status.success { background: rgba(0, 50, 0, 0.8); color: #00ff00; border-color: #00ff00;
                         text-shadow: 0 0 0.6em #00ff00; display: block; }
        #status.error { background: rgba(50, 0, 0, 0.8); color: #ff0000; border-color: #ff0000;
                        text-shadow: 0 0 0.6em #ff0000; display: block; }
        #status.loading { background: rgba(30, 20, 0, 0.8); color: #ffaa00; border-color: #ffaa00;
                          text-shadow: 0 0 0.6em #ffaa00; display: block; }
        .btn-update-link { display: block; width: 100%; padding: 0.38em; margin-top: 0.35em;
                           border: 1px solid #660000; border-radius: 0.25em; background: transparent;
                           color: #880000; font-size: 0.68em; font-weight: bold;
                           font-family: 'Courier New', monospace; text-transform: uppercase;
                           letter-spacing: 0.08em; text-align: center; text-decoration: none;
                           text-shadow: none; cursor: pointer; transition: all 0.2s; }
        .btn-update-link:hover { color: #cc0000; border-color: #cc0000;
                                 box-shadow: 0 0 0.5em rgba(255,0,0,0.4); }
        @media (min-width: 900px) and (min-height: 700px) {
            html { font-size: clamp(17px, min(2.4vw, 2.25vh), 25px); }
        }
        @media (min-width: 1400px) and (min-height: 850px) {
            html { font-size: clamp(18px, min(2.25vw, 2.15vh), 26px); }
        }
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
                <div class="setting-label">Timezone</div>
                <select id="timezone">
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
                <div class="setting-label">Clock</div>
                <select id="style">
                    <option value="0" selected>RND - Daily random style</option>
                    <option value="1">BIG - Large HH:MM, no seconds</option>
                    <option value="2">SEC - HH:MM with seconds below</option>
                    <option value="3">DECI - HH:MM with SS.d below</option>
                    <option value="4">DATE - HH:MM with date below</option>
                    <option value="5">WORD - Experimental mixed-size word clock</option>
                    <option value="6">ROMA - Roman numeral clock</option>
                    <option value="7">BIN - Binary clock</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Date</div>
                <select id="datestyle">
                    <option value="0" selected>DATE - Weekday and month/day</option>
                    <option value="1">YEAR - ISO week and day-of-year</option>
                    <option value="2">MOON - Lunar phase preview</option>
                    <option value="3">ZOD - Western zodiac</option>
                    <option value="4">CZOD - Chinese zodiac</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Hour</div>
                <select id="timefmt">
                    <option value="0" selected>24-HOUR - 00:00 to 23:59</option>
                    <option value="1">AM/PM - 12:00 to 11:59</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Bell</div>
                <select id="bellmode">
                    <option value="0" selected>NO BELL - Silent</option>
                    <option value="1">SINGLE HOURLY - One bell every full hour</option>
                    <option value="2">HOUR COUNT - Count full hours only</option>
                    <option value="3">HOUR COUNT + HALF - Count full hours, one bell at half hour</option>
                    <option value="4">PAIR - Hour count grouped in pairs</option>
                    <option value="5">SHIP'S BELL - Traditional 4-hour watch cycle</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Night</div>
                <select id="nightmode">
                    <option value="0" selected>OFF - No night mode</option>
                    <option value="1">LOW - Dim display 18:00-06:00</option>
                    <option value="2">LOW+MUTE - Dim display 18:00-06:00, bell muted 22:00-06:00</option>
                    <option value="3">DARK - Dim 18:00-22:00, off 22:00-06:00</option>
                    <option value="4">DARK+MUTE - Dim 18:00-22:00, off + bell muted 22:00-06:00</option>
                    <option value="5">MUTE - Bell muted 22:00-06:00 only</option>
                </select>
            </div>

            <div class="setting-row">
                <div class="setting-label">Time</div>
                <div class="toggle-group">
                    <button class="toggle-btn active" id="wifiAuto" onclick="setWifiMode('auto')">ATOMIC</button>
                    <button class="toggle-btn" id="wifiManual" onclick="setWifiMode('manual')">MANUAL</button>
                </div>
            </div>

            <div id="autoWifiPanel" class="panel-row">
                <div class="setup-spacer"></div>
                <div style="flex:1; min-width:0;">
                    <div class="row">
                        <input type="text" id="ssidInput" placeholder="Enter network name">
                        <button class="btn-scan" id="scanBtn" onclick="scanNetworks()">SCAN</button>
                    </div>
                    <div id="networkList" style="margin-top: 0.25em;"></div>
                    <input type="password" id="password" placeholder="Enter password" style="margin-top: 0.25em;">
                </div>
            </div>

            <div id="manualPanel" class="panel-row hidden">
                <div class="setup-spacer"></div>
                <div style="flex:1; min-width:0;">
                    <input type="date" id="manualDate" style="margin-top: 0;">
                    <input type="time" id="manualTime" style="margin-top: 0.2em;">
                </div>
            </div>

            <div id="status"></div>

            <button class="btn btn-primary" id="saveBtn" onclick="saveConfig()" style="margin-top: 0.45em;">Save Settings</button>
            <a href="/update" class="btn-update-link">Firmware Update</a>
        </div>
    </div>

    <script>
        let selectedSSID = '';
        let isScanning = false;
        let storedSsid = '';
        let storedPassword = '';
        let wifiMode = 'auto';

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
            // Keep SSID/password values when switching between ATOMIC and MANUAL.
            // Hidden fields are ignored when manual mode is saved, but restored when returning to ATOMIC.
        }

        async function loadSettings() {
            try {
                const response = await fetch('/status');
                const data = await response.json();

                const selects = [
                    ['timezone', data.timezone],
                    ['style', data.style],
                    ['datestyle', data.datestyle],
                    ['timefmt', data.timefmt],
                    ['bellmode', data.bellmode],
                    ['nightmode', data.nightmode]
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

                const now = new Date();
                document.getElementById('manualDate').value = now.getFullYear() + '-' +
                    String(now.getMonth() + 1).padStart(2, '0') + '-' +
                    String(now.getDate()).padStart(2, '0');
                document.getElementById('manualTime').value = String(now.getHours()).padStart(2, '0') + ':' +
                    String(now.getMinutes()).padStart(2, '0');

                storedSsid = data.storedSsid || '';
                storedPassword = data.storedPassword || '';
                if (storedSsid) {
                    selectedSSID = storedSsid;
                    document.getElementById('ssidInput').value = storedSsid;
                    document.getElementById('password').value = storedPassword;
                }
                setWifiMode('auto');
            } catch (e) {
                console.log('Error loading settings:', e);
                setWifiMode('auto');
            }
        }

        function showStatus(msg, type) {
            const el = document.getElementById('status');
            if (!el) return;
            el.textContent = msg;
            el.className = type || '';
            el.style.display = msg ? 'block' : 'none';
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
            showStatus('', '');
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
            document.getElementById('ssidInput').value = ssid;
            document.getElementById('password').disabled = !secured;
            if (!secured) document.getElementById('password').value = '';
            document.getElementById('password').focus();
        }

        function saveConfig() {
            let ssidToUse = wifiMode === 'auto' ? (document.getElementById('ssidInput').value || storedSsid) : '';
            let password = document.getElementById('password').value;
            const timezoneSelect = document.getElementById('timezone');
            const timezone = timezoneSelect.value;
            const timezoneName = timezoneSelect.options[timezoneSelect.selectedIndex].text;
            const style = document.getElementById('style').value;
            const dateStyle = document.getElementById('datestyle').value;
            const timefmt = document.getElementById('timefmt').value;
            const bellMode = document.getElementById('bellmode').value;
            const nightMode = document.getElementById('nightmode').value;
            const manualDate = document.getElementById('manualDate').value;
            const manualTime = document.getElementById('manualTime').value;

            if (!password && ssidToUse === storedSsid) {
                password = storedPassword;
            }

            showStatus('Saving config...', 'loading');
            document.getElementById('saveBtn').disabled = true;
            let saveUrl = '/save?ssid=' + encodeURIComponent(ssidToUse) +
                         '&password=' + encodeURIComponent(password) +
                         '&timezone=' + encodeURIComponent(timezone) +
                         '&tzname=' + encodeURIComponent(timezoneName) +
                         '&style=' + encodeURIComponent(style) +
                         '&datestyle=' + encodeURIComponent(dateStyle) +
                         '&timefmt=' + encodeURIComponent(timefmt) +
                         '&bellmode=' + encodeURIComponent(bellMode) +
                         '&nightmode=' + encodeURIComponent(nightMode);

            if (manualDate && manualTime) {
                saveUrl += '&manualDate=' + encodeURIComponent(manualDate) +
                           '&manualTime=' + encodeURIComponent(manualTime);
            }

            fetch(saveUrl)
                .then(r => r.text())
                .then(html => {
                    document.body.innerHTML = html;
                    setTimeout(function() {
                        const btn = document.querySelector('.btn');
                        if (btn) {
                            btn.addEventListener('click', function() {
                                window.close();
                                const msg = document.querySelector('.message');
                                if (msg) msg.innerHTML = 'You can now close this tab manually.<br>ChronoBell is restarting.';
                            });
                        }
                    }, 100);
                })
                .catch(() => {
                    document.body.innerHTML = '<div style="background:#000;color:#ff3333;font-family:Courier New,monospace;padding:0.45em;min-height:100vh;display:flex;align-items:center;justify-content:center;text-align:center;text-shadow:0 0 0.6em #ff0000;"><div style="width:min(96vw,42em);background:rgba(20,0,0,0.7);border:2px solid #330000;border-radius:0.25em;padding:1em;box-shadow:0 0 1.5em rgba(255,0,0,0.4),inset 0 0 1.2em rgba(50,0,0,0.5);"><h1 style="color:#ff0000;text-transform:uppercase;margin-bottom:0.5em;">Settings Saved</h1><p>ChronoBell is rebooting.</p><p style="margin-top:0.5em;color:#cc0000;">Please close this tab.</p></div></div>';
                });
        }

        window.onload = loadSettings;
    </script>
</body>
</html>
)rawliteral";
    _webServer.send(200, "text/html", html);
}

void ConfigPortal::handleScan() {
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
    String timezone = _webServer.arg("timezone");
    String tzname = _webServer.arg("tzname");
    String styleStr = _webServer.arg("style");
    String dateStyleStr = _webServer.arg("datestyle");
    String timefmtStr = _webServer.arg("timefmt");
    String bellModeStr = _webServer.arg("bellmode");
    String nightModeStr = _webServer.arg("nightmode");
    String manualDate = _webServer.arg("manualDate");
    String manualTime = _webServer.arg("manualTime");

    // SSID is optional - allows saving settings without WiFi configuration

    // Validate
    if (ssid.length() > 32) {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"SSID too long\"}");
        return;
    }
    if (password.length() > 64) {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Password too long\"}");
        return;
    }

    // Parse timezone offset in minutes so fractional offsets survive.
    int16_t tzOffsetMinutes = timezoneMinutesFromPortalValue(timezone);

    // Parse clock style (preserve existing if not specified)
    DisplayMode style = styleStr.length() > 0
        ? clampDisplayMode(styleStr.toInt())
        : _settings.displayMode;

    // Parse date style (preserve existing if not specified)
    DateStyle dateStyle = dateStyleStr.length() > 0
        ? clampDateStyle(dateStyleStr.toInt())
        : _settings.dateStyle;

    // Parse hour format (preserve existing if not specified)
    TimeFormat timeFormat = timefmtStr.length() > 0
        ? clampTimeFormat(timefmtStr.toInt())
        : _settings.timeFormat;

    // Parse bell mode (preserve existing if not specified)
    BellMode bellMode = bellModeStr.length() > 0
        ? clampBellMode(bellModeStr.toInt())
        : _settings.bellMode;

    // Parse night mode (preserve existing if not specified)
    NightMode nightMode = nightModeStr.length() > 0
        ? clampNightMode(nightModeStr.toInt())
        : _settings.nightMode;

    // Parse manual time if provided
    bool manualTimeEnabled = false;
    unsigned long manualEpochTime = 0;

    if (manualDate.length() > 0 && manualTime.length() > 0) {
        // Parse date: YYYY-MM-DD
        int year = manualDate.substring(0, 4).toInt();
        int month = manualDate.substring(5, 7).toInt();
        int day = manualDate.substring(8, 10).toInt();

        // Parse time: HH:MM
        int hour = manualTime.substring(0, 2).toInt();
        int minute = manualTime.substring(3, 5).toInt();

        // Convert to epoch time (seconds always set to 0 for manual time)
        struct tm tm;
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;  // Always set seconds to 0 for manual time
        tm.tm_isdst = -1;

        time_t epoch = mktime(&tm);
        if (epoch > 0) {
            manualEpochTime = (unsigned long)epoch;
            manualTimeEnabled = true;
            LOG("Manual time set: ");
            LOGLN(manualDate);
            LOGLN(manualTime);
        }
    }

    AppSettings settings = _settingsStore.load();
    settings.network.ssid = ssid;
    settings.network.password = password;
    settings.timezone.offsetMinutes = tzOffsetMinutes;
    settings.timezone.name = tzname;
    settings.displayMode = style;
    settings.dateStyle = dateStyle;
    settings.bellMode = bellMode;
    settings.timeFormat = timeFormat;
    settings.nightMode = nightMode;
    settings.manualTime.enabled = manualTimeEnabled;
    settings.manualTime.epoch = manualEpochTime;

    LOG("Manual time saved: enabled=");
    LOG(manualTimeEnabled);
    LOG(", epoch=");
    LOGLN(manualEpochTime);

    _settingsStore.save(settings);
    _settings = settings;

    LOG("Credentials saved for: ");
    LOGLN(ssid);
    LOG("Timezone saved: ");
    LOG(tzOffsetMinutes);
    LOG(" min (");
    LOG(tzname);
    LOGLN(")");
    LOG("Clock style: ");
    LOGLN(displayModeLabel(style));
    LOG("Date style: ");
    LOGLN(dateStyleLabel(dateStyle));
    LOG("Hour format: ");
    LOGLN(timeFormatLabel(timeFormat));
    LOG("Bell mode: ");
    LOGLN((int)bellMode);
    LOG("Night mode: ");
    LOGLN(nightModeLabel(nightMode));

    // Return HTML page with reboot message
    const char* rebootHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Settings Saved</title>
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
               text-shadow: 0 0 0.6em #ff0000, 0 0 1.2em #ff0000, 0 0 2em #ff0000;
               display: flex; align-items: center; justify-content: center; }
        .container { width: min(96vw, 42em); margin: 0 auto; }
        .card { background: rgba(20, 0, 0, 0.7); border: 2px solid #330000; border-radius: 0.25em;
                padding: 0.75em; margin-bottom: 0.35em; text-align: center;
                box-shadow: 0 0 1.5em rgba(255, 0, 0, 0.4), 0 0 3.5em rgba(255, 0, 0, 0.15), inset 0 0 1.2em rgba(50, 0, 0, 0.5); }
        h1 { text-align: center; margin-bottom: 0.45em; color: #ff0000; font-size: 2.1em; text-transform: uppercase;
             text-shadow: 0 0 0.4em #ff0000, 0 0 0.8em #ff0000, 0 0 1.6em #ff0000; }
        .message { font-size: 1em; color: #ff3333; line-height: 1.35; text-shadow: none; margin-bottom: 0.8em; }
        .status-icon { font-size: 2.2em; color: #ff0000; margin-bottom: 0.25em; }
        .btn { width: 100%; padding: 0.62em; border: 2px solid #ff0000; border-radius: 0.25em; cursor: pointer;
               font-size: 1em; font-weight: bold; margin-top: 0.5em; transition: all 0.2s;
               font-family: 'Courier New', Courier, monospace; text-transform: uppercase;
               background: transparent; color: #ff0000; }
        .btn:hover { background: #ff0000; color: #000000; box-shadow: 0 0 1.2em #ff0000; }
        @media (min-width: 900px) and (min-height: 700px) {
            html { font-size: clamp(17px, min(2.4vw, 2.25vh), 25px); }
        }
        @media (min-width: 1400px) and (min-height: 850px) {
            html { font-size: clamp(18px, min(2.25vw, 2.15vh), 26px); }
        }
        @media (max-width: 520px) {
            html { font-size: 15px; }
            body { padding: 0.6em; }
            .container { width: 100%; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <div class="status-icon">&#10004;</div>
            <h1>Settings Saved</h1>
            <p class="message">ChronoBell is rebooting.</p>
            <button class="btn" onclick="closeWindow()">Close Page</button>
        </div>
    </div>
    <script>
        function closeWindow() {
            window.close();
            const msg = document.querySelector('.message');
            if (msg) msg.innerHTML = 'You can now close this tab manually.<br>ChronoBell is restarting.';
        }

        setTimeout(function() {
            const msg = document.querySelector('.message');
            if (msg) msg.innerHTML = 'ChronoBell is rebooting.<br>Please close this tab.';
        }, 5000);
    </script>
</body>
</html>
    )rawliteral";

    _webServer.send(200, "text/html", rebootHtml);

    // Always reboot to apply all new settings
    delay(1000);
    LOGLN("Settings saved. Rebooting...");
    ESP.restart();
}

void ConfigPortal::handleStatus() {
    _settings = _settingsStore.load();

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

    // Include stored credentials if available
    if (_settings.network.ssid.length() > 0) {
        json += ",\"storedSsid\":\"";
        json += encodeJSON(_settings.network.ssid);
        json += "\",\"storedPassword\":\"";
        json += encodeJSON(_settings.network.password);
        json += "\"";
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
    if (_otaDisplayCb) _otaDisplayCb(true, 0, 0);
    LOGLN("OTA update started");
}

void ConfigPortal::setOtaDisplayCallback(std::function<void(bool, unsigned int, unsigned int)> cb) {
    _otaDisplayCb = cb;
}

bool ConfigPortal::isUpdating() {
    return _otaUpdate;
}

void ConfigPortal::handleUpdateForm() {
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Firmware Update - ChronoBell</title>
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
        .btn:hover:not(:disabled) { background: #ff0000; color: #000000; box-shadow: 0 0 1.2em #ff0000; }
        .btn-primary { background: transparent; color: #ff0000; }
        .btn-secondary { background: transparent; color: #cc0000; border-color: #cc0000; }
        .btn-back { background: transparent; color: #aa0000; border-color: #aa0000; }
        .btn-back:hover:not(:disabled) { background: #aa0000; color: #000000; box-shadow: 0 0 1.2em #aa0000; }
        .button-row { display: flex; gap: 0.55em; margin-top: 0.5em; }
        .button-row .btn { flex: 1; width: 50%; margin-top: 0; }
        .btn:disabled { opacity: 0.6; cursor: default; }
        input[type="file"] { width: 100%; padding: 0.46em; border: 2px solid #330000; border-radius: 0.25em;
               background: #0a0000; color: #ff3333; font-size: 1em; margin-top: 0.35em;
               font-family: 'Courier New', Courier, monospace; }
        input[type="file"]::file-selector-button {
               background: #330000; color: #ff3333; border: 1px solid #ff0000;
               padding: 0.42em 0.8em; border-radius: 0.25em; cursor: pointer; margin-right: 0.55em;
               font-family: 'Courier New', Courier, monospace; }
        input[type="file"]::file-selector-button:hover {
               background: #ff0000; color: #000000; }
        label { font-size: 0.95em; color: #ff0000; text-transform: uppercase; letter-spacing: 0.12em; text-shadow: none; }
        #status { text-align: center; padding: 0.62em; border-radius: 0.25em; margin-top: 0.5em;
                  font-size: 0.85em; display: none; border: 2px solid; text-shadow: none; }
        #status.success { background: rgba(0, 50, 0, 0.8); color: #00ff00; border-color: #00ff00;
                         text-shadow: 0 0 0.6em #00ff00; display: block; }
        #status.error { background: rgba(50, 0, 0, 0.8); color: #ff0000; border-color: #ff0000;
                       text-shadow: 0 0 0.6em #ff0000; display: block; }
        #status.loading { background: rgba(30, 20, 0, 0.8); color: #ffaa00; border-color: #ffaa00;
                         text-shadow: 0 0 0.6em #ffaa00; display: block; }
        .warning { background: rgba(50, 30, 0, 0.8); border: 2px solid #ffaa00; border-radius: 0.25em;
                  padding: 0.55em; margin-bottom: 0.35em; color: #ffaa00; font-size: 0.85em; text-shadow: 0 0 0.6em #ffaa00; }
        .progress-container { margin-top: 0.5em; display: none; }
        .progress-bar { width: 100%; height: 1.2em; background: #330000; border-radius: 0.25em; overflow: hidden; }
        .progress-fill { height: 100%; background: #ff0000; width: 0%; transition: width 0.3s;
                        box-shadow: 0 0 0.6em #ff0000; }
        .progress-text { text-align: center; margin-top: 0.35em; color: #ff3333; font-size: 0.85em; }
        .section-title { font-size: 0.85em; color: #ff0000; margin-bottom: 0.5em; text-transform: uppercase;
                        letter-spacing: 0.12em; text-shadow: 0 0 0.6em #ff0000; border-bottom: 1px solid #330000;
                        padding-bottom: 0.3em; }
        .loader { border: 0.15em solid #330000; border-top: 0.15em solid #ff0000; border-radius: 50%;
                  width: 1em; height: 1em; animation: spin 1s linear infinite; margin: 0 auto;
                  box-shadow: 0 0 0.6em #ff0000; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
        @media (min-width: 900px) and (min-height: 700px) {
            html { font-size: clamp(17px, min(2.4vw, 2.25vh), 25px); }
        }
        @media (min-width: 1400px) and (min-height: 850px) {
            html { font-size: clamp(18px, min(2.25vw, 2.15vh), 26px); }
        }
        @media (max-width: 520px) {
            html { font-size: 15px; }
            body { padding: 0.6em; }
            .container { width: 100%; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Firmware Update</h1>
        <div class="warning">
            <strong>WARNING:</strong> Updating firmware will overwrite the current firmware.
            Make sure you have a backup if needed. Do not power off during update.
        </div>

        <div class="card">
            <div class="section-title" id="updateTitle">Select Firmware File</div>
            <form id="updateForm" enctype="multipart/form-data">
                <input type="file" id="firmware" name="firmware" accept=".bin" required>
                <div class="button-row">
                    <button type="button" class="btn btn-back" id="cancelBtn" onclick="window.location.href='/'">
                        Cancel
                    </button>
                    <button type="submit" class="btn btn-primary" id="updateBtn">
                        Update
                    </button>
                </div>
            </form>
            <div id="status"></div>
        </div>

    </div>

    <script>
        const form = document.getElementById('updateForm');
        const updateBtn = document.getElementById('updateBtn');
        const cancelBtn = document.getElementById('cancelBtn');
        const status = document.getElementById('status');
        const updateTitle = document.getElementById('updateTitle');

        function showStatus(msg, type) {
            status.textContent = msg;
            status.className = type;
        }

        function showUpdateComplete(msg) {
            form.style.display = 'none';
            if (updateTitle) updateTitle.style.display = 'none';
            updateBtn.textContent = '100%';
            showStatus(msg || 'Update complete. Device is restarting.', 'success');
        }

        form.addEventListener('submit', async function(e) {
            e.preventDefault();

            const fileInput = document.getElementById('firmware');
            if (!fileInput.files.length) {
                showStatus('Please select a firmware file', 'error');
                return;
            }

            const file = fileInput.files[0];
            status.textContent = '';
            status.className = '';
            updateBtn.disabled = true;
            cancelBtn.disabled = true;
            updateBtn.textContent = '0%';

            const formData = new FormData();
            formData.append('firmware', file);

            try {
                const xhr = new XMLHttpRequest();

                xhr.upload.onprogress = function(e) {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        updateBtn.textContent = percent + '%';
                    }
                };

                xhr.onload = function() {
                    if (xhr.status === 200) {
                        showUpdateComplete('Update complete. Device is restarting.');

                        // Poll for restart
                        let attempts = 0;
                        const pollInterval = setInterval(() => {
                            attempts++;
                            fetch('/status').then(() => {
                                clearInterval(pollInterval);
                                showUpdateComplete('Restart detected. You can close this page.');
                            }).catch(() => {
                                if (attempts > 30) {
                                    clearInterval(pollInterval);
                                    showUpdateComplete('Update complete. Device is restarting.');
                                }
                            });
                        }, 1000);
                    } else {
                        showStatus('Update failed: ' + xhr.responseText, 'error');
                        updateBtn.disabled = false;
                        cancelBtn.disabled = false;
                        updateBtn.textContent = 'Update';
                    }
                };

                xhr.onerror = function() {
                    showStatus('Network error during upload', 'error');
                    updateBtn.disabled = false;
                    cancelBtn.disabled = false;
                    updateBtn.textContent = 'Update';
                };

                xhr.open('POST', '/update?size=' + encodeURIComponent(file.size));
                xhr.send(formData);
            } catch (e) {
                showStatus('Error: ' + e.message, 'error');
                updateBtn.disabled = false;
                cancelBtn.disabled = false;
                updateBtn.textContent = 'Update';
            }
        });
    </script>
</body>
</html>
    )rawliteral";

    _webServer.send(200, "text/html", html);
}

void ConfigPortal::handleUpdateUpload() {
    HTTPUpload& upload = _webServer.upload();

    if (upload.status == UPLOAD_FILE_START) {
        _otaUpdate = true;
        _otaExpectedSize = 0;

        if (_webServer.hasArg("size")) {
            _otaExpectedSize = (size_t)_webServer.arg("size").toInt();
        }

        if (_otaDisplayCb) _otaDisplayCb(true, 0, (unsigned int)_otaExpectedSize);
        LOGF("Starting firmware update: %s (%u bytes expected)\n", upload.filename.c_str(), (unsigned int)_otaExpectedSize);

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
            _otaDisplayCb(true, (unsigned int)written, (unsigned int)total);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        // Update finished
        if (_otaDisplayCb) {
            size_t total = _otaExpectedSize > 0 ? _otaExpectedSize : upload.totalSize;
            _otaDisplayCb(true, (unsigned int)total, (unsigned int)total);
        }
        if (Update.end(true)) {
            LOGF("Update successful! Size: %u bytes\n", upload.totalSize);
        } else {
            LOGF("Update failed: %s\n", Update.errorString());
            _otaUpdate = false;
            if (_otaDisplayCb) _otaDisplayCb(false, 0, 0);
        }
        _otaExpectedSize = 0;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        _otaUpdate = false;
        _otaExpectedSize = 0;
        if (_otaDisplayCb) _otaDisplayCb(false, 0, 0);
        LOGLN("Update aborted");
    }
}

void ConfigPortal::handleUpdateStatus() {
    String json = "{\"updating\":";
    json += _otaUpdate ? "true" : "false";
    json += ",\"progress\":";
    json += String(Update.isFinished() ? 100 : 0);
    json += ",\"error\":\"";
    json += encodeJSON(Update.errorString() ? Update.errorString() : "");
    json += "\"}";

    _webServer.send(200, "application/json", json);
}


