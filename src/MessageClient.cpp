#include "MessageClient.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <ctype.h>
#include <string.h>

#include "Display.h"
#include "TimeProvider.h"

namespace {
struct ParsedHttpUrl {
    String host;
    String path;
    uint16_t port = 80;
};

bool parseHttpUrl(const char* url, ParsedHttpUrl& parsed) {
    static const char prefix[] = "http://";
    if (!url || strncmp(url, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    String rest(url + sizeof(prefix) - 1);
    int slash = rest.indexOf('/');
    String authority = slash >= 0 ? rest.substring(0, slash) : rest;
    parsed.path = slash >= 0 ? rest.substring(slash) : "/";
    int colon = authority.lastIndexOf(':');
    if (colon >= 0) {
        parsed.host = authority.substring(0, colon);
        int port = authority.substring(colon + 1).toInt();
        if (port <= 0 || port > 65535) return false;
        parsed.port = (uint16_t)port;
    } else {
        parsed.host = authority;
    }
    return parsed.host.length() > 0 && parsed.path.length() > 0;
}

String limitString(const char* s, size_t maxLen) {
    if (!s) return String();
    String out(s);
    if (out.length() > maxLen) {
        out = out.substring(0, maxLen);
    }
    return out;
}

String limitString(const String& s, size_t maxLen) {
    if (s.length() <= maxLen) return s;
    return s.substring(0, maxLen);
}

uint16_t clampDuration(uint16_t seconds) {
    if (seconds < CHRONOMSG_MIN_DURATION_SEC) return CHRONOMSG_MIN_DURATION_SEC;
    if (seconds > CHRONOMSG_MAX_DURATION_SEC) return CHRONOMSG_MAX_DURATION_SEC;
    return seconds;
}

bool lowerEquals(const String& a, const char* b) {
    if (!b || a.length() != strlen(b)) return false;
    for (size_t i = 0; i < a.length(); ++i) {
        if (tolower((unsigned char)a[i]) != b[i]) return false;
    }
    return true;
}

bool currentEpoch(TimeProvider* provider, time_t& epoch) {
    return provider && provider->currentEpoch(epoch);
}

int skipJsonWhitespace(const String& json, int pos) {
    while (pos >= 0 && pos < (int)json.length()) {
        char c = json[pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        pos++;
    }
    return pos;
}

int findMatchingJsonChar(const String& json, int openPos, char openChar, char closeChar) {
    if (openPos < 0 || openPos >= (int)json.length() || json[openPos] != openChar) return -1;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (int i = openPos; i < (int)json.length(); ++i) {
        char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == openChar) {
            depth++;
        } else if (c == closeChar) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

int findJsonKeyValueStart(const String& json, const char* key, int start = 0, int end = -1) {
    if (!key) return -1;
    if (end < 0 || end > (int)json.length()) end = (int)json.length();
    String pattern = "\"";
    pattern += key;
    pattern += "\"";
    int pos = start;
    while (pos >= 0 && pos < end) {
        pos = json.indexOf(pattern, pos);
        if (pos < 0 || pos >= end) return -1;
        int afterKey = pos + pattern.length();
        int colon = skipJsonWhitespace(json, afterKey);
        if (colon < end && json[colon] == ':') {
            return skipJsonWhitespace(json, colon + 1);
        }
        pos = afterKey;
    }
    return -1;
}

String parseJsonStringAt(const String& json, int quotePos) {
    String out;
    if (quotePos < 0 || quotePos >= (int)json.length() || json[quotePos] != '"') return out;
    for (int i = quotePos + 1; i < (int)json.length(); ++i) {
        char c = json[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < (int)json.length()) {
            char e = json[++i];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                    // Non-ASCII escapes are intentionally collapsed to a space;
                    // later display normalization keeps only supported glyphs.
                    out += ' ';
                    i += 4;
                    break;
                default:
                    out += e;
                    break;
            }
        } else {
            out += c;
        }
    }
    return out;
}

String jsonStringField(const String& obj, const char* key, const char* fallback, size_t maxLen) {
    int value = findJsonKeyValueStart(obj, key);
    if (value < 0 || value >= (int)obj.length() || obj[value] != '"') {
        return limitString(fallback, maxLen);
    }
    return limitString(parseJsonStringAt(obj, value), maxLen);
}

uint32_t jsonUIntField(const String& obj, const char* key, uint32_t fallback) {
    int value = findJsonKeyValueStart(obj, key);
    if (value < 0 || value >= (int)obj.length()) return fallback;
    return (uint32_t)obj.substring(value).toInt();
}

int jsonIntField(const String& obj, const char* key, int fallback) {
    int value = findJsonKeyValueStart(obj, key);
    if (value < 0 || value >= (int)obj.length()) return fallback;
    return obj.substring(value).toInt();
}

bool jsonBoolField(const String& obj, const char* key, bool fallback) {
    int value = findJsonKeyValueStart(obj, key);
    if (value < 0 || value >= (int)obj.length()) return fallback;
    if (obj.startsWith("true", value)) return true;
    if (obj.startsWith("false", value)) return false;
    int numeric = obj.substring(value).toInt();
    return numeric != 0;
}

String jsonObjectField(const String& obj, const char* key) {
    int value = findJsonKeyValueStart(obj, key);
    if (value < 0 || value >= (int)obj.length() || obj[value] != '{') return String();
    int end = findMatchingJsonChar(obj, value, '{', '}');
    if (end < 0) return String();
    return obj.substring(value, end + 1);
}

bool parseChronoMessageObject(const String& obj, ChronoMessage& msg, TimeProvider* timeProvider) {
    msg = ChronoMessage();
    msg.id = jsonStringField(obj, "id", "", CHRONOMSG_MAX_ID_LEN);
    msg.id.trim();
    if (msg.id.length() == 0) return false;

    msg.source = jsonStringField(obj, "source", "", LOCAL_DISPLAY_TEXT_MAX_LEN);
    msg.type = jsonStringField(obj, "type", "", LOCAL_DISPLAY_TEXT_MAX_LEN);
    msg.priority = constrain(jsonIntField(obj, "priority", 5), 0, 9);
    msg.title = normalizeMessageText(jsonStringField(obj, "title", "", LOCAL_DISPLAY_TEXT_MAX_LEN));
    msg.body = normalizeMessageText(jsonStringField(obj, "body", "", LOCAL_DISPLAY_TEXT_MAX_LEN));
    if (msg.title.length() == 0 && msg.body.length() == 0) return false;

    msg.created = jsonUIntField(obj, "created", 0);
    msg.expires = jsonUIntField(obj, "expires", 0);
    time_t nowEpoch = 0;
    if (currentEpoch(timeProvider, nowEpoch) && msg.expires > 0 && msg.expires <= (uint32_t)nowEpoch) {
        return false;
    }

    String display = jsonObjectField(obj, "display");
    msg.repeat = jsonBoolField(display, "repeat", false);
    msg.durationSec = clampDuration((uint16_t)jsonUIntField(display, "duration", CHRONOMSG_DEFAULT_DURATION_SEC));
    msg.intervalSec = (uint16_t)jsonUIntField(display, "interval", chronoMessageDefaultIntervalSec(msg.priority));
    msg.indicator = jsonBoolField(display, "indicator", true);
    msg.dismissible = jsonBoolField(display, "dismissible", true);
    msg.valid = true;
    return true;
}

bool parseChronoMessagesBody(const String& body, ChronoMessage* out, uint8_t& count, TimeProvider* timeProvider) {
    count = 0;
    int value = findJsonKeyValueStart(body, "messages");
    if (value < 0 || value >= (int)body.length() || body[value] != '[') return true;
    int arrayEnd = findMatchingJsonChar(body, value, '[', ']');
    if (arrayEnd < 0) return false;

    int pos = value + 1;
    while (pos < arrayEnd && count < CHRONOMSG_MAX_MESSAGES) {
        pos = skipJsonWhitespace(body, pos);
        if (pos >= arrayEnd) break;
        if (body[pos] == ',') {
            pos++;
            continue;
        }
        if (body[pos] != '{') return false;
        int objEnd = findMatchingJsonChar(body, pos, '{', '}');
        if (objEnd < 0 || objEnd > arrayEnd) return false;
        ChronoMessage msg;
        if (parseChronoMessageObject(body.substring(pos, objEnd + 1), msg, timeProvider)) {
            out[count++] = msg;
        }
        pos = objEnd + 1;
    }
    return true;
}
}

uint16_t chronoMessageDefaultIntervalSec(int priority) {
    if (priority >= 9) return 60;
    if (priority >= 7) return 180;
    if (priority >= 5) return 600;
    return 0;
}

String normalizeMessageText(const String& text) {
    String out;
    out.reserve(LOCAL_DISPLAY_TEXT_MAX_LEN);
    bool pendingSpace = false;

    for (size_t i = 0; i < text.length() && out.length() < LOCAL_DISPLAY_TEXT_MAX_LEN; ++i) {
        unsigned char c = (unsigned char)text[i];
        char mapped = 0;

        if (c < 0x80) {
            if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '%' || c == '-' || c == '+' || c == '@' || c == '^' || c == 'v') {
                mapped = (char)c;
            } else if (c == '.' || c == '_' || c == '/' || c == ':' || c == ',' ||
                       c == ';' || c == '!' || c == '?' || isspace(c)) {
                mapped = ' ';
            } else {
                mapped = ' ';
            }
        } else if (c == 0xC3 && i + 1 < text.length()) {
            unsigned char d = (unsigned char)text[++i];
            switch (d) {
                case 0x81: case 0xA1: case 0x80: case 0xA0: case 0x82: case 0xA2: case 0x84: case 0xA4: mapped = 'A'; break;
                case 0x89: case 0xA9: case 0x88: case 0xA8: case 0x8A: case 0xAA: case 0x8B: case 0xAB: mapped = 'E'; break;
                case 0x8D: case 0xAD: case 0x8C: case 0xAC: case 0x8E: case 0xAE: case 0x8F: case 0xAF: mapped = 'I'; break;
                case 0x93: case 0xB3: case 0x92: case 0xB2: case 0x94: case 0xB4: case 0x96: case 0xB6: mapped = 'O'; break;
                case 0x9A: case 0xBA: case 0x99: case 0xB9: case 0x9B: case 0xBB: case 0x9C: case 0xBC: mapped = 'U'; break;
                case 0x91: case 0xB1: mapped = 'N'; break;
                default: mapped = ' '; break;
            }
        } else {
            mapped = ' ';
        }

        if (mapped == ' ') {
            pendingSpace = out.length() > 0;
            continue;
        }
        if (pendingSpace && out.length() < LOCAL_DISPLAY_TEXT_MAX_LEN) {
            out += ' ';
        }
        pendingSpace = false;
        out += mapped;
    }

    out.trim();
    return out;
}

MessageLayout layoutMessageText(const String& title, const String& body) {
    MessageLayout layout;
    String t = normalizeMessageText(title);
    String b = normalizeMessageText(body);
    const int maxW = COLS_PER_ROW;

    if (t.length() == 0 && b.length() > 0 && Display::textWidth(b.c_str(), true, 1, 2) <= maxW) {
        layout.kind = MessageLayoutKind::OneLine;
        layout.line1 = b;
        return layout;
    }

    String combined;
    if (t.length() > 0 && b.length() > 0) combined = t + " " + b;
    else combined = t.length() > 0 ? t : b;
    if (combined.length() > 0 && Display::textWidth(combined.c_str(), true, 1, 2) <= maxW) {
        layout.kind = MessageLayoutKind::OneLine;
        layout.line1 = combined;
        return layout;
    }

    if (t.length() > 0 && b.length() > 0 &&
        Display::textWidth(t.c_str(), true, 1, 2) <= maxW &&
        Display::textWidth(b.c_str(), true, 1, 2) <= maxW) {
        layout.kind = MessageLayoutKind::TwoLine;
        layout.line1 = t;
        layout.line2 = b;
        return layout;
    }

    if (b.length() > 0) {
        layout.kind = MessageLayoutKind::Scroll;
        layout.line1 = t;
        layout.line2 = b;
        layout.scrollLine1 = t.length() > 0 && Display::textWidth(t.c_str(), true, 1, 2) > maxW;
        layout.scrollLine2 = b.length() > 0 && Display::textWidth(b.c_str(), true, 1, 2) > maxW;
        return layout;
    }

    if (t.length() > 0) {
        layout.kind = MessageLayoutKind::Scroll;
        layout.line1 = t;
        layout.scrollLine1 = true;
    }
    return layout;
}

void MessageClient::begin(TimeProvider* timeProvider) {
    _timeProvider = timeProvider;
    if (!CHRONOMSG_ENABLED || _started) return;
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOGLN("ChronoMsg: mutex create failed");
        return;
    }
    _taskStop = false;
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "chronomsg", CHRONOMSG_TASK_STACK_WORDS,
                                            this, LOCAL_NETWORK_TASK_PRIORITY, &_task, 0);
    if (ok != pdPASS) {
        LOGLN("ChronoMsg: task create failed");
        _task = nullptr;
        return;
    }
    _started = true;
}

