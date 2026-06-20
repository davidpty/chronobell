#ifndef MATRIX_ANIM_H
#define MATRIX_ANIM_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace matrix_anim {

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

      spans[row].push_back(
          Span<Rows>{(static_cast<double>(start) + static_cast<double>(end)) / 2.0,
                     static_cast<double>(end - start)});
    }
  }

  return spans;
}

template <size_t Rows, size_t Cols>
void write_span(Grid<Rows, Cols>& grid, int row, double center, double length) {
  if (row < 0 || row >= static_cast<int>(Rows)) {
    return;
  }

  const double half = std::max(0.0, length) / 2.0;
  const double left = center - half;
  const double right = center + half;
  const int start = std::clamp(static_cast<int>(std::lround(left)), 0,
                               static_cast<int>(Cols) - 1);
  const int end = std::clamp(static_cast<int>(std::lround(right)), 0,
                             static_cast<int>(Cols) - 1);

  for (int col = std::min(start, end); col <= std::max(start, end); ++col) {
    grid[row][col] = 1;
  }
}

template <size_t Rows, size_t Cols>
void clear_grid(Grid<Rows, Cols>& grid) {
  for (auto& row : grid) {
    row.fill(0);
  }
}

template <size_t Rows, size_t Cols>
void render_grid(std::ostream& os,
                 const Grid<Rows, Cols>& grid,
                 int indent = 6,
                 std::string_view label = {}) {
  os << "\n\n";
  for (size_t row = 0; row < Rows; ++row) {
    os << std::string(indent, ' ');
    for (size_t col = 0; col < Cols; ++col) {
      os << (grid[row][col] ? "##" : "  ");
    }
    os << '\n';
  }
  if (!label.empty()) {
    os << "\n" << std::string(indent, ' ') << label << '\n';
  }
  os.flush();
}

template <size_t Rows, size_t Cols>
using TransitionFrame = Grid<Rows, Cols>;

template <size_t Rows, size_t Cols>
using TransitionFn = TransitionFrame<Rows, Cols> (*)(
    const GlyphSpans<Rows>&,
    const GlyphSpans<Rows>&,
    double);

template <size_t Rows, size_t Cols>
struct TransitionSpec {
  std::string_view name;
  TransitionFn<Rows, Cols> frame_fn;
};

template <size_t Rows>
double ease_smoothstep(double t) {
  return t * t * (3.0 - 2.0 * t);
}

template <size_t Rows, size_t Cols>
Grid<Rows, Cols> morph_transition(const GlyphSpans<Rows>& from,
                                  const GlyphSpans<Rows>& to,
                                  double t) {
  Grid<Rows, Cols> grid{};
  clear_grid(grid);
  const double eased = ease_smoothstep<Rows>(std::clamp(t, 0.0, 1.0));

  for (size_t row = 0; row < Rows; ++row) {
    const auto& src = from[row];
    const auto& dst = to[row];
    const size_t paired = std::min(src.size(), dst.size());

    for (size_t i = 0; i < paired; ++i) {
      const double center = src[i].center + (dst[i].center - src[i].center) * eased;
      const double length = src[i].length + (dst[i].length - src[i].length) * eased;
      write_span(grid, static_cast<int>(row), center, length);
    }

    for (size_t i = paired; i < src.size(); ++i) {
      const double center = src[i].center;
      const double length = src[i].length * (1.0 - eased);
      write_span(grid, static_cast<int>(row), center, length);
    }

    for (size_t i = paired; i < dst.size(); ++i) {
      const double center = dst[i].center;
      const double length = dst[i].length * eased;
      write_span(grid, static_cast<int>(row), center, length);
    }
  }

  return grid;
}

template <size_t Rows, size_t Cols>
Grid<Rows, Cols> wipe_transition(const GlyphSpans<Rows>& from,
                                 const GlyphSpans<Rows>& to,
                                 double t) {
  Grid<Rows, Cols> grid{};
  clear_grid(grid);
  const double eased = std::clamp(t, 0.0, 1.0);

  const auto& active = eased < 0.5 ? from : to;
  for (size_t row = 0; row < Rows; ++row) {
    for (const auto& span : active[row]) {
      write_span(grid, static_cast<int>(row), span.center, span.length);
    }
  }

  return grid;
}

template <size_t Rows, size_t Cols>
Grid<Rows, Cols> blink_transition(const GlyphSpans<Rows>& from,
                                  const GlyphSpans<Rows>& to,
                                  double t) {
  Grid<Rows, Cols> grid{};
  clear_grid(grid);

  if (t < 0.33) {
    for (size_t row = 0; row < Rows; ++row) {
      for (const auto& span : from[row]) {
        write_span(grid, static_cast<int>(row), span.center, span.length);
      }
    }
  } else if (t > 0.66) {
    for (size_t row = 0; row < Rows; ++row) {
      for (const auto& span : to[row]) {
        write_span(grid, static_cast<int>(row), span.center, span.length);
      }
    }
  }

  return grid;
}

template <size_t Rows, size_t Cols>
const TransitionSpec<Rows, Cols>* find_transition(std::string_view name,
                                                  const TransitionSpec<Rows, Cols>* specs,
                                                  size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (specs[i].name == name) {
      return &specs[i];
    }
  }
  return nullptr;
}

}  // namespace matrix_anim

#endif
