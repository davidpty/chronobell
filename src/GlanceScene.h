#ifndef GLANCE_SCENE_H
#define GLANCE_SCENE_H

#include <Arduino.h>
#include "Config.h"

#if FEATURE_GLANCE

class Display;

class GlanceScene {
public:
    enum class GazeDir : uint8_t {
        Left = 0,
        Center = 1,
        Right = 2
    };

    GlanceScene();

    void begin(GazeDir dir, bool isBootGreeting);
    void update(unsigned long nowMs);
    void render(Display& display);
    bool isActive() const;
    void handleTouch(GazeDir dir);

private:
    enum class Phase : uint8_t {
        Idle,
        Opening,
        Open,
        Closing,
        Finished
    };

    static constexpr unsigned long OPEN_MS = 100;
    static constexpr unsigned long HOLD_MS = 250;
    static constexpr unsigned long CLOSE_MS = 200;
    static constexpr unsigned long GREET_OPEN_MS = 300;
    static constexpr unsigned long GREET_HOLD_MS = 350;
    static constexpr unsigned long GREET_CLOSE_MS = 250;

    Phase _phase;
    GazeDir _gaze;
    unsigned long _startMs;
    unsigned long _phaseStartMs;
    uint8_t _pupilRadius;
    int8_t _pupilVertShift;
    bool _isBootGreeting;
    bool _pupilContracted;
};

#else

class GlanceScene {
public:
    enum class GazeDir : uint8_t { Left = 0, Center = 1, Right = 2 };
    GlanceScene() {}
    void begin(GazeDir, bool) {}
    void update(unsigned long) {}
    template <typename T> void render(T&) {}
    bool isActive() const { return false; }
    void handleTouch(GazeDir) {}
};

#endif

#endif