void MessageClient::stop() {
    _taskStop = true;
}

void MessageClient::update() {
    if (!_mutex) return;
    time_t nowEpoch = 0;
    bool validTime = currentEpoch(_timeProvider, nowEpoch);
    uint32_t nowMs = millis();
    if (xSemaphoreTake(_mutex, 0) != pdTRUE) return;
    pruneExpiredLocked(nowEpoch, validTime, nowMs);
    if (_previewVisible && (int32_t)(nowMs - _previewEndMs) >= 0) {
        hidePreviewLocked();
    }
    if (!_previewVisible) {
        int idx = selectedSlotLocked();
        if (idx >= 0) {
            MessageSlot& slot = _slots[idx];
            bool due = false;
            if (!slot.firstPreviewShown) {
                due = slot.firstPreviewDueMs > 0 && (int32_t)(nowMs - slot.firstPreviewDueMs) >= 0;
            } else if (slot.msg.repeat && slot.msg.intervalSec > 0) {
                due = (nowMs - slot.lastPreviewMs) >= (uint32_t)slot.msg.intervalSec * 1000UL;
            }
            if (due) startPreviewLocked(idx, nowMs);
        }
    }
    xSemaphoreGive(_mutex);
}

bool MessageClient::hasUnread() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    bool found = selectedSlotLocked() >= 0;
    xSemaphoreGive(_mutex);
    return found;
}

