#ifndef GUEST_WIFI_CONTROLLER_H
#define GUEST_WIFI_CONTROLLER_H

#include <Arduino.h>
#include "Config.h"

class GuestWifiController {
public:
    void begin();

    bool fetch(const char* url);
    void tick(int hours, int minutes, int year, int month, int day);

    bool isTextAvailable() const { return _passwordAvailable && !_disabled; }
    const char* ssid() const { return _ssid; }
    const char* password() const { return _password; }

    bool isDisabled() const { return _disabled; }

private:
    char _ssid[GUEST_WIFI_SSID_MAX_LEN];
    char _password[GUEST_WIFI_TEXT_MAX_LEN];
    bool _passwordAvailable = false;
    bool _disabled = false;
    bool _bootFetchDone = false;
    int _lastFetchDay = -1;
    unsigned long _lastFetchMs = 0;
    int _fetchFailCount = 0;
};

#endif
