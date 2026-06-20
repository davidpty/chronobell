#include "ScreenTransition.h"

#include <string.h>

namespace {
struct PixelCoord {
    int8_t x;
    int8_t y;
};

static int absInt(int value) {
    return value < 0 ? -value : value;
}

static uint16_t easeOutCubic(uint16_t t) {
    const uint32_t inverse = 1024U - t;
    const uint32_t cubic = (inverse * inverse * inverse) >> 20;
    return (uint16_t)(1024U - cubic);
}

static int interpolate(int start, int end, uint16_t progress) {
    int32_t scaled = (int32_t)(end - start) * progress;
    if (scaled >= 0) scaled += 512;
    else scaled -= 512;
    return start + scaled / 1024;
}

static uint16_t easeSmoothStep(uint16_t t) {
    uint32_t t2 = ((uint32_t)t * t) >> 10;
    uint32_t term = 3072U - 2U * t;
    uint32_t result = (t2 * term) >> 10;
    return result > 1024 ? 1024 : (uint16_t)result;
}

static uint16_t computeThreshold(uint16_t t) {
    const uint16_t LOOSE_END = 256;
    const uint16_t MORPH_END = 768;
    const uint16_t THRESH_MAX = 1536;
    if (t <= LOOSE_END) {
        return (uint16_t)((uint32_t)t * THRESH_MAX / LOOSE_END);
    } else if (t <= MORPH_END) {
        return THRESH_MAX;
    } else {
        uint16_t phaseT = t - MORPH_END;
        uint16_t phaseLen = 1024 - MORPH_END;
        return (uint16_t)(THRESH_MAX - (uint32_t)phaseT * THRESH_MAX / phaseLen);
    }
}

static void computeDistanceMap(const uint32_t frame[16], uint8_t dist[16][32], uint8_t maxDist) {
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            if (ScreenTransition::getPixelFromFrame(frame, x, y)) {
                dist[y][x] = 0;
            } else {
                uint8_t best = maxDist;
                if (x > 0 && dist[y][x - 1] + 1 < best) best = dist[y][x - 1] + 1;
                if (y > 0 && dist[y - 1][x] + 1 < best) best = dist[y - 1][x] + 1;
                dist[y][x] = best;
            }
        }
    }
    for (int y = 15; y >= 0; y--) {
        for (int x = 31; x >= 0; x--) {
            uint8_t best = dist[y][x];
            if (x < 31 && dist[y][x + 1] + 1 < best) best = dist[y][x + 1] + 1;
            if (y < 15 && dist[y + 1][x] + 1 < best) best = dist[y + 1][x] + 1;
            dist[y][x] = best < maxDist ? best : maxDist;
        }
    }
}
}

void ScreenTransition::clearFrame(uint32_t frame[16]) {
    memset(frame, 0, sizeof(uint32_t) * 16);
}

void ScreenTransition::copyFrame(uint32_t destination[16], const uint32_t source[16]) {
    memcpy(destination, source, sizeof(uint32_t) * 16);
}

bool ScreenTransition::getPixelFromFrame(const uint32_t frame[16], uint8_t x, uint8_t y) {
    return x < 32 && y < 16 && (frame[y] & (1UL << x)) != 0;
}

void ScreenTransition::setPixelInFrame(uint32_t frame[16], int x, int y) {
    if (x >= 0 && x < 32 && y >= 0 && y < 16) {
        frame[y] |= 1UL << x;
    }
}

void ScreenTransition::addParticle(int startX, int startY, int endX, int endY, uint8_t flags) {
    if (_particleCount >= SCREEN_TRANSITION_PARTICLE_MAX) return;
    TransitionParticle& particle = _particles[_particleCount];
    particle.startX = (int8_t)startX;
    particle.startY = (int8_t)startY;
    particle.endX = (int8_t)endX;
    particle.endY = (int8_t)endY;
    particle.phaseOffset = (uint8_t)((_particleCount * 37U + startX * 11U + startY * 17U) & 31U);
    particle.flags = flags;
    _particleCount++;
}

