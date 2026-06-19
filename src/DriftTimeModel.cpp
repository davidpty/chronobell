#include "DriftTimeModel.h"

#include "Config.h"

#include <math.h>

namespace {
static constexpr int SECONDS_PER_DAY = 24 * 60 * 60;
static constexpr unsigned long FRESH_DISPLAYED_SECOND_MS = 1500UL;
static constexpr double DRIFT_PI = 3.14159265358979323846;
static constexpr double MINIMUM_DISPLAY_RATE = 0.10;

static bool deadlineReached(unsigned long nowMs, unsigned long deadlineMs) {
    return (int32_t)(nowMs - deadlineMs) >= 0;
}
}

void DriftTimeModel::update(const ClockTime& realTime, unsigned long nowMs) {
    int realSecond = secondOfDay(realTime);
    if (!_initialized) {
        initialize(realTime, nowMs);
        return;
    }

    unsigned long updateElapsedMs = nowMs - _lastUpdateMs;
    int realJump = signedSecondDelta(realSecond, _lastRealSecond);
    if (realJump < -10 || realJump > 10) {
        // Preserve drift phase across RTC/NTP corrections while applying the
        // correction to displayed time. Subtract elapsed monotonic time so a
        // delayed loop iteration is not mistaken for a clock correction.
        int expectedAdvance = (int)((updateElapsedMs + 500UL) / 1000UL);
        _phaseStartDisplayedSecond += realJump - expectedAdvance;
    }
    _lastRealSecond = realSecond;
    _lastUpdateMs = nowMs;

    unsigned long durationMs = currentPhaseDurationSeconds() * 1000UL;
    while (deadlineReached(nowMs, _phaseStartMs + durationMs)) {
        finishCurrentPhase();
        _phaseStartMs += durationMs;
        _phase = nextPhase();
        durationMs = currentPhaseDurationSeconds() * 1000UL;
    }

    unsigned long elapsedMs = nowMs - _phaseStartMs;
    int64_t displayedSerial = _phaseStartDisplayedSecond +
                              (int64_t)floor(phaseProgressSeconds(elapsedMs) + 1.0e-9);
    int nextDisplayedSecond = wrapSecondOfDay(displayedSerial);
    if (nextDisplayedSecond != _displayedSecond) {
        _displayedSecond = nextDisplayedSecond;
        _lastDisplayChangeMs = nowMs;
    }
}

void DriftTimeModel::reset(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs);
}

void DriftTimeModel::activate(const ClockTime& realTime, unsigned long nowMs) {
    initialize(realTime, nowMs);
}

void DriftTimeModel::initialize(const ClockTime& realTime, unsigned long nowMs) {
    _initialized = true;
    _displayedSecond = secondOfDay(realTime);
    _lastRealSecond = _displayedSecond;
    _phaseStartDisplayedSecond = _displayedSecond;
    _phaseStartMs = nowMs;
    _lastUpdateMs = nowMs;
    _lastDisplayChangeMs = nowMs;
    _phase = directionMode() == 0 ? Phase::InitialToBehind : Phase::Away;
}

void DriftTimeModel::finishCurrentPhase() {
    unsigned long durationMs = currentPhaseDurationSeconds() * 1000UL;
    _phaseStartDisplayedSecond += (int64_t)llround(phaseProgressSeconds(durationMs));
}

DriftTimeModel::Phase DriftTimeModel::nextPhase() const {
    if (directionMode() == 0) {
        switch (_phase) {
            case Phase::InitialToBehind: return Phase::BehindToAhead;
            case Phase::BehindToAhead:   return Phase::AheadToBehind;
            case Phase::AheadToBehind:   return Phase::BehindToAhead;
            default:                     return Phase::InitialToBehind;
        }
    }
    return _phase == Phase::Away ? Phase::Return : Phase::Away;
}

double DriftTimeModel::phaseProgressSeconds(unsigned long elapsedMs) const {
    double duration = (double)currentPhaseDurationSeconds();
    double elapsedSeconds = (double)elapsedMs / 1000.0;
    double u = elapsedSeconds / duration;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;

    // Cosine interpolation has zero slope at both endpoints, so adjacent
    // phases meet without a tempo discontinuity.
    double curve = (1.0 - cos(DRIFT_PI * u)) * 0.5;
    double offsetDelta = 0.0;
    double offset = (double)maxOffsetSeconds();

    switch (_phase) {
        case Phase::Away:
            offsetDelta = (double)directionSign() * offset * curve;
            break;
        case Phase::Return:
            offsetDelta = -(double)directionSign() * offset * curve;
            break;
        case Phase::InitialToBehind:
            offsetDelta = -offset * curve;
            break;
        case Phase::BehindToAhead:
            offsetDelta = 2.0 * offset * curve;
            break;
        case Phase::AheadToBehind:
            offsetDelta = -2.0 * offset * curve;
            break;
    }

    // These complete, zero-mean tempo waves integrate to zero at each phase
    // boundary, so variation cannot move an endpoint or create a step.
    double tempoVariation = (double)tempoVariationPercent() / 100.0;
    double variationProgress = duration * tempoVariation *
        (0.65 * (1.0 - cos(4.0 * DRIFT_PI * u)) / (4.0 * DRIFT_PI) +
         0.35 * (1.0 - cos(8.0 * DRIFT_PI * u)) / (8.0 * DRIFT_PI));

    return elapsedSeconds + offsetDelta + variationProgress;
}

