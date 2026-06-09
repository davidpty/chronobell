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
        body { font-family: 'Courier New', Courier, monospace;
               background: #000000; color: #ff3333; min-height: 100vh; padding: 20px;
               background-image:
                   linear-gradient(rgba(20, 0, 0, 0.8) 1px, transparent 1px),
                   linear-gradient(90deg, rgba(20, 0, 0, 0.8) 1px, transparent 1px),
                   radial-gradient(ellipse at center, rgba(10, 0, 0, 0.95) 0%, #000000 100%);
               background-size: 4px 4px, 4px 4px, 100% 100%;
               text-shadow: 0 0 10px #ff0000, 0 0 20px #ff0000, 0 0 30px #ff0000; }
        .container { max-width: 400px; margin: 0 auto; }
        h1 { text-align: center; margin-bottom: 24px; color: #ff0000; font-size: 28px;
             text-shadow: 0 0 10px #ff0000, 0 0 20px #ff0000, 0 0 40px #ff0000; }
        .card { background: rgba(20, 0, 0, 0.7); border: 2px solid #330000; border-radius: 4px;
                padding: 16px; margin-bottom: 16px; box-shadow: inset 0 0 20px rgba(50, 0, 0, 0.5); }
        .btn { width: 100%; padding: 12px; border: 2px solid #ff0000; border-radius: 4px; cursor: pointer;
               font-size: 14px; font-weight: bold; margin-top: 12px; transition: all 0.2s;
               font-family: 'Courier New', Courier, monospace; text-transform: uppercase; }
        .btn:hover { background: #ff0000; color: #000000; box-shadow: 0 0 20px #ff0000; }
        .btn-primary { background: transparent; color: #ff0000; }
        .btn-secondary { background: transparent; color: #cc0000; border-color: #cc0000; }
        select, input:not([type="file"]) { width: 100%; padding: 10px; border: 2px solid #330000; border-radius: 4px;
                background: #0a0000; color: #ff3333; font-size: 14px; margin-top: 8px;
                font-family: 'Courier New', Courier, monospace; }
        input[type="file"] {
            display: block;
            width: 100%;
            padding: 12px;
            border: 2px solid #ff0000;
            border-radius: 4px;
            background: #0a0000;
            color: #ff3333;
            font-size: 16px;
            margin-top: 8px;
            cursor: pointer;
            -webkit-tap-highlight-color: transparent;
        }
        input[type="file"]::-webkit-file-upload-button {
            background: #ff0000;
            color: #000000;
            padding: 8px 16px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-weight: bold;
            margin-right: 12px;
        }
        select:focus, input:focus { outline: none; border-color: #ff0000; box-shadow: 0 0 10px #ff0000; }
        label { font-size: 12px; color: #aa0000; text-transform: uppercase; letter-spacing: 2px; }
        #status { text-align: center; padding: 12px; border-radius: 4px; margin-bottom: 16px;
                  font-size: 14px; display: none; border: 2px solid; }
        #status.success { background: rgba(0, 50, 0, 0.8); color: #00ff00; border-color: #00ff00;
                         text-shadow: 0 0 10px #00ff00; display: block; }
        #status.error { background: rgba(50, 0, 0, 0.8); color: #ff0000; border-color: #ff0000;
                        text-shadow: 0 0 10px #ff0000; display: block; }
        #status.loading { background: rgba(30, 20, 0, 0.8); color: #ffaa00; border-color: #ffaa00;
                          text-shadow: 0 0 10px #ffaa00; display: block; }
        .network-item { display: flex; justify-content: space-between; align-items: center;
                        padding: 10px; background: rgba(10, 0, 0, 0.8); border-radius: 4px; margin-bottom: 8px;
                        cursor: pointer; border: 1px solid #330000; transition: all 0.2s; }
        .network-item:hover { border-color: #ff0000; background: rgba(30, 0, 0, 0.8); }
        .network-item.selected { border-color: #ff0000; background: rgba(40, 0, 0, 0.8);
                                box-shadow: 0 0 15px rgba(255, 0, 0, 0.3); }
        .network-item.selected::after { content: '[X]'; color: #ff0000; font-size: 14px;
                                         text-shadow: 0 0 10px #ff0000; }
        .network-name { font-weight: bold; color: #ff3333; }
        .network-signal { font-size: 11px; color: #880000; }
        .network-sec { font-size: 11px; color: #aa5500; }
        .hidden { display: none; }
        .loader { border: 3px solid #330000; border-top: 3px solid #ff0000; border-radius: 50%;
                  width: 20px; height: 20px; animation: spin 1s linear infinite; margin: 0 auto;
                  box-shadow: 0 0 10px #ff0000; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
        .section-title { font-size: 12px; color: #ff0000; margin-bottom: 12px; text-transform: uppercase;
                         letter-spacing: 2px; text-shadow: 0 0 10px #ff0000; border-bottom: 1px solid #330000;
                         padding-bottom: 8px; }
        h2 { font-size: 16px; color: #cc0000; margin-bottom: 16px; text-transform: uppercase; letter-spacing: 2px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ChronoBell</h1>

        <div class="card">
            <div class="section-title">Timezone</div>
            <label for="timezone">Select your timezone:</label>
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

        <div class="card">
            <div class="section-title">Clock Style</div>
            <label for="style">Choose your clock style:</label>
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

        <div class="card">
            <div class="section-title">Date Style</div>
            <label for="datestyle">Choose your permanent date style:</label>
            <select id="datestyle">
                <option value="0" selected>DATE - weekday and month/day</option>
                <option value="1">YEAR - ISO week and day-of-year</option>
                <option value="2">MOON - lunar phase preview</option>
                <option value="3">ZOD - Western zodiac</option>
                <option value="4">CZOD - Chinese zodiac</option>
            </select>
        </div>

        <div class="card">
            <div class="section-title">Hour Format</div>
            <label for="timefmt">Choose your hour format:</label>
            <select id="timefmt">
                <option value="0" selected>24-HOUR - 00:00 to 23:59</option>
                <option value="1">AM/PM - 12:00 to 11:59 (no indicator)</option>
            </select>
        </div>

        <div class="card">
            <div class="section-title">Bell Mode</div>
            <label for="bellmode">Choose your bell schedule:</label>
            <select id="bellmode">
                <option value="0" selected>NO BELL - Silent</option>
                <option value="1">SINGLE HOURLY - One bell every full hour</option>
                <option value="2">HOUR COUNT - Count full hours only</option>
                <option value="3">HOUR COUNT + HALF - Count full hours, one bell at half hour</option>
                <option value="4">PAIR - Hour count grouped in pairs</option>
                <option value="5">SHIP'S BELL - Traditional 4-hour watch cycle</option>
            </select>
        </div>

        <div class="card">
            <div class="section-title">Night Mode</div>
            <label for="nightmode">Choose your night-mode preset:</label>
            <select id="nightmode">
                <option value="0" selected>OFF - no night mode</option>
                <option value="1">LOW - low display 18:00-06:00</option>
                <option value="2">LOW+MUTE - low display 18:00-06:00, bell muted 22:00-06:00</option>
                <option value="3">DARK - low 18:00-22:00, off 22:00-06:00</option>
                <option value="4">DARK+MUTE - low 18:00-22:00, off + bell muted 22:00-06:00</option>
                <option value="5">MUTE - bell muted 22:00-06:00 only</option>
            </select>
        </div>

        <div class="card">
            <div class="section-title">Manual Time Setting</div>
            <p style="font-size: 12px; color: #666; margin-bottom: 12px;">
                Only needed if atomic clock is not available.
            </p>
            <label for="manualDate">Date:</label>
            <input type="date" id="manualDate">
            <label for="manualTime" style="margin-top: 12px;">Time:</label>
            <input type="time" id="manualTime">

        </div>

        <div class="card">
            <div class="section-title">Automatic Time Setting</div>
            <p style="font-size: 12px; color: #666; margin-bottom: 12px;">
                Time syncs with the atomic clock online when connected.
            </p>
            <button class="btn btn-secondary" id="scanBtn" onclick="scanNetworks()">
                Scan Networks
            </button>
            <div id="networkList" style="margin-top: 16px;"></div>

            <label for="password" style="margin-top: 16px; display: block;">Password</label>
            <input type="password" id="password" placeholder="Enter password" disabled>
            <p style="font-size: 12px; color: #666; margin-top: 8px;">
                Leave empty for open networks
            </p>
        </div>

        <button class="btn btn-primary" id="saveBtn" onclick="saveConfig()">
            Save Settings
        </button>

        <button class="btn btn-secondary" onclick="window.location.href='/update'" style="margin-top: 16px;">
            Firmware Update >>
        </button>
    </div>
    <script>
        let selectedSSID = '';
        let isScanning = false;
        let storedSsid = '';
        let storedPassword = '';

        // Load current settings on page load
        async function loadSettings() {
            console.log('[DEBUG] loadSettings() called');
            try {
                const response = await fetch('/status');
                const responseText = await response.text();
                console.log('[DEBUG] Raw /status response:', responseText);
                const data = JSON.parse(responseText);
                console.log('[DEBUG] /status parsed successfully');

                // Set timezone dropdown
                const timezoneSelect = document.getElementById('timezone');
                for (let i = 0; i < timezoneSelect.options.length; i++) {
                    if (timezoneSelect.options[i].value === String(data.timezone)) {
                        timezoneSelect.selectedIndex = i;
                        break;
                    }
                }

                // Set clock style dropdown
                const styleSelect = document.getElementById('style');
                for (let i = 0; i < styleSelect.options.length; i++) {
                    if (String(data.style) === styleSelect.options[i].value) {
                        styleSelect.selectedIndex = i;
                        break;
                    }
                }

                // Set date style dropdown
                const dateStyleSelect = document.getElementById('datestyle');
                for (let i = 0; i < dateStyleSelect.options.length; i++) {
                    if (String(data.datestyle) === dateStyleSelect.options[i].value) {
                        dateStyleSelect.selectedIndex = i;
                        break;
                    }
                }

                // Set hour format dropdown
                const timefmtSelect = document.getElementById('timefmt');
                for (let i = 0; i < timefmtSelect.options.length; i++) {
                    if (String(data.timefmt) === timefmtSelect.options[i].value) {
                        timefmtSelect.selectedIndex = i;
                        break;
                    }
                }

                // Set bell mode dropdown
                const bellModeSelect = document.getElementById('bellmode');
                for (let i = 0; i < bellModeSelect.options.length; i++) {
                    if (String(data.bellmode) === bellModeSelect.options[i].value) {
                        bellModeSelect.selectedIndex = i;
                        break;
                    }
                }

                // Set night mode dropdown
                const nightModeSelect = document.getElementById('nightmode');
                if (nightModeSelect && data.nightmode !== undefined) {
                    for (let i = 0; i < nightModeSelect.options.length; i++) {
                        if (String(data.nightmode) === nightModeSelect.options[i].value) {
                            nightModeSelect.selectedIndex = i;
                            break;
                        }
                    }
                }
                console.log('[DEBUG] Loaded settings: timezone=' + data.timezone + ' (' + data.tzname + '), style=' + data.style + ', datestyle=' + data.datestyle + ', timefmt=' + data.timefmt + ', bellmode=' + data.bellmode + ', nightmode=' + data.nightmode);

                // Pre-fill manual time with browser's current time
                const now = new Date();
                const dateInput = document.getElementById('manualDate');
                const timeInput = document.getElementById('manualTime');

                // Format date as YYYY-MM-DD
                const year = now.getFullYear();
                const month = String(now.getMonth() + 1).padStart(2, '0');
                const day = String(now.getDate()).padStart(2, '0');
                dateInput.value = year + '-' + month + '-' + day;

                // Format time as HH:MM
                const hours = String(now.getHours()).padStart(2, '0');
                const minutes = String(now.getMinutes()).padStart(2, '0');
                timeInput.value = hours + ':' + minutes;

                console.log('[DEBUG] Browser time pre-filled:', dateInput.value, timeInput.value);

                // Store credentials
                storedSsid = data.storedSsid || '';
                storedPassword = data.storedPassword || '';
                console.log('[DEBUG] storedSsid:', storedSsid, 'storedPassword:', storedPassword ? '***' : 'empty');

                // If we have stored credentials, pre-select and enable save
                if (storedSsid) {
                    console.log('[DEBUG] Stored network found, setting selectedSSID');
                    selectedSSID = storedSsid;
                    document.getElementById('password').disabled = false;
                    document.getElementById('password').value = storedPassword;
                    checkSaveReady();

                    // Show that stored network is selected
                    showStatus('Stored network loaded: ' + storedSsid + ' <<', 'success');

                    // Add stored network to list for visibility
                    const list = document.getElementById('networkList');
                    list.innerHTML = '<div class="network-item selected" onclick="clearSelection()">' +
                        '<div><div class="network-name">' + storedSsid + '</div>' +
                        '<div class="network-signal">Stored Network</div></div>' +
                        '<div class="network-sec">&#128274;</div></div>' +
                        '<p style="font-size: 11px; color: #666; margin-top: 8px;">' +
                        'Leave password blank to keep current password</p>';
                } else {
                    // No stored network, check if a network was selected during scan
                    console.log('[DEBUG] No stored network, checking selection');
                    checkSaveReady();
                }
            } catch (e) {
                console.log('Error loading settings:', e);
            }
        }

        function clearSelection() {
            selectedSSID = '';
            document.querySelectorAll('.network-item').forEach(el => el.classList.remove('selected'));
            document.getElementById('password').value = '';
            checkSaveReady();
        }

        function showStatus(msg, type) {
            const el = document.getElementById('status');
            if (!el) return; // Status element not present
            el.textContent = msg;
            el.className = type;
            if (!msg) el.style.display = 'none';
        }

        function scanNetworks() {
            if (isScanning) return;
            isScanning = true;
            showStatus('Scanning >> ', 'loading');
            document.getElementById('scanBtn').innerHTML = '<div class="loader"></div>';
            fetch('/scan').then(r => r.json()).then(data => {
                isScanning = false;
                document.getElementById('scanBtn').textContent = '<< SCAN NETWORKS >>';
                if (data.networks.length === 0) {
                    showStatus('No networks found...', 'error');
                    return;
                }
                const list = data.networks.map(n =>
                    '<div class="network-item" onclick="selectNetwork(\'' + n.ssid.replace(/'/g, "\\'") + '\', ' + n.secured + ')" id="net-' + n.ssid.replace(/'/g, "\\'") + '">' +
                    '<div><div class="network-name">' + n.ssid + '</div>' +
                    '<div class="network-signal">' + n.signal + '%</div></div>' +
                    (n.secured ? '<div class="network-sec">&#128274;</div>' : '<div class="network-sec">&#128275;</div>') +
                    '</div>'
                ).join('');
                document.getElementById('networkList').innerHTML = list;
                showStatus(data.networks.length + ' networks found <<', 'success');
            }).catch(() => {
                isScanning = false;
                document.getElementById('scanBtn').textContent = '<< SCAN NETWORKS >>';
                showStatus('Scan failed <<', 'error');
            });
        }

        function selectNetwork(ssid, secured) {
            document.querySelectorAll('.network-item').forEach(el => el.classList.remove('selected'));
            const el = document.getElementById('net-' + ssid);
            if (el) el.classList.add('selected');
            selectedSSID = ssid;
            document.getElementById('password').disabled = !secured;
            if (!secured) document.getElementById('password').value = '';
            checkSaveReady();
        }

        function checkSaveReady() {
            const btn = document.getElementById('saveBtn');
            // Always enable save button - users can save settings without WiFi
            console.log('[DEBUG] checkSaveReady(): Always enabling save button');
            btn.disabled = false;
            console.log('[DEBUG] Save button disabled =', btn.disabled);
        }

        function saveConfig() {
            let ssidToUse = selectedSSID || storedSsid;
            let password = document.getElementById('password').value;
            const timezoneSelect = document.getElementById('timezone');
            const timezone = timezoneSelect.value;
            const timezoneName = timezoneSelect.options[timezoneSelect.selectedIndex].text.split(' ')[1] + ' ' +
                                 timezoneSelect.options[timezoneSelect.selectedIndex].text.split(' ')[2] || '';
            const styleSelect = document.getElementById('style');
            const style = styleSelect.value;
            const dateStyleSelect = document.getElementById('datestyle');
            const dateStyle = dateStyleSelect.value;
            const timefmtSelect = document.getElementById('timefmt');
            const timefmt = timefmtSelect.value;
            const bellModeSelect = document.getElementById('bellmode');
            const bellMode = bellModeSelect.value;
            const nightModeSelect = document.getElementById('nightmode');
            const nightMode = nightModeSelect ? nightModeSelect.value : '0';

            // Get manual time if set
            const manualDate = document.getElementById('manualDate').value;
            const manualTime = document.getElementById('manualTime').value;

            // If no password entered and we have a stored SSID, use stored password
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

            // Add manual time parameters if set
            if (manualDate && manualTime) {
                saveUrl += '&manualDate=' + encodeURIComponent(manualDate) +
                           '&manualTime=' + encodeURIComponent(manualTime);
            }

            fetch(saveUrl)
                .then(r => r.text())
                .then(html => {
                    // Replace entire page with reboot message
                    document.body.innerHTML = html;
                    // Add close button functionality
                    setTimeout(function() {
                        const btn = document.querySelector('.btn');
                        if (btn) {
                            btn.addEventListener('click', function() {
                                window.close();
                                document.querySelector('.message').innerHTML = 'You can now close this tab manually.<br>The ChronoBell will continue without Wi-Fi.';
                            });
                        }
                    }, 100);
                })
                .catch(() => {
                    // Connection lost means ESP32 is rebooting
                    document.body.innerHTML = '<div style="background:#000;color:#0f0;font-family:monospace;padding:20px;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;"><h1>REBOOTING</h1><p>ESP32 is restarting...</p><p>Please wait a few seconds, then close this page.</p></div>';
                });
        }

        // Load settings when page loads
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
    <title>Rebooting...</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: 'Courier New', Courier, monospace;
               background: #000000; color: #00ff00; min-height: 100vh;
               display: flex; align-items: center; justify-content: center;
               background-image:
                   linear-gradient(rgba(0, 20, 0, 0.8) 1px, transparent 1px),
                   linear-gradient(90deg, rgba(0, 20, 0, 0.8) 1px, transparent 1px),
                   radial-gradient(ellipse at center, rgba(0, 10, 0, 0.95) 0%, #000000 100%);
               background-size: 4px 4px, 4px 4px, 100% 100%;
               text-shadow: 0 0 10px #00ff00, 0 0 20px #00ff00, 0 0 30px #00ff00; }
        .container { text-align: center; padding: 20px; }
        h1 { font-size: 32px; margin-bottom: 24px; color: #00ff00; }
        .message { font-size: 18px; margin-bottom: 32px; color: #00cc00; }
        .checkmark { font-size: 60px; margin: 0 auto 24px; color: #00ff00;
                   text-shadow: 0 0 20px #00ff00; }
        .btn { padding: 12px 24px; border: 2px solid #00ff00; border-radius: 4px;
               background: transparent; color: #00ff00; font-size: 14px;
               font-family: 'Courier New', Courier, monospace; cursor: pointer;
               text-transform: uppercase; transition: all 0.2s; }
        .btn:hover { background: #00ff00; color: #000000; box-shadow: 0 0 20px #00ff00; }
    </style>
</head>
<body>
    <div class="container">
        <div class="checkmark">&#10004;</div>
        <h1>Settings Saved!</h1>
        <p class="message">ChronoBell is rebooting...</p>
        <button class="btn" onclick="closeWindow()">Close This Page</button>
    </div>
    <script>
        function closeWindow() {
            window.close();
            // If window.close() doesn't work, show message
            if (window.opener) {
                window.close();
            }
            document.querySelector('.message').innerHTML = 'You can now close this tab manually.<br>The ChronoBell will continue with the saved settings.';
        }

        // Auto-show close hint after delay
        setTimeout(function() {
            document.querySelector('.message').innerHTML = 'ChronoBell is rebooting.<br>It will work with or without Wi-Fi connection.<br>Please close this tab.';
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
        body { font-family: 'Courier New', Courier, monospace;
               background: #000000; color: #ff3333; min-height: 100vh; padding: 20px;
               background-image:
                   linear-gradient(rgba(20, 0, 0, 0.8) 1px, transparent 1px),
                   linear-gradient(90deg, rgba(20, 0, 0, 0.8) 1px, transparent 1px),
                   radial-gradient(ellipse at center, rgba(10, 0, 0, 0.95) 0%, #000000 100%);
               background-size: 4px 4px, 4px 4px, 100% 100%;
               text-shadow: 0 0 10px #ff0000, 0 0 20px #ff0000, 0 0 30px #ff0000; }
        .container { max-width: 400px; margin: 0 auto; }
        h1 { text-align: center; margin-bottom: 24px; color: #ff0000; font-size: 28px;
             text-shadow: 0 0 10px #ff0000, 0 0 20px #ff0000, 0 0 40px #ff0000; }
        .card { background: rgba(20, 0, 0, 0.7); border: 2px solid #330000; border-radius: 4px;
                padding: 16px; margin-bottom: 16px; box-shadow: inset 0 0 20px rgba(50, 0, 0, 0.5); }
        .btn { width: 100%; padding: 12px; border: 2px solid #ff0000; border-radius: 4px; cursor: pointer;
               font-size: 14px; font-weight: bold; margin-top: 12px; transition: all 0.2s;
               font-family: 'Courier New', Courier, monospace; text-transform: uppercase; }
        .btn:hover { background: #ff0000; color: #000000; box-shadow: 0 0 20px #ff0000; }
        .btn-primary { background: transparent; color: #ff0000; }
        .btn-secondary { background: transparent; color: #cc0000; border-color: #cc0000; }
        .btn-back { background: transparent; color: #aa0000; border-color: #aa0000; margin-top: 20px; }
        input[type="file"] { width: 100%; padding: 10px; border: 2px solid #330000; border-radius: 4px;
               background: #0a0000; color: #ff3333; font-size: 14px; margin-top: 8px;
               font-family: 'Courier New', Courier, monospace; }
        input[type="file"]::file-selector-button {
               background: #330000; color: #ff3333; border: 1px solid #ff0000;
               padding: 8px 16px; border-radius: 4px; cursor: pointer; margin-right: 10px;
               font-family: 'Courier New', Courier, monospace; }
        input[type="file"]::file-selector-button:hover {
               background: #ff0000; color: #000000; }
        label { font-size: 12px; color: #aa0000; text-transform: uppercase; letter-spacing: 2px; }
        #status { text-align: center; padding: 12px; border-radius: 4px; margin-bottom: 16px;
                  font-size: 14px; display: none; border: 2px solid; }
        #status.success { background: rgba(0, 50, 0, 0.8); color: #00ff00; border-color: #00ff00;
                         text-shadow: 0 0 10px #00ff00; display: block; }
        #status.error { background: rgba(50, 0, 0, 0.8); color: #ff0000; border-color: #ff0000;
                       text-shadow: 0 0 10px #ff0000; display: block; }
        #status.loading { background: rgba(30, 20, 0, 0.8); color: #ffaa00; border-color: #ffaa00;
                         text-shadow: 0 0 10px #ffaa00; display: block; }
        .warning { background: rgba(50, 30, 0, 0.8); border: 2px solid #ffaa00; border-radius: 4px;
                  padding: 16px; margin-bottom: 16px; color: #ffaa00; text-shadow: 0 0 10px #ffaa00; }
        .progress-container { margin-top: 20px; display: none; }
        .progress-bar { width: 100%; height: 24px; background: #330000; border-radius: 4px; overflow: hidden; }
        .progress-fill { height: 100%; background: #ff0000; width: 0%; transition: width 0.3s;
                        box-shadow: 0 0 10px #ff0000; }
        .progress-text { text-align: center; margin-top: 8px; color: #ff3333; font-size: 14px; }
        .section-title { font-size: 12px; color: #ff0000; margin-bottom: 12px; text-transform: uppercase;
                        letter-spacing: 2px; text-shadow: 0 0 10px #ff0000; border-bottom: 1px solid #330000;
                        padding-bottom: 8px; }
        .loader { border: 3px solid #330000; border-top: 3px solid #ff0000; border-radius: 50%;
                  width: 20px; height: 20px; animation: spin 1s linear infinite; margin: 0 auto;
                  box-shadow: 0 0 10px #ff0000; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>Firmware Update</h1>
        <div id="status"></div>

        <div class="warning">
            <strong>WARNING:</strong> Updating firmware will overwrite the current firmware.
            Make sure you have a backup if needed. Do not power off during update.
        </div>

        <div class="card">
            <div class="section-title">Select Firmware File</div>
            <form id="updateForm" enctype="multipart/form-data">
                <input type="file" id="firmware" name="firmware" accept=".bin" required onclick="this.click()">
                <button type="submit" class="btn btn-primary" id="updateBtn">
                    Start Update
                </button>
            </form>
        </div>

        <div class="progress-container" id="progressContainer">
            <div class="section-title">Upload Progress</div>
            <div class="progress-bar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="progress-text" id="progressText">0%</div>
        </div>

        <button class="btn btn-back" onclick="window.location.href='/'">
            << Back to Settings
        </button>
    </div>

    <script>
        const form = document.getElementById('updateForm');
        const progressContainer = document.getElementById('progressContainer');
        const progressFill = document.getElementById('progressFill');
        const progressText = document.getElementById('progressText');
        const updateBtn = document.getElementById('updateBtn');
        const status = document.getElementById('status');

        function showStatus(msg, type) {
            status.textContent = msg;
            status.className = type;
        }

        form.addEventListener('submit', async function(e) {
            e.preventDefault();

            const fileInput = document.getElementById('firmware');
            if (!fileInput.files.length) {
                showStatus('Please select a firmware file', 'error');
                return;
            }

            const file = fileInput.files[0];
            showStatus('Uploading firmware...', 'loading');
            progressContainer.style.display = 'block';
            updateBtn.disabled = true;
            updateBtn.innerHTML = '<div class="loader"></div>';

            const formData = new FormData();
            formData.append('firmware', file);

            try {
                const xhr = new XMLHttpRequest();

                xhr.upload.onprogress = function(e) {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        progressFill.style.width = percent + '%';
                        progressText.textContent = percent + '%';
                    }
                };

                xhr.onload = function() {
                    if (xhr.status === 200) {
                        showStatus('Update successful! Device will restart...', 'success');
                        progressFill.style.background = '#00ff00';
                        progressFill.style.boxShadow = '0 0 10px #00ff00';

                        // Poll for restart
                        let attempts = 0;
                        const pollInterval = setInterval(() => {
                            attempts++;
                            fetch('/status').then(() => {
                                clearInterval(pollInterval);
                                showStatus('Device restarted successfully!', 'success');
                            }).catch(() => {
                                if (attempts > 30) {
                                    clearInterval(pollInterval);
                                    showStatus('Update complete. You can now close this page.', 'success');
                                }
                            });
                        }, 1000);
                    } else {
                        showStatus('Update failed: ' + xhr.responseText, 'error');
                        updateBtn.disabled = false;
                        updateBtn.textContent = 'Retry Update';
                    }
                };

                xhr.onerror = function() {
                    showStatus('Network error during upload', 'error');
                    updateBtn.disabled = false;
                    updateBtn.textContent = 'Retry Update';
                };

                xhr.open('POST', '/update');
                xhr.send(formData);
            } catch (e) {
                showStatus('Error: ' + e.message, 'error');
                updateBtn.disabled = false;
                updateBtn.textContent = 'Retry Update';
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
        if (_otaDisplayCb) _otaDisplayCb(true, 0, 100);
        LOGF("Starting firmware update: %s\n", upload.filename.c_str());

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOGLN("Update begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Writing firmware to flash
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            LOGLN("Update write failed");
        }
        if (_otaDisplayCb) {
            float p = Update.progress();
            _otaDisplayCb(true, (unsigned int)(p * 100.0f), 100);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        // Update finished
        if (_otaDisplayCb) _otaDisplayCb(true, 100, 100);
        if (Update.end(true)) {
            LOGF("Update successful! Size: %u bytes\n", upload.totalSize);
        } else {
            LOGF("Update failed: %s\n", Update.errorString());
            _otaUpdate = false;
            if (_otaDisplayCb) _otaDisplayCb(false, 0, 0);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        _otaUpdate = false;
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


