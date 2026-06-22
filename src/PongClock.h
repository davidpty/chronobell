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
        MissFlash = 3,
        CenterTravel = 4,
        CenterBallHold = 5,
        ScoreHold = 6,
        ResetPause = 7
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
        bool leftScoreVisible = true;
        bool rightScoreVisible = true;
        uint8_t oldScoreHour = 0;
        uint8_t oldScoreMinute = 0;
        float scoreTransitionProgress = -1.0f;
        // Debug counters for testing. They are not rendered, but are exposed
        // through snapshot() so unusual recovery paths can be monitored.
        uint16_t forcedSaveCount = 0;
        uint16_t scoreBoxRecoveryCount = 0;
        uint16_t staleCatchupCount = 0;
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
    void startFreshServe(ClockTime time, unsigned long nowMs, TimeFormat format);
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
    static constexpr unsigned long PONG_NEW_SCORE_SETTLE_MS = 1000UL;
    static constexpr unsigned long PONG_CENTER_BALL_BLINK_MS = 1000UL;
    static constexpr unsigned long PONG_CENTER_BALL_STEADY_MS = 1000UL;
    static constexpr unsigned long PONG_CENTER_BALL_HOLD_MS = PONG_NEW_SCORE_SETTLE_MS + PONG_CENTER_BALL_BLINK_MS + PONG_CENTER_BALL_STEADY_MS;
    static constexpr unsigned long PONG_SCORE_FLASH_MS = 1000UL;
    static constexpr unsigned long PONG_SCORE_HOLD_BLANK_MS = 750UL;
    static constexpr unsigned long PONG_SCORE_HOLD_MS = PONG_SCORE_FLASH_MS + PONG_SCORE_HOLD_BLANK_MS;
    static constexpr unsigned long PONG_MISS_FLASH_MS = 3000UL;
    static constexpr unsigned long PONG_RESET_PAUSE_MS = 450UL;
    static constexpr unsigned long PONG_STALE_BREAK_MS = 10000UL;
    static constexpr unsigned long PONG_STALE_PRESSURE_MS = 30000UL;
    static constexpr unsigned long PONG_STALE_FORCE_MS = 60000UL;
    static constexpr unsigned long PONG_RALLY_MAX_MS = 180000UL;
    static constexpr uint16_t PONG_RESONANCE_HIT_THRESHOLD = 6;
    static constexpr uint16_t PONG_RESONANCE_NUDGE_INTERVAL = 4;
    static constexpr uint8_t PONG_PHYSICS_DIVIDER = 1;
    static constexpr uint16_t PONG_LEFT_MIN_STEP_MS = 52;
    static constexpr uint16_t PONG_LEFT_STEP_JITTER_MS = 64;
    static constexpr uint16_t PONG_RIGHT_MIN_STEP_MS = 44;
    static constexpr uint16_t PONG_RIGHT_STEP_JITTER_MS = 72;
    static constexpr uint16_t PONG_LEFT_FAR_TRACK_MS = 84;
    static constexpr uint16_t PONG_RIGHT_FAR_TRACK_MS = 76;
    static constexpr uint16_t PONG_LEFT_NEAR_TRACK_MS = 30;
    static constexpr uint16_t PONG_RIGHT_NEAR_TRACK_MS = 26;
    static constexpr int PONG_MISS_COMMIT_DISTANCE = 3;
    static constexpr int16_t PONG_VEL_BASE = 8;
    static constexpr int16_t PONG_VEL_SERVE = 10;
    static constexpr int16_t PONG_VEL_CLIP = 12;

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
        unsigned long rallyStartMs = 0;
        PaddleTempo leftTempo = PaddleTempo::Cruise;
        PaddleTempo rightTempo = PaddleTempo::Cruise;
        unsigned long leftTempoUntilMs = 0;
        unsigned long rightTempoUntilMs = 0;
        uint8_t leftTempoSeed = 0;
        uint8_t rightTempoSeed = 0;
        int16_t ballXF = PONG_BALL_START_X * PONG_VEL_BASE;
        int16_t ballYF = PONG_BALL_START_Y * PONG_VEL_BASE;
        int16_t ballDxF = PONG_VEL_SERVE;
        int16_t ballDyF = PONG_VEL_SERVE;
        uint16_t rallyHits = 0;
        int8_t lossExitX = 0;
        int8_t lossExitY = 0;
        int8_t lossPaddleY = 8;
        unsigned long scoreStaleSinceMs = 0;
        uint8_t oldScoreHour = 0;
        uint8_t oldScoreMinute = 0;
        bool initialized = false;
    };

    State _state;
    Snapshot _snapshot;

    static uint16_t jitterDelay(uint16_t minMs, uint16_t spreadMs);
    static uint16_t minuteOfDay(int hours, int minutes);
    static int visibleHour(int rawHours, TimeFormat format);
    static uint16_t visibleMinuteOfDay(int hours, int minutes, TimeFormat format);
    static int8_t clampPaddleY(int y);
    static int8_t playfieldCenterY();
    static int8_t servePaddleY();
    static int8_t awayPaddleY(int ballY);
    static PaddleTempo nextTempoMode(PaddleTempo current, bool ballNear, bool chaseSide, uint8_t seed);
    static uint16_t tempoStepDelay(PaddleTempo tempo, bool ballNear, bool chaseSide, uint8_t seed);
    static uint8_t tempoStepCap(PaddleTempo tempo);
    static int glyphWidth(uint8_t glyph);
    static int textWidth(const char* s);
    void syncSnapshot(TimeFormat format);
    void prepareServeState(ClockTime time, unsigned long nowMs, TimeFormat format);
    void armRally(TimeFormat format, ClockTime time, unsigned long nowMs);
    void resetBall(ClockTime time, unsigned long nowMs, TimeFormat format);
};

#endif // PONG_CLOCK_H
