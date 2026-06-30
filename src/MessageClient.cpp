#include "MessageClient.h"

#if CHRONOSERVE_ENABLED

#include <WiFi.h>
#include <WiFiClient.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "Display.h"
#include "TimeProvider.h"

static String combineMessageText(const String& title, const String& body);

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

bool lowerEquals(const String& a, const char* b) {
    if (!b || a.length() != strlen(b)) return false;
    for (size_t i = 0; i < a.length(); ++i) {
        if (tolower((unsigned char)a[i]) != b[i]) return false;
    }
    return true;
}

MessagePolicyKind policyKindFromString(const String& value) {
    if (lowerEquals(value, "repeat")) return MessagePolicyKind::Repeat;
    if (lowerEquals(value, "inbox")) return MessagePolicyKind::Inbox;
    return MessagePolicyKind::Temporary;
}

uint8_t displayModeFromSize(const String& value) {
    if (lowerEquals(value, "small")) return 0;
    if (lowerEquals(value, "big")) return 2;
    return 1;
}

bool currentEpoch(TimeProvider* provider, time_t& epoch) {
    return provider && provider->currentEpoch(epoch);
}

uint32_t chronoScrollPassMsForWidth(int textWidthPx) {
    if (textWidthPx <= 0) return 0;
    uint32_t finalFrame = (uint32_t)COLS_PER_ROW + (uint32_t)textWidthPx;
    return (finalFrame + 1UL) * (uint32_t)CHRONOSERVE_SCROLL_STEP_MS;
}

int chronoScrollTextWidth(const String& text, uint8_t mode) {
    int width = 0;
    bool inWord = false;
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == ' ') {
            if (inWord) {
                width += CHRONOSERVE_SCROLL_WORD_GAP_PX;
                inWord = false;
            }
            continue;
        }
        if (inWord) {
            width += 1;
        }
        width += (mode == 2) ? Display::charWidthBig(c) : Display::charWidth(c, mode == 0);
        inWord = true;
    }
    if (width > 0) {
        width += CHRONOSERVE_SCROLL_EXIT_PAD_PX;
    }
    return width;
}

static bool chronoTextFitsOneLine(const String& text, uint8_t mode) {
    int width = (mode == 2)
        ? Display::textWidthBig(text.c_str(), 1, 2)
        : Display::textWidth(text.c_str(), mode == 0, 1, 2);
    return width <= COLS_PER_ROW;
}

