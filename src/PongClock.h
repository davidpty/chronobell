#ifndef PONG_CLOCK_H
#define PONG_CLOCK_H

#include <Arduino.h>

#include "AppSettings.h"
#include "fonts.h"
#include "RtcClock.h"

class PongClockEngine {
public:
    enum class MissSide : uint8_t {
        None = 0,
        Left = 1,
        Right = 2
    };

    enum class Phase : uint8_t {
        Rally = 0,
        LeadIn = 1,
        MissFlight = 2,
        CenterTravel = 3,
        CenterBallHold = 4,
        ScoreHold = 5,
        ResetPause = 6
    };

    enum class MissEscape : uint8_t {
        TopGap = 0,
        BottomGap = 1,
        SideExit = 2
    };

    enum class PaddleTempo : uint8_t {
        Hold = 0,
        Drift = 1,
        Cruise = 2,
        Burst = 3
    };

    struct Snapshot {
        int8_t ballX = 15;
        int8_t ballY = 10;
        int8_t ballDx = 1;
        int8_t ballDy = 1;
        int8_t leftPaddleY = 8;
        int8_t rightPaddleY = 8;
        uint8_t scoreHour = 0;
        uint8_t scoreMinute = 0;
        MissSide pendingMissSide = MissSide::None;
        Phase phase = Phase::Rally;
        bool pendingMiss = false;
        bool ballVisible = true;
        bool pendingScoreValid = false;
        uint8_t pendingScoreHour = 0;
        uint8_t pendingScoreMinute = 0;
        MissEscape missEscape = MissEscape::SideExit;
    };

    struct ScoreLayout {
        int leftX = 0;
        int leftWidth = 0;
        int rightX = 0;
        int rightWidth = 0;
        int topY = 0;
        int bottomY = 0;
    };

    void reset(ClockTime time, unsigned long nowMs, TimeFormat format);
    void update(ClockTime time, unsigned long nowMs, TimeFormat format);

    const Snapshot& snapshot() const { return _snapshot; }
    ScoreLayout scoreLayout(TimeFormat format) const;

private:
    static constexpr int PONG_SCORE_Y = 0;
    static constexpr int PONG_PLAY_TOP = 0;
    static constexpr int PONG_PLAY_BOTTOM = 15;
    static constexpr int PONG_PADDLE_HEIGHT = 4;
    static constexpr int PONG_PADDLE_MIN_Y = PONG_SCORE_Y + SEC_FONT_HEIGHT;
    static constexpr int PONG_PADDLE_MAX_Y = PONG_PLAY_BOTTOM - PONG_PADDLE_HEIGHT + 1;
    static constexpr int PONG_BALL_START_X = 15;
    static constexpr int PONG_BALL_START_Y = 10;
    static constexpr unsigned long PONG_CENTER_TRAVEL_MS = 900UL;
    static constexpr unsigned long PONG_CENTER_BALL_HOLD_MS = 700UL;
    static constexpr unsigned long PONG_SCORE_HOLD_MS = 650UL;
    static constexpr unsigned long PONG_RESET_PAUSE_MS = 650UL;
    static constexpr uint8_t PONG_PHYSICS_DIVIDER = 2;
    static constexpr uint16_t PONG_LEFT_MIN_STEP_MS = 52;
    static constexpr uint16_t PONG_LEFT_STEP_JITTER_MS = 64;
    static constexpr uint16_t PONG_RIGHT_MIN_STEP_MS = 44;
    static constexpr uint16_t PONG_RIGHT_STEP_JITTER_MS = 72;
    static constexpr uint16_t PONG_LEFT_FAR_TRACK_MS = 84;
    static constexpr uint16_t PONG_RIGHT_FAR_TRACK_MS = 76;
    static constexpr uint16_t PONG_LEFT_NEAR_TRACK_MS = 30;
    static constexpr uint16_t PONG_RIGHT_NEAR_TRACK_MS = 26;
    static constexpr int PONG_MISS_COMMIT_DISTANCE = 3;

    struct State {
        Snapshot view;
        uint8_t physicsDividerCounter = 0;
        unsigned long leftNextMoveMs = 0;
        unsigned long rightNextMoveMs = 0;
        uint8_t leftCadence = 0;
        uint8_t rightCadence = 0;
        int8_t leftTargetY = 8;
        int8_t rightTargetY = 8;
        unsigned long phaseUntilMs = 0;
        MissEscape missEscape = MissEscape::SideExit;
        PaddleTempo leftTempo = PaddleTempo::Cruise;
        PaddleTempo rightTempo = PaddleTempo::Cruise;
        unsigned long leftTempoUntilMs = 0;
        unsigned long rightTempoUntilMs = 0;
        uint8_t leftTempoSeed = 0;
        uint8_t rightTempoSeed = 0;
        bool initialized = false;
    };

    State _state;
    Snapshot _snapshot;

    static uint16_t jitterDelay(uint16_t minMs, uint16_t spreadMs);
    static uint16_t minuteOfDay(int hours, int minutes);
    static int visibleHour(int rawHours, TimeFormat format);
    static uint16_t visibleMinuteOfDay(int hours, int minutes, TimeFormat format);
    static int8_t clampPaddleY(int y);
    static int8_t centerPaddleY();
    static int8_t awayPaddleY(int ballY);
    static PaddleTempo nextTempoMode(PaddleTempo current, bool ballNear, bool chaseSide, uint8_t seed);
    static uint16_t tempoStepDelay(PaddleTempo tempo, bool ballNear, bool chaseSide, uint8_t seed);
    static uint8_t tempoStepCap(PaddleTempo tempo);
    static uint8_t fontIndex(char c);
    static int glyphWidth(uint8_t glyph);
    static int textWidth(const char* s);
    void syncSnapshot(TimeFormat format);
    void resetBall(ClockTime time, unsigned long nowMs, TimeFormat format, bool afterMiss);
};

#endif // PONG_CLOCK_H
