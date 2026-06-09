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
        Serial.println("Guest WiFi: disabled (URL is empty)");
    }
}

bool GuestWifiController::fetch(const char* url) {
    if (_disabled || !url || url[0] == '\0') {
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Guest WiFi: fetch skipped — WiFi not connected");
        return false;
    }

    Serial.print("Guest WiFi: fetching ");
    Serial.print(url);
    Serial.println(" ...");

    HTTPClient http;
    http.setTimeout(GUEST_WIFI_FETCH_TIMEOUT_MS);
    http.begin(url);

    int code = http.GET();
    if (code <= 0) {
        Serial.print("Guest WiFi: HTTP failed, code=");
        Serial.println(code);
        http.end();
        return false;
    }

    if (code != 200) {
        Serial.print("Guest WiFi: HTTP ");
        Serial.println(code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    if (body.length() == 0) {
        Serial.println("Guest WiFi: empty body");
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
        if (ssidLen >= GUEST_WIFI_SSID_MAX_LEN) {
            ssidLen = GUEST_WIFI_SSID_MAX_LEN - 1;
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
        Serial.println("Guest WiFi: text too wide for display, rejected");
        _ssid[0] = '\0';
        _password[0] = '\0';
        return false;
    }

    Serial.print("Guest WiFi: SSID=\"");
    Serial.print(_ssid);
    Serial.print("\" password=\"");
    Serial.print(_password);
    Serial.println("\"");
    _passwordAvailable = true;
    return true;
}

void GuestWifiController::tryBootFetch() {
    if (_disabled || _bootFetchDone) {
        return;
    }
    _bootFetchDone = true;
    fetch(GUEST_WIFI_URL);
}

void GuestWifiController::tryTimedFetch(int hours, int minutes, int year, int month, int day) {
    if (_disabled) {
        return;
    }
    if (hours != GUEST_WIFI_FETCH_HOUR || minutes != GUEST_WIFI_FETCH_MINUTE) {
        return;
    }
    int todayKey = year * 10000 + month * 100 + day;
    if (todayKey == _lastFetchDay) {
        return;
    }
    _lastFetchDay = todayKey;
    fetch(GUEST_WIFI_URL);
}

void GuestWifiController::clear() {
    _ssid[0] = '\0';
    _password[0] = '\0';
    _passwordAvailable = false;
}
