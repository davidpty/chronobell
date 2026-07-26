#ifndef CONFIG_PORTAL_PAGE_H
#define CONFIG_PORTAL_PAGE_H

static const char CONFIG_PORTAL_PAGE_TEMPLATE[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ChronoBell</title>
    <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' fill='%23000' rx='4'/%3E%3Ctext x='16' y='23' text-anchor='middle' font-size='22' font-family='sans-serif' font-weight='bold' fill='%23f00'%3EC%3C/text%3E%3C/svg%3E">
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
        .timer-card { width: 100%; margin-bottom: 0.35em; }
        .timer-brand { margin: 0 0 0.35em; font-size: 0.95em; color: #ff0000; text-transform: uppercase; letter-spacing: 0.12em; text-shadow: none; white-space: nowrap; min-width: 6.7em; }
        .timer-panel { display: grid; gap: 0.45em; width: 100%; justify-items: stretch; }
        .timer-screen { background: rgba(10, 0, 0, 0.86); border: 1px solid #330000; border-radius: 0.25em; padding: 0.28em 0.32em 0.32em; box-shadow: inset 0 0 1em rgba(255, 0, 0, 0.08); width: 100%; display: flex; flex-direction: column; gap: 0.35em; }
        .display-frame { border: 1px solid #4d0000; border-radius: 0.2em; padding: 0.12em 0.14em 0.14em; background: linear-gradient(rgba(255, 0, 0, 0.03), rgba(255, 0, 0, 0.01)), #060000; box-shadow: inset 0 0 1.2em rgba(255, 0, 0, 0.12); width: 100%; }
        .pixel-display { display: block; width: 100%; height: auto; aspect-ratio: 32 / 16; background: transparent; shape-rendering: crispEdges; image-rendering: pixelated; }
        .pixel-dot.off { fill: #ff3a3a; fill-opacity: 0.08; }
        .pixel-dot.on { fill: #ff3a3a; fill-opacity: 1; }
        .button-strip { display: grid; grid-template-columns: repeat(3, 1fr); gap: 0.45em; width: 100%; }
        .button-card { appearance: none; min-width: 4.5em; padding: 0.46em 0.5em; border: 2px solid #cc0000; border-radius: 0.25em; background: transparent; color: #cc0000; font-family: 'Courier New', monospace; font-size: 1.1em; font-weight: bold; text-transform: uppercase; cursor: pointer; text-align: center; transition: all 0.15s; touch-action: none; user-select: none; }
        .button-card:active { background: #cc0000; color: #000000; box-shadow: 0 0 1em #cc0000; }
        @media (hover: hover) { .button-card:hover { background: #cc0000; color: #000000; box-shadow: 0 0 1em #cc0000; } }
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
        <div class="card timer-card">
            <div class="timer-brand">ChronoBell</div>
            <div class="timer-panel">
                <div class="timer-screen">
                    <div class="display-frame">
                        <svg class="pixel-display" id="timerDisplaySvg" viewBox="0 0 32 16" preserveAspectRatio="none" aria-label="ChronoBell timer display"></svg>
                    </div>
                    <div class="button-strip">
                        <button class="button-card" id="timerPrevBtn" type="button" aria-label="Previous"><span>◄</span></button>
                        <button class="button-card" id="timerModeBtn" type="button" aria-label="Mode"><span>●</span></button>
                        <button class="button-card" id="timerNextBtn" type="button" aria-label="Next"><span>►</span></button>
                    </div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="setting-row">
                <div class="setting-label">Style</div>
                <select id="style" onchange="onStyleChange(this.value)">
                    <option value="0" selected>RND - Daily random style</option>
                    <option value="1">BIG - Large HH:MM, no seconds</option>
                    <option value="2">INFO - Seconds, date, wday, or alternate</option>
                    <option value="3">WORD - Mixed-size word clock display</option>
                    <option value="4">ROMA - Roman numeral clock</option>
                    <option value="5">DIAL - Minimal analog dial</option>
                    <option value="6">BAR - Progress bar clock</option>
                    <option value="7">BIN - Binary clock</option>
                    <option value="8">PONG - Who wins the game?</option>
                    <option value="9">DRIFT - Irregular BIG-style clock</option>
                </select>
            </div>

            <div class="setting-row hidden" id="infoLineRow">
                <div class="setting-label">INFO</div>
                <select id="infoLine" onchange="applyInfoLine(this.value)"></select>
            </div>

            <div class="setting-row hidden" id="separatorRow">
                <div class="setting-label">Separator</div>
                <select id="separator" onchange="applySeparator(this.value)"></select>
            </div>

            <div class="setting-row hidden" id="dialMarksRow">
                <div class="setting-label">Marks</div>
                <select id="dialMarks" onchange="applyDialMarks(this.value)">
                    <option value="0">OFF - Hide cardinal marks</option>
                    <option value="1">ON - Show cardinal marks</option>
                </select>
            </div>

            <div class="setting-row hidden" id="barSecondsRow">
                <div class="setting-label">SECOND</div>
                <select id="barSeconds" onchange="applyBarSeconds(this.value)">
                    <option value="0">OFF</option>
                    <option value="1">ON</option>
                </select>
            </div>

            <div class="setting-row hidden" id="binSecondsRow">
                <div class="setting-label">SECOND</div>
                <select id="binSeconds" onchange="applyBinSeconds(this.value)">
                    <option value="0">OFF</option>
                    <option value="1">ON</option>
                </select>
            </div>

            <div class="setting-row hidden" id="rndIntervalRow">
                <div class="setting-label">CYCLE</div>
                <select id="rndInterval" onchange="applyRndInterval(this.value)">
                    <option value="0">1 min</option>
                    <option value="1">5 min</option>
                    <option value="2">10 min</option>
                    <option value="3">15 min</option>
                    <option value="4">30 min</option>
                    <option value="5">60 min</option>
                    <option value="6">90 min</option>
                    <option value="7">2 hours</option>
                    <option value="8">4 hours</option>
                    <option value="9">6 hours</option>
                    <option value="10">12 hours</option>
                    <option value="11">24 hours</option>
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
                    <option value="0" selected>AM/PM - 12:00 to 11:59</option>
                    <option value="1">24-HOUR - 00:00 to 23:59</option>
                </select>
            </div>

            __ANIM_ROW__

            <div class="setting-row">
                <div class="setting-label">Night</div>
                <select id="nightmode" onchange="applySetting('nightmode', this.value)">
                    <option value="0" selected>OFF - No night mode</option>
                    <option value="1">LOW - Dim display __NIGHT_DIM_START__-__NIGHT_DIM_END__</option>
                    <option value="2">LOW+MUTE - Dim display __NIGHT_DIM_START__-__NIGHT_DIM_END__, bell muted __NIGHT_MUTE_START__-__NIGHT_MUTE_END__</option>
                    <option value="3">DARK - Dim __NIGHT_DIM_START__-__NIGHT_DARK_START__, off __NIGHT_DARK_START__-__NIGHT_DARK_END__</option>
                    <option value="4">DARK+MUTE - Dim __NIGHT_DIM_START__-__NIGHT_DARK_START__, off + bell muted __NIGHT_MUTE_START__-__NIGHT_MUTE_END__</option>
                    <option value="5">MUTE - Bell muted __NIGHT_MUTE_START__-__NIGHT_MUTE_END__ only</option>
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
                <div class="setting-label">Alarm</div>
                <select id="alarm_mode" onchange="applySetting('alarm_mode', this.value); toggleAlarmTime()">
                    <option value="0">OFF</option>
                    <option value="1">ONCE</option>
                    <option value="2">DAILY</option>
                    <option value="3">WEEKDAY</option>
                    <option value="4">WEEKEND</option>
                </select>
            </div>
            <div class="setting-row hidden" id="alarm_time_row">
                <div class="setting-label">Wakes</div>
                <div class="row" style="flex:1; min-width:0;">
                    <input type="time" id="alarm_time" value="__INITIAL_ALARM_TIME__" onchange="applyAlarmTime()" style="margin-top:0;">
                </div>
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
        let timerPollTimer = null;
        let wifiFieldsDirty = false;
        let separatorSetting = 0;
        let driftSeparatorSetting = 0;
        let dialMarksSetting = Number("__INITIAL_DIALMARKS__");
        let barSecondsSetting = Number("__INITIAL_BARSECONDS__");
        let binSecondsSetting = Number("__INITIAL_BINSECONDS__");
        let rndIntervalSetting = Number("__INITIAL_RNDINTERVAL__");
        let infoLineSetting = 0;
        let timerState = {};
        const timerDisplaySvg = document.getElementById('timerDisplaySvg');
        const timerPrevBtn = document.getElementById('timerPrevBtn');
        const timerModeBtn = document.getElementById('timerModeBtn');
        const timerNextBtn = document.getElementById('timerNextBtn');
        function updateTimerDisplay(data) {
            timerState = data || {};
            if (!timerDisplaySvg) return;
            timerDisplaySvg.innerHTML = String(timerState.displaySvg || '');
        }

        function loadTimerStatus() {
            return fetch('/timerstatus')
                .then(function(r) { return r.json(); })
                .then(function(data) { updateTimerDisplay(data.timer || {}); })
                .catch(function() { updateTimerDisplay(timerState); });
        }

        function startTimerPoll() {
            if (timerPollTimer) return;
            timerPollTimer = setInterval(function() { loadTimerStatus(); }, 500);
        }

        function stopTimerPoll() {
            if (timerPollTimer) {
                clearInterval(timerPollTimer);
                timerPollTimer = null;
            }
        }

        function sendTimerAction(action) {
            return fetch('/timer?action=' + encodeURIComponent(action))
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data && data.timer) {
                        updateTimerDisplay(data.timer);
                    }
                    return Promise.all([loadTimerStatus(), loadSettings(false)]);
                })
                .catch(function() {
                    return Promise.all([loadTimerStatus(), loadSettings(false)]);
                });
        }

        function bindHoldAction(button, shortAction, longAction) {
            if (!button) return;
            let holdTimer = null;
            let held = false;
            let suppressClick = false;

            function clearHoldTimer() {
                if (holdTimer) {
                    clearTimeout(holdTimer);
                    holdTimer = null;
                }
            }

            function startHold(e) {
                if (e && e.pointerType === 'mouse' && e.button !== 0) return;
                held = false;
                suppressClick = false;
                clearHoldTimer();
                holdTimer = setTimeout(function() {
                    held = true;
                    sendTimerAction(longAction);
                }, 1500);
                if (button.setPointerCapture && e && e.pointerId !== undefined) {
                    try { button.setPointerCapture(e.pointerId); } catch (err) {}
                }
                if (e && e.preventDefault) e.preventDefault();
            }

            function endHold(e) {
                clearHoldTimer();
                if (!held) {
                    sendTimerAction(shortAction);
                }
                suppressClick = true;
                if (button.releasePointerCapture && e && e.pointerId !== undefined) {
                    try { button.releasePointerCapture(e.pointerId); } catch (err) {}
                }
                if (e && e.preventDefault) e.preventDefault();
            }

            button.addEventListener('pointerdown', startHold);
            button.addEventListener('pointerup', endHold);
            button.addEventListener('pointercancel', clearHoldTimer);
            button.addEventListener('pointerleave', clearHoldTimer);
            button.addEventListener('contextmenu', function(e) { e.preventDefault(); });
            button.onclick = function(e) {
                if (suppressClick) {
                    suppressClick = false;
                    return;
                }
                clearHoldTimer();
                sendTimerAction(shortAction);
                if (e && e.preventDefault) e.preventDefault();
            };
        }

        if (timerPrevBtn) {
            timerPrevBtn.onclick = function() { sendTimerAction('prev'); };
        }
        if (timerModeBtn) {
            bindHoldAction(timerModeBtn, 'mode', 'middle-long');
        }
        if (timerNextBtn) {
            timerNextBtn.onclick = function() { sendTimerAction('next'); };
        }

        function syncInfoLineRow() {
            const style = Number(document.getElementById('style').value);
            const row = document.getElementById('infoLineRow');
            const select = document.getElementById('infoLine');
            const configurable = style === 2;
            row.classList.toggle('hidden', !configurable);
            if (!configurable) return;

            const options = [
                [0, 'SEC - Seconds on the second line'],
                [1, 'DECI - Deciseconds on the second line'],
                [2, 'DATE - Date on the second line'],
                [3, 'WDAY - Day-of-week on the second line'],
                [4, 'ALT - Alternate DATE and WDAY']
            ];
            select.innerHTML = '';
            options.forEach(function(option) {
                const el = document.createElement('option');
                el.value = String(option[0]);
                el.textContent = option[1];
                select.appendChild(el);
            });
            select.value = String(infoLineSetting || 0);
        }

        function syncSeparatorRow() {
            const style = Number(document.getElementById('style').value);
            const row = document.getElementById('separatorRow');
            const select = document.getElementById('separator');
            const configurable = style === 1 || style === 2 || style === 9;
            row.classList.toggle('hidden', !configurable);
            if (!configurable) return;

            const options = style === 9
                ? [[0, 'SOLID - Always on, position shows drift'],
                   [1, 'BLINK - Both dots blink, position shows drift']]
                : [[0, 'SOLID - Always visible'],
                   [1, 'BLINK - Blink each second']];
            select.innerHTML = '';
            options.forEach(function(option) {
                const el = document.createElement('option');
                el.value = String(option[0]);
                el.textContent = option[1];
                select.appendChild(el);
            });
            select.value = String(style === 9 ? driftSeparatorSetting : separatorSetting);
        }

        function syncDialMarksRow() {
            const style = Number(document.getElementById('style').value);
            const row = document.getElementById('dialMarksRow');
            const select = document.getElementById('dialMarks');
            row.classList.toggle('hidden', style !== 5);
            select.value = String(dialMarksSetting);
        }

        function syncBarSecondsRow() {
            const style = Number(document.getElementById('style').value);
            const row = document.getElementById('barSecondsRow');
            const select = document.getElementById('barSeconds');
            row.classList.toggle('hidden', style !== 6);
            select.value = String(barSecondsSetting);
        }

        function syncBinSecondsRow() {
            const style = Number(document.getElementById('style').value);
            const row = document.getElementById('binSecondsRow');
            const select = document.getElementById('binSeconds');
            row.classList.toggle('hidden', style !== 7);
            select.value = String(binSecondsSetting);
        }

        function syncRndIntervalRow() {
            const style = Number(document.getElementById('style').value);
            const row = document.getElementById('rndIntervalRow');
            const select = document.getElementById('rndInterval');
            row.classList.toggle('hidden', style !== 0);
            select.value = String(rndIntervalSetting);
        }

        function onStyleChange(value) {
            syncInfoLineRow();
            syncSeparatorRow();
            syncDialMarksRow();
            syncBarSecondsRow();
            syncBinSecondsRow();
            syncRndIntervalRow();
            const infoLine = document.getElementById('infoLine');
            const separator = document.getElementById('separator');
            const dialMarks = document.getElementById('dialMarks');
            const barSeconds = document.getElementById('barSeconds');
            const binSeconds = document.getElementById('binSeconds');
            const rndInterval = document.getElementById('rndInterval');
            if (infoLine) infoLine.disabled = true;
            separator.disabled = true;
            dialMarks.disabled = true;
            if (barSeconds) barSeconds.disabled = true;
            if (binSeconds) binSeconds.disabled = true;
            if (rndInterval) rndInterval.disabled = true;
            applySetting('style', value).finally(function() {
                if (infoLine) infoLine.disabled = false;
                separator.disabled = false;
                dialMarks.disabled = false;
                if (barSeconds) barSeconds.disabled = false;
                if (binSeconds) binSeconds.disabled = false;
                if (rndInterval) rndInterval.disabled = false;
            });
        }

        function applyInfoLine(value) {
            infoLineSetting = Number(value);
            return fetch('/apply?field=infoline&value=' + encodeURIComponent(value));
        }

        function applySeparator(value) {
            const style = Number(document.getElementById('style').value);
            const parsed = Number(value);
            if (style === 9) {
                driftSeparatorSetting = parsed;
            } else {
                separatorSetting = parsed;
            }
            return fetch('/apply?field=separator&style=' + encodeURIComponent(style) +
                         '&value=' + encodeURIComponent(value));
        }

        function applyDialMarks(value) {
            dialMarksSetting = Number(value);
            return applySetting('dialmarks', value);
        }

        function applyBarSeconds(value) {
            barSecondsSetting = Number(value);
            return applySetting('barseconds', value);
        }

        function applyBinSeconds(value) {
            binSecondsSetting = Number(value);
            return applySetting('binseconds', value);
        }

        function applyRndInterval(value) {
            rndIntervalSetting = Number(value);
            return applySetting('rndinterval', value);
        }

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

        function toggleAlarmTime() {
            const mode = document.getElementById('alarm_mode').value;
            document.getElementById('alarm_time_row').style.display =
                mode === '0' ? 'none' : 'flex';
        }

        function applyAlarmTime() {
            const val = document.getElementById('alarm_time').value;
            if (val) {
                applySetting('alarm_time', val);
            }
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
                infoLine: "__INITIAL_INFOLINE__",
                datestyle: "__INITIAL_DATESTYLE__",
                timefmt: "__INITIAL_TIMEFMT__",
                anim: "__INITIAL_ANIM__",
                nightmode: "__INITIAL_NIGHTMODE__",
                bellmode: "__INITIAL_BELLMODE__",
                alarm_mode: "__INITIAL_ALARM_MODE__",
                brightness: "__INITIAL_BRIGHTNESS__",
                timezone: "__INITIAL_TIMEZONE__",
                manualMode: "__INITIAL_MANUAL_MODE__"
            };

            const pairs = [
                ['style', initial.style],
                ['infoLine', initial.infoLine],
                ['datestyle', initial.datestyle],
                ['timefmt', initial.timefmt],
                ['anim', initial.anim],
                ['nightmode', initial.nightmode],
                ['bellmode', initial.bellmode],
                ['alarm_mode', initial.alarm_mode],
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
            syncInfoLineRow();
            syncSeparatorRow();
            syncDialMarksRow();
            syncBarSecondsRow();
            syncRndIntervalRow();
            toggleAlarmTime();
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
                ['infoLine', data.infoLineMode],
                ['anim', data.anim],
                ['datestyle', data.datestyle],
                ['timefmt', data.timefmt],
                ['nightmode', data.nightmode],
                    ['bellmode', data.bellmode],
                    ['alarm_mode', data.alarm_mode],
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

                if (data.infoLineMode !== undefined) {
                    infoLineSetting = Number(data.infoLineMode);
                    syncInfoLineRow();
                }

                if (data.separatorBig !== undefined) {
                    separatorSetting = Number(data.separatorBig);
                }
                if (data.separatorDrift !== undefined) {
                    driftSeparatorSetting = Number(data.separatorDrift);
                }
                if (data.dialMarks !== undefined) {
                    dialMarksSetting = Number(data.dialMarks);
                }
                if (data.rndInterval !== undefined) {
                    rndIntervalSetting = Number(data.rndInterval);
                }
                syncSeparatorRow();
                syncDialMarksRow();
                syncRndIntervalRow();

                if (data.alarm_time !== undefined) {
                    document.getElementById('alarm_time').value = data.alarm_time;
                }

                updateHotspotFromStatus(data);
                if (data.timer) {
                    updateTimerDisplay(data.timer);
                }

                if (!data.manualTime) {
                    const now = new Date();
                    document.getElementById('manualDate').value = now.getFullYear() + '-' +
                        String(now.getMonth() + 1).padStart(2, '0') + '-' +
                        String(now.getDate()).padStart(2, '0');
                    document.getElementById('manualTime').value = String(now.getHours()).padStart(2, '0') + ':' +
                        String(now.getMinutes()).padStart(2, '0');
                    document.getElementById('manualSec').value = String(now.getSeconds()).padStart(2, '0');
                }

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
        startTimerPoll();
        loadTimerStatus();
    };
    </script>
</body>
</html>
)rawliteral";

static const char CONFIG_PORTAL_ANIM_ROW_TEMPLATE[] = R"rawliteral(
            <div class="setting-row">
                <div class="setting-label">ANIM</div>
                <select id="anim" onchange="applySetting('anim', this.value)">
                    <option value="0" selected>OFF - Instant switching</option>
                    <option value="1">ON - Enable transition animations</option>
                </select>
            </div>
)rawliteral";

#endif // CONFIG_PORTAL_PAGE_H
