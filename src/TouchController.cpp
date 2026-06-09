#include "TouchController.h"

#include <Wire.h>
#include "Config.h"

bool TouchController::begin() {
    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) {
        _available = false;
        return false;
    }

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0xFD);
    if (Wire.endTransmission(false) != 0) {
        _available = false;
        return false;
    }

    Wire.requestFrom(CAP1188_I2C_ADDRESS, 1);
    if (Wire.available()) {
        uint8_t productId = Wire.read();
        LOG("CAP1188 Product ID: 0x");
        LOGLN(productId, HEX);
    }

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x72);
    Wire.write(CAP1188_ENABLED_INPUTS);
    Wire.endTransmission();

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x74);
    Wire.write(CAP1188_ENABLED_INPUTS);
    Wire.endTransmission();

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x21);
    Wire.write(CAP1188_ENABLED_INPUTS);
    Wire.endTransmission();

    LOGLN("CAP1188 inputs 1, 4, and 8 enabled");

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x2A);
    Wire.write(0x00);
    Wire.endTransmission();

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x1F);
    Wire.write(((CAP1188_DELTA_SENSE & 0x07) << 4) | 0x0F);
    Wire.endTransmission();

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x30);
    for (int i = 0; i < CAP1188_NUM_TOUCHES; i++) {
        uint8_t inputMask = (1 << i);
        Wire.write((CAP1188_ENABLED_INPUTS & inputMask) ? CAP1188_TOUCH_THRESHOLD : 0xFF);
    }
    Wire.endTransmission();

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x26);
    Wire.write(CAP1188_ENABLED_INPUTS);
    Wire.endTransmission();
    delay(250);

    LOGLN("CAP1188 sensitivity configured");
    _available = true;
    return true;
}

void TouchController::setHandler(uint8_t pad, const TouchPadConfig& config) {
    if (pad < 1 || pad > CAP1188_NUM_TOUCHES) return;
    _configs[pad - 1] = config;
}

void TouchController::setPadRepeat(uint8_t pad, void (*onRepeat)(uint8_t), uint32_t initialDelayMs, uint32_t rateMs) {
    if (pad < 1 || pad > CAP1188_NUM_TOUCHES) return;
    _configs[pad - 1].onRepeat = onRepeat;
    _configs[pad - 1].repeatInitialDelayMs = initialDelayMs;
    _configs[pad - 1].repeatRateMs = rateMs;
}

void TouchController::update() {
    if (!_available) return;

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x03);
    if (Wire.endTransmission(false) != 0) {
        _available = false;
        LOGLN("CAP1188 communication error");
        return;
    }

    Wire.requestFrom(CAP1188_I2C_ADDRESS, 1);
    if (!Wire.available()) return;

    uint8_t status = Wire.read() & CAP1188_ENABLED_INPUTS;

    Wire.beginTransmission(CAP1188_I2C_ADDRESS);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();

    uint32_t now = millis();
    if (_firstRead) {
        _prevRawStatus = status;
        _prevStatus = 0;
        _touchStatus = 0;
        _pressEdgeMask = 0;
        _releaseEdgeMask = 0;
        for (uint8_t i = 0; i < CAP1188_NUM_TOUCHES; i++) {
            _rawChangeMs[i] = now;
        }
        _firstRead = false;
        return;
    }

    for (uint8_t i = 0; i < CAP1188_NUM_TOUCHES; i++) {
        uint8_t mask = (1 << i);
        bool rawPressed = (status & mask) != 0;
        bool rawWasPressed = (_prevRawStatus & mask) != 0;
        bool pressed = (_touchStatus & mask) != 0;
        bool wasPressed = (_prevStatus & mask) != 0;

        if (rawPressed != rawWasPressed) {
            _rawChangeMs[i] = now;
        }

        uint32_t requiredDebounceMs = rawPressed ? CAP1188_PRESS_DEBOUNCE_MS : CAP1188_RELEASE_DEBOUNCE_MS;
        if (rawPressed != pressed && (now - _rawChangeMs[i] >= requiredDebounceMs)) {
            if (rawPressed) {
                _touchStatus |= mask;
            } else {
                _touchStatus &= ~mask;
            }
            pressed = rawPressed;
        }

        if (pressed && !wasPressed) {
            _pressStartMs[i] = now;
            _holdFired[i] = false;
            _repeatActive[i] = false;
            _lastRepeatMs[i] = 0;
            _pressEdgeMask |= mask;
            _releaseEdgeMask &= ~mask;
            if (_configs[i].onPress) {
                _configs[i].onPress(i + 1);
            }
        } else if (!pressed && wasPressed) {
            _releaseEdgeMask |= mask;
            _pressEdgeMask &= ~mask;
            _repeatActive[i] = false;
            uint32_t pressDuration = now - _pressStartMs[i];
            if (!_holdFired[i] &&
                pressDuration >= CAP1188_MIN_ACTION_TOUCH_MS &&
                _configs[i].onRelease) {
                _configs[i].onRelease(i + 1);
            }
        } else if (pressed && wasPressed && !_holdFired[i] &&
                   _configs[i].onHold &&
                   _configs[i].holdMs > 0 &&
                   (now - _pressStartMs[i] >= _configs[i].holdMs)) {
            _holdFired[i] = true;
            _configs[i].onHold(i + 1);
        }

        if (pressed && wasPressed &&
            _configs[i].onRepeat &&
            _configs[i].repeatInitialDelayMs > 0 &&
            _configs[i].repeatRateMs > 0 &&
            (now - _pressStartMs[i] >= _configs[i].repeatInitialDelayMs)) {
            if (!_repeatActive[i]) {
                _repeatActive[i] = true;
                _lastRepeatMs[i] = now;
                _configs[i].onRepeat(i + 1);
            } else if (now - _lastRepeatMs[i] >= _configs[i].repeatRateMs) {
                _lastRepeatMs[i] = now;
                _configs[i].onRepeat(i + 1);
            }
        }
    }

    _prevRawStatus = status;
    _prevStatus = _touchStatus;
}

bool TouchController::isPressed(uint8_t pad) const {
    if (pad < 1 || pad > CAP1188_NUM_TOUCHES) return false;
    return (_touchStatus & (1 << (pad - 1))) != 0;
}

uint32_t TouchController::heldMs(uint8_t pad) const {
    if (pad < 1 || pad > CAP1188_NUM_TOUCHES || !isPressed(pad)) return 0;
    return millis() - _pressStartMs[pad - 1];
}


