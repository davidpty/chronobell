#include "PongClock.h"

#include <string.h>

#include "fonts.h"

namespace {
static uint16_t randomJitter(uint16_t minMs, uint16_t spreadMs) {
    if (spreadMs == 0) {
        return minMs;
    }
    return (uint16_t)(minMs + (esp_random() % (uint32_t)(spreadMs + 1)));
}

static uint16_t randomRange(uint16_t minValue, uint16_t maxValue) {
    if (maxValue <= minValue) {
        return minValue;
    }
    return (uint16_t)(minValue + (esp_random() % (uint32_t)(maxValue - minValue + 1)));
}

static constexpr int kPongMidX = COLS_PER_ROW / 2;
static constexpr int kPongMidY = (TOTAL_ROWS - 1) / 2;

static const int16_t kServeVels[][2] = {
    { 8,  8}, { 8,  4}, { 8,  2}, { 8,  1}, { 8,  0},
    { 8, -1}, { 8, -2}, { 8, -4}, { 8, -8},
    { 4,  8}, { 2,  8}, { 1,  8}, { 0,  8},
    {-1,  8}, {-2,  8}, {-4,  8},
    {-8,  8}, {-8,  4}, {-8,  2}, {-8,  0},
    {-8, -8}, {-8, -4}, {-8, -2}, {-8, -1},
    {-4, -8}, {-2, -8}, {-1, -8}, { 0, -8},
    { 1, -8}, { 2, -8}, { 4, -8},
    { 8, -8}, { 8, -4}, { 8, -2}, { 8, -1}
};
static constexpr int kServeVelCount = (int)(sizeof(kServeVels) / sizeof(kServeVels[0]));
static constexpr int16_t kVelBase = 8;
static constexpr int16_t kVelClip = 12;

static int predictIntercept(int16_t ballXF, int16_t ballYF,
                             int16_t velXF, int16_t velYF,
                             int paddleXPx, int topPx, int bottomPx) {
    int ballPx = ballXF / kVelBase;
    int16_t dxF = (paddleXPx - ballPx) * kVelBase;
    if (dxF == 0) return ballYF / kVelBase;
    if ((dxF > 0 && velXF <= 0) || (dxF < 0 && velXF >= 0))
        return (topPx + bottomPx) / 2;
    int steps = dxF / velXF;
    if (steps <= 0) {
        if ((dxF > 0 && velXF <= 0) || (dxF < 0 && velXF >= 0))
            return (topPx + bottomPx) / 2;
        steps = 1;
    }
    if (steps > 120) return (topPx + bottomPx) / 2;
    int yF = ballYF;
    int16_t vy = velYF;
    int topF = topPx * kVelBase;
    int bottomF = bottomPx * kVelBase;
    for (int s = 0; s < steps; s++) {
        yF += vy;
        int py = yF / kVelBase;
        if (py < topPx) { yF = 2 * topF - yF; vy = -vy; }
        else if (py > bottomPx) { yF = 2 * bottomF - yF; vy = -vy; }
    }
    return yF / kVelBase;
}

static uint8_t tempoBias(uint8_t base, int delta) {
    int value = (int)base + delta;
    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }
    return (uint8_t)value;
}
}

void PongClockEngine::reset(ClockTime time, unsigned long nowMs, TimeFormat format) {
    _state.initialized = true;
    resetBall(time, nowMs, format);
}

uint16_t PongClockEngine::jitterDelay(uint16_t minMs, uint16_t spreadMs) {
    return randomJitter(minMs, spreadMs);
}

uint16_t PongClockEngine::minuteOfDay(int hours, int minutes) {
    hours %= 24;
    if (hours < 0) hours += 24;
    minutes %= 60;
    if (minutes < 0) minutes += 60;
    return (uint16_t)(hours * 60 + minutes);
}

int PongClockEngine::visibleHour(int rawHours, TimeFormat format) {
    if (format == TimeFormat::AmPm) {
        int h = rawHours % 12;
        if (h <= 0) {
            h += 12;
        }
        return h;
    }
    return rawHours;
}

uint16_t PongClockEngine::visibleMinuteOfDay(int hours, int minutes, TimeFormat format) {
    return (uint16_t)(visibleHour(hours, format) * 60 + (minutes % 60 + 60) % 60);
}

PongClockEngine::PaddleTempo PongClockEngine::nextTempoMode(PaddleTempo current, bool ballNear, bool chaseSide, uint8_t seed) {
    uint8_t roll = (uint8_t)(esp_random() % 100U);
    roll = (uint8_t)((roll + (seed * 17U)) % 100U);

    uint8_t holdWeight = ballNear ? 12U : 28U;
    uint8_t driftWeight = ballNear ? 28U : 36U;
    uint8_t cruiseWeight = ballNear ? 30U : 24U;
    uint8_t burstWeight = ballNear ? 30U : 12U;

    if (chaseSide) {
        holdWeight = tempoBias(holdWeight, -6);
        driftWeight = tempoBias(driftWeight, -4);
        cruiseWeight = tempoBias(cruiseWeight, 6);
        burstWeight = tempoBias(burstWeight, 10);
    } else {
        holdWeight = tempoBias(holdWeight, 10);
        driftWeight = tempoBias(driftWeight, 4);
        cruiseWeight = tempoBias(cruiseWeight, -4);
        burstWeight = tempoBias(burstWeight, -6);
    }

    if (current == PaddleTempo::Hold) {
        holdWeight = tempoBias(holdWeight, 8);
    } else if (current == PaddleTempo::Burst) {
        burstWeight = tempoBias(burstWeight, 8);
    }

    uint8_t total = (uint8_t)(holdWeight + driftWeight + cruiseWeight + burstWeight);
    if (total == 0) {
        return PaddleTempo::Cruise;
    }
    roll %= total;
    if (roll < holdWeight) return PaddleTempo::Hold;
    roll = (uint8_t)(roll - holdWeight);
    if (roll < driftWeight) return PaddleTempo::Drift;
    roll = (uint8_t)(roll - driftWeight);
    if (roll < cruiseWeight) return PaddleTempo::Cruise;
    return PaddleTempo::Burst;
}

