#include "NightModeController.h"

#include "Config.h"

void NightModeController::begin(NightMode mode) {
    _mode = mode;
    _wakeExpiresMs = 0;
    _suppressed = false;
}

void NightModeController::setMode(NightMode mode) {
    _mode = mode;
}

int8_t NightModeController::tick(const ClockTime& now, int8_t userBrightness) {
    if (userBrightness < 0) userBrightness = 0;
    if (userBrightness > 15) userBrightness = 15;

    bool awake = (_wakeExpiresMs != 0) && ((long)(millis() - _wakeExpiresMs) < 0);
    if (_wakeExpiresMs != 0 && !awake) {
        _wakeExpiresMs = 0;
    }

    bool darkMode = (_mode == NightMode::Dark || _mode == NightMode::DarkMute);
    bool dimMode = (_mode == NightMode::Dim || _mode == NightMode::DimMute);
    bool inDim = inDimWindow(now.hours);
    bool inDark = inDarkWindow(now.hours);

    if (darkMode && inDark && !awake) {
        _suppressed = true;
        _userBrightnessActive = false;
        return 0;
    }

    if ((darkMode && (inDim || awake)) || (dimMode && inDim)) {
        _suppressed = false;
        _userBrightnessActive = false;
        return 0;
    }

    _suppressed = false;
    _userBrightnessActive = true;
    return userBrightness;
}

bool NightModeController::consumeWakePress() {
    if (!_suppressed) {
        return false;
    }
    if (_wakeExpiresMs != 0) {
        // Already awake; let the press pass through.
        return false;
    }
    _wakeExpiresMs = millis() + NIGHT_DISPLAY_WAKE_MINUTES * 60000UL;
    return true;
}

void NightModeController::noteUserActivity() {
    if (_wakeExpiresMs == 0) {
        return;
    }
    _wakeExpiresMs = millis() + NIGHT_DISPLAY_WAKE_MINUTES * 60000UL;
}

bool NightModeController::shouldMuteAutomaticBell(const ClockTime& now) const {
    if (_mode != NightMode::DimMute &&
        _mode != NightMode::DarkMute &&
        _mode != NightMode::Mute) {
        return false;
    }
    return inMuteWindow(now.hours);
}

bool NightModeController::inDimWindow(int h) {
    return (h >= NIGHT_DIM_START_HOUR) || (h < NIGHT_DIM_END_HOUR);
}

bool NightModeController::inDarkWindow(int h) {
    return (h >= NIGHT_DARK_START_HOUR) || (h < NIGHT_DARK_END_HOUR);
}

bool NightModeController::inMuteWindow(int h) {
    return (h >= NIGHT_MUTE_START_HOUR) || (h < NIGHT_MUTE_END_HOUR);
}
