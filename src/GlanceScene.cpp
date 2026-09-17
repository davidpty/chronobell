#include "GlanceScene.h"

#if FEATURE_GLANCE

#include "Config.h"
#include "Display.h"

GlanceScene::GlanceScene()
    : _phase(Phase::Idle)
    , _gaze(GazeDir::Center)
    , _startMs(0)
    , _phaseStartMs(0)
    , _pupilRadius(3)
    , _pupilVertShift(0)
    , _isBootGreeting(false)
    , _pupilContracted(false)
{
}

void GlanceScene::begin(GazeDir dir, bool isBootGreeting) {
    _gaze = dir;
    _isBootGreeting = isBootGreeting;
    _phase = Phase::Opening;
    _startMs = millis();
    _phaseStartMs = _startMs;
    _pupilContracted = false;

    uint32_t seed = esp_random();
    _pupilRadius = 3 + (seed & 1);
    _pupilVertShift = ((int8_t)((seed >> 1) & 3)) - 1;
    if (_pupilVertShift < -1) _pupilVertShift = 0;
}

void GlanceScene::update(unsigned long nowMs) {
    if (_phase == Phase::Idle || _phase == Phase::Finished) return;

    unsigned long elapsed = nowMs - _phaseStartMs;
    unsigned long dur = _isBootGreeting ? GREET_OPEN_MS : OPEN_MS;

    switch (_phase) {
        case Phase::Opening:
            if (elapsed >= dur) {
                _phase = Phase::Open;
                _phaseStartMs = nowMs;
            }
            break;

        case Phase::Open: {
            unsigned long holdDur = _isBootGreeting ? GREET_HOLD_MS : HOLD_MS;
            if (_isBootGreeting && !_pupilContracted && elapsed >= holdDur - 150) {
                _pupilContracted = true;
                if (_pupilRadius > 2) _pupilRadius--;
            }
            if (elapsed >= holdDur) {
                _phase = Phase::Closing;
                _phaseStartMs = nowMs;
            }
            break;
        }

        case Phase::Closing: {
            unsigned long closeDur = _isBootGreeting ? GREET_CLOSE_MS : CLOSE_MS;
            if (elapsed >= closeDur) {
                _phase = Phase::Finished;
            }
            break;
        }

        default:
            break;
    }
}

bool GlanceScene::isActive() const {
    return _phase != Phase::Idle && _phase != Phase::Finished;
}

void GlanceScene::handleTouch(GazeDir dir) {
    _gaze = dir;
    _phase = Phase::Opening;
    _phaseStartMs = millis();
    _pupilContracted = false;

    if (_isBootGreeting && _pupilRadius < 3) _pupilRadius = 3;
}

static int8_t gazeShift(GlanceScene::GazeDir dir) {
    switch (dir) {
        case GlanceScene::GazeDir::Left:   return -3;
        case GlanceScene::GazeDir::Right:  return 3;
        default:                           return 0;
    }
}

void GlanceScene::render(Display& display) {
    unsigned long nowMs = millis();
    update(nowMs);

    if (_phase == Phase::Idle || _phase == Phase::Finished) return;

    unsigned long phaseElapsed = nowMs - _phaseStartMs;
    unsigned long openDur = _isBootGreeting ? GREET_OPEN_MS : OPEN_MS;
    unsigned long closeDur = _isBootGreeting ? GREET_CLOSE_MS : CLOSE_MS;

    float openness;
    switch (_phase) {
        case Phase::Opening:
            openness = (float)phaseElapsed / (float)openDur;
            if (openness > 1.0f) openness = 1.0f;
            break;
        case Phase::Open:
            openness = 1.0f;
            break;
        case Phase::Closing:
            openness = 1.0f - (float)phaseElapsed / (float)closeDur;
            if (openness < 0.0f) openness = 0.0f;
            break;
        default:
            openness = 0.0f;
            break;
    }

    display.clearBuffer();

    int clipAmount = (int)(8.0f * (1.0f - openness));
    int visibleTop = clipAmount;
    int visibleBottom = 15 - clipAmount;

    if (visibleTop > visibleBottom) {
        display.renderBuffer();
        return;
    }

    int irisCx = 15;
    int irisCy = 7;
    int gazeOff = gazeShift(_gaze);
    int pupilCx = irisCx + gazeOff;
    int pupilCy = irisCy + _pupilVertShift;

    for (int y = visibleTop; y <= visibleBottom; y++) {
        for (int x = 0; x < COLS_PER_ROW; x++) {
            bool on = false;

            int dxSclera = x - irisCx;
            int dySclera = y - irisCy;
            float rx = 14.0f;
            float ry = 7.0f;
            if ((float)(dxSclera*dxSclera) / (rx*rx) + (float)(dySclera*dySclera) / (ry*ry) <= 1.0f) {
                on = true;
            }

            if (on) {
                int dxIris = x - irisCx;
                int dyIris = y - irisCy;
                float irisR = 6.0f;
                if ((float)(dxIris*dxIris + dyIris*dyIris) <= irisR*irisR) {
                    on = false;
                }
            }

            if (!on) {
                int dxPupil = x - pupilCx;
                int dyPupil = y - pupilCy;
                if (dxPupil*dxPupil + dyPupil*dyPupil <= _pupilRadius*_pupilRadius) {
                    on = true;
                }
            }

            display.setPixel(x, y, on);
        }
    }

    display.renderBuffer();
}

#endif