uint16_t PongClockEngine::tempoStepDelay(PaddleTempo tempo, bool ballNear, bool chaseSide, uint8_t seed) {
    switch (tempo) {
        case PaddleTempo::Hold:
            return randomRange(ballNear ? 100U : 140U, ballNear ? 200U : 280U);
        case PaddleTempo::Drift:
            return randomRange(56U, ballNear ? 100U : 120U) + (uint16_t)(seed & 5U);
        case PaddleTempo::Cruise:
            return randomRange(ballNear ? 24U : 36U, ballNear ? 60U : 80U) + (uint16_t)((seed >> 1) & 7U);
        case PaddleTempo::Burst:
        default:
            return randomRange(ballNear ? 12U : 18U, chaseSide ? 40U : 52U) + (uint16_t)((seed >> 2) & 3U);
    }
}

uint8_t PongClockEngine::tempoStepCap(PaddleTempo tempo) {
    switch (tempo) {
        case PaddleTempo::Hold:
            return 0;
        case PaddleTempo::Drift:
            return 1;
        case PaddleTempo::Cruise:
            return 2;
        case PaddleTempo::Burst:
        default:
            return 3;
    }
}

int8_t PongClockEngine::clampPaddleY(int y) {
    if (y < PONG_PADDLE_MIN_Y) return (int8_t)PONG_PADDLE_MIN_Y;
    if (y > PONG_PADDLE_MAX_Y) return (int8_t)PONG_PADDLE_MAX_Y;
    return (int8_t)y;
}

int8_t PongClockEngine::centerPaddleY() {
    int scoreBottom = PONG_SCORE_Y + SEC_FONT_HEIGHT - 1;
    int mid = (scoreBottom + PONG_PLAY_BOTTOM) / 2;
    return (int8_t)(mid - PONG_PADDLE_HEIGHT / 2);
}

int8_t PongClockEngine::awayPaddleY(int ballY) {
    return (ballY < ((PONG_PLAY_TOP + PONG_PLAY_BOTTOM) / 2))
        ? (int8_t)PONG_PADDLE_MAX_Y
        : (int8_t)PONG_PADDLE_MIN_Y;
}

int PongClockEngine::glyphWidth(uint8_t glyph) {
    if (glyph >= 10) return 0;
    int left = FONT_SMALL_COLS;
    int right = -1;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < SEC_FONT_HEIGHT; row++) {
            if (FONT_SMALL[glyph][row][col]) {
                if (col < left) left = col;
                if (col > right) right = col;
            }
        }
    }
    return right >= left ? (right - left + 1) : 0;
}

int PongClockEngine::textWidth(const char* s) {
    int width = 0;
    bool inWord = false;
    while (*s) {
        if (*s == ' ') {
            if (inWord) {
                width += 2;
                inWord = false;
            }
        } else {
            if (inWord) {
                width += 1;
            }
            width += glyphWidth((uint8_t)charToGlyphIndex(*s));
            inWord = true;
        }
        ++s;
    }
    return width;
}

PongClockEngine::ScoreLayout PongClockEngine::scoreLayout(TimeFormat format) const {
    ScoreLayout layout;
    char leftBuf[4];
    char rightBuf[4];
    snprintf(leftBuf, sizeof(leftBuf), "%u", (unsigned)visibleHour(_state.view.scoreHour, format));
    snprintf(rightBuf, sizeof(rightBuf), "%u", (unsigned)_state.view.scoreMinute);

    layout.leftX = 0;
    layout.leftWidth = textWidth(leftBuf);
    layout.rightWidth = textWidth(rightBuf);
    layout.rightX = COLS_PER_ROW - layout.rightWidth;
    layout.topY = PONG_SCORE_Y;
    layout.bottomY = PONG_SCORE_Y + SEC_FONT_HEIGHT - 1;
    return layout;
}

void PongClockEngine::syncSnapshot(TimeFormat format) {
    _state.view.ballX = _state.ballXF / PONG_VEL_BASE;
    _state.view.ballY = _state.ballYF / PONG_VEL_BASE;
    _state.view.ballDx = (_state.ballDxF > 0) ? 1 : (_state.ballDxF < 0) ? -1 : 0;
    _state.view.ballDy = (_state.ballDyF > 0) ? 1 : (_state.ballDyF < 0) ? -1 : 0;
    _snapshot = _state.view;
    _snapshot.scoreHour = (uint8_t)visibleHour(_state.view.scoreHour, format);
    _snapshot.scoreMinute = _state.view.scoreMinute;
    if (_snapshot.phase != Phase::MissFlash &&
        _snapshot.phase != Phase::CenterBallHold &&
        _snapshot.phase != Phase::ScoreHold) {
        _snapshot.ballVisible = true;
    }
}

