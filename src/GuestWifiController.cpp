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
    const size_t maxBodyLen = (LOCAL_DISPLAY_TEXT_MAX_LEN * 2) + 4;
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

bool readHttpResponse(WiFiClient& client, String& body, unsigned long startMs) {
    String response;
    const size_t maxResponseLen = (LOCAL_DISPLAY_TEXT_MAX_LEN * 2) + 512;
    response.reserve(maxResponseLen);

    while ((client.connected() || client.available()) &&
           millis() - startMs < LOCAL_HTTP_TOTAL_TIMEOUT_MS) {
        while (client.available()) {
            if (response.length() >= maxResponseLen) {
                return false;
            }
            response += (char)client.read();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    int bodyStart = response.indexOf("\r\n\r\n");
    int separatorLen = 4;
    if (bodyStart < 0) {
        bodyStart = response.indexOf("\n\n");
        separatorLen = 2;
    }
    if (bodyStart < 0) {
        return false;
    }

    String status = response.substring(0, response.indexOf('\n'));
    status.trim();
    int firstSpace = status.indexOf(' ');
    int code = firstSpace >= 0 ? status.substring(firstSpace + 1).toInt() : 0;
    if (!status.startsWith("HTTP/1.") || code != 200) {
        LOG("Guest WiFi: ");
        LOGLN(status);
        return false;
    }

    body = response.substring(bodyStart + separatorLen);
    body.trim();
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

    char buf[LOCAL_DISPLAY_TEXT_MAX_LEN];
    memcpy(buf, text, split);
    buf[split] = '\0';
    const char* line2 = text + split;
    while (*line2 == ' ') line2++;

    return Display::textWidth(buf, true, 1, 2) <= COLS_PER_ROW &&
           Display::textWidth(line2, true, 1, 2) <= COLS_PER_ROW;
}
}

void GuestWifiController::begin() {
    _ssid[0] = '\0';
    _password[0] = '\0';
    _disabled = (GUEST_WIFI_URL[0] == '\0');
    if (_disabled) {
        LOGLN("Guest WiFi: disabled (URL is empty)");
        return;
    }

    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOGLN("Guest WiFi: mutex create failed");
        _disabled = true;
        return;
    }

    _taskStop = false;
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "guestwifi", GUEST_WIFI_TASK_STACK_WORDS,
                                            this, LOCAL_NETWORK_TASK_PRIORITY, &_task, 0);
    if (ok != pdPASS) {
        LOGLN("Guest WiFi: task create failed");
        _task = nullptr;
        _disabled = true;
    }
}

void GuestWifiController::stop() {
    _taskStop = true;
}

bool GuestWifiController::isTextAvailable() const {
    if (_disabled || !_mutex) return false;
    if (xSemaphoreTake(_mutex, 0) != pdTRUE) {
        return _passwordAvailable;
    }
    bool available = _passwordAvailable;
    xSemaphoreGive(_mutex);
    return available;
}

bool GuestWifiController::copyText(char* ssidOut, size_t ssidLen, char* passwordOut, size_t passwordLen) const {
    if (!ssidOut || !passwordOut || ssidLen == 0 || passwordLen == 0) return false;
    ssidOut[0] = '\0';
    passwordOut[0] = '\0';
    if (_disabled || !_mutex) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return false;
    bool available = _passwordAvailable;
    if (available) {
        strlcpy(ssidOut, _ssid, ssidLen);
        strlcpy(passwordOut, _password, passwordLen);
    }
    xSemaphoreGive(_mutex);
    return available;
}

bool GuestWifiController::bootFetchDone() const {
    if (_disabled || !_mutex) return true;
    if (xSemaphoreTake(_mutex, 0) != pdTRUE) return _bootFetchDone;
    bool done = _bootFetchDone;
    xSemaphoreGive(_mutex);
    return done;
}

bool GuestWifiController::fetchBlocking(const char* url, char* ssidOut, size_t ssidLen, char* passwordOut, size_t passwordLen) {
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
    client.setTimeout(LOCAL_HTTP_CONNECT_TIMEOUT_MS);
    unsigned long startMs = millis();
    if (!client.connect(parsed.host.c_str(), parsed.port, LOCAL_HTTP_CONNECT_TIMEOUT_MS)) {
        LOGLN("Guest WiFi: HTTP connect failed");
        return false;
    }

    client.print("GET ");
    client.print(parsed.path);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(parsed.host);
    client.print("\r\nConnection: close\r\nUser-Agent: ChronoBell\r\n\r\n");

    String body;
    bool ok = readHttpResponse(client, body, startMs);
    client.stop();
    if (!ok) {
        LOGLN("Guest WiFi: HTTP fetch failed");
        return false;
    }

    if (body.length() == 0) {
        LOGLN("Guest WiFi: empty body");
        return false;
    }

    ssidOut[0] = '\0';
    passwordOut[0] = '\0';

    int newlinePos = body.indexOf('\n');
    if (newlinePos == -1) {
        // Single line — treat as password only (backward compat)
        body.trim();
        size_t len = body.length();
        if (len >= passwordLen) {
            len = passwordLen - 1;
        }
        memcpy(passwordOut, body.c_str(), len);
        passwordOut[len] = '\0';
    } else {
        // Two lines: first = SSID, second = password
        String ssidStr = body.substring(0, newlinePos);
        ssidStr.trim();
        size_t copiedSsidLen = ssidStr.length();
        if (copiedSsidLen >= ssidLen) {
            copiedSsidLen = ssidLen - 1;
        }
        memcpy(ssidOut, ssidStr.c_str(), copiedSsidLen);
        ssidOut[copiedSsidLen] = '\0';

        String passStr = body.substring(newlinePos + 1);
        passStr.trim();
        size_t passLen = passStr.length();
        if (passLen >= passwordLen) {
            passLen = passwordLen - 1;
        }
        memcpy(passwordOut, passStr.c_str(), passLen);
        passwordOut[passLen] = '\0';
    }

    // Both strings must fit the display (each is shown full-screen)
    if (!textFitsDisplay(ssidOut) || !textFitsDisplay(passwordOut)) {
        LOGLN("Guest WiFi: text too wide for display, rejected");
        ssidOut[0] = '\0';
        passwordOut[0] = '\0';
        return false;
    }

    LOG("Guest WiFi: SSID=\"");
    LOG(ssidOut);
    LOG("\" password=\"");
    LOG(passwordOut);
    LOGLN("\"");
    return true;
}