static bool chronoTextFitsTwoSmallLines(const String& text, String& line1, String& line2) {
    line1 = String();
    line2 = String();
    if (text.length() == 0) return false;

    int bestScore = INT_MAX;
    bool found = false;
    String bestLine1;
    String bestLine2;

    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] != ' ') continue;

        String a = text.substring(0, i);
        String b = text.substring(i + 1);
        a.trim();
        b.trim();
        if (a.length() == 0 || b.length() == 0) continue;

        int w1 = Display::textWidth(a.c_str(), true, 1, 2);
        int w2 = Display::textWidth(b.c_str(), true, 1, 2);
        if (w1 > COLS_PER_ROW || w2 > COLS_PER_ROW) continue;

        int score = abs(w1 - w2);
        if (!found || score < bestScore || (score == bestScore && (w1 + w2) > (Display::textWidth(bestLine1.c_str(), true, 1, 2) + Display::textWidth(bestLine2.c_str(), true, 1, 2)))) {
            bestScore = score;
            found = true;
            bestLine1 = a;
            bestLine2 = b;
        }
    }

    if (!found) return false;
    line1 = bestLine1;
    line2 = bestLine2;
    return true;
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
    msg.id = jsonStringField(obj, "id", "", CHRONOSERVE_MAX_ID_LEN);
    msg.id.trim();
    if (msg.id.length() == 0) return false;

    msg.source = jsonStringField(obj, "source", "", LOCAL_DISPLAY_TEXT_MAX_LEN);
    msg.type = jsonStringField(obj, "type", "", LOCAL_DISPLAY_TEXT_MAX_LEN);
    msg.revision = jsonUIntField(obj, "revision", 0);
    msg.priority = constrain(jsonIntField(obj, "priority", 5), 0, 9);
    msg.title = normalizeMessageText(jsonStringField(obj, "title", "", CHRONOSERVE_MAX_TEXT_LEN));
    msg.body = normalizeMessageText(jsonStringField(obj, "body", "", CHRONOSERVE_MAX_TEXT_LEN));
    if (msg.title.length() == 0 && msg.body.length() == 0) return false;
    msg.created = jsonUIntField(obj, "created", 0);
    msg.expires = jsonUIntField(obj, "expires", 0);
    time_t nowEpoch = 0;
    if (currentEpoch(timeProvider, nowEpoch) && msg.expires > 0 && msg.expires <= (uint32_t)nowEpoch) {
        return false;
    }

    String policy = jsonObjectField(obj, "policy");
    msg.policy = policyKindFromString(jsonStringField(policy, "kind", "temporary", LOCAL_DISPLAY_TEXT_MAX_LEN));
    msg.indicator = jsonBoolField(policy, "indicator", msg.policy == MessagePolicyKind::Inbox);
    msg.dismissible = jsonBoolField(policy, "dismissible", msg.policy == MessagePolicyKind::Inbox);
    msg.repeatSec = (uint16_t)constrain(jsonIntField(policy, "repeatSec", 0), 0, 3600);
    if (msg.policy == MessagePolicyKind::Repeat && msg.repeatSec == 0) {
        msg.repeatSec = CHRONOSERVE_SONG_REPEAT_SEC;
    }

    String display = jsonObjectField(obj, "display");
    msg.displayMode = displayModeFromSize(jsonStringField(display, "size", "medium", LOCAL_DISPLAY_TEXT_MAX_LEN));
    msg.force = jsonBoolField(display, "force", false);
    msg.renderText = combineMessageText(msg.title, msg.body);

    int bellPos = findJsonKeyValueStart(display, "bell");
    if (bellPos >= 0 && bellPos < (int)display.length()) {
        if (display[bellPos] == '[') {
            int arrayEnd = findMatchingJsonChar(display, bellPos, '[', ']');
            if (arrayEnd > bellPos) {
                int p = bellPos + 1;
                while (p < arrayEnd && msg.bellPatternCount < 8) {
                    p = skipJsonWhitespace(display, p);
                    if (p >= arrayEnd || display[p] == ']') break;
                    if (display[p] == ',') { p++; continue; }
                    int val = display.substring(p).toInt();
                    if (val > 0 && val <= 20) {
                        msg.bellPattern[msg.bellPatternCount++] = (uint8_t)val;
                    }
                    while (p < arrayEnd && display[p] >= '0' && display[p] <= '9') p++;
                }
            }
        } else {
            int val = display.substring(bellPos).toInt();
            if (val > 0 && val <= 20) {
                msg.bellPattern[0] = (uint8_t)val;
                msg.bellPatternCount = 1;
            }
        }
    }

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
    while (pos < arrayEnd && count < CHRONOSERVE_MAX_MESSAGES) {
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

String normalizeMessageText(const String& text) {
    String out;
    out.reserve(CHRONOSERVE_MAX_TEXT_LEN);
    bool pendingSpace = false;

    for (size_t i = 0; i < text.length() && out.length() < CHRONOSERVE_MAX_TEXT_LEN; ++i) {
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
        if (pendingSpace && out.length() < CHRONOSERVE_MAX_TEXT_LEN) {
            out += ' ';
        }
        pendingSpace = false;
        out += mapped;
    }

    out.trim();
    return out;
}

static String combineMessageText(const String& title, const String& body) {
    if (title.length() == 0) return body;
    if (body.length() == 0) return title;
    String combined = title;
    combined += ' ';
    combined += body;
    return combined;
}

MessageLayout layoutMessageText(const String& title, const String& body, uint8_t displayMode) {
    MessageLayout layout;
    layout.displayMode = displayMode;
    String combined = combineMessageText(title, body);
    if (combined.length() == 0) {
        return layout;
    }
    if (chronoTextFitsOneLine(combined, displayMode)) {
        layout.kind = MessageLayoutKind::CenteredOneLine;
        layout.line1 = combined;
        return layout;
    }
    if (displayMode == 0) {
        String line1;
        String line2;
        if (chronoTextFitsTwoSmallLines(combined, line1, line2)) {
            layout.kind = MessageLayoutKind::CenteredTwoLine;
            layout.line1 = line1;
            layout.line2 = line2;
            return layout;
        }
    }
    layout.kind = MessageLayoutKind::Scroll;
    layout.line1 = combined;
    return layout;
}

void MessageClient::begin(TimeProvider* timeProvider) {
    _timeProvider = timeProvider;
    if (!CHRONOSERVE_ENABLED || _started) return;
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOGLN("ChronoServe: mutex create failed");
        return;
    }
    _taskStop = false;
    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "chronoserve", CHRONOSERVE_TASK_STACK_WORDS,
                                            this, LOCAL_NETWORK_TASK_PRIORITY, &_task, 0);
    if (ok != pdPASS) {
        LOGLN("ChronoServe: task create failed");
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
    if (_previewVisible &&
        ((_previewWaitForRenderFinish && _previewRenderFinished) ||
         (int32_t)(nowMs - _previewSafetyEndMs) >= 0 ||
         ((int32_t)(nowMs - _previewEndMs) >= 0 && !_previewWaitForRenderFinish))) {
        hidePreviewLocked();
    }
    if (!_previewVisible) {
        int bestIdx = -1;
        for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
            const MessageSlot& s = _slots[i];
            if (!s.msg.valid || !s.unread) continue;
            bool due = false;
            if (!s.firstPreviewShown) {
                due = s.firstPreviewDueMs > 0 && (int32_t)(nowMs - s.firstPreviewDueMs) >= 0;
            } else if (s.msg.policy == MessagePolicyKind::Repeat) {
                uint32_t repeatMs = (uint32_t)(s.msg.repeatSec > 0 ? s.msg.repeatSec : CHRONOSERVE_SONG_REPEAT_SEC) * 1000UL;
                uint32_t basis = s.lastPreviewEndMs > 0 ? s.lastPreviewEndMs : s.lastPreviewMs;
                due = basis > 0 && (int32_t)(nowMs - (basis + repeatMs)) >= 0;
            }
            if (!due) continue;
            if (bestIdx < 0 ||
                s.msg.priority > _slots[bestIdx].msg.priority ||
                (s.msg.priority == _slots[bestIdx].msg.priority && s.msg.created > _slots[bestIdx].msg.created) ||
                (s.msg.priority == _slots[bestIdx].msg.priority && s.msg.created == _slots[bestIdx].msg.created && s.order < _slots[bestIdx].order)) {
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) startPreviewLocked(bestIdx, nowMs);
    }
    xSemaphoreGive(_mutex);
}