void PongClockEngine::prepareServeState(ClockTime time, unsigned long nowMs, TimeFormat format) {
    (void)format;
    _state.view.scoreHour = (uint8_t)time.hours;
    _state.view.scoreMinute = (uint8_t)time.minutes;
    _state.view.pendingMiss = false;
    _state.view.pendingMissSide = MissSide::None;
    _state.view.pendingScoreValid = false;
    _state.view.ballX = PONG_BALL_START_X;
    _state.view.ballY = PONG_BALL_START_Y;
    _state.view.leftPaddleY = centerPaddleY();
    _state.view.rightPaddleY = centerPaddleY();
    _state.leftTargetY = _state.view.leftPaddleY;
    _state.rightTargetY = _state.view.rightPaddleY;
    _state.view.ballDx = 0;
    _state.view.ballDy = 0;
    _state.phaseUntilMs = 0;
    _state.rallyStartMs = 0;
    _state.physicsDividerCounter = 0;
    _state.leftTempoSeed = (uint8_t)(esp_random() & 0xFFU);
    _state.rightTempoSeed = (uint8_t)(esp_random() & 0xFFU);
    _state.leftTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
    _state.rightTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
    _state.leftTempoUntilMs = nowMs + randomRange(160U, 340U);
    _state.rightTempoUntilMs = nowMs + randomRange(140U, 360U);
    _state.leftNextMoveMs = nowMs + tempoStepDelay(_state.leftTempo, false, false, _state.leftTempoSeed);
    _state.rightNextMoveMs = nowMs + tempoStepDelay(_state.rightTempo, false, false, _state.rightTempoSeed);
    _state.rallyHits = 0;
    _state.ballXF = PONG_BALL_START_X * PONG_VEL_BASE;
    _state.ballYF = PONG_BALL_START_Y * PONG_VEL_BASE;
    _state.view.ballVisible = true;
}

void PongClockEngine::armRally(TimeFormat format, ClockTime time, unsigned long nowMs) {
    (void)format;
    _state.view.phase = Phase::Rally;
    _state.phaseUntilMs = 0;
    _state.rallyStartMs = nowMs;
    int idx = (time.hours + time.minutes + time.seconds) % kServeVelCount;
    int16_t dx = kServeVels[idx][0];
    int16_t dy = kServeVels[idx][1];
    static constexpr int16_t kMinServeComp = 4;
    if (abs(dx) < kMinServeComp) dx = (dx >= 0) ? kMinServeComp : -kMinServeComp;
    if (abs(dy) < kMinServeComp) dy = (dy >= 0) ? kMinServeComp : -kMinServeComp;
    if ((_state.view.pendingMissSide == MissSide::Right)) {
        dx = -abs(dx);
    } else if (_state.view.pendingMissSide == MissSide::Left) {
        dx = abs(dx);
    } else if ((time.hours + time.minutes + time.seconds + (int)((nowMs >> 8) & 0xFF)) & 1) {
        dx = abs(dx);
    } else {
        dx = -abs(dx);
    }
    _state.ballDxF = dx;
    _state.ballDyF = dy;
    _state.view.ballDx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    _state.view.ballDy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
    _state.leftTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
    _state.rightTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
    _state.leftTempoUntilMs = nowMs + randomRange(120U, 260U);
    _state.rightTempoUntilMs = nowMs + randomRange(120U, 260U);
    _state.leftNextMoveMs = nowMs + tempoStepDelay(_state.leftTempo, false, false, _state.leftTempoSeed);
    _state.rightNextMoveMs = nowMs + tempoStepDelay(_state.rightTempo, false, false, _state.rightTempoSeed);
}

void PongClockEngine::resetBall(ClockTime time, unsigned long nowMs, TimeFormat format) {
    prepareServeState(time, nowMs, format);
    _state.view.phase = Phase::Rally;
    armRally(format, time, nowMs);
}

void PongClockEngine::startFreshServe(ClockTime time, unsigned long nowMs, TimeFormat format) {
    prepareServeState(time, nowMs, format);
    _state.view.phase = Phase::CenterBallHold;
    _state.phaseUntilMs = nowMs + PONG_CENTER_BALL_HOLD_MS;
    _state.view.ballVisible = true;
}

static void nudgeBallAngle(int16_t& ballDxF, int16_t& ballDyF, int16_t& ballXF, int16_t& ballYF) {
    static constexpr int16_t kMaxVel = 12;
    int16_t dxStep = (esp_random() & 1U) ? 1 : -1;
    int16_t dyStep = (esp_random() & 1U) ? 1 : -1;

    int16_t nextDx = ballDxF + dxStep;
    int16_t nextDy = ballDyF + dyStep;

    if (nextDx == 0) {
        nextDx += (dxStep > 0) ? 1 : -1;
    }
    if (nextDy == 0) {
        nextDy += (dyStep > 0) ? 1 : -1;
    }

    if (abs(nextDx) > kMaxVel) {
        nextDx = (nextDx > 0) ? kMaxVel : -kMaxVel;
    }
    if (abs(nextDy) > kMaxVel) {
        nextDy = (nextDy > 0) ? kMaxVel : -kMaxVel;
    }

    ballDxF = nextDx;
    ballDyF = nextDy;
    ballXF += dxStep;
    ballYF += dyStep;
}

