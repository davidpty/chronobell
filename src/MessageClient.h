#ifndef MESSAGE_CLIENT_H
#define MESSAGE_CLIENT_H

#include <Arduino.h>
#include <time.h>

#include "Config.h"

#if CHRONOMSG_ENABLED

struct ChronoMessage {
    String id;
    String source;
    String type;
    int priority = 5;
    String title;
    String body;
    uint32_t created = 0;
    uint32_t expires = 0;
    bool repeat = false;
    uint16_t durationSec = CHRONOMSG_DEFAULT_DURATION_SEC;
    uint16_t intervalSec = 0;
    bool indicator = true;
    bool dismissible = true;
    bool autoDismiss = false;
    uint8_t displayMode = 1;
    uint16_t scrollStepMs = 0;
    bool force = false;
    uint8_t bellPattern[8] = {};
    uint8_t bellPatternCount = 0;
    bool valid = false;
};

enum class MessageLayoutKind : uint8_t {
    None = 0,
    OneLine,
    TwoLine,
    Scroll
};

struct MessageLayout {
    MessageLayoutKind kind = MessageLayoutKind::None;
    String line1;
    String line2;
    bool scrollLine1 = false;
    bool scrollLine2 = false;
    uint8_t displayMode = 0;
    uint16_t scrollStepMs = 0;
};

class TimeProvider;

String normalizeMessageText(const String& text);
MessageLayout layoutMessageText(const String& title, const String& body, uint8_t displayMode = 0, uint16_t scrollStepMs = 0);
uint16_t chronoMessageDefaultIntervalSec(int priority);

class MessageClient {
public:
    void begin(TimeProvider* timeProvider);
    void update();
    void stop();

    bool hasUnread() const;
    int unreadCount() const;
    int highestPriority() const;
    int indicatorPriority() const;
    bool currentPreview(ChronoMessage& message) const;
    bool showCurrentNow();
    bool showNextUnread();
    bool showPrevUnread();
    bool dismissCurrentOrHide();
    bool hidePreview();
    bool isPreviewVisible() const { return _previewVisible; }
    uint32_t previewStartMs() const { return _previewStartMs; }

private:
    struct MessageSlot {
        ChronoMessage msg;
        bool unread = false;
        bool known = false;
        uint32_t firstPreviewDueMs = 0;
        uint32_t lastPreviewMs = 0;
        bool firstPreviewShown = false;
        uint8_t order = 0;
    };

    struct DismissedId {
        String id;
        bool valid = false;
    };

    void mergeMessages(const ChronoMessage* incoming, uint8_t count);
    void pruneExpiredLocked(time_t nowEpoch, bool timeValid, uint32_t nowMs);
    int selectedSlotLocked() const;
    int findSlotByIdLocked(const String& id) const;
    bool isDismissedLocked(const String& id) const;
    void rememberDismissedLocked(const String& id);
    bool copySelectedLocked(ChronoMessage& message) const;
    void startPreviewLocked(int idx, uint32_t nowMs);
    void hidePreviewLocked();
    void queueDismissalLocked(const String& id);
    bool sendDismissal(const String& id);
    static void taskEntry(void* arg);
    void taskLoop();
    bool pollOnce(ChronoMessage* out, uint8_t& count);

    TimeProvider* _timeProvider = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    volatile bool _taskStop = false;
    bool _started = false;

    String _pendingDismissalIds[CHRONOMSG_MAX_MESSAGES];
    uint8_t _pendingDismissalCount = 0;

    MessageSlot _slots[CHRONOMSG_MAX_MESSAGES];
    DismissedId _dismissed[CHRONOMSG_MAX_MESSAGES];
    int _previewSlot = -1;
    bool _previewVisible = false;
    bool _manualPreview = false;
    uint32_t _previewStartMs = 0;
    uint32_t _previewEndMs = 0;
};

#endif

#endif
