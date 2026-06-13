#include "GuestWifiController.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <cstring>

#include "Config.h"
#include "Display.h"

namespace {
bool textFitsDisplay(const char* text) {
    if (text[0] == '\0') return true;

    int fullW = Display::textWidth(text, true, 1, 2);
    if (fullW <= COLS_PER_ROW) return true;

    size_t len = strlen(text);
    int halfTarget = fullW / 2;
    size_t split = 0;
    int cum = 0;
    bool inWord = false;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == ' ') {
            if (inWord) { cum += 2; inWord = false; }
        } else {
            if (inWord) cum += 1;
            cum += Display::charWidth(c, true);
            inWord = true;
        }
        if (cum >= halfTarget) {
            split = i + 1;
            break;
        }
    }
    if (split == 0) split = len / 2;

    char buf[GUEST_WIFI_TEXT_MAX_LEN];
    memcpy(buf, text, split);
    buf[split] = '\0';
    const char* line2 = text + split;
    while (*line2 == ' ') line2++;

    return Display::textWidth(buf, true, 1, 2) <= COLS_PER_ROW &&
           Display::textWidth(line2, true, 1, 2) <= COLS_PER_ROW;
}
}

void GuestWifiController::begin() {
    _disabled = (GUEST_WIFI_URL[0] == '\0');
    if (_disabled) {
        LOGLN("Guest WiFi: disabled (URL is empty)");
    }
}

bool GuestWifiController::fetch(const char* url) {
    if (_disabled || !url || url[0] == '\0') {
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOGLN("Guest WiFi: fetch skipped — WiFi not connected");
        return false;
    }

    LOG("Guest WiFi: fetching ");
    LOG(url);
    LOGLN(" ...");

    HTTPClient http;
    http.setTimeout(GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL);
    http.begin(url);

    int code = http.GET();
    if (code <= 0) {
        LOG("Guest WiFi: HTTP failed, code=");
        LOGLN(code);
        http.end();
        return false;
    }

    if (code != 200) {
        LOG("Guest WiFi: HTTP ");
        LOGLN(code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    if (body.length() == 0) {
        LOGLN("Guest WiFi: empty body");
        return false;
    }

    _ssid[0] = '\0';
    _password[0] = '\0';

    int newlinePos = body.indexOf('\n');
    if (newlinePos == -1) {
        // Single line — treat as password only (backward compat)
        body.trim();
        size_t len = body.length();
        if (len >= GUEST_WIFI_TEXT_MAX_LEN) {
            len = GUEST_WIFI_TEXT_MAX_LEN - 1;
        }
        memcpy(_password, body.c_str(), len);
        _password[len] = '\0';
    } else {
        // Two lines: first = SSID, second = password
        String ssidStr = body.substring(0, newlinePos);
        ssidStr.trim();
        size_t ssidLen = ssidStr.length();
        if (ssidLen >= GUEST_WIFI_TEXT_MAX_LEN) {
            ssidLen = GUEST_WIFI_TEXT_MAX_LEN - 1;
        }
        memcpy(_ssid, ssidStr.c_str(), ssidLen);
        _ssid[ssidLen] = '\0';

        String passStr = body.substring(newlinePos + 1);
        passStr.trim();
        size_t passLen = passStr.length();
        if (passLen >= GUEST_WIFI_TEXT_MAX_LEN) {
            passLen = GUEST_WIFI_TEXT_MAX_LEN - 1;
        }
        memcpy(_password, passStr.c_str(), passLen);
        _password[passLen] = '\0';
    }

    // Both strings must fit the display (each is shown full-screen)
    if (!textFitsDisplay(_ssid) || !textFitsDisplay(_password)) {
        LOGLN("Guest WiFi: text too wide for display, rejected");
        _ssid[0] = '\0';
        _password[0] = '\0';
        return false;
    }

    LOG("Guest WiFi: SSID=\"");
    LOG(_ssid);
    LOG("\" password=\"");
    LOG(_password);
    LOGLN("\"");
    _fetchFailCount = 0;
    _passwordAvailable = true;
    return true;
}

void GuestWifiController::tick(int hours, int minutes, int year, int month, int day) {
    if (_disabled) {
        return;
    }

    unsigned long nowMs = millis();
    if (nowMs - _lastFetchMs < (unsigned long)GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL) {
        return; // Throttle: at most 1 fetch attempt per minute
    }

    // Boot fetch: fire once at startup, but only mark done on success.
    // If WiFi isn't connected yet (async boot), retry each tick instead
    // of falling through to the 5‑minute retry path.
    if (!_bootFetchDone) {
        _lastFetchMs = nowMs;
        if (fetch(GUEST_WIFI_URL)) {
            _bootFetchDone = true;
        } else {
            _fetchFailCount++;
        }
        return;
    }

    // Daily timed fetch: at the configured time, once per day.
    int todayKey = year * 10000 + month * 100 + day;
    if (hours == GUEST_WIFI_FETCH_HOUR && minutes == GUEST_WIFI_FETCH_MINUTE && todayKey != _lastFetchDay) {
        _lastFetchDay = todayKey;
        _lastFetchMs = nowMs;
        _fetchFailCount = 0; // Reset retry counter for the new day
        if (!fetch(GUEST_WIFI_URL)) {
            _fetchFailCount++;
        }
        return;
    }

    // Retry: if a previous fetch (boot or timed) failed, try again once per
    // fetch interval, up to the configured failure cap.
    if (_fetchFailCount > 0 && _fetchFailCount < GUEST_WIFI_FETCH_MAX_FAILURES) {
        if (nowMs - _lastFetchMs >= (unsigned long)GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL) {
            _lastFetchMs = nowMs;
            _fetchFailCount++;
            if (fetch(GUEST_WIFI_URL)) {
                _fetchFailCount = 0;
            }
        }
        return;
    }
}
