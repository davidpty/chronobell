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

static PongClockEngine::MissEscape chooseMissEscape(const PongClockEngine::Snapshot& view,
                                                    PongClockEngine::MissSide missSide) {
    int paddleY = (missSide == PongClockEngine::MissSide::Left) ? view.leftPaddleY : view.rightPaddleY;
    bool ballUpper = view.ballY <= kPongMidY;
    bool paddleUpper = paddleY <= kPongMidY;
    int sideDistance = (missSide == PongClockEngine::MissSide::Left)
        ? view.ballX
        : (COLS_PER_ROW - 1 - view.ballX);

    if (ballUpper != paddleUpper) {
        return ballUpper ? PongClockEngine::MissEscape::TopGap : PongClockEngine::MissEscape::BottomGap;
    }
    if (sideDistance <= 5) {
        return PongClockEngine::MissEscape::SideExit;
    }
    if ((esp_random() & 1U) == 0) {
        return ballUpper ? PongClockEngine::MissEscape::TopGap : PongClockEngine::MissEscape::BottomGap;
    }
    return PongClockEngine::MissEscape::SideExit;
}

static void steerMissBall(PongClockEngine::Snapshot& view,
                          PongClockEngine::MissSide missSide,
                          PongClockEngine::MissEscape missEscape) {
    switch (missEscape) {
        case PongClockEngine::MissEscape::TopGap:
            view.ballDy = -1;
            view.ballDx = (view.ballX < kPongMidX) ? 1 : (view.ballX > kPongMidX ? -1 : 0);
            break;
        case PongClockEngine::MissEscape::BottomGap:
            view.ballDy = 1;
            view.ballDx = (view.ballX < kPongMidX) ? 1 : (view.ballX > kPongMidX ? -1 : 0);
            break;
        case PongClockEngine::MissEscape::SideExit:
            view.ballDx = (missSide == PongClockEngine::MissSide::Left) ? -1 : 1;
            if (view.ballY <= SEC_FONT_HEIGHT - 1) {
                view.ballDy = 1;
            } else if (view.ballY >= TOTAL_ROWS - 3) {
                view.ballDy = -1;
            } else {
                view.ballDy = 0;
            }
            break;
    }
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
    resetBall(time, nowMs, format, false);
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
            return randomRange(ballNear ? 120U : 180U, ballNear ? 260U : 360U);
        case PaddleTempo::Drift:
            return randomRange(66U, ballNear ? 120U : 150U) + (uint16_t)(seed & 5U);
        case PaddleTempo::Cruise:
            return randomRange(ballNear ? 28U : 44U, ballNear ? 72U : 98U) + (uint16_t)((seed >> 1) & 7U);
        case PaddleTempo::Burst:
        default:
            return randomRange(ballNear ? 14U : 22U, chaseSide ? 48U : 64U) + (uint16_t)((seed >> 2) & 3U);
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
    return (int8_t)((PONG_PADDLE_MIN_Y + PONG_PADDLE_MAX_Y) / 2);
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
    _snapshot = _state.view;
    _snapshot.scoreHour = (uint8_t)visibleHour(_state.view.scoreHour, format);
    _snapshot.scoreMinute = _state.view.scoreMinute;
    _snapshot.ballVisible = (_snapshot.phase != Phase::CenterTravel);
}

