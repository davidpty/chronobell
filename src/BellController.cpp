#include "BellController.h"

#include "Config.h"

void BellController::begin() {
    pinMode(BELL_PIN, OUTPUT);
    digitalWrite(BELL_PIN, LOW);
}

void BellController::update(const ClockTime& currentTime, bool timeValid, BellMode mode,
                            bool countdownExpired, bool muteAutomatic,
                            bool suppressScheduledStrike) {
    if (!suppressScheduledStrike && !countdownExpired && !muteAutomatic &&
        mode != BellMode::Off && !_sequenceActive && timeValid) {
        if ((currentTime.minutes == 0 || currentTime.minutes == 30) && currentTime.seconds <= 1) {
            int eventKey = (currentTime.hours * 60) + currentTime.minutes;
            if (eventKey != _lastEventKey) {
                _lastEventKey = eventKey;
                uint8_t strikeGroupSize = 0;
                uint8_t count = computeStrikesForEvent(mode, currentTime.hours, currentTime.minutes, strikeGroupSize);
                if (count > 0) {
                    queue(count, strikeGroupSize, false);
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

    uint8_t strikeGroupSize = 0;
    uint8_t count = computeStrikesForEvent(mode, eventH, eventM, strikeGroupSize);
    if (count > 0) {
        queuePreview(count, strikeGroupSize);
    }
}

void BellController::queueCountdownAlert() {
    static const uint8_t kCountdownAlertPattern[] = {3, 2, 1};
    const uint8_t groupCount = sizeof(kCountdownAlertPattern) / sizeof(kCountdownAlertPattern[0]);
    const uint8_t totalStrikes = 3 + 2 + 1;
    if (groupCount == 0) {
        return;
    }

    queuePattern(totalStrikes, kCountdownAlertPattern, groupCount, true);
}

void BellController::queueNewYearAlert() {
    // Twelve ungrouped strikes: recognizable as midnight and distinct from
    // the countdown timer's 3-2-1 pattern.
    queue(12, 0, true, "Queued New Year strikes: ");
}

void BellController::queueCountdownStartAlert() {
    // Three grouped strikes at the start of the final 10-minute countdown.
    queue(3, 3, true, "Countdown start: ");
}

void BellController::stop() {
    _sequenceActive = false;
    _sequenceTotal = 0;
    _sequenceRemaining = 0;
    _sequenceIndex = 0;
    _sequenceGroupSize = 0;
    _nextStrikeMs = 0;
    _sequenceGroupPattern = nullptr;
    _sequenceGroupPatternCount = 0;
    _sequenceGroupPatternIndex = 0;
    _sequenceGroupProgress = 0;
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

uint8_t BellController::computeStrikesForEvent(BellMode mode, int eventH, int eventM, uint8_t& strikeGroupSize) {
    strikeGroupSize = 0;

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
        strikeGroupSize = 2;
        return hourCount12(eventH);
    }
    if (mode == BellMode::Triple) {
        if (eventM != 0) return 0;
        strikeGroupSize = 3;
        return hourCount12(eventH);
    }
    if (mode == BellMode::Ships) {
        strikeGroupSize = 2;
        return shipBellCount(eventH, eventM);
    }
    return 0;
}

bool BellController::computeMostRecentEventTime(BellMode mode, int currentH, int currentM, int& eventH, int& eventM) {
    if (mode == BellMode::SingleHour ||
        mode == BellMode::HourCount ||
        mode == BellMode::Pair ||
        mode == BellMode::Triple) {
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
    _sequenceGroupPattern = nullptr;
    _sequenceGroupPatternCount = 0;
    _sequenceGroupPatternIndex = 0;
    _sequenceGroupProgress = 0;
    _sequenceTotal = count;
    _sequenceRemaining = count;
    _sequenceIndex = 0;
    _nextStrikeMs = 0;

    if (label) {
        LOG(label);
    } else {
        LOG(force ? "Queued timer alert strikes: " : "Queued bell strikes: ");
    }
    LOGLN(count);
}

void BellController::queuePattern(uint8_t count, const uint8_t* groupPattern, uint8_t patternCount, bool force, const char* label) {
    if (count == 0 || (!force && _sequenceActive)) {
        return;
    }

    _sequenceActive = true;
    _sequenceGroupSize = 0;
    _sequenceGroupPattern = groupPattern;
    _sequenceGroupPatternCount = patternCount;
    _sequenceGroupPatternIndex = 0;
    _sequenceGroupProgress = 0;
    _sequenceTotal = count;
    _sequenceRemaining = count;
    _sequenceIndex = 0;
    _nextStrikeMs = 0;

    if (label) {
        LOG(label);
    } else {
        LOG(force ? "Queued timer alert strikes: " : "Queued bell strikes: ");
    }
    LOGLN(count);
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

    bool groupComplete = false;
    if (_sequenceGroupPattern && _sequenceGroupPatternIndex < _sequenceGroupPatternCount) {
        uint8_t currentGroupSize = _sequenceGroupPattern[_sequenceGroupPatternIndex];
        _sequenceGroupProgress++;
        if (_sequenceGroupProgress >= currentGroupSize) {
            _sequenceGroupProgress = 0;
            _sequenceGroupPatternIndex++;
            groupComplete = true;
        }
    } else if (_sequenceGroupSize > 1 && _sequenceIndex % _sequenceGroupSize == 0) {
        groupComplete = true;
    }

    unsigned long nextGapMs = groupComplete ? BELL_GROUP_GAP_MS : BELL_STRIKE_GAP_MS;
    _nextStrikeMs = currentMs + BELL_COIL_ON_MS + BELL_COIL_OFF_MS + nextGapMs;
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
    if (_pulseActive && millis() - _pulseStartMs >= BELL_COIL_ON_MS) {
        digitalWrite(BELL_PIN, LOW);
        _pulseActive = false;
    }
}