int MessageClient::unreadCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return 0;
    int count = 0;
    for (int i = 0; i < CHRONOMSG_MAX_MESSAGES; i++) {
        if (_slots[i].known && _slots[i].unread) count++;
    }
    xSemaphoreGive(_mutex);
    return count;
}

int MessageClient::highestPriority() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return -1;
    int hp = -1;
    for (int i = 0; i < CHRONOMSG_MAX_MESSAGES; i++) {
        if (_slots[i].known && _slots[i].unread) {
            if (_slots[i].msg.priority > hp) hp = _slots[i].msg.priority;
        }
    }
    xSemaphoreGive(_mutex);
    return hp;
}

int MessageClient::indicatorPriority() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return -1;
    int idx = selectedSlotLocked();
    int priority = idx >= 0 ? _slots[idx].msg.priority : -1;
    xSemaphoreGive(_mutex);
    return priority;
}

bool MessageClient::currentPreview(ChronoMessage& message) const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    bool ok = _previewVisible && _previewSlot >= 0 && copySelectedLocked(message);
    xSemaphoreGive(_mutex);
    return ok;
}

bool MessageClient::showCurrentNow() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    int idx = selectedSlotLocked();
    if (idx >= 0) {
        _manualPreview = true;
        startPreviewLocked(idx, millis());
    }
    xSemaphoreGive(_mutex);
    return idx >= 0;
}

