#include "fonts.h"
#include "matrix_anim.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr int kDefaultHoldSeconds = 10;
constexpr int kDefaultTransitionMs = 900;
constexpr int kFrameMs = 75;

enum class FontKind {
  kBig,
  kMedium,
  kSmall,
  kAll,
};

struct Options {
  FontKind font = FontKind::kAll;
  std::string transition = "morph";
  int hold_seconds = kDefaultHoldSeconds;
  int transition_ms = kDefaultTransitionMs;
  bool loop = false;
};

void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--font big|medium|small|all] [--transition NAME] [--seconds N]\n"
      << "       [--transition-ms N] [--fast] [--loop]\n"
      << "\n"
      << "Renders all glyphs from the selected font table(s).\n"
      << "  --font NAME          Font family to use (default: all)\n"
      << "  --transition NAME    morph|wipe|blink (default: morph)\n"
      << "  --seconds N          Hold each glyph for N seconds total (default: 10)\n"
      << "  --transition-ms N    Morph duration between digits (default: 900)\n"
      << "  --fast               Shortcut for --seconds 1 --transition-ms 200\n"
      << "  --loop               Repeat the sequence forever\n";
}

FontKind parse_font_kind(const std::string& value) {
  if (value == "big") {
    return FontKind::kBig;
  }
  if (value == "medium") {
    return FontKind::kMedium;
  }
  if (value == "small") {
    return FontKind::kSmall;
  }
  if (value == "all") {
    return FontKind::kAll;
  }

  std::cerr << "Unknown font: " << value << "\n";
  std::exit(1);
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--font" && i + 1 < argc) {
      options.font = parse_font_kind(argv[++i]);
    } else if (arg == "--transition" && i + 1 < argc) {
      options.transition = argv[++i];
    } else if (arg == "--seconds" && i + 1 < argc) {
      options.hold_seconds = std::max(1, std::atoi(argv[++i]));
    } else if (arg == "--transition-ms" && i + 1 < argc) {
      options.transition_ms = std::max(0, std::atoi(argv[++i]));
    } else if (arg == "--fast") {
      options.hold_seconds = 1;
      options.transition_ms = 200;
    } else if (arg == "--loop") {
      options.loop = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      std::exit(1);
    }
  }
  return options;
}

void clear_screen() {
  std::cout << "\x1b[2J\x1b[H";
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
void render_glyph(const uint8_t (&font)[GlyphCount][Rows][Cols], int glyph_index) {
  matrix_anim::Grid<Rows, Cols> grid{};
  const auto spans = matrix_anim::extract_spans(font, static_cast<size_t>(glyph_index));

  for (size_t row = 0; row < Rows; ++row) {
    for (const auto& span : spans[row]) {
      matrix_anim::write_span(grid, static_cast<int>(row), span.center, span.length);
    }
  }

  clear_screen();
  matrix_anim::render_grid(std::cout, grid, 6,
                           std::string("glyph ") + std::to_string(glyph_index) + "/" +
                               std::to_string(GlyphCount - 1));
}

template <size_t Rows, size_t Cols>
void render_transition_frame(const matrix_anim::TransitionSpec<Rows, Cols>& transition,
                             const matrix_anim::GlyphSpans<Rows>& from,
                             const matrix_anim::GlyphSpans<Rows>& to,
                             double t,
                             int glyph_index,
                             int glyph_count) {
  clear_screen();
  auto frame = transition.frame_fn(from, to, t);
  matrix_anim::render_grid(std::cout, frame, 6,
                           std::string("glyph ") + std::to_string(glyph_index) + "/" +
                               std::to_string(glyph_count - 1));
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
void run_demo_for_font_once(const uint8_t (&font)[GlyphCount][Rows][Cols],
                            const Options& options) {
  const std::array<matrix_anim::TransitionSpec<Rows, Cols>, 3> transitions{{
      {"morph", &matrix_anim::morph_transition<Rows, Cols>},
      {"wipe", &matrix_anim::wipe_transition<Rows, Cols>},
      {"blink", &matrix_anim::blink_transition<Rows, Cols>},
  }};

  const auto* transition =
      matrix_anim::find_transition<Rows, Cols>(options.transition, transitions.data(), transitions.size());
  if (transition == nullptr) {
    std::cerr << "Unknown transition: " << options.transition << "\n";
    std::cerr << "Available transitions: morph, wipe, blink\n";
    std::exit(1);
  }

  const int hold_ms = options.hold_seconds * 1000;
  int previous_digit = -1;

  previous_digit = -1;
  for (int digit = 0; digit < static_cast<int>(GlyphCount); ++digit) {
    if (previous_digit < 0) {
      render_glyph(font, digit);
      std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    } else {
      const auto from = matrix_anim::extract_spans(font, static_cast<size_t>(previous_digit));
      const auto to = matrix_anim::extract_spans(font, static_cast<size_t>(digit));
        const int frames = std::max(4, options.transition_ms / kFrameMs);
        const int base_delay = frames > 0 ? options.transition_ms / frames : 0;
        const int remainder = frames > 0 ? options.transition_ms % frames : 0;

        for (int frame = 0; frame < frames; ++frame) {
          const double t = frames == 1 ? 1.0 : static_cast<double>(frame) / (frames - 1);
          render_transition_frame(*transition, from, to, t, digit,
                                  static_cast<int>(GlyphCount));
          const int delay = base_delay + (frame < remainder ? 1 : 0);
          if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
          }
      }

      render_glyph(font, digit);
      std::this_thread::sleep_for(
          std::chrono::milliseconds(std::max(0, hold_ms - options.transition_ms)));
    }
    previous_digit = digit;
  }
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
void run_demo_for_font(const uint8_t (&font)[GlyphCount][Rows][Cols],
                       const Options& options) {
  do {
    run_demo_for_font_once(font, options);
  } while (options.loop);
}

void run_all_fonts(const Options& options) {
  do {
    run_demo_for_font_once(FONT_BIG, options);
    run_demo_for_font_once(FONT_MEDIUM, options);
    run_demo_for_font_once(FONT_SMALL, options);
  } while (options.loop);
}

void dispatch_font(const Options& options) {
  switch (options.font) {
    case FontKind::kBig:
      run_demo_for_font(FONT_BIG, options);
      break;
    case FontKind::kMedium:
      run_demo_for_font(FONT_MEDIUM, options);
      break;
    case FontKind::kSmall:
      run_demo_for_font(FONT_SMALL, options);
      break;
    case FontKind::kAll:
      run_all_fonts(options);
      break;
  }
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parse_options(argc, argv);
  dispatch_font(options);
  return 0;
}
