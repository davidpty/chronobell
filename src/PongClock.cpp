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

static bool scoreBoxContainsPx(int x, int y, int boxLeft, int boxWidth, int boxTop, int boxBottom) {
    if (boxWidth <= 0) {
        return false;
    }
    int boxRight = boxLeft + boxWidth - 1;
    return x >= boxLeft && x <= boxRight && y >= boxTop && y <= boxBottom;
}

static int scoreBoxRightPx(int boxLeft, int boxWidth) {
    return boxLeft + boxWidth - 1;
}

static void bumpCounter(uint16_t& counter) {
    if (counter < 0xFFFFU) {
        ++counter;
    }
}

static int predictInterceptWithScoreBoxes(int16_t ballXF, int16_t ballYF,
                                          int16_t velXF, int16_t velYF,
                                          int paddleXPx, int topPx, int bottomPx,
                                          const PongClockEngine::ScoreLayout& layout) {
    int16_t xF = ballXF;
    int16_t yF = ballYF;
    int16_t dxF = velXF;
    int16_t dyF = velYF;

    if (dxF == 0) {
        return yF / kVelBase;
    }

    for (int step = 0; step < 180; ++step) {
        int prevX = xF / kVelBase;
        int prevY = yF / kVelBase;

        xF += dxF;
        yF += dyF;

        int nextX = xF / kVelBase;
        int nextY = yF / kVelBase;

        if (nextY < topPx) {
            yF = topPx * kVelBase;
            dyF = abs(dyF);
            nextY = topPx;
        } else if (nextY > bottomPx) {
            yF = bottomPx * kVelBase;
            dyF = -abs(dyF);
            nextY = bottomPx;
        }

        auto reflectScore = [&](int boxLeft, int boxWidth, int boxTop, int boxBottom) {
            if (boxWidth <= 0) return;
            int boxRight = scoreBoxRightPx(boxLeft, boxWidth);
            bool inside = scoreBoxContainsPx(nextX, nextY, boxLeft, boxWidth, boxTop, boxBottom);
            if (!inside) return;

            bool fromLeft = prevX < boxLeft;
            bool fromRight = prevX > boxRight;
            bool fromBelow = prevY > boxBottom;
            bool fromAbove = prevY < boxTop;
            bool fromSide = fromLeft || fromRight;
            bool fromVertical = fromBelow || fromAbove;

            if (fromSide || (!fromVertical && abs(dxF) >= abs(dyF))) {
                if (fromRight || (!fromLeft && nextX > (boxLeft + boxRight) / 2)) {
                    dxF = abs(dxF);
                    xF = (boxRight + 1) * kVelBase;
                } else {
                    dxF = -abs(dxF);
                    xF = (boxLeft - 1) * kVelBase;
                }
            } else {
                // Score boxes sit at the top of the screen. If the ball ever
                // gets embedded or the entry side is ambiguous, expel it below
                // the score box instead of allowing repeated internal bounces.
                if (fromAbove) {
                    dyF = -abs(dyF);
                    yF = (boxTop - 1) * kVelBase;
                } else {
                    dyF = abs(dyF);
                    yF = (boxBottom + 1) * kVelBase;
                }
            }

            nextX = xF / kVelBase;
            nextY = yF / kVelBase;
            if (scoreBoxContainsPx(nextX, nextY, boxLeft, boxWidth, boxTop, boxBottom)) {
                yF = (boxBottom + 1) * kVelBase;
                dyF = abs(dyF);
                if (dyF == 0) dyF = kVelBase;
            }
        };

        reflectScore(layout.leftX, layout.leftWidth, layout.topY, layout.bottomY);
        reflectScore(layout.rightX, layout.rightWidth, layout.topY, layout.bottomY);

        nextX = xF / kVelBase;
        if ((dxF < 0 && nextX <= paddleXPx) ||
            (dxF > 0 && nextX >= paddleXPx)) {
            return yF / kVelBase;
        }
    }

    return (topPx + bottomPx) / 2;
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

static bool segmentIntersectsRect(int32_t x0, int32_t y0,
                                  int32_t x1, int32_t y1,
                                  int32_t left, int32_t top,
                                  int32_t right, int32_t bottom) {
    int32_t minX = min(x0, x1);
    int32_t maxX = max(x0, x1);
    int32_t minY = min(y0, y1);
    int32_t maxY = max(y0, y1);
    if (maxX < left || minX > right || maxY < top || minY > bottom) {
        return false;
    }

    // Liang-Barsky clipping against the four rectangle edges.
    double dx = (double)(x1 - x0);
    double dy = (double)(y1 - y0);
    double p[4] = { -dx, dx, -dy, dy };
    double q[4] = {
        (double)(x0 - left),
        (double)(right - x0),
        (double)(y0 - top),
        (double)(bottom - y0)
    };

    double u1 = 0.0;
    double u2 = 1.0;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) {
                return false;
            }
            continue;
        }
        double t = q[i] / p[i];
        if (p[i] < 0.0) {
            if (t > u2) return false;
            if (t > u1) u1 = t;
        } else {
            if (t < u1) return false;
            if (t < u2) u2 = t;
        }
    }
    return u1 <= u2;
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