bool MessageClient::showNextUnread() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    int start = _previewVisible ? _previewSlot : -1;
    int next = -1;
    for (int i = 1; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        int idx = (start + i) % CHRONOMSG_MAX_MESSAGES;
        if (_slots[idx].msg.valid && _slots[idx].unread) {
            next = idx;
            break;
        }
    }
    if (next >= 0) {
        _manualPreview = true;
        startPreviewLocked(next, millis());
    }
    xSemaphoreGive(_mutex);
    return next >= 0;
}

bool MessageClient::showPrevUnread() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    int start = _previewVisible ? _previewSlot : 0;
    int prev = -1;
    for (int i = 1; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        int idx = (start - i + CHRONOMSG_MAX_MESSAGES) % CHRONOMSG_MAX_MESSAGES;
        if (_slots[idx].msg.valid && _slots[idx].unread) {
            prev = idx;
            break;
        }
    }
    if (prev >= 0) {
        _manualPreview = true;
        startPreviewLocked(prev, millis());
    }
    xSemaphoreGive(_mutex);
    return prev >= 0;
}

bool MessageClient::hidePreview() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    if (!_previewVisible) { xSemaphoreGive(_mutex); return false; }
    hidePreviewLocked();
    xSemaphoreGive(_mutex);
    return true;
}