bool MessageClient::hasUnread() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    bool found = false;
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        const MessageSlot& s = _slots[i];
        if (s.msg.valid && s.unread && s.msg.policy == MessagePolicyKind::Inbox && s.msg.indicator) { found = true; break; }
    }
    xSemaphoreGive(_mutex);
    return found;
}

int MessageClient::unreadCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return 0;
    int count = 0;
    for (int i = 0; i < CHRONOSERVE_MAX_MESSAGES; i++) {
        if (_slots[i].known && _slots[i].unread &&
            _slots[i].msg.policy == MessagePolicyKind::Inbox && _slots[i].msg.indicator) count++;
    }
    xSemaphoreGive(_mutex);
    return count;
}

int MessageClient::highestPriority() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return -1;
    int hp = -1;
    for (int i = 0; i < CHRONOSERVE_MAX_MESSAGES; i++) {
        if (_slots[i].known && _slots[i].unread &&
            _slots[i].msg.policy == MessagePolicyKind::Inbox && _slots[i].msg.indicator) {
            if (_slots[i].msg.priority > hp) hp = _slots[i].msg.priority;
        }
    }
    xSemaphoreGive(_mutex);
    return hp;
}

int MessageClient::indicatorPriority() const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return -1;
    int hp = -1;
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        const MessageSlot& s = _slots[i];
        if (s.msg.valid && s.unread && s.msg.policy == MessagePolicyKind::Inbox && s.msg.indicator) {
            if (s.msg.priority > hp) hp = s.msg.priority;
        }
    }
    xSemaphoreGive(_mutex);
    return hp;
}

bool MessageClient::currentPreview(ChronoMessage& message) const {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    bool ok = _previewVisible && _previewSlot >= 0 && copySelectedLocked(message);
    xSemaphoreGive(_mutex);
    return ok;
}