int8_t PongClockEngine::playfieldCenterY() {
    int scoreBottom = PONG_SCORE_Y + SEC_FONT_HEIGHT - 1;
    int mid = (scoreBottom + PONG_PLAY_BOTTOM) / 2;
    return (int8_t)(mid - PONG_PADDLE_HEIGHT / 2);
}

int8_t PongClockEngine::servePaddleY() {
    // Initial/serve hold position: vertically align the paddle with the served ball.
    // This keeps the startup pose visually coherent: the ball starts at y=10,
    // so a 4 px paddle starts at y=8 and covers y=8..11.
    return clampPaddleY(PONG_BALL_START_Y - (PONG_PADDLE_HEIGHT / 2));
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
    _state.view.leftPaddleY = servePaddleY();
    _state.view.rightPaddleY = servePaddleY();
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
        if (_state.lossExitY < 0) {
            _state.lossExitY = 0;
        } else if (_state.lossExitY > PONG_PLAY_BOTTOM) {
            _state.lossExitY = PONG_PLAY_BOTTOM;
        }
        int8_t wallX = (missSide == MissSide::Left) ? 0 : (COLS_PER_ROW - 1);
        _state.lossExitX = wallX;
        _state.lossPaddleY = (missSide == MissSide::Left)
            ? _state.view.leftPaddleY
            : _state.view.rightPaddleY;
        _state.ballXF = wallX * PONG_VEL_BASE;
        _state.ballYF = _state.lossExitY * PONG_VEL_BASE;
        _state.ballDxF = 0;
        _state.ballDyF = 0;
        _state.view.ballX = wallX;
        _state.view.ballY = _state.lossExitY;
        _state.view.ballDx = 0;
        _state.view.ballDy = 0;
        _state.leftTargetY = servePaddleY();
        _state.rightTargetY = servePaddleY();
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

        if (stepCap == 0) {
            return;
        }

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

    auto movePaddleOnePixelSmooth = [&](bool leftSide, int targetY, uint16_t intervalMs) {
        int8_t& paddleY = leftSide ? _state.view.leftPaddleY : _state.view.rightPaddleY;
        int8_t& target = leftSide ? _state.leftTargetY : _state.rightTargetY;
        unsigned long& nextMoveAtMs = leftSide ? _state.leftNextMoveMs : _state.rightNextMoveMs;

        target = clampPaddleY(targetY);

        if ((int32_t)(nowMs - nextMoveAtMs) < 0) {
            return;
        }

        // Deterministic settle movement: one visible pixel at a time, no jitter,
        // no +/-1 deadband, and no random hold. This avoids both missed movement
        // and sudden jumps during the new-score/serve handoff.
        if (paddleY < target) {
            paddleY = clampPaddleY(paddleY + 1);
            nextMoveAtMs = nowMs + intervalMs;
        } else if (paddleY > target) {
            paddleY = clampPaddleY(paddleY - 1);
            nextMoveAtMs = nowMs + intervalMs;
        } else {
            nextMoveAtMs = nowMs + intervalMs;
        }
    };

    if (_state.view.phase == Phase::MissFlash) {
        MissSide loseSide = _state.view.pendingMissSide;
        int32_t remaining = (int32_t)(_state.phaseUntilMs - nowMs);
        unsigned long elapsed = PONG_MISS_FLASH_MS - (unsigned long)max(remaining, (int32_t)0);

        if (loseSide == MissSide::Left) {
            movePaddleToward(false, servePaddleY(), 88, 32, 1);
        } else {
            movePaddleToward(true, servePaddleY(), 88, 32, 1);
        }

        int8_t missY = clampPaddleY(_state.lossPaddleY);
        int8_t wallX = (loseSide == MissSide::Left) ? 0 : (COLS_PER_ROW - 1);
        _state.view.ballX = wallX;
        _state.view.ballY = _state.lossExitY;

        if (elapsed < 2000) {
            // Stage 1: paddle slow blink exactly where it missed; ball steady.
            _state.view.ballVisible = true;
            bool paddleOn = ((nowMs / 350) & 1) == 0;
            if (loseSide == MissSide::Left)
                _state.view.leftPaddleY = paddleOn ? missY : -4;
            else
                _state.view.rightPaddleY = paddleOn ? missY : -4;
        } else {
            // Stage 2: ball fast blink at the exit point; paddle stays at miss position.
            _state.view.ballVisible = ((nowMs / 75) & 1) == 0;
            if (loseSide == MissSide::Left)
                _state.view.leftPaddleY = missY;
            else
                _state.view.rightPaddleY = missY;
        }

        if (remaining < 0) {
            _state.oldScoreHour = _state.view.scoreHour;
            _state.oldScoreMinute = _state.view.scoreMinute;
            _state.view.pendingScoreHour = (uint8_t)time.hours;
            _state.view.pendingScoreMinute = (uint8_t)time.minutes;
            _state.view.pendingScoreValid = false;
            _state.view.phase = Phase::ResetPause;
            _state.phaseUntilMs = nowMs + PONG_RESET_PAUSE_MS;
            _state.view.ballVisible = true;
            _state.view.ballX = _state.lossExitX;
            _state.view.ballY = _state.lossExitY;
            _state.ballXF = _state.lossExitX * PONG_VEL_BASE;
            _state.ballYF = _state.lossExitY * PONG_VEL_BASE;
            _state.ballDxF = 0;
            _state.ballDyF = 0;
            _state.view.ballDx = 0;
            _state.view.ballDy = 0;
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

    if (_state.view.phase == Phase::ResetPause) {
        _state.view.ballVisible = true;
        _state.view.ballX = _state.lossExitX;
        _state.view.ballY = _state.lossExitY;
        _state.ballXF = _state.lossExitX * PONG_VEL_BASE;
        _state.ballYF = _state.lossExitY * PONG_VEL_BASE;
        _state.ballDxF = 0;
        _state.ballDyF = 0;
        _state.view.ballDx = 0;
        _state.view.ballDy = 0;

        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }

        _state.view.phase = Phase::ScoreHold;
        _state.phaseUntilMs = nowMs + PONG_SCORE_HOLD_MS;
        _state.rallyHits = 0;
        _state.view.ballVisible = false;
        _state.leftNextMoveMs = nowMs;
        _state.rightNextMoveMs = nowMs;
        _state.leftTempo = PaddleTempo::Cruise;
        _state.rightTempo = PaddleTempo::Cruise;
        _state.leftTempoUntilMs = nowMs + 240U;
        _state.rightTempoUntilMs = nowMs + 240U;
        syncSnapshot(format);
        return;
    }

    if (_state.view.phase == Phase::CenterBallHold) {
        int32_t cbRemaining = (int32_t)(_state.phaseUntilMs - nowMs);
        unsigned long cbElapsed = PONG_CENTER_BALL_HOLD_MS - (unsigned long)max(cbRemaining, (int32_t)0);

        if (cbElapsed < PONG_NEW_SCORE_SETTLE_MS) {
            // New score settles with the centered serve ball visible and steady.
            // During this whole settle window, both paddles move deterministically
            // toward the serve position, one pixel at a time.
            movePaddleOnePixelSmooth(true, servePaddleY(), 125U);
            movePaddleOnePixelSmooth(false, servePaddleY(), 125U);
            _state.view.ballVisible = true;
        } else if (cbElapsed < (PONG_NEW_SCORE_SETTLE_MS + PONG_CENTER_BALL_BLINK_MS)) {
            // Then blink the centered ball quickly as the next serve cue. Keep
            // smoothing paddles in case one still has a pixel left to settle.
            movePaddleOnePixelSmooth(true, servePaddleY(), 90U);
            movePaddleOnePixelSmooth(false, servePaddleY(), 90U);
            _state.view.ballVisible = ((nowMs / 75) & 1) == 0;
        } else {
            // Finally hold the ball steady briefly before the rally starts.
            movePaddleOnePixelSmooth(true, servePaddleY(), 90U);
            movePaddleOnePixelSmooth(false, servePaddleY(), 90U);
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
        _state.leftTargetY = servePaddleY();
        _state.rightTargetY = servePaddleY();
        _state.leftNextMoveMs = nowMs;
        _state.rightNextMoveMs = nowMs;
        _state.physicsDividerCounter = 0;
        syncSnapshot(format);
        return;
    }

    uint16_t displayedMinuteOfDay = visibleMinuteOfDay(_state.view.scoreHour, _state.view.scoreMinute, format);
    uint16_t currentMinuteOfDay = visibleMinuteOfDay(time.hours, time.minutes, format);
    bool scoreStale = currentMinuteOfDay != displayedMinuteOfDay;
    if (scoreStale) {
        if (_state.scoreStaleSinceMs == 0) {
            _state.scoreStaleSinceMs = nowMs;
        }
    } else {
        _state.scoreStaleSinceMs = 0;
    }
    unsigned long scoreStaleAgeMs = scoreStale ? (nowMs - _state.scoreStaleSinceMs) : 0;
    bool stalePressure = scoreStaleAgeMs >= PONG_STALE_PRESSURE_MS;
    bool staleForce = scoreStaleAgeMs >= PONG_STALE_FORCE_MS;
    // A stale score waits for a believable point. Urgent mode never teleports a
    // loss; it only makes the next legitimate paddle return drive the ball toward
    // the required loser so the following game can catch up naturally.
    bool urgentScoreCatchup = scoreStale &&
        (staleForce || scoreStaleAgeMs >= 45000UL || time.seconds >= 50);

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
        // Do not force a score update by teleporting the ball to the losing wall.
        // The score may be stale briefly, but the loss sequence must start only
        // from a real ball exit at the intended side.
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
        // Do not make a stale-score loss happen by timeout or by moving the losing
        // paddle away while the ball is elsewhere. A loss is allowed only when the
        // ball is physically approaching that side and reaches the side naturally.
        bool pressureMissSide = false;

        if ((int32_t)(nowMs - nextMoveAtMs) < 0) {
            return;
        }

        int ballDistance = abs(_state.view.ballX - (leftSide ? 0 : (COLS_PER_ROW - 1)));
        bool ballNear = ballDistance <= 6;
        bool ballApproaching = leftSide ? (_state.view.ballDx < 0) : (_state.view.ballDx > 0);
        bool chaseSide = ballApproaching || ballNear;
        bool protectedSide = !missThisSide &&
            (_state.view.phase == Phase::Rally ||
             _state.view.phase == Phase::LeadIn ||
             _state.view.phase == Phase::MissFlight);
        bool perfectTrack = protectedSide;
        bool mustSave = protectedSide && ballApproaching;
        bool earlyTrack = false;
        bool needUrgency = false;
        int ballDistToWall = leftSide ? _state.view.ballX : (COLS_PER_ROW - 1 - _state.view.ballX);
        if (mustSave) {
            int paddleDist = abs((int)paddleY - (int)targetY);
            earlyTrack = true;
            needUrgency = ballDistToWall <= (6 + paddleDist);
        }
        bool missCommit = missThisSide &&
                          _state.view.phase == Phase::MissFlight &&
                          ballApproaching &&
                          ballDistance <= (urgentScoreCatchup ? 14 :
                                           (PONG_MISS_COMMIT_DISTANCE +
                                            (stalePressure ? 1 : 0) +
                                            (staleForce ? 2 : 0)));

        if (pressureMissSide) {
            tempo = PaddleTempo::Burst;
        } else if (mustSave) {
            tempo = PaddleTempo::Burst;
        } else if (needUrgency) {
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
        auto nearMissY = [&]() {
            int missAbove = _state.view.ballY + 1;
            int missBelow = _state.view.ballY - PONG_PADDLE_HEIGHT;
            return (_state.view.ballY < ((PONG_PLAY_TOP + PONG_PLAY_BOTTOM) / 2))
                ? clampPaddleY(missAbove)
                : clampPaddleY(missBelow);
        };

        if (missCommit) {
            aim = nearMissY();
        } else if (pressureMissSide) {
            aim = nearMissY();
        } else if (_state.view.phase == Phase::CenterBallHold ||
                   _state.view.phase == Phase::ScoreHold) {
            aim = servePaddleY();
        } else if (_state.view.phase == Phase::LeadIn && missThisSide) {
            aim = servePaddleY();
        } else {
            ScoreLayout currentLayout = scoreLayout(format);
            aim = predictInterceptWithScoreBoxes(_state.ballXF, _state.ballYF,
                                                 _state.ballDxF, _state.ballDyF,
                                                 leftSide ? 0 : (COLS_PER_ROW - 1),
                                                 SEC_FONT_HEIGHT, PONG_PLAY_BOTTOM,
                                                 currentLayout) - (PONG_PADDLE_HEIGHT / 2);
            if (_state.view.phase == Phase::LeadIn && !missThisSide) {
                aim += (leftSide ? -1 : 1);
            }
        }

        int desiredTarget = clampPaddleY(aim);
        int previousTarget = targetY;
        int lagWeight = 0;

        if (mustSave) {
            lagWeight = 0;
        } else if (perfectTrack && !missCommit && !needUrgency) {
            if (ballDistance > 12) {
                lagWeight = 3;
            } else if (ballDistance > 7) {
                lagWeight = 2;
            } else {
                lagWeight = 1;
            }
        } else if (earlyTrack) {
            lagWeight = 1;
        }

        if (lagWeight > 0) {
            desiredTarget = clampPaddleY((previousTarget * lagWeight + desiredTarget) / (lagWeight + 1));
        }

        int maxTargetDelta = 0;
        if (pressureMissSide) {
            maxTargetDelta = 2;
        } else if (mustSave) {
            maxTargetDelta = 3;
        } else if (needUrgency) {
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

        if (mustSave) {
            int paddleCenter = paddleY + (PONG_PADDLE_HEIGHT / 2);
            int targetCenter = targetY + (PONG_PADDLE_HEIGHT / 2);
            int travel = abs(paddleCenter - targetCenter);
            if (ballDistToWall <= travel + 4) {
                targetY = clampPaddleY(aim);
                tempo = PaddleTempo::Burst;
                nextMoveAtMs = nowMs;
            }
        }

        int stepCap = 0;
        if (mustSave) {
            stepCap = 1;
        } else if (needUrgency) {
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
            if (!mustSave && !needUrgency && !pressureMissSide && !ballNear) {
                if (tempo == PaddleTempo::Drift && ((esp_random() & 3U) == 0)) {
                    step = 0;
                } else if (tempo == PaddleTempo::Cruise && ((esp_random() & 7U) == 0)) {
                    step = 0;
                } else if (tempo == PaddleTempo::Burst && ((esp_random() & 11U) == 0)) {
                    step = 0;
                }
            }
            if (missCommit || pressureMissSide) {
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
        } else if (staleForce) {
            delay = randomRange(8U, 16U);
        } else if (pressureMissSide) {
            delay = randomRange(12U, 24U);
        } else if (mustSave) {
            delay = randomRange(12U, 22U);
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

    int16_t prevBallXF = _state.ballXF;
    int16_t prevBallYF = _state.ballYF;
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
        uint16_t hitThreshold = staleForce ? 2U : (stalePressure ? 4U : PONG_RESONANCE_HIT_THRESHOLD);
        uint16_t nudgeInterval = staleForce ? 2U : PONG_RESONANCE_NUDGE_INTERVAL;
        if (_state.rallyHits < hitThreshold) {
            return;
        }
        if ((_state.rallyHits % nudgeInterval) != 0) {
            return;
        }
        nudgeBallAngle(_state.ballDxF, _state.ballDyF, _state.ballXF, _state.ballYF);
        _state.view.ballDx = (_state.ballDxF > 0) ? 1 : (_state.ballDxF < 0) ? -1 : 0;
        _state.view.ballDy = (_state.ballDyF > 0) ? 1 : (_state.ballDyF < 0) ? -1 : 0;
    };

    auto bounceOffScoreBox = [&](int boxLeft, int boxWidth, int boxTop, int boxBottom) {
        if (boxWidth <= 0) {
            return false;
        }

        int boxRight = scoreBoxRightPx(boxLeft, boxWidth);
        bool insideNow = scoreBoxContainsPx(nextX, nextY, boxLeft, boxWidth, boxTop, boxBottom);
        bool sweptHit = segmentIntersectsRect(prevBallXF, prevBallYF, _state.ballXF, _state.ballYF,
                                              boxLeft * PONG_VEL_BASE, boxTop * PONG_VEL_BASE,
                                              ((boxRight + 1) * PONG_VEL_BASE) - 1,
                                              ((boxBottom + 1) * PONG_VEL_BASE) - 1);
        if (!insideNow && !sweptHit) {
            return false;
        }
        if (insideNow) {
            bumpCounter(_state.view.scoreBoxRecoveryCount);
        }

        bool fromLeft = prevBallX < boxLeft;
        bool fromRight = prevBallX > boxRight;
        bool fromBelow = prevBallY > boxBottom;
        bool fromAbove = prevBallY < boxTop;
        bool hitSide = fromLeft || fromRight;
        bool hitVertical = fromBelow || fromAbove;

        if (!hitSide && !hitVertical) {
            // Already embedded or a corner/fast-sweep ambiguity. Pick a single
            // clean exit face and place the ball outside the score box now.
            int distLeft = abs(nextX - boxLeft);
            int distRight = abs(boxRight - nextX);
            int distBottom = abs(boxBottom - nextY);
            if (distBottom <= distLeft && distBottom <= distRight) {
                hitVertical = true;
                fromBelow = true;
            } else {
                hitSide = true;
                if (distLeft <= distRight) {
                    fromLeft = true;
                } else {
                    fromRight = true;
                }
            }
        }

        if (hitSide && (!hitVertical || abs(_state.ballDxF) >= abs(_state.ballDyF))) {
            if (fromRight) {
                _state.ballDxF = abs(_state.ballDxF);
                if (_state.ballDxF == 0) _state.ballDxF = PONG_VEL_BASE;
                _state.ballXF = (boxRight + 1) * PONG_VEL_BASE;
            } else {
                _state.ballDxF = -abs(_state.ballDxF);
                if (_state.ballDxF == 0) _state.ballDxF = -PONG_VEL_BASE;
                _state.ballXF = (boxLeft - 1) * PONG_VEL_BASE;
            }
            _state.view.ballDx = (_state.ballDxF > 0) ? 1 : -1;
        } else {
            // The score is attached to the top edge, so the safe ambiguous
            // recovery is below the score area. This prevents score-box traps.
            if (fromAbove) {
                _state.ballDyF = -abs(_state.ballDyF);
                if (_state.ballDyF == 0) _state.ballDyF = -PONG_VEL_BASE;
                _state.ballYF = (boxTop - 1) * PONG_VEL_BASE;
            } else {
                _state.ballDyF = abs(_state.ballDyF);
                if (_state.ballDyF == 0) _state.ballDyF = PONG_VEL_BASE;
                _state.ballYF = (boxBottom + 1) * PONG_VEL_BASE;
            }
            _state.view.ballDy = (_state.ballDyF > 0) ? 1 : -1;
        }

        nextX = _state.ballXF / PONG_VEL_BASE;
        nextY = _state.ballYF / PONG_VEL_BASE;

        if (scoreBoxContainsPx(nextX, nextY, boxLeft, boxWidth, boxTop, boxBottom)) {
            bumpCounter(_state.view.scoreBoxRecoveryCount);
            _state.ballYF = (boxBottom + 1) * PONG_VEL_BASE;
            _state.ballDyF = abs(_state.ballDyF);
            if (_state.ballDyF == 0) _state.ballDyF = PONG_VEL_BASE;
            _state.view.ballDy = 1;
            nextY = boxBottom + 1;
        }

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
        bool allowedMiss = _state.view.pendingMiss &&
                           _state.view.pendingMissSide == MissSide::Left &&
                           (_state.view.phase == Phase::MissFlight || scoreStale);
        if (!hit && !allowedMiss) {
            bumpCounter(_state.view.forcedSaveCount);
            int legalY = clampPaddleY(nextY - (PONG_PADDLE_HEIGHT / 2));
            _state.view.leftPaddleY = legalY;
            _state.leftTargetY = legalY;
            hit = true;
        }
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
            if (urgentScoreCatchup && _state.view.pendingMissSide == MissSide::Right) {
                bumpCounter(_state.view.staleCatchupCount);
                _state.ballDxF = PONG_VEL_CLIP;
                _state.ballDyF = (nextY < playfieldCenterY()) ? 2 : -2;
            }
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
        bool allowedMiss = _state.view.pendingMiss &&
                           _state.view.pendingMissSide == MissSide::Right &&
                           (_state.view.phase == Phase::MissFlight || scoreStale);
        if (!hit && !allowedMiss) {
            bumpCounter(_state.view.forcedSaveCount);
            int legalY = clampPaddleY(nextY - (PONG_PADDLE_HEIGHT / 2));
            _state.view.rightPaddleY = legalY;
            _state.rightTargetY = legalY;
            hit = true;
        }
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
            if (urgentScoreCatchup && _state.view.pendingMissSide == MissSide::Left) {
                bumpCounter(_state.view.staleCatchupCount);
                _state.ballDxF = -PONG_VEL_CLIP;
                _state.ballDyF = (nextY < playfieldCenterY()) ? 2 : -2;
            }
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
        bool exitedLeft = _state.view.ballX < 0;
        bool exitedRight = _state.view.ballX >= COLS_PER_ROW;
        MissSide exitSide = exitedLeft ? MissSide::Left : MissSide::Right;
        bool correctExitSide = _state.view.pendingMiss &&
                               _state.view.pendingMissSide == exitSide &&
                               (_state.view.phase == Phase::MissFlight || scoreStale);
        int yLo = prevBallY < _state.view.ballY ? prevBallY : _state.view.ballY;
        int yHi = prevBallY > _state.view.ballY ? prevBallY : _state.view.ballY;
        bool inScoreY = yHi >= 0 && yLo < SEC_FONT_HEIGHT;

        if (correctExitSide && !inScoreY) {
            _state.lossExitY = _state.view.ballY;
            beginMissSequence(exitSide);
        } else if (exitedLeft) {
            _state.ballXF = 0;
            _state.view.ballX = 0;
            _state.ballDxF = abs(_state.ballDxF);
            _state.view.ballDx = 1;
        } else if (exitedRight) {
            _state.ballXF = (COLS_PER_ROW - 1) * PONG_VEL_BASE;
            _state.view.ballX = COLS_PER_ROW - 1;
            _state.ballDxF = -abs(_state.ballDxF);
            _state.view.ballDx = -1;
        }
    }

    syncSnapshot(format);
}
