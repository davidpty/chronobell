#ifndef GUEST_WIFI_CONTROLLER_H
#define GUEST_WIFI_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

#if GUEST_WIFI_ENABLED
class GuestWifiController {
public:
    void begin();
    void stop();

    void tick(int hours, int minutes, int year, int month, int day);

    bool isTextAvailable() const;
    const char* ssid() const { return _ssid; }
    const char* password() const { return _password; }
    bool copyText(char* ssidOut, size_t ssidLen, char* passwordOut, size_t passwordLen) const;
    bool bootFetchDone() const;

    bool isDisabled() const { return _disabled; }

private:
    enum class FetchReason : uint8_t {
        Boot = 0,
        Daily = 1,
        Retry = 2
    };

    bool requestFetch(FetchReason reason, unsigned long nowMs);
    bool fetchBlocking(const char* url, char* ssidOut, size_t ssidLen, char* passwordOut, size_t passwordLen);
    void applyFetchResult(bool ok, FetchReason reason, const char* ssid, const char* password);
    static void taskEntry(void* arg);
    void taskLoop();

    char _ssid[LOCAL_DISPLAY_TEXT_MAX_LEN];
    char _password[LOCAL_DISPLAY_TEXT_MAX_LEN];
    bool _passwordAvailable = false;
    bool _disabled = false;
    bool _bootFetchDone = false;
    int _lastFetchDay = -1;
    unsigned long _lastFetchMs = 0;
    int _fetchFailCount = 0;
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _task = nullptr;
    volatile bool _taskStop = false;
    bool _fetchRequested = false;
    bool _fetchInProgress = false;
    FetchReason _fetchReason = FetchReason::Boot;
};
#endif

#endif