bool MessageClient::dismissCurrentOrHide() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    if (!_previewVisible || _previewSlot < 0 || _previewSlot >= CHRONOMSG_MAX_MESSAGES) {
        xSemaphoreGive(_mutex);
        return false;
    }
    MessageSlot& slot = _slots[_previewSlot];
    if (slot.msg.dismissible) {
        String dismissedId = slot.msg.id;
        rememberDismissedLocked(dismissedId);
        queueDismissalLocked(dismissedId);
        slot = MessageSlot();
    }
    hidePreviewLocked();
    xSemaphoreGive(_mutex);
    return true;
}

void MessageClient::queueDismissalLocked(const String& id) {
    if (id.length() == 0) return;
    if (_pendingDismissalCount >= CHRONOMSG_MAX_MESSAGES) return;
    for (uint8_t i = 0; i < _pendingDismissalCount; ++i) {
        if (_pendingDismissalIds[i] == id) return;
    }
    _pendingDismissalIds[_pendingDismissalCount++] = id;
}

bool MessageClient::sendDismissal(const String& id) {
    if (id.length() == 0 || WiFi.status() != WL_CONNECTED) return false;
    String url = String(CHRONOMSG_URL) + "?dismiss=" + id;
    ParsedHttpUrl parsed;
    if (!parseHttpUrl(url.c_str(), parsed)) return false;
    WiFiClient client;
    client.setTimeout(LOCAL_HTTP_CONNECT_TIMEOUT_MS);
    if (!client.connect(parsed.host.c_str(), parsed.port, LOCAL_HTTP_CONNECT_TIMEOUT_MS)) {
        client.stop();
        return false;
    }
    client.print("GET ");
    client.print(parsed.path);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(parsed.host);
    client.print("\r\nConnection: close\r\nUser-Agent: ChronoBell\r\n\r\n");
    uint32_t t0 = millis();
    while (client.connected() && (millis() - t0) < 500UL) {
        while (client.available()) client.read();
        delay(5);
    }
    client.stop();
    return true;
}

void MessageClient::mergeMessages(const ChronoMessage* incoming, uint8_t count) {
    if (!_mutex || !incoming) return;
    time_t nowEpoch = 0;
    bool validTime = currentEpoch(_timeProvider, nowEpoch);
    uint32_t nowMs = millis();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    bool seen[CHRONOMSG_MAX_MESSAGES] = {};

    for (uint8_t d = 0; d < CHRONOMSG_MAX_MESSAGES; ++d) {
        if (!_dismissed[d].valid) continue;
        bool stillPresent = false;
        for (uint8_t i = 0; i < count && i < CHRONOMSG_MAX_MESSAGES; ++i) {
            if (incoming[i].valid && incoming[i].id == _dismissed[d].id) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            _dismissed[d] = DismissedId();
        }
    }

    for (uint8_t i = 0; i < count && i < CHRONOMSG_MAX_MESSAGES; ++i) {
        ChronoMessage msg = incoming[i];
        if (!msg.valid || msg.id.length() == 0 || isDismissedLocked(msg.id)) continue;
        if (validTime && msg.expires > 0 && msg.expires <= (uint32_t)nowEpoch) continue;

        int idx = findSlotByIdLocked(msg.id);
        bool isNew = idx < 0;
        if (idx < 0) {
            for (uint8_t s = 0; s < CHRONOMSG_MAX_MESSAGES; ++s) {
                if (!_slots[s].msg.valid) {
                    idx = s;
                    break;
                }
            }
        }
        if (idx < 0) continue;

        MessageSlot& slot = _slots[idx];
        int oldPriority = slot.msg.priority;
        slot.msg = msg;
        slot.unread = true;
        slot.known = true;
        slot.order = i;
        seen[idx] = true;

        if (isNew) {
            slot.firstPreviewShown = false;
            slot.lastPreviewMs = 0;
            if (msg.priority >= 9) slot.firstPreviewDueMs = nowMs;
            else if (msg.priority >= 7) slot.firstPreviewDueMs = nowMs + 5000UL;
            else if (msg.priority >= 5) slot.firstPreviewDueMs = nowMs + 1000UL;
            else slot.firstPreviewDueMs = 0;
        } else if (msg.priority > oldPriority) {
            slot.firstPreviewShown = false;
            if (msg.priority >= 9) slot.firstPreviewDueMs = nowMs;
            else if (msg.priority >= 7) slot.firstPreviewDueMs = nowMs + 5000UL;
            else if (msg.priority >= 5 && slot.firstPreviewDueMs == 0) slot.firstPreviewDueMs = nowMs + 1000UL;
            else slot.firstPreviewDueMs = 0;
        }
    }

    for (uint8_t i = 0; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        if (_slots[i].msg.valid && !seen[i]) {
            if (_previewSlot == (int)i) hidePreviewLocked();
            _slots[i] = MessageSlot();
        }
    }
    pruneExpiredLocked(nowEpoch, validTime, nowMs);
    xSemaphoreGive(_mutex);
}