void GuestWifiController::applyFetchResult(bool ok, FetchReason reason, const char* ssid, const char* password) {
    if (!_mutex) return;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (ok) {
        strlcpy(_ssid, ssid ? ssid : "", sizeof(_ssid));
        strlcpy(_password, password ? password : "", sizeof(_password));
        _passwordAvailable = _password[0] != '\0';
        _fetchFailCount = 0;
        _bootFetchDone = true;
    } else {
        if (_fetchFailCount < GUEST_WIFI_FETCH_MAX_FAILURES) {
            _fetchFailCount++;
        }
        if (reason == FetchReason::Boot && _fetchFailCount >= GUEST_WIFI_FETCH_MAX_FAILURES) {
            _bootFetchDone = true;
        }
    }
    _fetchInProgress = false;
    xSemaphoreGive(_mutex);
}

bool GuestWifiController::requestFetch(FetchReason reason, unsigned long nowMs) {
    if (!_mutex) return false;
    if (xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    if (_fetchRequested || _fetchInProgress) {
        xSemaphoreGive(_mutex);
        return false;
    }
    _fetchReason = reason;
    _fetchRequested = true;
    _lastFetchMs = nowMs;
    xSemaphoreGive(_mutex);
    return true;
}

void GuestWifiController::taskEntry(void* arg) {
    static_cast<GuestWifiController*>(arg)->taskLoop();
}

void GuestWifiController::taskLoop() {
    while (!_taskStop) {
        bool shouldFetch = false;
        FetchReason reason = FetchReason::Boot;
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (_fetchRequested && !_fetchInProgress) {
                _fetchRequested = false;
                _fetchInProgress = true;
                reason = _fetchReason;
                shouldFetch = true;
            }
            xSemaphoreGive(_mutex);
        }

        if (shouldFetch) {
            char ssid[LOCAL_DISPLAY_TEXT_MAX_LEN];
            char password[LOCAL_DISPLAY_TEXT_MAX_LEN];
            bool ok = fetchBlocking(GUEST_WIFI_URL, ssid, sizeof(ssid), password, sizeof(password));
            applyFetchResult(ok, reason, ssid, password);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(nullptr);
}

void GuestWifiController::tick(int hours, int minutes, int year, int month, int day) {
    if (_disabled) {
        return;
    }

    unsigned long nowMs = millis();
    bool bootFetchDone = true;
    int fetchFailCount = 0;
    unsigned long lastFetchMs = 0;
    if (_mutex && xSemaphoreTake(_mutex, 0) == pdTRUE) {
        bootFetchDone = _bootFetchDone;
        fetchFailCount = _fetchFailCount;
        lastFetchMs = _lastFetchMs;
        xSemaphoreGive(_mutex);
    }

    if (lastFetchMs != 0 &&
        nowMs - lastFetchMs < (unsigned long)GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL) {
        return; // Throttle: at most 1 fetch attempt per minute
    }

    // Boot fetch: fire once at startup, but only mark done on success.
    // If WiFi isn't connected yet (async boot), retry each tick instead
    // of falling through to the 5‑minute retry path.
    if (!bootFetchDone) {
        requestFetch(FetchReason::Boot, nowMs);
        return;
    }

    // Daily timed fetch: at the configured time, once per day.
    int todayKey = year * 10000 + month * 100 + day;
    if (hours == GUEST_WIFI_FETCH_HOUR && minutes == GUEST_WIFI_FETCH_MINUTE && todayKey != _lastFetchDay) {
        _lastFetchDay = todayKey;
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            _fetchFailCount = 0; // Reset retry counter for the new day
            fetchFailCount = 0;
            xSemaphoreGive(_mutex);
        }
        requestFetch(FetchReason::Daily, nowMs);
        return;
    }

    // Retry: if a previous fetch (boot or timed) failed, try again once per
    // fetch interval, up to the configured failure cap.
    if (fetchFailCount > 0 && fetchFailCount < GUEST_WIFI_FETCH_MAX_FAILURES) {
        if (nowMs - lastFetchMs >= (unsigned long)GUEST_WIFI_FETCH_TIMEOUT_SECONDS * 1000UL) {
            requestFetch(FetchReason::Retry, nowMs);
        }
        return;
    }
}
