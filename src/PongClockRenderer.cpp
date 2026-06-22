#include "PongClockRenderer.h"

#include "Display.h"

void PongClockRenderer::drawScore(Display& display, const PongClockEngine& engine, TimeFormat format) const {
    PongClockEngine::ScoreLayout layout = engine.scoreLayout(format);
    char leftBuf[4];
    char rightBuf[4];
    snprintf(leftBuf, sizeof(leftBuf), "%u", (unsigned)engine.snapshot().scoreHour);
    snprintf(rightBuf, sizeof(rightBuf), "%u", (unsigned)engine.snapshot().scoreMinute);

    display.drawSmallText(leftBuf, layout.leftX, layout.topY);
    display.drawSmallText(rightBuf, layout.rightX, layout.topY);
}

void PongClockRenderer::drawPaddles(Display& display, const PongClockEngine::Snapshot& pong) const {
    for (int dy = 0; dy < 4; dy++) {
        int yLeft = pong.leftPaddleY + dy;
        int yRight = pong.rightPaddleY + dy;
        if (yLeft >= 0 && yLeft < TOTAL_ROWS) {
            display.setPixel(0, (uint8_t)yLeft, true);
        }
        if (yRight >= 0 && yRight < TOTAL_ROWS) {
            display.setPixel(COLS_PER_ROW - 1, (uint8_t)yRight, true);
        }
    }
}

void PongClockRenderer::drawBall(Display& display, const PongClockEngine::Snapshot& pong) const {
    if (pong.ballVisible &&
        pong.ballX >= 0 && pong.ballX < COLS_PER_ROW &&
        pong.ballY >= 0 && pong.ballY < TOTAL_ROWS) {
        display.setPixel((uint8_t)pong.ballX, (uint8_t)pong.ballY, true);
    }
}

void PongClockRenderer::render(Display& display, const PongClockEngine& engine, TimeFormat format) const {
    const auto& pong = engine.snapshot();
    drawScore(display, engine, format);
    drawPaddles(display, pong);
    drawBall(display, pong);
}