void ScreenTransition::start(const uint32_t oldFrame[16], const uint32_t newFrame[16], uint32_t nowMs) {
    copyFrame(_oldFrame, oldFrame);
    copyFrame(_newFrame, newFrame);
    _startMs = nowMs;
    _durationMs = SCREEN_TRANSITION_DURATION_MS;
    _type = _preferredType;
    _particleCount = 0;

    if (_type == ScreenTransitionType::ShapeMorph) {
        const uint8_t MAX_DIST = 7;
        computeDistanceMap(oldFrame, _oldDist, MAX_DIST);
        computeDistanceMap(newFrame, _newDist, MAX_DIST);
        _active = true;
        return;
    }

    PixelCoord moving[SCREEN_TRANSITION_PARTICLE_MAX];
    bool assigned[SCREEN_TRANSITION_PARTICLE_MAX] = {};
    uint16_t movingCount = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            if (getPixelFromFrame(oldFrame, x, y) && !getPixelFromFrame(newFrame, x, y) &&
                movingCount < SCREEN_TRANSITION_PARTICLE_MAX) {
                moving[movingCount++] = {(int8_t)x, (int8_t)y};
            }
        }
    }

    // Destination order is stable. Manhattan matching keeps migration local.
    for (int y = 0; y < 16 && _particleCount < SCREEN_TRANSITION_PARTICLE_MAX; y++) {
        for (int x = 0; x < 32 && _particleCount < SCREEN_TRANSITION_PARTICLE_MAX; x++) {
            if (getPixelFromFrame(oldFrame, x, y) || !getPixelFromFrame(newFrame, x, y)) continue;
            int best = -1;
            int bestDistance = 1000;
            for (uint16_t i = 0; i < movingCount; i++) {
                if (assigned[i]) continue;
                int distance = absInt(moving[i].x - x) + absInt(moving[i].y - y);
                if (distance < bestDistance) {
                    best = i;
                    bestDistance = distance;
                }
            }
            if (best >= 0) {
                assigned[best] = true;
                addParticle(moving[best].x, moving[best].y, x, y, HasDestination);
            } else {
                int spawnX = x + (((x * 3 + y) & 1) ? 1 : -1);
                int spawnY = y + (((x + y * 5) & 1) ? 1 : 0);
                addParticle(spawnX, spawnY, x, y, HasDestination | BirthParticle);
            }
        }
    }

    for (uint16_t i = 0; i < movingCount && _particleCount < SCREEN_TRANSITION_PARTICLE_MAX; i++) {
        if (assigned[i]) continue;
        int dx = (i % 3) - 1;
        int dy = 1 + (i % 4);
        addParticle(moving[i].x, moving[i].y, moving[i].x + dx, moving[i].y + dy, 0);
    }
    _active = true;
}

bool ScreenTransition::render(uint32_t nowMs, uint32_t outputFrame[16]) {
    if (!_active) return false;

    if (_type == ScreenTransitionType::ShapeMorph) {
        return renderShapeMorph(nowMs, outputFrame);
    }

    // --- ParticleDissolve ---
    uint32_t elapsed = nowMs - _startMs;
    if (elapsed >= _durationMs) {
        copyFrame(outputFrame, _newFrame);
        _active = false;
        _type = ScreenTransitionType::None;
        return true;
    }

    clearFrame(outputFrame);
    uint16_t globalProgress = (uint16_t)((elapsed * 1024UL) / _durationMs);
    uint16_t settleProgress = (uint16_t)(((uint32_t)SCREEN_TRANSITION_SETTLE_MS * 1024U) / _durationMs);
    uint16_t motionEnd = settleProgress < 512 ? (uint16_t)(1024 - settleProgress) : 512;

    // Matching pixels are structural anchors and never flicker.
    for (int y = 0; y < 16; y++) {
        outputFrame[y] = _oldFrame[y] & _newFrame[y];
    }

    for (uint16_t i = 0; i < _particleCount; i++) {
        const TransitionParticle& particle = _particles[i];
        bool birth = (particle.flags & BirthParticle) != 0;
        bool destination = (particle.flags & HasDestination) != 0;
        uint16_t delay = birth ? (uint16_t)(560 + particle.phaseOffset * 5) : (uint16_t)(particle.phaseOffset * 5);
        if (globalProgress < delay) {
            if (!birth) setPixelInFrame(outputFrame, particle.startX, particle.startY);
            continue;
        }
        uint16_t span = motionEnd > delay ? (uint16_t)(motionEnd - delay) : 1;
        uint16_t local = (uint16_t)(((uint32_t)(globalProgress - delay) * 1024U) / span);
        if (local > 1024) local = 1024;

        if (!destination && globalProgress > (uint16_t)(700 + particle.phaseOffset * 5)) continue;
        uint16_t eased = easeOutCubic(local);
        int x = interpolate(particle.startX, particle.endX, eased);
        int y = interpolate(particle.startY, particle.endY, eased);
        if (local < 600 && ((i + local / 120) & 3U) == 0) {
            x += ((i >> 1) & 1U) ? 1 : -1;
        }
        setPixelInFrame(outputFrame, x, y);
    }
    return true;
}

bool ScreenTransition::renderShapeMorph(uint32_t nowMs, uint32_t outputFrame[16]) {
    uint32_t elapsed = nowMs - _startMs;
    if (elapsed >= _durationMs) {
        copyFrame(outputFrame, _newFrame);
        _active = false;
        _type = ScreenTransitionType::None;
        return true;
    }

    uint16_t t = (uint16_t)((elapsed * 1024UL) / _durationMs);
    uint16_t tEased = easeSmoothStep(t);
    uint16_t threshold = computeThreshold(tEased);

    clearFrame(outputFrame);

    for (uint8_t y = 0; y < 16; y++) {
        for (uint8_t x = 0; x < 32; x++) {
            uint16_t d = ((uint16_t)_oldDist[y][x] * (1024U - tEased) +
                          (uint16_t)_newDist[y][x] * tEased);

            // Edge disturbance: deterministic wobble near boundary during morph phase
            if (tEased > 256 && tEased < 768) {
                int16_t diff = (int16_t)d - (int16_t)threshold;
                if (diff > -768 && diff < 768) {
                    uint8_t wobble = (uint8_t)(x * 17U + y * 43U + (tEased >> 3));
                    if ((wobble & 3U) == 1U) {
                        d += 1024;
                    } else if ((wobble & 3U) == 2U) {
                        d -= 512;
                    }
                }
            }

            if (d <= threshold) {
                setPixelInFrame(outputFrame, x, y);
            }
        }
    }
    return true;
}