void PongClockEngine::update(ClockTime time, unsigned long nowMs, TimeFormat format) {
    if (!_state.initialized) {
        reset(time, nowMs, format);
    }

    auto beginMissSequence = [&](MissSide missSide) {
        _state.view.pendingMiss = false;
        _state.view.pendingMissSide = missSide;
        _state.view.pendingScoreValid = true;
        _state.view.phase = Phase::MissFlash;
        _state.phaseUntilMs = nowMs + PONG_MISS_FLASH_MS;
        _state.rallyHits = 0;
        _state.view.ballVisible = true;
        int8_t wallX = (missSide == MissSide::Left) ? 0 : (COLS_PER_ROW - 1);
        _state.ballXF = wallX * PONG_VEL_BASE;
        _state.ballYF = _state.lossExitY * PONG_VEL_BASE;
        _state.ballDxF = 0;
        _state.ballDyF = 0;
        _state.view.ballX = wallX;
        _state.view.ballY = _state.lossExitY;
        _state.view.ballDx = 0;
        _state.view.ballDy = 0;
        _state.leftTargetY = centerPaddleY();
        _state.rightTargetY = centerPaddleY();
        _state.leftNextMoveMs = nowMs;
        _state.rightNextMoveMs = nowMs;
        _state.leftTempo = PaddleTempo::Cruise;
        _state.rightTempo = PaddleTempo::Cruise;
        _state.leftTempoUntilMs = nowMs + 240U;
        _state.rightTempoUntilMs = nowMs + 240U;
    };

    auto movePaddleToward = [&](bool leftSide, int targetY, uint16_t minDelay, uint16_t spreadDelay, uint8_t stepCap) {
        int8_t& paddleY = leftSide ? _state.view.leftPaddleY : _state.view.rightPaddleY;
        int8_t& target = leftSide ? _state.leftTargetY : _state.rightTargetY;
        unsigned long& nextMoveAtMs = leftSide ? _state.leftNextMoveMs : _state.rightNextMoveMs;
        bool quietCenter = _state.view.phase == Phase::MissFlash ||
                           _state.view.phase == Phase::CenterBallHold ||
                           _state.view.phase == Phase::ScoreHold;

        if ((int32_t)(nowMs - nextMoveAtMs) < 0) {
            return;
        }

        target = clampPaddleY(targetY);
        int step = 1 + (int)(esp_random() % stepCap);
        if (paddleY + 1 < target) {
            paddleY = clampPaddleY(paddleY + step);
        } else if (paddleY > target + 1) {
            paddleY = clampPaddleY(paddleY - step);
        } else if (!quietCenter && (esp_random() & 3U) == 0) {
            paddleY = clampPaddleY(paddleY + (((esp_random() & 1U) == 0) ? -1 : 1));
        }

        nextMoveAtMs = nowMs + jitterDelay(minDelay, spreadDelay);
    };

    if (_state.view.phase == Phase::MissFlash) {
        MissSide loseSide = _state.view.pendingMissSide;
        int32_t remaining = (int32_t)(_state.phaseUntilMs - nowMs);
        unsigned long elapsed = PONG_MISS_FLASH_MS - (unsigned long)max(remaining, (int32_t)0);

        if (loseSide == MissSide::Left) {
            movePaddleToward(false, centerPaddleY(), 88, 32, 1);
        } else {
            movePaddleToward(true, centerPaddleY(), 88, 32, 1);
        }

        int8_t awayY = awayPaddleY(_state.lossExitY);
        int8_t wallX = (loseSide == MissSide::Left) ? 0 : (COLS_PER_ROW - 1);
        _state.view.ballX = wallX;
        _state.view.ballY = _state.lossExitY;

        if (elapsed < 2000) {
            // Stage 1: paddle slow blink, ball steady
            _state.view.ballVisible = true;
            bool paddleOn = ((nowMs / 350) & 1) == 0;
            if (loseSide == MissSide::Left)
                _state.view.leftPaddleY = paddleOn ? awayY : -4;
            else
                _state.view.rightPaddleY = paddleOn ? awayY : -4;
        } else {
            // Stage 2: ball fast blink, paddle steady
            _state.view.ballVisible = ((nowMs / 75) & 1) == 0;
            if (loseSide == MissSide::Left)
                _state.view.leftPaddleY = awayY;
            else
                _state.view.rightPaddleY = awayY;
        }

        if (remaining < 0) {
            _state.view.ballVisible = false;
            _state.view.leftPaddleY = centerPaddleY();
            _state.view.rightPaddleY = centerPaddleY();
            _state.oldScoreHour = _state.view.scoreHour;
            _state.oldScoreMinute = _state.view.scoreMinute;
            _state.view.pendingScoreHour = (uint8_t)time.hours;
            _state.view.pendingScoreMinute = (uint8_t)time.minutes;
            _state.view.pendingScoreValid = false;
            _state.view.phase = Phase::ScoreHold;
            _state.phaseUntilMs = nowMs + PONG_SCORE_HOLD_MS;
            _state.rallyHits = 0;
            _state.ballXF = PONG_BALL_START_X * PONG_VEL_BASE;
            _state.ballYF = PONG_BALL_START_Y * PONG_VEL_BASE;
            _state.ballDxF = 0;
            _state.ballDyF = 0;
            _state.leftNextMoveMs = nowMs;
            _state.rightNextMoveMs = nowMs;
            _state.leftTempo = PaddleTempo::Cruise;
            _state.rightTempo = PaddleTempo::Cruise;
            _state.leftTempoUntilMs = nowMs + 240U;
            _state.rightTempoUntilMs = nowMs + 240U;
        }
        syncSnapshot(format);
        return;
    }

    if (_state.view.phase == Phase::CenterBallHold) {
        movePaddleToward(true, centerPaddleY(), 44, 18, 1);
        movePaddleToward(false, centerPaddleY(), 44, 18, 1);

        int32_t cbRemaining = (int32_t)(_state.phaseUntilMs - nowMs);
        unsigned long cbElapsed = PONG_CENTER_BALL_HOLD_MS - (unsigned long)max(cbRemaining, (int32_t)0);

        if (cbElapsed < 1000) {
            _state.view.ballVisible = ((nowMs / 75) & 1) == 0;
        } else {
            _state.view.ballVisible = true;
        }

        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }
        _state.physicsDividerCounter = 0;
        armRally(format, time, nowMs);
        syncSnapshot(format);
        return;
    }

    if (_state.view.phase == Phase::ScoreHold) {
        _state.view.oldScoreHour = _state.oldScoreHour;
        _state.view.oldScoreMinute = _state.oldScoreMinute;
        movePaddleToward(true, centerPaddleY(), 36, 12, 1);
        movePaddleToward(false, centerPaddleY(), 36, 12, 1);

        int32_t scoreRemaining = (int32_t)(_state.phaseUntilMs - nowMs);
        unsigned long scoreElapsed = PONG_SCORE_HOLD_MS - (unsigned long)max(scoreRemaining, (int32_t)0);

        static constexpr unsigned long FLASH_END = PONG_SCORE_FLASH_MS;
        static constexpr unsigned long BLANK_END = FLASH_END + PONG_SCORE_HOLD_BLANK_MS;

        bool rightWon = (_state.view.pendingMissSide == MissSide::Left);

        if (scoreElapsed < FLASH_END) {
            // Phase 1: flash old score
            bool blinkOn = ((scoreElapsed / (PONG_SCORE_FLASH_MS / 5)) & 1) == 1;
            if (rightWon) {
                _state.view.rightScoreVisible = blinkOn;
                _state.view.leftScoreVisible = true;
            } else {
                _state.view.leftScoreVisible = blinkOn;
                _state.view.rightScoreVisible = true;
            }
        } else if (scoreElapsed < BLANK_END) {
            // Phase 2: hold old score steady
            _state.view.leftScoreVisible = true;
            _state.view.rightScoreVisible = true;
        } else {
            // Show new score steady
            _state.view.scoreHour = _state.view.pendingScoreHour;
            _state.view.scoreMinute = _state.view.pendingScoreMinute;
            _state.view.leftScoreVisible = true;
            _state.view.rightScoreVisible = true;
        }

        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }
        _state.view.phase = Phase::CenterBallHold;
        _state.phaseUntilMs = nowMs + PONG_CENTER_BALL_HOLD_MS;
        _state.rallyHits = 0;
        _state.view.ballVisible = true;
        _state.view.ballX = PONG_BALL_START_X;
        _state.view.ballY = PONG_BALL_START_Y;
        _state.ballXF = PONG_BALL_START_X * PONG_VEL_BASE;
        _state.ballYF = PONG_BALL_START_Y * PONG_VEL_BASE;
        _state.ballDxF = 0;
        _state.ballDyF = 0;
        _state.view.ballDx = 0;
        _state.view.ballDy = 0;
        _state.leftNextMoveMs = nowMs;
        _state.rightNextMoveMs = nowMs;
        _state.physicsDividerCounter = 0;
        syncSnapshot(format);
        return;
    }

    uint16_t displayedMinuteOfDay = visibleMinuteOfDay(_state.view.scoreHour, _state.view.scoreMinute, format);
    uint16_t currentMinuteOfDay = visibleMinuteOfDay(time.hours, time.minutes, format);
    bool scoreStale = currentMinuteOfDay != displayedMinuteOfDay;

    int displayedHourVisible = visibleHour(_state.view.scoreHour, format);
    int currentHourVisible = visibleHour(time.hours, format);
    MissSide currentMissSide = (currentHourVisible != displayedHourVisible) ? MissSide::Right : MissSide::Left;
    MissSide leadInMissSide = (time.minutes == 59) ? MissSide::Right : MissSide::Left;

    if (!scoreStale && _state.view.phase == Phase::Rally && time.seconds >= 56) {
        _state.view.pendingMiss = true;
        _state.view.pendingMissSide = leadInMissSide;
        _state.view.phase = Phase::LeadIn;
    }

    if (scoreStale) {
        if (!_state.view.pendingMiss || _state.view.pendingMissSide != currentMissSide) {
            _state.view.pendingMiss = true;
            _state.view.pendingMissSide = currentMissSide;
        }
        if (_state.view.phase == Phase::Rally || _state.view.phase == Phase::LeadIn) {
            _state.view.phase = Phase::MissFlight;
        }
    }

    if (_state.view.phase == Phase::Rally ||
        _state.view.phase == Phase::LeadIn ||
        _state.view.phase == Phase::MissFlight) {
        unsigned long rallyAgeMs = nowMs - _state.rallyStartMs;
        if (scoreStale && rallyAgeMs >= PONG_STALE_BREAK_MS) {
            _state.view.pendingMiss = true;
            _state.view.pendingMissSide = currentMissSide;
            _state.lossExitY = _state.view.ballY;
            beginMissSequence(currentMissSide);
            syncSnapshot(format);
            return;
        }
        if (!scoreStale && rallyAgeMs >= PONG_RALLY_MAX_MS) {
            startFreshServe(time, nowMs, format);
            syncSnapshot(format);
            return;
        }
    }

    auto updatePaddle = [&](bool leftSide) {
        int8_t& paddleY = leftSide ? _state.view.leftPaddleY : _state.view.rightPaddleY;
        int8_t& targetY = leftSide ? _state.leftTargetY : _state.rightTargetY;
        unsigned long& nextMoveAtMs = leftSide ? _state.leftNextMoveMs : _state.rightNextMoveMs;
        PaddleTempo& tempo = leftSide ? _state.leftTempo : _state.rightTempo;
        unsigned long& tempoUntilMs = leftSide ? _state.leftTempoUntilMs : _state.rightTempoUntilMs;
        uint8_t tempoSeed = leftSide ? _state.leftTempoSeed : _state.rightTempoSeed;
        bool missThisSide = _state.view.pendingMiss &&
                            ((leftSide && _state.view.pendingMissSide == MissSide::Left) ||
                             (!leftSide && _state.view.pendingMissSide == MissSide::Right));

        if ((int32_t)(nowMs - nextMoveAtMs) < 0) {
            return;
        }

        int ballDistance = abs(_state.view.ballX - (leftSide ? 0 : (COLS_PER_ROW - 1)));
        bool ballNear = ballDistance <= 6;
        bool ballApproaching = leftSide ? (_state.view.ballDx < 0) : (_state.view.ballDx > 0);
        bool chaseSide = ballApproaching || ballNear;
        bool perfectTrack = !missThisSide &&
            (_state.view.phase == Phase::Rally || _state.view.phase == Phase::LeadIn);
        bool earlyTrack = false;
        bool needUrgency = false;
        if (perfectTrack && ballApproaching) {
            int paddleDist = abs((int)paddleY - (int)targetY);
            int ballDistToWall = leftSide ? _state.view.ballX : (COLS_PER_ROW - 1 - _state.view.ballX);
            earlyTrack = ballDistToWall <= (12 + paddleDist * 2);
            needUrgency = ballDistToWall <= (4 + paddleDist);
        }
        bool missCommit = missThisSide &&
                          _state.view.phase == Phase::MissFlight &&
                          ballDistance <= PONG_MISS_COMMIT_DISTANCE;

        if (needUrgency) {
            tempo = PaddleTempo::Burst;
        } else if (earlyTrack) {
            tempo = PaddleTempo::Cruise;
        } else if (_state.view.phase == Phase::LeadIn && missThisSide) {
            tempo = PaddleTempo::Burst;
        } else if ((int32_t)(nowMs - tempoUntilMs) >= 0) {
            if (missCommit) {
                tempo = PaddleTempo::Burst;
            } else if (_state.view.phase == Phase::CenterBallHold ||
                       _state.view.phase == Phase::ScoreHold) {
                tempo = PaddleTempo::Cruise;
            } else {
                tempo = nextTempoMode(tempo, ballNear, chaseSide, tempoSeed);
            }
            tempoUntilMs = nowMs + randomRange(ballNear ? 120U : 180U, ballNear ? 300U : 420U);
        }

        int aim;
        if (missCommit) {
            aim = awayPaddleY(_state.view.ballY);
        } else if (_state.view.phase == Phase::CenterBallHold ||
                   _state.view.phase == Phase::ScoreHold) {
            aim = centerPaddleY();
        } else if (_state.view.phase == Phase::LeadIn && missThisSide) {
            aim = centerPaddleY();
        } else {
            aim = predictIntercept(_state.ballXF, _state.ballYF,
                                    _state.ballDxF, _state.ballDyF,
                                    leftSide ? 0 : (COLS_PER_ROW - 1),
                                    SEC_FONT_HEIGHT, PONG_PLAY_BOTTOM) - (PONG_PADDLE_HEIGHT / 2);
            if (_state.view.phase == Phase::LeadIn && !missThisSide) {
                aim += (leftSide ? -1 : 1);
            }
        }

        int desiredTarget = clampPaddleY(aim);
        int previousTarget = targetY;
        int lagWeight = 0;

        if (perfectTrack && !missCommit && !needUrgency) {
            if (ballDistance > 12) {
                lagWeight = 3;
            } else if (ballDistance > 7) {
                lagWeight = 2;
            } else {
                lagWeight = 1;
            }
            if (ballApproaching) {
                lagWeight += 1;
            }
        } else if (earlyTrack) {
            lagWeight = 1;
        }

        if (lagWeight > 0) {
            desiredTarget = clampPaddleY((previousTarget * lagWeight + desiredTarget) / (lagWeight + 1));
        }

        int maxTargetDelta = 0;
        if (needUrgency) {
            maxTargetDelta = ballNear ? 2 : 1;
        } else if (earlyTrack) {
            maxTargetDelta = 1;
        } else if (perfectTrack) {
            maxTargetDelta = ballNear ? 1 : 2;
        } else {
            maxTargetDelta = 2;
        }

        if (desiredTarget > previousTarget + maxTargetDelta) {
            desiredTarget = previousTarget + maxTargetDelta;
        } else if (desiredTarget < previousTarget - maxTargetDelta) {
            desiredTarget = previousTarget - maxTargetDelta;
        }

        targetY = clampPaddleY(desiredTarget);

        int stepCap = 0;
        if (needUrgency) {
            stepCap = ballNear ? 2 : 1;
        } else if (earlyTrack) {
            stepCap = ballNear ? 1 : 1;
        } else if (_state.view.phase == Phase::CenterBallHold ||
                   _state.view.phase == Phase::ScoreHold) {
            stepCap = 1;
        } else if (tempo == PaddleTempo::Hold) {
            stepCap = 0;
        } else if (tempo == PaddleTempo::Drift) {
            stepCap = 1;
        } else if (tempo == PaddleTempo::Cruise) {
            stepCap = ballNear ? 2 : 1;
        } else {
            stepCap = ballNear ? 3 : 2;
        }

        int step = 0;
        if (stepCap > 0) {
            step = 1 + (int)(esp_random() % stepCap);
            if (!needUrgency && !ballNear) {
                if (tempo == PaddleTempo::Drift && ((esp_random() & 3U) == 0)) {
                    step = 0;
                } else if (tempo == PaddleTempo::Cruise && ((esp_random() & 7U) == 0)) {
                    step = 0;
                } else if (tempo == PaddleTempo::Burst && ((esp_random() & 11U) == 0)) {
                    step = 0;
                }
            }
            if (missCommit) {
                step = 1 + (int)(esp_random() % 2U);
            }
        }

        if (step > 0) {
            if (paddleY + 1 < targetY) {
                paddleY = clampPaddleY(paddleY + step);
            } else if (paddleY > targetY + 1) {
                paddleY = clampPaddleY(paddleY - step);
            }
        }

        uint16_t delay;
        if (_state.view.phase == Phase::CenterBallHold ||
                   _state.view.phase == Phase::ScoreHold) {
            delay = jitterDelay(56U, 16U);
        } else if (earlyTrack) {
            delay = randomRange(ballNear ? 18U : 28U, ballNear ? 36U : 52U);
        } else if (needUrgency) {
            delay = randomRange(12U, 24U);
        } else if (missCommit) {
            delay = tempoStepDelay(PaddleTempo::Burst, ballNear, chaseSide, tempoSeed);
        } else {
            delay = tempoStepDelay(tempo, ballNear, chaseSide, tempoSeed);
        }
        nextMoveAtMs = nowMs + delay;
    };

    updatePaddle(true);
    updatePaddle(false);

    _state.physicsDividerCounter++;
    if (_state.physicsDividerCounter < PONG_PHYSICS_DIVIDER) {
        syncSnapshot(format);
        return;
    }
    _state.physicsDividerCounter = 0;

    _state.ballXF += _state.ballDxF;
    _state.ballYF += _state.ballDyF;
    int prevBallX = _state.view.ballX;
    int prevBallY = _state.view.ballY;
    int nextX = _state.ballXF / PONG_VEL_BASE;
    int nextY = _state.ballYF / PONG_VEL_BASE;
    _state.view.ballX = (int8_t)nextX;
    _state.view.ballY = (int8_t)nextY;
    _state.view.ballDx = (_state.ballDxF > 0) ? 1 : (_state.ballDxF < 0) ? -1 : 0;
    _state.view.ballDy = (_state.ballDyF > 0) ? 1 : (_state.ballDyF < 0) ? -1 : 0;
    bool bounced = false;
    ScoreLayout layout = scoreLayout(format);

    auto maybeNudgeResonance = [&]() {
        if (_state.rallyHits < PONG_RESONANCE_HIT_THRESHOLD) {
            return;
        }
        if ((_state.rallyHits % PONG_RESONANCE_NUDGE_INTERVAL) != 0) {
            return;
        }
        nudgeBallAngle(_state.ballDxF, _state.ballDyF, _state.ballXF, _state.ballYF);
        _state.view.ballDx = (_state.ballDxF > 0) ? 1 : (_state.ballDxF < 0) ? -1 : 0;
        _state.view.ballDy = (_state.ballDyF > 0) ? 1 : (_state.ballDyF < 0) ? -1 : 0;
    };

    auto bounceOffScoreBox = [&](int boxLeft, int boxWidth, int boxTop, int boxBottom) {
        int boxRight = boxLeft + boxWidth - 1;
        if (nextX < boxLeft || nextX > boxRight || nextY < boxTop || nextY > boxBottom) {
            return false;
        }

        bool hitSide = (prevBallX < boxLeft) || (prevBallX > boxRight);
        bool hitVertical = (prevBallY < boxTop) || (prevBallY > boxBottom);
        if (!hitSide && !hitVertical) {
            _state.ballXF = (_state.ballDxF >= 0)
                ? (boxRight + 1) * PONG_VEL_BASE
                : (boxLeft - 1) * PONG_VEL_BASE;
            _state.ballYF = (_state.ballDyF >= 0)
                ? (boxBottom + 1) * PONG_VEL_BASE
                : (boxTop - 1) * PONG_VEL_BASE;
        } else {
            if (hitSide) {
                _state.ballDxF = -_state.ballDxF;
                _state.view.ballDx = (_state.ballDxF > 0) ? 1 : -1;
                _state.ballXF = (_state.ballDxF > 0)
                    ? (boxRight + 1) * PONG_VEL_BASE
                    : (boxLeft - 1) * PONG_VEL_BASE;
            }
            if (hitVertical) {
                _state.ballDyF = -_state.ballDyF;
                _state.view.ballDy = (_state.ballDyF > 0) ? 1 : -1;
                _state.ballYF = (_state.ballDyF > 0)
                    ? (boxBottom + 1) * PONG_VEL_BASE
                    : (boxTop - 1) * PONG_VEL_BASE;
            }
        }
        nextX = _state.ballXF / PONG_VEL_BASE;
        nextY = _state.ballYF / PONG_VEL_BASE;
        bounced = true;
        return true;
    };

    if (bounceOffScoreBox(layout.leftX, layout.leftWidth, layout.topY, layout.bottomY) ||
        bounceOffScoreBox(layout.rightX, layout.rightWidth, layout.topY, layout.bottomY)) {
        // handled by lambda
    }

    if (nextY < PONG_PLAY_TOP) {
        _state.ballYF = PONG_PLAY_TOP * PONG_VEL_BASE;
        _state.ballDyF = abs(_state.ballDyF);
        nextY = PONG_PLAY_TOP;
        bounced = true;
        maybeNudgeResonance();
    } else if (nextY > PONG_PLAY_BOTTOM) {
        _state.ballYF = PONG_PLAY_BOTTOM * PONG_VEL_BASE;
        _state.ballDyF = -abs(_state.ballDyF);
        nextY = PONG_PLAY_BOTTOM;
        bounced = true;
        maybeNudgeResonance();
    }

    if (_state.view.ballDx < 0 && nextX <= 0) {
        int yLo = prevBallY < nextY ? prevBallY : nextY;
        int yHi = prevBallY > nextY ? prevBallY : nextY;
        int paddleBot = _state.view.leftPaddleY + PONG_PADDLE_HEIGHT;
        bool hit = yHi >= _state.view.leftPaddleY && yLo < paddleBot;
        if (hit) {
            _state.rallyHits++;
            if (_state.rallyHits >= 2 && (_state.rallyHits % 2 == 0)) {
                int16_t factor = 100 + min((int16_t)((_state.rallyHits / 2) * 8), (int16_t)60);
                _state.ballDxF = (_state.ballDxF * factor) / 100;
                _state.ballDyF = (_state.ballDyF * factor) / 100;
                if (abs(_state.ballDxF) > PONG_VEL_CLIP)
                    _state.ballDxF = (_state.ballDxF > 0) ? PONG_VEL_CLIP : -PONG_VEL_CLIP;
                if (abs(_state.ballDyF) > PONG_VEL_CLIP)
                    _state.ballDyF = (_state.ballDyF > 0) ? PONG_VEL_CLIP : -PONG_VEL_CLIP;
            }
            int impact = nextY - _state.view.leftPaddleY;
            static const int16_t kRetFrac[] = { -6, -3, 3, 6 };
            int16_t speed = max(abs(_state.ballDxF), abs(_state.ballDyF));
            _state.ballDyF = (kRetFrac[impact < 0 ? 0 : (impact > 3 ? 3 : impact)] * speed) / PONG_VEL_BASE;
            _state.ballDxF = abs(_state.ballDxF);
            _state.ballXF = PONG_VEL_BASE;
            _state.ballYF = nextY * PONG_VEL_BASE;
            maybeNudgeResonance();
            syncSnapshot(format);
            return;
        } else if (nextX < 0) {
            if (_state.view.pendingMiss && _state.view.pendingMissSide == MissSide::Left &&
                (_state.view.phase == Phase::MissFlight || scoreStale)) {
                bool inScoreY = yHi >= 0 && yLo < SEC_FONT_HEIGHT;
                if (!inScoreY) {
                    _state.lossExitY = nextY;
                    beginMissSequence(MissSide::Left);
                    syncSnapshot(format);
                    return;
                }
            }
            _state.ballXF = 0;
            _state.ballDxF = PONG_VEL_BASE;
            nextX = 0;
            _state.view.ballDx = 1;
            bounced = true;
        }
    } else if (_state.view.ballDx > 0 && nextX >= COLS_PER_ROW - 1) {
        int yLo = prevBallY < nextY ? prevBallY : nextY;
        int yHi = prevBallY > nextY ? prevBallY : nextY;
        int paddleBot = _state.view.rightPaddleY + PONG_PADDLE_HEIGHT;
        bool hit = yHi >= _state.view.rightPaddleY && yLo < paddleBot;
        if (hit) {
            _state.rallyHits++;
            if (_state.rallyHits >= 2 && (_state.rallyHits % 2 == 0)) {
                int16_t factor = 100 + min((int16_t)((_state.rallyHits / 2) * 8), (int16_t)60);
                _state.ballDxF = (_state.ballDxF * factor) / 100;
                _state.ballDyF = (_state.ballDyF * factor) / 100;
                if (abs(_state.ballDxF) > PONG_VEL_CLIP)
                    _state.ballDxF = (_state.ballDxF > 0) ? PONG_VEL_CLIP : -PONG_VEL_CLIP;
                if (abs(_state.ballDyF) > PONG_VEL_CLIP)
                    _state.ballDyF = (_state.ballDyF > 0) ? PONG_VEL_CLIP : -PONG_VEL_CLIP;
            }
            int impact = nextY - _state.view.rightPaddleY;
            static const int16_t kRetFrac[] = { -6, -3, 3, 6 };
            int16_t speed = max(abs(_state.ballDxF), abs(_state.ballDyF));
            _state.ballDyF = (kRetFrac[impact < 0 ? 0 : (impact > 3 ? 3 : impact)] * speed) / PONG_VEL_BASE;
            _state.ballDxF = -abs(_state.ballDxF);
            int16_t rightEdge = (COLS_PER_ROW - 1) * PONG_VEL_BASE;
            _state.ballXF = rightEdge - PONG_VEL_BASE;
            _state.ballYF = nextY * PONG_VEL_BASE;
            maybeNudgeResonance();
            syncSnapshot(format);
            return;
        } else if (nextX >= COLS_PER_ROW) {
            if (_state.view.pendingMiss && _state.view.pendingMissSide == MissSide::Right &&
                (_state.view.phase == Phase::MissFlight || scoreStale)) {
                bool inScoreY = yHi >= 0 && yLo < SEC_FONT_HEIGHT;
                if (!inScoreY) {
                    _state.lossExitY = nextY;
                    beginMissSequence(MissSide::Right);
                    syncSnapshot(format);
                    return;
                }
            }
            _state.ballXF = (COLS_PER_ROW - 1) * PONG_VEL_BASE;
            _state.ballDxF = -PONG_VEL_BASE;
            nextX = COLS_PER_ROW - 1;
            _state.view.ballDx = -1;
            bounced = true;
        }
    }

    _state.view.ballX = (int8_t)nextX;
    _state.view.ballY = (int8_t)nextY;

    if (!bounced && (_state.view.ballX < 0 || _state.view.ballX >= COLS_PER_ROW)) {
        if (_state.view.pendingMiss && (_state.view.phase == Phase::MissFlight || scoreStale)) {
            int yLo = prevBallY < _state.view.ballY ? prevBallY : _state.view.ballY;
            int yHi = prevBallY > _state.view.ballY ? prevBallY : _state.view.ballY;
            bool inScoreY = yHi >= 0 && yLo < SEC_FONT_HEIGHT;
            if (!inScoreY) {
                _state.lossExitY = _state.view.ballY;
                beginMissSequence(_state.view.pendingMissSide);
            }
        } else {
            if (_state.view.ballX < 0) {
                _state.ballXF = 0;
                _state.view.ballX = 0;
            } else if (_state.view.ballX >= COLS_PER_ROW) {
                _state.ballXF = (COLS_PER_ROW - 1) * PONG_VEL_BASE;
                _state.view.ballX = COLS_PER_ROW - 1;
            }
        }
    }

    syncSnapshot(format);
}