void PongClockEngine::resetBall(ClockTime time, unsigned long nowMs, TimeFormat format, bool afterMiss) {
    (void)format;
    _state.view.scoreHour = (uint8_t)time.hours;
    _state.view.scoreMinute = (uint8_t)time.minutes;
    _state.view.pendingMiss = false;
    _state.view.phase = afterMiss ? Phase::ResetPause : Phase::Rally;
    _state.view.pendingMissSide = MissSide::None;
    _state.view.pendingScoreValid = false;
    _state.view.ballX = PONG_BALL_START_X;
    _state.view.ballY = PONG_BALL_START_Y;
    _state.view.leftPaddleY = afterMiss ? centerPaddleY() : clampPaddleY(centerPaddleY() + (int)(esp_random() % 3U) - 1);
    _state.view.rightPaddleY = afterMiss ? centerPaddleY() : clampPaddleY(centerPaddleY() + (int)(esp_random() % 3U) - 1);
    _state.leftTargetY = _state.view.leftPaddleY;
    _state.rightTargetY = _state.view.rightPaddleY;
    _state.view.ballDx = 0;
    _state.view.ballDy = 0;
    _state.phaseUntilMs = afterMiss ? nowMs + PONG_RESET_PAUSE_MS : 0;
    _state.physicsDividerCounter = 0;
    _state.leftTempoSeed = (uint8_t)(esp_random() & 0xFFU);
    _state.rightTempoSeed = (uint8_t)(esp_random() & 0xFFU);
    _state.leftTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
    _state.rightTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
    _state.leftTempoUntilMs = nowMs + randomRange(160U, 340U);
    _state.rightTempoUntilMs = nowMs + randomRange(140U, 360U);
    _state.leftNextMoveMs = nowMs + tempoStepDelay(_state.leftTempo, false, false, _state.leftTempoSeed);
    _state.rightNextMoveMs = nowMs + tempoStepDelay(_state.rightTempo, false, false, _state.rightTempoSeed);

    int horizontalSeed = time.hours + time.minutes + time.seconds + (int)((nowMs >> 8) & 0xFF);
    int verticalSeed = time.minutes + time.seconds + (int)((nowMs >> 4) & 0xFF);
    if (afterMiss && _state.view.pendingMissSide == MissSide::Right) {
        _state.view.ballDx = -1;
    } else if (afterMiss && _state.view.pendingMissSide == MissSide::Left) {
        _state.view.ballDx = 1;
    } else {
        _state.view.ballDx = (horizontalSeed & 1) ? 1 : -1;
    }
    _state.view.ballDy = (verticalSeed & 1) ? 1 : -1;
    _state.view.ballVisible = _state.view.phase != Phase::CenterTravel;
}