void MessageClient::pruneExpiredLocked(time_t nowEpoch, bool timeValid, uint32_t) {
    if (!timeValid) return;
    for (uint8_t i = 0; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        if (_slots[i].msg.valid && _slots[i].msg.expires > 0 &&
            _slots[i].msg.expires <= (uint32_t)nowEpoch) {
            if (_previewSlot == (int)i) hidePreviewLocked();
            _slots[i] = MessageSlot();
        }
    }
}

int MessageClient::selectedSlotLocked() const {
    int best = -1;
    for (uint8_t i = 0; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        const MessageSlot& s = _slots[i];
        if (!s.msg.valid || !s.unread) continue;
        if (best < 0 ||
            s.msg.priority > _slots[best].msg.priority ||
            (s.msg.priority == _slots[best].msg.priority && s.msg.created > _slots[best].msg.created) ||
            (s.msg.priority == _slots[best].msg.priority && s.msg.created == _slots[best].msg.created && s.order < _slots[best].order)) {
            best = i;
        }
    }
    return best;
}

int MessageClient::findSlotByIdLocked(const String& id) const {
    for (uint8_t i = 0; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        if (_slots[i].msg.valid && _slots[i].msg.id == id) return i;
    }
    return -1;
}

bool MessageClient::isDismissedLocked(const String& id) const {
    for (uint8_t i = 0; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        if (_dismissed[i].valid && _dismissed[i].id == id) return true;
    }
    return false;
}

void MessageClient::rememberDismissedLocked(const String& id) {
    if (id.length() == 0) return;
    for (uint8_t i = 0; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        if (!_dismissed[i].valid) {
            _dismissed[i].id = id;
            _dismissed[i].valid = true;
            return;
        }
    }
    for (uint8_t i = 1; i < CHRONOMSG_MAX_MESSAGES; ++i) {
        _dismissed[i - 1] = _dismissed[i];
    }
    _dismissed[CHRONOMSG_MAX_MESSAGES - 1].id = id;
    _dismissed[CHRONOMSG_MAX_MESSAGES - 1].valid = true;
}

bool MessageClient::copySelectedLocked(ChronoMessage& message) const {
    int idx = _previewVisible ? _previewSlot : selectedSlotLocked();
    if (idx < 0 || idx >= CHRONOMSG_MAX_MESSAGES || !_slots[idx].msg.valid) return false;
    message = _slots[idx].msg;
    return true;
}

