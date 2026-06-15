#include "GuestWifiController.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <cstring>

#include "Config.h"
#include "Display.h"

namespace {
struct ParsedHttpUrl {
    String host;
    String path;
    uint16_t port = 80;
};

bool parseHttpUrl(const char* url, ParsedHttpUrl& parsed) {
    static const char PREFIX[] = "http://";
    if (!url || strncmp(url, PREFIX, sizeof(PREFIX) - 1) != 0) {
        return false;
    }

    String rest(url + sizeof(PREFIX) - 1);
    int slash = rest.indexOf('/');
    String authority = slash >= 0 ? rest.substring(0, slash) : rest;
    parsed.path = slash >= 0 ? rest.substring(slash) : "/";
    if (authority.length() == 0 || parsed.path.length() == 0) {
        return false;
    }

    int colon = authority.lastIndexOf(':');
    if (colon >= 0) {
        parsed.host = authority.substring(0, colon);
        int port = authority.substring(colon + 1).toInt();
        if (parsed.host.length() == 0 || port <= 0 || port > 65535) {
            return false;
        }
        parsed.port = (uint16_t)port;
    } else {
        parsed.host = authority;
        parsed.port = 80;
    }

    return parsed.host.length() > 0;
}

bool readHttpBody(WiFiClient& client, String& body) {
    String status = client.readStringUntil('\n');
    status.trim();
    int firstSpace = status.indexOf(' ');
    int code = firstSpace >= 0 ? status.substring(firstSpace + 1).toInt() : 0;
    if (!status.startsWith("HTTP/1.") || code != 200) {
        LOG("Guest WiFi: ");
        LOGLN(status);
        return false;
    }

    while (client.connected() || client.available()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
            break;
        }
    }

    body = "";
    const size_t maxBodyLen = (GUEST_WIFI_TEXT_MAX_LEN * 2) + 4;
    unsigned long idleStart = millis();
    while ((client.connected() || client.available()) && body.length() < maxBodyLen) {
        while (client.available() && body.length() < maxBodyLen) {
            body += (char)client.read();
            idleStart = millis();
        }
        if (millis() - idleStart > (unsigned long)GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL) {
            break;
        }
        delay(1);
    }
    return body.length() > 0;
}

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
        LOGLN("Guest WiFi: skipped (no WiFi)");
        return false;
    }

    LOG("Guest WiFi: fetching ");
    LOG(url);
    LOGLN(" ...");

    ParsedHttpUrl parsed;
    if (!parseHttpUrl(url, parsed)) {
        LOGLN("Guest WiFi: only plain http:// URLs are supported");
        return false;
    }

    WiFiClient client;
    client.setTimeout(GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL);
    if (!client.connect(parsed.host.c_str(), parsed.port)) {
        LOGLN("Guest WiFi: HTTP connect failed");
        return false;
    }

    client.print("GET ");
    client.print(parsed.path);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(parsed.host);
    client.print("\r\nConnection: close\r\nUser-Agent: ChronoBell\r\n\r\n");

    String body;
    bool ok = readHttpBody(client, body);
    client.stop();
    if (!ok) {
        LOGLN("Guest WiFi: HTTP fetch failed");
        return false;
    }

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
