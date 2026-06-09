#include "BellController.h"

#include "Config.h"

void BellController::begin() {
    pinMode(BELL_PIN, OUTPUT);
    digitalWrite(BELL_PIN, LOW);
}

void BellController::update(const ClockTime& currentTime, bool timeValid, BellMode mode,
                            bool countdownExpired, bool muteAutomatic) {
    if (!countdownExpired && !muteAutomatic && mode != BellMode::Off && !_sequenceActive && timeValid) {
        if ((currentTime.minutes == 0 || currentTime.minutes == 30) && currentTime.seconds <= 1) {
            int eventKey = (currentTime.hours * 60) + currentTime.minutes;
            if (eventKey != _lastEventKey) {
                _lastEventKey = eventKey;
                bool groupedPairs = false;
                uint8_t count = computeStrikesForEvent(mode, currentTime.hours, currentTime.minutes, groupedPairs);
                if (count > 0) {
                    queue(count, groupedPairs ? 2 : 0, false);
                }
            }
        }
    }

    updateSequence();
    updatePulse();
}

void BellController::preview(BellMode mode, const ClockTime& currentTime, bool timeValid) {
    stop();

    if (mode == BellMode::Off || !timeValid) {
        return;
    }

    int eventH;
    int eventM;
    if (!computeMostRecentEventTime(mode, currentTime.hours, currentTime.minutes, eventH, eventM)) {
        return;
    }

    bool groupedPairs = false;
    uint8_t count = computeStrikesForEvent(mode, eventH, eventM, groupedPairs);
    if (count > 0) {
        queuePreview(count, groupedPairs ? 2 : 0);
    }
}

void BellController::queueForced(uint8_t count) {
    queue(count, 0, true);
}

void BellController::queueForcedPairs(uint8_t count) {
    queue(count, 2, true);
}

void BellController::queueForcedGrouped(uint8_t groupCount, uint8_t groupSize) {
    if (groupCount == 0 || groupSize == 0) {
        return;
    }

    queue((uint8_t)(groupCount * groupSize), groupSize, true);
}

void BellController::stop() {
    _sequenceActive = false;
    _sequenceRemaining = 0;
    _nextStrikeMs = 0;
    if (_pulseActive) {
        digitalWrite(BELL_PIN, LOW);
        _pulseActive = false;
    }
}

bool BellController::isBusy() const {
    return _sequenceActive || _pulseActive;
}

uint8_t BellController::hourCount12(int hours) {
    uint8_t count = hours % 12;
    return count == 0 ? 12 : count;
}

uint8_t BellController::shipBellCount(int hours, int minutes) {
    uint8_t count = ((hours % 4) * 2) + (minutes == 30 ? 1 : 0);
    return count == 0 ? 8 : count;
}

uint8_t BellController::computeStrikesForEvent(BellMode mode, int eventH, int eventM, bool& groupedPairs) {
    groupedPairs = false;

    if (mode == BellMode::Off) {
        return 0;
    }
    if (mode == BellMode::SingleHour) {
        return (eventM == 0) ? 1 : 0;
    }
    if (mode == BellMode::HourCount) {
        return (eventM == 0) ? hourCount12(eventH) : 0;
    }
    if (mode == BellMode::HourCountHalf) {
        return (eventM == 0) ? hourCount12(eventH) : 1;
    }
    if (mode == BellMode::Pair) {
        if (eventM != 0) return 0;
        groupedPairs = true;
        return hourCount12(eventH);
    }
    if (mode == BellMode::Ships) {
        groupedPairs = true;
        return shipBellCount(eventH, eventM);
    }
    return 0;
}

bool BellController::computeMostRecentEventTime(BellMode mode, int currentH, int currentM, int& eventH, int& eventM) {
    if (mode == BellMode::SingleHour ||
        mode == BellMode::HourCount ||
        mode == BellMode::Pair) {
        eventH = currentH;
        eventM = 0;
        return true;
    }
    if (mode == BellMode::HourCountHalf ||
        mode == BellMode::Ships) {
        eventH = currentH;
        eventM = (currentM >= 30) ? 30 : 0;
        return true;
    }
    return false;
}

void BellController::queue(uint8_t count, uint8_t groupSize, bool force, const char* label) {
    if (count == 0 || (!force && _sequenceActive)) {
        return;
    }

    _sequenceActive = true;
    _sequenceGroupSize = groupSize;
    _sequenceTotal = count;
    _sequenceRemaining = count;
    _sequenceIndex = 0;
    _nextStrikeMs = 0;

    if (label) {
        Serial.print(label);
    } else {
        Serial.print(force ? "Queued timer alert strikes: " : "Queued bell strikes: ");
    }
    Serial.println(count);
}

void BellController::queuePreview(uint8_t count, uint8_t groupSize) {
    queue(count, groupSize, true, "Queued bell preview strikes: ");
    updateSequence();
}

void BellController::updateSequence() {
    if (!_sequenceActive || _sequenceRemaining == 0 || _pulseActive) {
        return;
    }

    unsigned long currentMs = millis();
    if (_nextStrikeMs != 0 && currentMs < _nextStrikeMs) {
        return;
    }

    triggerPulse();
    _sequenceRemaining--;
    _sequenceIndex++;

    if (_sequenceRemaining == 0) {
        _sequenceActive = false;
        return;
    }

    unsigned long nextGapMs = BELL_HOUR_GAP_MS;
    if (_sequenceGroupSize > 1) {
        nextGapMs = (_sequenceIndex % _sequenceGroupSize == 0) ? BELL_SHIP_GROUP_GAP_MS : BELL_SHIP_PAIR_GAP_MS;
    }
    _nextStrikeMs = currentMs + BELL_PULSE_MS + nextGapMs;
}

void BellController::triggerPulse() {
    if (_pulseActive) {
        return;
    }

    _pulseStartMs = millis();
    _pulseActive = true;
    digitalWrite(BELL_PIN, HIGH);
}

void BellController::updatePulse() {
    if (_pulseActive && millis() - _pulseStartMs >= BELL_PULSE_MS) {
        digitalWrite(BELL_PIN, LOW);
        _pulseActive = false;
    }
}
