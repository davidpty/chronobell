#ifndef DIGIT_TRANSITION_H
#define DIGIT_TRANSITION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Config.h"

namespace digit_transition {

template <size_t Rows, size_t Cols>
using Grid = std::array<std::array<uint8_t, Cols>, Rows>;

template <size_t Rows>
struct Span {
    double center = 0.0;
    double length = 0.0;
};

template <size_t Rows>
using RowSpans = std::vector<Span<Rows>>;

template <size_t Rows>
using GlyphSpans = std::array<RowSpans<Rows>, Rows>;

struct DigitCellState {
    bool initialized = false;
    bool visible = false;
    uint8_t digit = 0;
    bool active = false;
    bool fromVisible = false;
    uint8_t fromDigit = 0;
    bool toVisible = false;
    uint8_t toDigit = 0;
    unsigned long startMs = 0;

    void reset() {
        *this = {};
    }
};

struct DigitCellFrame {
    bool transition = false;
    bool fromVisible = false;
    uint8_t fromDigit = 0;
    bool toVisible = false;
    uint8_t toDigit = 0;
    double progress = 0.0;
};

static const unsigned long kTransitionDurationMs = TRANSITION_MS;

template <typename T>
T clamp_value(T value, T low, T high) {
    return value < low ? low : (value > high ? high : value);
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
GlyphSpans<Rows> extract_spans(const uint8_t (&font)[GlyphCount][Rows][Cols],
                               size_t glyph_index) {
    GlyphSpans<Rows> spans;
    const auto& glyph = font[glyph_index];

    for (size_t row = 0; row < Rows; ++row) {
        size_t col = 0;
        while (col < Cols) {
            while (col < Cols && glyph[row][col] == 0) {
                ++col;
            }
            if (col >= Cols) {
                break;
            }

            const size_t start = col;
            while (col < Cols && glyph[row][col] != 0) {
                ++col;
            }
            const size_t end = col - 1;

            Span<Rows> span;
            span.center = (static_cast<double>(start) + static_cast<double>(end)) / 2.0;
            span.length = static_cast<double>(end - start);
            spans[row].push_back(span);
        }
    }

    return spans;
}

template <size_t Rows, size_t Cols>
void clear_grid(Grid<Rows, Cols>& grid) {
    for (auto& row : grid) {
        row.fill(0);
    }
}

template <size_t Rows, size_t Cols, typename PixelWriter>
void emit_grid(PixelWriter&& writePixel,
               const Grid<Rows, Cols>& grid,
               int x,
               int y) {
    for (size_t row = 0; row < Rows; ++row) {
        for (size_t col = 0; col < Cols; ++col) {
            if (grid[row][col]) {
                writePixel(x + static_cast<int>(col), y + static_cast<int>(row), true);
            }
        }
    }
}

template <size_t Rows, size_t Cols, typename PixelWriter>
void write_span(PixelWriter&& writePixel, int x, int y, int row, double center, double length) {
    if (row < 0 || row >= static_cast<int>(Rows)) {
        return;
    }

    const double half = std::max(0.0, length) / 2.0;
    const double left = center - half;
    const double right = center + half;
    const int start = clamp_value(static_cast<int>(std::lround(left)), 0,
                                  static_cast<int>(Cols) - 1);
    const int end = clamp_value(static_cast<int>(std::lround(right)), 0,
                                static_cast<int>(Cols) - 1);

    for (int col = std::min(start, end); col <= std::max(start, end); ++col) {
        writePixel(x + col, y + row, true);
    }
}

template <size_t GlyphCount, size_t Rows, size_t Cols, typename PixelWriter>
void draw_glyph(PixelWriter&& writePixel,
                const uint8_t (&font)[GlyphCount][Rows][Cols],
                int glyphIndex,
                int x,
                int y) {
    if (glyphIndex < 0 || glyphIndex >= static_cast<int>(GlyphCount)) {
        return;
    }

    for (size_t row = 0; row < Rows; ++row) {
        for (size_t col = 0; col < Cols; ++col) {
            if (font[glyphIndex][row][col]) {
                writePixel(x + static_cast<int>(col), y + static_cast<int>(row), true);
            }
        }
    }
}

template <size_t Rows>
double ease_smoothstep(double t) {
    t = clamp_value(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

template <size_t Rows, size_t Cols>
Grid<Rows, Cols> morph_transition(const GlyphSpans<Rows>& from,
                                  const GlyphSpans<Rows>& to,
                                  double t) {
    Grid<Rows, Cols> grid{};
    clear_grid(grid);
    const double eased = ease_smoothstep<Rows>(t);

    for (size_t row = 0; row < Rows; ++row) {
        const auto& src = from[row];
        const auto& dst = to[row];
        const size_t paired = std::min(src.size(), dst.size());

        for (size_t i = 0; i < paired; ++i) {
            const double center = src[i].center + (dst[i].center - src[i].center) * eased;
            const double length = src[i].length + (dst[i].length - src[i].length) * eased;
            write_span<Rows, Cols>([&](int px, int py, bool on) {
                (void)on;
                grid[py - 0][px - 0] = 1;
            }, 0, 0, static_cast<int>(row), center, length);
        }

        for (size_t i = paired; i < src.size(); ++i) {
            const double center = src[i].center;
            const double length = src[i].length * (1.0 - eased);
            write_span<Rows, Cols>([&](int px, int py, bool on) {
                (void)on;
                grid[py - 0][px - 0] = 1;
            }, 0, 0, static_cast<int>(row), center, length);
        }

        for (size_t i = paired; i < dst.size(); ++i) {
            const double center = dst[i].center;
            const double length = dst[i].length * eased;
            write_span<Rows, Cols>([&](int px, int py, bool on) {
                (void)on;
                grid[py - 0][px - 0] = 1;
            }, 0, 0, static_cast<int>(row), center, length);
        }
    }

    return grid;
}

template <size_t GlyphCount, size_t Rows, size_t Cols, typename PixelWriter>
void draw_transition_glyph(PixelWriter&& writePixel,
                           const uint8_t (&font)[GlyphCount][Rows][Cols],
                           int fromGlyph,
                           int toGlyph,
                           double t,
                           int x,
                           int y) {
    if (TRANSITION != TRANSITION_MORPH) {
        draw_glyph(writePixel, font, toGlyph, x, y);
        return;
    }

    GlyphSpans<Rows> from;
    GlyphSpans<Rows> to;
    if (fromGlyph >= 0 && fromGlyph < static_cast<int>(GlyphCount)) {
        from = extract_spans(font, static_cast<size_t>(fromGlyph));
    }
    if (toGlyph >= 0 && toGlyph < static_cast<int>(GlyphCount)) {
        to = extract_spans(font, static_cast<size_t>(toGlyph));
    }

    Grid<Rows, Cols> grid = morph_transition<Rows, Cols>(from, to, t);
    emit_grid(writePixel, grid, x, y);
}

inline DigitCellFrame advance_cell(DigitCellState& state,
                                   bool visible,
                                   uint8_t digit,
                                   unsigned long nowMs) {
    DigitCellFrame frame;

    if (!state.initialized) {
        state.initialized = true;
        state.visible = visible;
        state.digit = digit;
        return frame;
    }

    if (!state.active) {
        if (state.visible == visible && state.digit == digit) {
            return frame;
        }

        state.active = true;
        state.fromVisible = state.visible;
        state.fromDigit = state.digit;
        state.toVisible = visible;
        state.toDigit = digit;
        state.startMs = nowMs;
    } else if (state.toVisible != visible || state.toDigit != digit) {
        state.fromVisible = state.toVisible;
        state.fromDigit = state.toDigit;
        state.toVisible = visible;
        state.toDigit = digit;
        state.startMs = nowMs;
    }

    const unsigned long elapsed = nowMs - state.startMs;
    if (elapsed >= kTransitionDurationMs) {
        state.active = false;
        state.visible = visible;
        state.digit = digit;
        return frame;
    }

    frame.transition = true;
    frame.fromVisible = state.fromVisible;
    frame.fromDigit = state.fromDigit;
    frame.toVisible = state.toVisible;
    frame.toDigit = state.toDigit;
    frame.progress = static_cast<double>(elapsed) / static_cast<double>(kTransitionDurationMs);
    return frame;
}

template <size_t GlyphCount, size_t Rows, size_t Cols, typename PixelWriter>
void render_digit_cell(PixelWriter&& writePixel,
                       const uint8_t (&font)[GlyphCount][Rows][Cols],
                       DigitCellState& state,
                       bool visible,
                       uint8_t digit,
                       int x,
                       int y,
                       unsigned long nowMs) {
    const DigitCellFrame frame = advance_cell(state, visible, digit, nowMs);
    if (frame.transition) {
        draw_transition_glyph(writePixel,
                              font,
                              frame.fromVisible ? static_cast<int>(frame.fromDigit) : -1,
                              frame.toVisible ? static_cast<int>(frame.toDigit) : -1,
                              frame.progress,
                              x,
                              y);
    } else {
        draw_glyph(writePixel, font, visible ? static_cast<int>(digit) : -1, x, y);
    }
}

}  // namespace digit_transition

#endif // DIGIT_TRANSITION_H