void PongClockEngine::update(ClockTime time, unsigned long nowMs, TimeFormat format) {
    if (!_state.initialized) {
        reset(time, nowMs, format);
    }

    auto beginMissSequence = [&](MissSide missSide) {
        _state.view.pendingMiss = false;
        _state.view.pendingMissSide = missSide;
        _state.view.pendingScoreValid = true;
        _state.view.phase = Phase::CenterTravel;
        _state.phaseUntilMs = nowMs + PONG_CENTER_TRAVEL_MS;
        _state.view.ballVisible = false;
        _state.view.ballX = PONG_BALL_START_X;
        _state.view.ballY = PONG_BALL_START_Y;
        _state.view.ballDx = 0;
        _state.view.ballDy = 0;
        _state.physicsDividerCounter = 0;
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
        bool quietCenter = _state.view.phase == Phase::CenterTravel ||
                           _state.view.phase == Phase::CenterBallHold ||
                           _state.view.phase == Phase::ScoreHold ||
                           _state.view.phase == Phase::ResetPause;

        if ((int32_t)(nowMs - nextMoveAtMs) < 0) {
            return;
        }

        target = clampPaddleY(targetY);
        int step = 1 + (int)(esp_random() % stepCap);
        if (paddleY < target) {
            paddleY = clampPaddleY(paddleY + step);
        } else if (paddleY > target) {
            paddleY = clampPaddleY(paddleY - step);
        } else if (!quietCenter && (esp_random() & 3U) == 0) {
            paddleY = clampPaddleY(paddleY + (((esp_random() & 1U) == 0) ? -1 : 1));
        }

        nextMoveAtMs = nowMs + jitterDelay(minDelay, spreadDelay);
    };

    if (_state.view.phase == Phase::CenterTravel) {
        movePaddleToward(true, centerPaddleY(), 88, 32, 1);
        movePaddleToward(false, centerPaddleY(), 88, 32, 1);
        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }
        _state.view.ballVisible = true;
        _state.view.ballX = PONG_BALL_START_X;
        _state.view.ballY = PONG_BALL_START_Y;
        _state.view.phase = Phase::CenterBallHold;
        _state.phaseUntilMs = nowMs + PONG_CENTER_BALL_HOLD_MS;
        _state.physicsDividerCounter = 0;
        syncSnapshot(format);
        return;
    }

    if (_state.view.phase == Phase::CenterBallHold) {
        movePaddleToward(true, centerPaddleY(), 44, 18, 1);
        movePaddleToward(false, centerPaddleY(), 44, 18, 1);
        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }
        _state.view.pendingScoreHour = (uint8_t)time.hours;
        _state.view.pendingScoreMinute = (uint8_t)time.minutes;
        _state.view.scoreHour = _state.view.pendingScoreHour;
        _state.view.scoreMinute = _state.view.pendingScoreMinute;
        _state.view.pendingScoreValid = false;
        _state.view.phase = Phase::ScoreHold;
        _state.phaseUntilMs = nowMs + PONG_SCORE_HOLD_MS;
        _state.physicsDividerCounter = 0;
        syncSnapshot(format);
        return;
    }

    if (_state.view.phase == Phase::ScoreHold) {
        movePaddleToward(true, centerPaddleY(), 36, 12, 1);
        movePaddleToward(false, centerPaddleY(), 36, 12, 1);
        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }
        _state.view.phase = Phase::ResetPause;
        _state.phaseUntilMs = nowMs + PONG_RESET_PAUSE_MS;
        _state.view.ballX = PONG_BALL_START_X;
        _state.view.ballY = PONG_BALL_START_Y;
        _state.view.missEscape = MissEscape::SideExit;
        _state.view.ballDx = ((esp_random() & 1U) == 0) ? 1 : -1;
        _state.view.ballDy = ((esp_random() & 1U) == 0) ? 1 : -1;
        _state.view.ballVisible = true;
        _state.physicsDividerCounter = 0;
        _state.leftTempo = PaddleTempo::Cruise;
        _state.rightTempo = PaddleTempo::Cruise;
        _state.leftTempoUntilMs = nowMs + randomRange(120U, 240U);
        _state.rightTempoUntilMs = nowMs + randomRange(120U, 240U);
        syncSnapshot(format);
        return;
    }

    if (_state.view.phase == Phase::ResetPause) {
        movePaddleToward(true, centerPaddleY(), 34, 12, 1);
        movePaddleToward(false, centerPaddleY(), 34, 12, 1);
        if ((int32_t)(nowMs - _state.phaseUntilMs) < 0) {
            syncSnapshot(format);
            return;
        }
        _state.view.phase = Phase::Rally;
        _state.phaseUntilMs = 0;
        _state.view.missEscape = MissEscape::SideExit;
        _state.physicsDividerCounter = 0;
        _state.leftTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
        _state.rightTempo = (esp_random() & 1U) ? PaddleTempo::Cruise : PaddleTempo::Drift;
        _state.leftTempoUntilMs = nowMs + randomRange(120U, 260U);
        _state.rightTempoUntilMs = nowMs + randomRange(120U, 260U);
        _state.leftNextMoveMs = nowMs + tempoStepDelay(_state.leftTempo, false, false, _state.leftTempoSeed);
        _state.rightNextMoveMs = nowMs + tempoStepDelay(_state.rightTempo, false, false, _state.rightTempoSeed);
    }

    uint16_t displayedMinuteOfDay = visibleMinuteOfDay(_state.view.scoreHour, _state.view.scoreMinute, format);
    uint16_t currentMinuteOfDay = visibleMinuteOfDay(time.hours, time.minutes, format);
    bool scoreStale = currentMinuteOfDay != displayedMinuteOfDay;

    int displayedHourVisible = visibleHour(_state.view.scoreHour, format);
    int currentHourVisible = visibleHour(time.hours, format);
    MissSide currentMissSide = (currentHourVisible != displayedHourVisible) ? MissSide::Left : MissSide::Right;
    MissSide leadInMissSide = (time.minutes == 59) ? MissSide::Left : MissSide::Right;

    if (!scoreStale && _state.view.phase == Phase::Rally && time.seconds >= 56) {
        _state.view.pendingMiss = true;
        _state.view.pendingMissSide = leadInMissSide;
        _state.view.missEscape = chooseMissEscape(_state.view, leadInMissSide);
        _state.view.phase = Phase::LeadIn;
    }

    if (scoreStale) {
        if (!_state.view.pendingMiss || _state.view.pendingMissSide != currentMissSide) {
            _state.view.pendingMiss = true;
            _state.view.pendingMissSide = currentMissSide;
            _state.view.missEscape = chooseMissEscape(_state.view, currentMissSide);
        }
        if (_state.view.phase == Phase::Rally || _state.view.phase == Phase::LeadIn) {
            _state.view.phase = Phase::MissFlight;
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
        bool missCommit = missThisSide &&
                          _state.view.phase == Phase::MissFlight &&
                          ballDistance <= PONG_MISS_COMMIT_DISTANCE;

        if ((int32_t)(nowMs - tempoUntilMs) >= 0) {
            if (missCommit) {
                tempo = PaddleTempo::Burst;
            } else if (_state.view.phase == Phase::CenterTravel ||
                       _state.view.phase == Phase::CenterBallHold ||
                       _state.view.phase == Phase::ScoreHold ||
                       _state.view.phase == Phase::ResetPause) {
                tempo = PaddleTempo::Cruise;
            } else {
                tempo = nextTempoMode(tempo, ballNear, chaseSide, tempoSeed);
            }
            tempoUntilMs = nowMs + randomRange(ballNear ? 120U : 180U, ballNear ? 300U : 420U);
        }

        int aim;
        if (missCommit) {
            if (_state.view.missEscape == MissEscape::TopGap) {
                aim = PONG_PADDLE_MAX_Y;
            } else if (_state.view.missEscape == MissEscape::BottomGap) {
                aim = PONG_PADDLE_MIN_Y;
            } else {
                aim = (_state.view.ballY <= kPongMidY) ? PONG_PADDLE_MAX_Y : PONG_PADDLE_MIN_Y;
            }
        } else if (_state.view.phase == Phase::CenterTravel ||
                   _state.view.phase == Phase::CenterBallHold ||
                   _state.view.phase == Phase::ScoreHold ||
                   _state.view.phase == Phase::ResetPause) {
            aim = centerPaddleY();
        } else {
            aim = _state.view.ballY - (PONG_PADDLE_HEIGHT / 2);
            int wobble = (int)(esp_random() % 3U) - 1;
            int chaseJitter = (int)(esp_random() % 2U);
            if (_state.view.ballDy > 0) {
                aim += chaseJitter;
            } else if (_state.view.ballDy < 0) {
                aim -= chaseJitter;
            }
            aim += wobble;
            if (_state.view.phase == Phase::LeadIn && !missThisSide) {
                aim += (leftSide ? -1 : 1);
            }
        }

        targetY = clampPaddleY(aim);

        int stepCap = 0;
        if (_state.view.phase == Phase::CenterTravel ||
            _state.view.phase == Phase::CenterBallHold ||
            _state.view.phase == Phase::ScoreHold ||
            _state.view.phase == Phase::ResetPause) {
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
            if (tempo == PaddleTempo::Drift && ((esp_random() & 3U) == 0)) {
                step = 0;
            } else if (tempo == PaddleTempo::Cruise && ((esp_random() & 7U) == 0)) {
                step = 0;
            } else if (tempo == PaddleTempo::Burst && ((esp_random() & 11U) == 0)) {
                step = 0;
            }
            if (missCommit) {
                step = 1 + (int)(esp_random() % 2U);
            }
        }

        if (step > 0) {
            if (paddleY < targetY) {
                paddleY = clampPaddleY(paddleY + step);
            } else if (paddleY > targetY) {
                paddleY = clampPaddleY(paddleY - step);
            } else if (!missThisSide && ((esp_random() & 3U) == 0)) {
                paddleY = clampPaddleY(paddleY + (((esp_random() & 1U) == 0) ? -1 : 1));
            }
        }

        uint16_t delay;
        if (_state.view.phase == Phase::CenterTravel) {
            delay = jitterDelay(96U, 24U);
        } else if (_state.view.phase == Phase::CenterBallHold ||
                   _state.view.phase == Phase::ScoreHold ||
                   _state.view.phase == Phase::ResetPause) {
            delay = jitterDelay(56U, 16U);
        } else if (missCommit) {
            delay = tempoStepDelay(PaddleTempo::Burst, ballNear, chaseSide, tempoSeed);
        } else {
            delay = tempoStepDelay(tempo, ballNear, chaseSide, tempoSeed);
        }
        nextMoveAtMs = nowMs + delay;
    };

    updatePaddle(true);
    updatePaddle(false);

    if (_state.view.phase == Phase::MissFlight && _state.view.pendingMiss) {
        steerMissBall(_state.view, _state.view.pendingMissSide, _state.view.missEscape);
    }

    _state.physicsDividerCounter++;
    if (_state.physicsDividerCounter < PONG_PHYSICS_DIVIDER) {
        syncSnapshot(format);
        return;
    }
    _state.physicsDividerCounter = 0;

    int nextX = _state.view.ballX + _state.view.ballDx;
    int nextY = _state.view.ballY + _state.view.ballDy;
    bool bounced = false;
    ScoreLayout layout = scoreLayout(format);

    auto bounceOffScoreBox = [&](int boxLeft, int boxWidth, int boxTop, int boxBottom) {
        int boxRight = boxLeft + boxWidth - 1;
        if (nextX < boxLeft || nextX > boxRight || nextY < boxTop || nextY > boxBottom) {
            return false;
        }

        bool hitSide = (_state.view.ballX < boxLeft) || (_state.view.ballX > boxRight);
        bool hitVertical = (_state.view.ballY < boxTop) || (_state.view.ballY > boxBottom);
        if (hitSide) {
            _state.view.ballDx = (int8_t)-_state.view.ballDx;
        }
        if (hitVertical || (!hitSide && !hitVertical)) {
            _state.view.ballDy = (int8_t)-_state.view.ballDy;
        }
        nextX = _state.view.ballX + _state.view.ballDx;
        nextY = _state.view.ballY + _state.view.ballDy;
        bounced = true;
        return true;
    };

    if (bounceOffScoreBox(layout.leftX, layout.leftWidth, layout.topY, layout.bottomY) ||
        bounceOffScoreBox(layout.rightX, layout.rightWidth, layout.topY, layout.bottomY)) {
        // handled by lambda
    } else if (_state.view.phase == Phase::MissFlight && _state.view.pendingMiss) {
        bool missExit = false;
        switch (_state.view.missEscape) {
            case MissEscape::TopGap:
                missExit = (nextY < PONG_PLAY_TOP);
                break;
            case MissEscape::BottomGap:
                missExit = (nextY > PONG_PLAY_BOTTOM);
                break;
            case MissEscape::SideExit:
                missExit = (nextX < 0 || nextX >= COLS_PER_ROW);
                break;
        }
        if (missExit) {
            beginMissSequence(_state.view.pendingMissSide);
            syncSnapshot(format);
            return;
        }
    } else if (nextY < PONG_PLAY_TOP) {
        _state.view.ballDy = 1;
        nextY = PONG_PLAY_TOP;
        bounced = true;
    } else if (nextY > PONG_PLAY_BOTTOM) {
        nextY = PONG_PLAY_BOTTOM;
        _state.view.ballDy = -1;
        bounced = true;
    }

    if (_state.view.ballDx < 0 && nextX <= 0) {
        bool hit = nextY >= _state.view.leftPaddleY && nextY < _state.view.leftPaddleY + PONG_PADDLE_HEIGHT;
        if (hit) {
            _state.view.ballDx = 1;
            _state.view.ballX = 1;
            _state.view.ballY = (int8_t)nextY;
            int impact = nextY - _state.view.leftPaddleY;
            if (impact <= 1) {
                _state.view.ballDy = -1;
            } else if (impact >= PONG_PADDLE_HEIGHT - 2) {
                _state.view.ballDy = 1;
            } else {
                _state.view.ballDy = (_state.view.ballDy >= 0) ? -1 : 1;
            }
            syncSnapshot(format);
            return;
        } else if (nextX < 0) {
            if (_state.view.pendingMiss && _state.view.pendingMissSide == MissSide::Left &&
                (_state.view.phase == Phase::MissFlight || scoreStale)) {
                beginMissSequence(MissSide::Left);
                syncSnapshot(format);
                return;
            }
            nextX = 0;
            _state.view.ballDx = 1;
            bounced = true;
        }
    } else if (_state.view.ballDx > 0 && nextX >= COLS_PER_ROW - 1) {
        bool hit = nextY >= _state.view.rightPaddleY && nextY < _state.view.rightPaddleY + PONG_PADDLE_HEIGHT;
        if (hit) {
            _state.view.ballDx = -1;
            _state.view.ballX = COLS_PER_ROW - 2;
            _state.view.ballY = (int8_t)nextY;
            int impact = nextY - _state.view.rightPaddleY;
            if (impact <= 1) {
                _state.view.ballDy = -1;
            } else if (impact >= PONG_PADDLE_HEIGHT - 2) {
                _state.view.ballDy = 1;
            } else {
                _state.view.ballDy = (_state.view.ballDy >= 0) ? -1 : 1;
            }
            syncSnapshot(format);
            return;
        } else if (nextX >= COLS_PER_ROW) {
            if (_state.view.pendingMiss && _state.view.pendingMissSide == MissSide::Right &&
                (_state.view.phase == Phase::MissFlight || scoreStale)) {
                beginMissSequence(MissSide::Right);
                syncSnapshot(format);
                return;
            }
            nextX = COLS_PER_ROW - 1;
            _state.view.ballDx = -1;
            bounced = true;
        }
    }

    _state.view.ballX = (int8_t)nextX;
    _state.view.ballY = (int8_t)nextY;

    if (!bounced && (_state.view.ballX < 0 || _state.view.ballX >= COLS_PER_ROW)) {
        if (_state.view.pendingMiss && (_state.view.phase == Phase::MissFlight || scoreStale)) {
            beginMissSequence(_state.view.pendingMissSide);
        } else {
            if (_state.view.ballX < 0) {
                _state.view.ballX = 0;
            } else if (_state.view.ballX >= COLS_PER_ROW) {
                _state.view.ballX = COLS_PER_ROW - 1;
            }
        }
    }

    syncSnapshot(format);
}
