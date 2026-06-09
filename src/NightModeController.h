#ifndef NIGHT_MODE_CONTROLLER_H
#define NIGHT_MODE_CONTROLLER_H

#include <Arduino.h>

#include "AppSettings.h"
#include "RtcClock.h"

class NightModeController {
public:
    void begin(NightMode mode);

    void setMode(NightMode mode);

    // Called once per render tick. Updates internal time-window state.
    // Returns the brightness the display should run at right now:
    //   0          = night dim level
    //   userB      = normal user brightness
    // The user brightness is supplied by the caller (Display owns it).
    int8_t tick(const ClockTime& now, int8_t userBrightness);

    // True only when dark mode is in its off window and the first touch
    // should be consumed as a wake. Dim brightness does not count.
    bool isDisplaySuppressed() const { return _suppressed; }

    // First-press-while-suppressed helper. If the display is suppressed and
    // no wake is currently active, opens a wake window and returns true.
    // Otherwise returns false and the caller should act on the press.
    bool consumeWakePress();

    // Extend the current wake window (e.g. on any touch activity).
    void noteUserActivity();

    // True if the automatic hour/half-hour bell should be suppressed at
    // the given time-of-day (countdown alerts and forced bells are not
    // affected; they go through queueForced* paths).
    bool shouldMuteAutomaticBell(const ClockTime& now) const;

private:
    static bool inDimWindow(int h);
    static bool inDarkWindow(int h);
    static bool inMuteWindow(int h);

    NightMode _mode = NightMode::Off;

    // 0 = no active wake, otherwise the millis() deadline when the wake
    // window expires. We use unsigned long (matching millis()).
    unsigned long _wakeExpiresMs = 0;

    // Latest tick result; consulted by isDisplaySuppressed() / consumeWakePress().
    bool _suppressed = false;
    bool _userBrightnessActive = true;
};

#endif // NIGHT_MODE_CONTROLLER_H
