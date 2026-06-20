#ifndef SCREEN_TRANSITION_H
#define SCREEN_TRANSITION_H

#include <Arduino.h>
#include "Config.h"

enum class ScreenTransitionType : uint8_t {
    None,
    ParticleDissolve,
    ShapeMorph
};

struct TransitionParticle {
    int8_t startX;
    int8_t startY;
    int8_t endX;
    int8_t endY;
    uint8_t phaseOffset;
    uint8_t flags;
};

class ScreenTransition {
public:
    void start(const uint32_t oldFrame[16], const uint32_t newFrame[16], uint32_t nowMs);
    bool render(uint32_t nowMs, uint32_t outputFrame[16]);
    bool active() const { return _active; }
    void setType(ScreenTransitionType type) { _preferredType = type; }

    static void clearFrame(uint32_t frame[16]);
    static void copyFrame(uint32_t destination[16], const uint32_t source[16]);
    static bool getPixelFromFrame(const uint32_t frame[16], uint8_t x, uint8_t y);
    static void setPixelInFrame(uint32_t frame[16], int x, int y);

private:
    enum ParticleFlags : uint8_t {
        HasDestination = 1,
        BirthParticle = 2
    };

    void addParticle(int startX, int startY, int endX, int endY, uint8_t flags);
    bool renderShapeMorph(uint32_t nowMs, uint32_t outputFrame[16]);

    bool _active = false;
    uint32_t _oldFrame[16] = {};
    uint32_t _newFrame[16] = {};
    uint32_t _startMs = 0;
    uint16_t _durationMs = SCREEN_TRANSITION_DURATION_MS;
    ScreenTransitionType _type = ScreenTransitionType::None;
#if SCREEN_TRANSITION == 2
    ScreenTransitionType _preferredType = ScreenTransitionType::ParticleDissolve;
#else
    ScreenTransitionType _preferredType = ScreenTransitionType::ShapeMorph;
#endif
    TransitionParticle _particles[SCREEN_TRANSITION_PARTICLE_MAX] = {};
    uint16_t _particleCount = 0;
    uint8_t _oldDist[16][32] = {};
    uint8_t _newDist[16][32] = {};
};

#endif