bool MessageClient::showCurrentNow() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    int idx = selectedSlotLocked(true);
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
    for (int i = 1; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        int idx = (start + i) % CHRONOSERVE_MAX_MESSAGES;
        if (_slots[idx].msg.valid && _slots[idx].unread &&
            _slots[idx].msg.policy == MessagePolicyKind::Inbox) {
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
    for (int i = 1; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        int idx = (start - i + CHRONOSERVE_MAX_MESSAGES) % CHRONOSERVE_MAX_MESSAGES;
        if (_slots[idx].msg.valid && _slots[idx].unread &&
            _slots[idx].msg.policy == MessagePolicyKind::Inbox) {
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

void MessageClient::noteCurrentPreviewRendered(bool finished) {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return;
    if (_previewVisible) {
        _previewRenderFinished = finished;
    }
    xSemaphoreGive(_mutex);
}

bool MessageClient::dismissCurrentOrHide() {
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return false;
    if (!_previewVisible || _previewSlot < 0 || _previewSlot >= CHRONOSERVE_MAX_MESSAGES) {
        xSemaphoreGive(_mutex);
        return false;
    }
    int start = _previewSlot;
    MessageSlot& slot = _slots[start];
    if (slot.msg.policy == MessagePolicyKind::Inbox && slot.msg.dismissible) {
        String dismissedId = slot.msg.id;
        rememberDismissedLocked(dismissedId);
        queueDismissalLocked(dismissedId);
        slot = MessageSlot();

        int next = -1;
        for (int i = 1; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
            int idx = (start + i) % CHRONOSERVE_MAX_MESSAGES;
            if (_slots[idx].msg.valid && _slots[idx].unread &&
                _slots[idx].msg.policy == MessagePolicyKind::Inbox) {
                next = idx;
                break;
            }
        }
        if (next >= 0) {
            _manualPreview = true;
            startPreviewLocked(next, millis());
            xSemaphoreGive(_mutex);
            return true;
        }
    }
    hidePreviewLocked();
    xSemaphoreGive(_mutex);
    return true;
}

void MessageClient::queueDismissalLocked(const String& id) {
    if (id.length() == 0) return;
    if (_pendingDismissalCount >= CHRONOSERVE_MAX_MESSAGES) return;
    for (uint8_t i = 0; i < _pendingDismissalCount; ++i) {
        if (_pendingDismissalIds[i] == id) return;
    }
    _pendingDismissalIds[_pendingDismissalCount++] = id;
}

bool MessageClient::sendDismissal(const String& id) {
    if (id.length() == 0 || WiFi.status() != WL_CONNECTED) return false;
    String url = String(CHRONOSERVE_URL) + "?msg&dismiss=" + id;
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
    bool seen[CHRONOSERVE_MAX_MESSAGES] = {};

    for (uint8_t d = 0; d < CHRONOSERVE_MAX_MESSAGES; ++d) {
        if (!_dismissed[d].valid) continue;
        bool stillPresent = false;
        for (uint8_t i = 0; i < count && i < CHRONOSERVE_MAX_MESSAGES; ++i) {
            if (incoming[i].valid && incoming[i].id == _dismissed[d].id) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            _dismissed[d] = DismissedId();
        }
    }

    for (uint8_t i = 0; i < count && i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        ChronoMessage msg = incoming[i];
        if (!msg.valid || msg.id.length() == 0 || isDismissedLocked(msg.id)) continue;
        if (validTime && msg.expires > 0 && msg.expires <= (uint32_t)nowEpoch) continue;

        int idx = findSlotByIdLocked(msg.id);
        bool isNew = idx < 0;
        if (idx < 0) {
            for (uint8_t s = 0; s < CHRONOSERVE_MAX_MESSAGES; ++s) {
                if (!_slots[s].msg.valid) {
                    idx = s;
                    break;
                }
            }
        }
        if (idx < 0) continue;

        MessageSlot& slot = _slots[idx];
        int oldPriority = slot.msg.priority;
        uint32_t oldRevision = slot.msg.revision;
        slot.msg = msg;
        slot.unread = true;
        slot.known = true;
        slot.order = i;
        seen[idx] = true;

        if (isNew || msg.revision != oldRevision) {
            slot.firstPreviewShown = false;
            slot.lastPreviewMs = 0;
            slot.lastPreviewEndMs = 0;
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

    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
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
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        if (_slots[i].msg.valid && _slots[i].msg.expires > 0 &&
            _slots[i].msg.expires <= (uint32_t)nowEpoch) {
            if (_previewSlot == (int)i) hidePreviewLocked();
            _slots[i] = MessageSlot();
        }
    }
}

int MessageClient::selectedSlotLocked(bool inboxOnly) const {
    int best = -1;
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        const MessageSlot& s = _slots[i];
        if (!s.msg.valid || !s.unread) continue;
        if (inboxOnly && s.msg.policy != MessagePolicyKind::Inbox) continue;
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
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        if (_slots[i].msg.valid && _slots[i].msg.id == id) return i;
    }
    return -1;
}

bool MessageClient::isDismissedLocked(const String& id) const {
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        if (_dismissed[i].valid && _dismissed[i].id == id) return true;
    }
    return false;
}

void MessageClient::rememberDismissedLocked(const String& id) {
    if (id.length() == 0) return;
    for (uint8_t i = 0; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        if (!_dismissed[i].valid) {
            _dismissed[i].id = id;
            _dismissed[i].valid = true;
            return;
        }
    }
    for (uint8_t i = 1; i < CHRONOSERVE_MAX_MESSAGES; ++i) {
        _dismissed[i - 1] = _dismissed[i];
    }
    _dismissed[CHRONOSERVE_MAX_MESSAGES - 1].id = id;
    _dismissed[CHRONOSERVE_MAX_MESSAGES - 1].valid = true;
}

bool MessageClient::copySelectedLocked(ChronoMessage& message) const {
    int idx = _previewVisible ? _previewSlot : selectedSlotLocked(true);
    if (idx < 0 || idx >= CHRONOSERVE_MAX_MESSAGES || !_slots[idx].msg.valid) return false;
    message = _slots[idx].msg;
    if (message.renderText.length() == 0) {
        message.renderText = combineMessageText(message.title, message.body);
    }
    return true;
}

void MessageClient::startPreviewLocked(int idx, uint32_t nowMs) {
    if (idx < 0 || idx >= CHRONOSERVE_MAX_MESSAGES || !_slots[idx].msg.valid) return;
    _previewSlot = idx;
    _previewVisible = true;

    const ChronoMessage& msg = _slots[idx].msg;
    uint32_t previewMs = _manualPreview ? MENU_TIMEOUT_SHORT_SECONDS * 1000UL
                                        : (uint32_t)CHRONOSERVE_MIN_DURATION_SEC * 1000UL;

    MessageLayout layout = layoutMessageText(msg.renderText.length() > 0 ? msg.renderText : msg.title,
                                             msg.renderText.length() > 0 ? String() : msg.body,
                                             msg.displayMode);
    _previewWaitForRenderFinish = layout.kind == MessageLayoutKind::Scroll && layout.line1.length() > 0;
    _previewRenderFinished = !_previewWaitForRenderFinish;
    if (layout.kind == MessageLayoutKind::Scroll && layout.line1.length() > 0) {
        int scrollW = chronoScrollTextWidth(layout.line1, layout.displayMode);
        if (scrollW > 0) {
            uint32_t passMs = chronoScrollPassMsForWidth(scrollW);
            uint32_t minScrollMs = passMs * (uint32_t)CHRONOSERVE_MIN_SCROLL_CYCLES;
            previewMs = max(previewMs, minScrollMs);
        }
    }

    _previewStartMs = nowMs;
    _previewEndMs = nowMs + previewMs;
    _previewSafetyEndMs = nowMs + previewMs + 5000UL;

    _slots[idx].firstPreviewShown = true;
    _slots[idx].lastPreviewMs = nowMs;
    _slots[idx].lastPreviewEndMs = _previewEndMs;
}

void MessageClient::hidePreviewLocked() {
    if (_previewSlot >= 0 && _previewSlot < CHRONOSERVE_MAX_MESSAGES && _slots[_previewSlot].msg.valid) {
        _slots[_previewSlot].lastPreviewEndMs = millis();
    }
    _manualPreview = false;
    _previewWaitForRenderFinish = false;
    _previewRenderFinished = true;
    _previewVisible = false;
    _previewSlot = -1;
    _previewEndMs = 0;
    _previewSafetyEndMs = 0;
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
        if (lastPollMs == 0 || (now - lastPollMs) >= (uint32_t)CHRONOSERVE_POLL_INTERVAL_SEC * 1000UL) {
            lastPollMs = now;
            while (true) {
                String dismissId;
                if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    if (_pendingDismissalCount > 0) dismissId = _pendingDismissalIds[0];
                    xSemaphoreGive(_mutex);
                }
                if (dismissId.length() == 0) break;
                if (!sendDismissal(dismissId)) break;
                if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    if (_pendingDismissalCount > 0 && _pendingDismissalIds[0] == dismissId) {
                        for (uint8_t i = 1; i < _pendingDismissalCount; ++i) {
                            _pendingDismissalIds[i - 1] = _pendingDismissalIds[i];
                        }
                        _pendingDismissalIds[_pendingDismissalCount - 1] = String();
                        _pendingDismissalCount--;
                    }
                    xSemaphoreGive(_mutex);
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            ChronoMessage parsed[CHRONOSERVE_MAX_MESSAGES];
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
    String url = String(CHRONOSERVE_URL) + "?msg";
    ParsedHttpUrl parsed;
    if (!parseHttpUrl(url.c_str(), parsed)) return false;

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
    response.reserve(CHRONOSERVE_MAX_RESPONSE_BYTES);
    while ((client.connected() || client.available()) &&
           (millis() - startMs) < LOCAL_HTTP_TOTAL_TIMEOUT_MS) {
        while (client.available()) {
            if (response.length() >= CHRONOSERVE_MAX_RESPONSE_BYTES) {
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
    LOGF("ChronoServe: parsed %u message(s)\n", (unsigned)count);
    return true;
}

#endif