unsigned long DriftTimeModel::currentPhaseDurationSeconds() const {
    unsigned long duration = (unsigned long)effectivePhaseSeconds();
    if (directionMode() == 0 && _phase != Phase::InitialToBehind) {
        duration *= 2UL;
    }
    return duration > 0 ? duration : 1UL;
}

ClockTime DriftTimeModel::displayTime(const ClockTime& realTime, unsigned long nowMs) const {
    (void)nowMs;
    int displayedSecond = _initialized ? _displayedSecond : secondOfDay(realTime);
    ClockTime time;
    time.hours = displayedSecond / 3600;
    time.minutes = (displayedSecond / 60) % 60;
    time.seconds = displayedSecond % 60;
    return time;
}

int DriftTimeModel::driftDirection() const {
    if (!_initialized) return 0;
    switch (_phase) {
        case Phase::InitialToBehind: return -1;
        case Phase::BehindToAhead:   return 1;
        case Phase::AheadToBehind:   return -1;
        case Phase::Away:            return directionSign();
        case Phase::Return:          return -directionSign();
    }
    return 0;
}

int DriftTimeModel::offsetMinutes(const ClockTime& realTime) const {
    if (!_initialized) return 0;
    int offsetSeconds = signedSecondDelta(_displayedSecond, secondOfDay(realTime));
    if (offsetSeconds >= 0) return (offsetSeconds + 30) / 60;
    return -((-offsetSeconds + 30) / 60);
}

bool DriftTimeModel::displayedMinuteFresh(unsigned long nowMs) const {
    return _initialized && (nowMs - _lastDisplayChangeMs) <= FRESH_DISPLAYED_SECOND_MS;
}

bool DriftTimeModel::separatorVisible() const {
    return !_initialized || ((_displayedSecond & 1) == 0);
}

int DriftTimeModel::secondOfDay(const ClockTime& time) {
    int hours = time.hours;
    while (hours < 0) hours += 24;
    hours %= 24;
    int minutes = clampInt(time.minutes, 0, 59);
    int seconds = clampInt(time.seconds, 0, 59);
    return (hours * 3600) + (minutes * 60) + seconds;
}

int DriftTimeModel::wrapSecondOfDay(int64_t second) {
    second %= SECONDS_PER_DAY;
    if (second < 0) second += SECONDS_PER_DAY;
    return (int)second;
}

int DriftTimeModel::signedSecondDelta(int fromSecond, int toSecond) {
    int diff = wrapSecondOfDay(fromSecond) - wrapSecondOfDay(toSecond);
    while (diff <= -SECONDS_PER_DAY / 2) diff += SECONDS_PER_DAY;
    while (diff > SECONDS_PER_DAY / 2) diff -= SECONDS_PER_DAY;
    return diff;
}

int DriftTimeModel::maxOffsetSeconds() {
    return clampInt(DRIFT_MAX_OFFSET_MINUTES, 1, 12 * 60) * 60;
}

int DriftTimeModel::requestedPhaseSeconds() {
    return clampInt(DRIFT_TIME_TO_MAX_OFFSET_MINUTES, 1, 12 * 60) * 60;
}

int DriftTimeModel::effectivePhaseSeconds() {
    double tempoVariation = (double)tempoVariationPercent() / 100.0;
    double availableRate = 1.0 - MINIMUM_DISPLAY_RATE - tempoVariation;
    if (availableRate < 0.05) availableRate = 0.05;

    int minimumSafe = (int)ceil((double)maxOffsetSeconds() * (DRIFT_PI * 0.5) /
                                availableRate);
    int requested = requestedPhaseSeconds();
    return requested > minimumSafe ? requested : minimumSafe;
}

int DriftTimeModel::tempoVariationPercent() {
    return clampInt(DRIFT_TEMPO_VARIATION_PERCENT, 0, 25);
}

int DriftTimeModel::directionMode() {
    return clampInt(DRIFT_PATTERN, 0, 2);
}

int DriftTimeModel::directionSign() {
    return directionMode() == 2 ? 1 : -1;
}

int DriftTimeModel::clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}