void MessageClient::startPreviewLocked(int idx, uint32_t nowMs) {
    if (idx < 0 || idx >= CHRONOMSG_MAX_MESSAGES || !_slots[idx].msg.valid) return;
    _previewSlot = idx;
    _previewVisible = true;

    uint32_t previewMs;
    if (_manualPreview) {
        previewMs = MENU_TIMEOUT_SHORT_SECONDS * 1000UL;
    } else {
        previewMs = (uint32_t)clampDuration(_slots[idx].msg.durationSec) * 1000UL;
        MessageLayout layout = layoutMessageText(_slots[idx].msg.title, _slots[idx].msg.body);
        if (layout.kind == MessageLayoutKind::Scroll) {
            int scrollW = 0;
            if (layout.line1.length() > 0) {
                int w = Display::textWidth(layout.line1.c_str(), true, 1, 2);
                if (w > COLS_PER_ROW) scrollW = max(scrollW, w);
            }
            if (layout.line2.length() > 0) {
                int w = Display::textWidth(layout.line2.c_str(), true, 1, 2);
                if (w > COLS_PER_ROW) scrollW = max(scrollW, w);
            }
            if (scrollW > 0) {
                uint32_t cycleMs = (uint32_t)(scrollW + COLS_PER_ROW + CHRONOMSG_SCROLL_REPEAT_GAP_PX)
                                 * CHRONOMSG_SCROLL_STEP_MS;
                previewMs = cycleMs * CHRONOMSG_MIN_SCROLL_CYCLES;
            }
        }
    }
    _previewStartMs = nowMs;
    _previewEndMs = nowMs + previewMs;

    _slots[idx].firstPreviewShown = true;
    _slots[idx].lastPreviewMs = nowMs;
}

void MessageClient::hidePreviewLocked() {
    _manualPreview = false;
    _previewVisible = false;
    _previewSlot = -1;
    _previewEndMs = 0;
}

void MessageClient::taskEntry(void* arg) {
    static_cast<MessageClient*>(arg)->taskLoop();
}

void MessageClient::taskLoop() {
    uint32_t lastPollMs = 0;
    uint32_t lastWifiWaitMs = 0;
    while (!_taskStop) {
        uint32_t now = millis();
        if (WiFi.status() != WL_CONNECTED) {
            if (lastWifiWaitMs == 0 || (now - lastWifiWaitMs) >= 1000UL) {
                lastWifiWaitMs = now;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (lastPollMs == 0 || (now - lastPollMs) >= CHRONOMSG_POLL_INTERVAL_MS) {
            lastPollMs = now;
            while (_pendingDismissalCount > 0) {
                sendDismissal(_pendingDismissalIds[0]);
                for (uint8_t i = 1; i < _pendingDismissalCount; ++i) {
                    _pendingDismissalIds[i - 1] = _pendingDismissalIds[i];
                }
                _pendingDismissalCount--;
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            ChronoMessage parsed[CHRONOMSG_MAX_MESSAGES];
            uint8_t count = 0;
            if (pollOnce(parsed, count)) {
                mergeMessages(parsed, count);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelete(nullptr);
}

bool MessageClient::pollOnce(ChronoMessage* out, uint8_t& count) {
    count = 0;
    if (WiFi.status() != WL_CONNECTED) return false;
    ParsedHttpUrl parsed;
    if (!parseHttpUrl(CHRONOMSG_URL, parsed)) return false;

    WiFiClient client;
    client.setTimeout(LOCAL_HTTP_CONNECT_TIMEOUT_MS);
    uint32_t startMs = millis();
    if (!client.connect(parsed.host.c_str(), parsed.port, LOCAL_HTTP_CONNECT_TIMEOUT_MS)) {
        client.stop();
        return false;
    }
    client.print("GET ");
    client.print(parsed.path);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(parsed.host);
    client.print("\r\nConnection: close\r\nUser-Agent: ChronoBell\r\nAccept: application/json\r\n\r\n");

    String response;
    response.reserve(CHRONOMSG_MAX_RESPONSE_BYTES);
    while ((client.connected() || client.available()) &&
           (millis() - startMs) < LOCAL_HTTP_TOTAL_TIMEOUT_MS) {
        while (client.available()) {
            if (response.length() >= CHRONOMSG_MAX_RESPONSE_BYTES) {
                client.stop();
                return false;
            }
            response += (char)client.read();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    client.stop();
    int bodyStart = response.indexOf("\r\n\r\n");
    if (bodyStart < 0) bodyStart = response.indexOf("\n\n");
    if (bodyStart < 0) return false;
    String headers = response.substring(0, bodyStart);
    if (!headers.startsWith("HTTP/1.") || headers.indexOf(" 200 ") < 0) return false;
    String body = response.substring(bodyStart + (response[bodyStart] == '\r' ? 4 : 2));
    body.trim();
    if (body.length() == 0 || body[0] != '{') return false;

    if (!parseChronoMessagesBody(body, out, count, _timeProvider)) return false;
    LOGF("ChronoMsg: parsed %u message(s)\n", (unsigned)count);
    return true;
}
