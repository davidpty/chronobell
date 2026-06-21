#include "ClockApp.h"

#include <Wire.h>
#include <time.h>
#include <esp_system.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "Config.h"
#include "fonts.h"
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
#include "DigitTransition.h"
#endif

static const uint16_t COUNTDOWN_PRESET_MINUTES[] = {
    1, 3, 5, 10, 15, 20, 25, 30, 45, 60, 90
};
static const uint8_t COUNTDOWN_PRESET_COUNT =
    sizeof(COUNTDOWN_PRESET_MINUTES) / sizeof(COUNTDOWN_PRESET_MINUTES[0]);

namespace {
bool sameDate(const ClockDate& a, const ClockDate& b) {
    return a.day == b.day &&
           a.date == b.date &&
           a.month == b.month &&
           a.year == b.year;
}

static const DisplayMode QUICK_STYLE_POOL[] = {
    DisplayMode::LargeDigitsOnly,
    DisplayMode::Info,
    DisplayMode::Word,
    DisplayMode::Roma,
    DisplayMode::Dial,
    DisplayMode::Bin,
    DisplayMode::Drift,
};

static const uint8_t QUICK_STYLE_POOL_COUNT =
    sizeof(QUICK_STYLE_POOL) / sizeof(QUICK_STYLE_POOL[0]);

static const DisplayMode RANDOM_STYLE_POOL[] = {
    DisplayMode::LargeDigitsOnly,
    DisplayMode::Info,
    DisplayMode::Word,
    DisplayMode::Roma,
    DisplayMode::Bin,
};

static const uint8_t RANDOM_STYLE_POOL_COUNT =
    sizeof(RANDOM_STYLE_POOL) / sizeof(RANDOM_STYLE_POOL[0]);

int randomStyleIntervalHours() {
    int hours = RND_STYLE_INTERVAL_HOURS;
    if (hours < 1 || hours > 24) return 24;
    return hours;
}

void appendJsonString(String& out, const String& value) {
    out += '"';
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out += c;
                break;
        }
    }
    out += '"';
}

static void appendJsonUInt64(String& out, uint64_t value) {
    char buf[21];
    size_t pos = sizeof(buf);
    buf[--pos] = '\0';
    do {
        buf[--pos] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value > 0 && pos > 0);
    out += &buf[pos];
}

static void appendSvgCircle(String& out, float cx, float cy, float r = 0.42f) {
    out += "<circle class=\"pixel-dot\" cx=\"";
    out += String(cx, 1);
    out += "\" cy=\"";
    out += String(cy, 1);
    out += "\" r=\"";
    out += String(r, 2);
    out += "\"></circle>";
}

static int normalizeGlyphIndex(char c) {
    if (c == ' ' || c == '\0') return -1;
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    switch (c) {
        case '%': return 36;
        case '-': return 37;
        case '+': return 38;
        case 'o': case 'O': return 39;
        case '^': return 40;
        case '@': return 41;
        case 'v': case 'V': return 42;
        case ':': return 43;
        default:  return -1;
    }
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
static void glyphBounds(const uint8_t (&font)[GlyphCount][Rows][Cols], int glyphIndex, int& left, int& right) {
    left = Cols;
    right = -1;
    if (glyphIndex < 0 || glyphIndex >= (int)GlyphCount) {
        return;
    }
    for (size_t col = 0; col < Cols; ++col) {
        for (size_t row = 0; row < Rows; ++row) {
            if (font[glyphIndex][row][col]) {
                if ((int)col < left) left = (int)col;
                if ((int)col > right) right = (int)col;
                break;
            }
        }
    }
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
static int glyphTextWidth(const uint8_t (&font)[GlyphCount][Rows][Cols], char c) {
    int glyph = normalizeGlyphIndex(c);
    int left, right;
    glyphBounds(font, glyph, left, right);
    return right >= left ? (right - left + 1) : 0;
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
static int measureText(const uint8_t (&font)[GlyphCount][Rows][Cols], const String& text, int letterSpacing = 1, int wordGap = 2) {
    int width = 0;
    bool inWord = false;
    for (size_t i = 0; i < text.length(); ++i) {
        char ch = text[i];
        if (ch == ' ') {
            if (inWord) {
                width += wordGap;
                inWord = false;
            }
            continue;
        }
        int glyphWidth = glyphTextWidth(font, ch);
        if (glyphWidth <= 0) continue;
        if (inWord) width += letterSpacing;
        width += glyphWidth;
        inWord = true;
    }
    return width;
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
static void appendGlyphSvg(String& out, const uint8_t (&font)[GlyphCount][Rows][Cols], char c, int x, int y) {
    int glyph = normalizeGlyphIndex(c);
    if (glyph < 0 || glyph >= (int)GlyphCount) return;
    int left, right;
    glyphBounds(font, glyph, left, right);
    if (right < left) return;

    for (int row = 0; row < (int)Rows; ++row) {
        for (int col = left; col <= right; ++col) {
            if (!font[glyph][row][col]) continue;
            appendSvgCircle(out, (float)(x + (col - left)) + 0.5f, (float)y + (float)row + 0.5f);
        }
    }
}

template <size_t GlyphCount, size_t Rows, size_t Cols>
static int appendTextSvg(String& out, const uint8_t (&font)[GlyphCount][Rows][Cols], const String& text, int x, int y, int letterSpacing = 1, int wordGap = 2) {
    bool inWord = false;
    for (size_t i = 0; i < text.length(); ++i) {
        char ch = text[i];
        if (ch == ' ') {
            if (inWord) {
                x += wordGap;
                inWord = false;
            }
            continue;
        }
        int glyphWidth = glyphTextWidth(font, ch);
        if (glyphWidth <= 0) continue;
        int glyph = normalizeGlyphIndex(ch);
        int left, right;
        glyphBounds(font, glyph, left, right);
        if (inWord) x += letterSpacing;
        if (right >= left) {
            for (int row = 0; row < (int)Rows; ++row) {
                for (int col = left; col <= right; ++col) {
                    if (!font[glyph][row][col]) continue;
                    appendSvgCircle(out, (float)x + (float)(col - left) + 0.5f, (float)y + (float)row + 0.5f);
                }
            }
            x += right - left + 1;
        }
        inWord = true;
    }
    return x;
}

static void appendCenteredTextSvg(String& out, const uint8_t (&font)[44][SEC_FONT_HEIGHT][4], const String& text, int y) {
    int width = measureText(font, text, 1, 2);
    int x = (32 - width) / 2;
    appendTextSvg(out, font, text, x, y, 1, 2);
}

static void appendCenteredTextSvg(String& out, const uint8_t (&font)[44][TIME_FONT_MEDIUM_HEIGHT][6], const String& text, int y) {
    int width = measureText(font, text, 1, 2);
    int x = (32 - width) / 2;
    appendTextSvg(out, font, text, x, y, 1, 2);
}

static String dayOfYearLabel(int year, int month, int day) {
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    if (epoch <= 0) return "DAY 001";
    struct tm start = {};
    start.tm_year = year - 1900;
    start.tm_mon = 0;
    start.tm_mday = 1;
    start.tm_isdst = -1;
    time_t startEpoch = mktime(&start);
    long diffDays = (long)((epoch - startEpoch) / 86400L) + 1;
    return "DAY " + String(diffDays);
}

static float moonAgeDays(int year, int month, int day) {
    const double synodicMonth = 29.530588853;
    const double ref = 946728840000.0; // 2000-01-06 18:14 UTC
    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;
    time_t epoch = mktime(&tm);
    double age = fmod((double(epoch) * 1000.0 - ref) / 86400000.0, synodicMonth);
    if (age < 0) age += synodicMonth;
    return (float)age;
}

static const char* westernZodiacSign(int month, int day) {
    if ((month == 1 && day >= 20) || (month == 2 && day <= 18)) return "AQUARIUS";
    if ((month == 2 && day >= 19) || (month == 3 && day <= 20)) return "PISCES";
    if ((month == 3 && day >= 21) || (month == 4 && day <= 19)) return "ARIES";
    if ((month == 4 && day >= 20) || (month == 5 && day <= 20)) return "TAURUS";
    if ((month == 5 && day >= 21) || (month == 6 && day <= 20)) return "GEMINI";
    if ((month == 6 && day >= 21) || (month == 7 && day <= 22)) return "CANCER";
    if ((month == 7 && day >= 23) || (month == 8 && day <= 22)) return "LEO";
    if ((month == 8 && day >= 23) || (month == 9 && day <= 22)) return "VIRGO";
    if ((month == 9 && day >= 23) || (month == 10 && day <= 22)) return "LIBRA";
    if ((month == 10 && day >= 23) || (month == 11 && day <= 21)) return "SCORPIO";
    if ((month == 11 && day >= 22) || (month == 12 && day <= 21)) return "SAGITTARIUS";
    return "CAPRICORN";
}

static const char* westernZodiacElement(const char* sign) {
    if (!strcmp(sign, "ARIES") || !strcmp(sign, "LEO") || !strcmp(sign, "SAGITTARIUS")) return "FIRE";
    if (!strcmp(sign, "TAURUS") || !strcmp(sign, "VIRGO") || !strcmp(sign, "CAPRICORN")) return "EARTH";
    if (!strcmp(sign, "GEMINI") || !strcmp(sign, "LIBRA") || !strcmp(sign, "AQUARIUS")) return "AIR";
    return "WATER";
}

static String westernZodiacLabel(const char* sign) {
    if (!strcmp(sign, "SCORPIO")) return "SCORP";
    if (!strcmp(sign, "SAGITTARIUS")) return "SAGIT";
    if (!strcmp(sign, "CAPRICORN")) return "CAPRI";
    if (!strcmp(sign, "AQUARIUS")) return "AQUAR";
    return String(sign);
}

static const char* chineseZodiacAnimal(int year) {
    static const char* animals[] = {"RAT", "OX", "TIGER", "RABBIT", "DRAGON", "SNAKE", "HORSE", "GOAT", "MONKEY", "ROOSTER", "DOG", "PIG"};
    return animals[((year - 4) % 12 + 12) % 12];
}

static const char* chineseZodiacAnimalCode(int year) {
    static const char* codes[] = {"RAT", "OX", "TIGER", "RABBIT", "DRGN", "SNAKE", "HORSE", "GOAT", "MNKY", "RSTR", "DOG", "PIG"};
    return codes[((year - 4) % 12 + 12) % 12];
}

static const char* chineseZodiacElement(int year) {
    static const char* elements[] = {"WOOD", "WOOD", "FIRE", "FIRE", "EARTH", "EARTH", "METAL", "METAL", "WATER", "WATER"};
    return elements[((year - 4) % 10 + 10) % 10];
}

static String formatDateLines(DateStyle style, const ClockDate& date) {
    const char* weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    String top;
    String bottom;

    switch (style) {
        case DateStyle::Year:
            top = String(date.year);
            bottom = dayOfYearLabel(date.year, date.month, date.date);
            break;
        case DateStyle::Moon: {
            top = "MOON";
            float age = moonAgeDays(date.year, date.month, date.date);
            float fullPoint = 14.765294f;
            if (age >= 14.4f && age <= 15.1f) {
                bottom = "FULL";
            } else if (age <= 0.6f || age >= 28.9f) {
                bottom = "NEW";
            } else if (age < fullPoint) {
                bottom = "FULL IN " + String(max(1, (int)lroundf(fullPoint - age)));
            } else {
                bottom = "NEW IN " + String(max(1, (int)lroundf(29.530588853f - age)));
            }
            break;
        }
        case DateStyle::Zod: {
            const char* sign = westernZodiacSign(date.month, date.date);
            top = westernZodiacLabel(sign);
            bottom = westernZodiacElement(sign);
            break;
        }
        case DateStyle::Czod: {
            int year = date.year;
            top = chineseZodiacAnimal(year);
            bottom = chineseZodiacElement(year);
            if (measureText(FONT_SMALL, top, 1, 2) > 32) {
                top = chineseZodiacAnimalCode(year);
            }
            break;
        }
        case DateStyle::Date:
        default:
            top = weekdays[(date.day > 0 ? date.day - 1 : 0) % 7];
            bottom = String(months[max(0, date.month - 1)]) + " " + (date.date < 10 ? "0" : "") + String(date.date);
            break;
    }

    String svg;
    svg.reserve(256);
    appendCenteredTextSvg(svg, FONT_SMALL, top, 0);
    appendCenteredTextSvg(svg, FONT_SMALL, bottom, 11);
    return svg;
}

static String renderTimerDisplaySvg(const String& view, const ClockTime& time, const ClockDate& date,
                                   bool guestAvailable, bool guestShowSsid, const String& guestSsid, const String& guestPassword,
                                   bool stopwatchRunning, uint64_t stopwatchMs,
                                   bool countdownRunning, bool countdownExpired, uint32_t countdownMs, uint32_t countdownElapsedSinceExpiryMs,
                                   DateStyle dateStyle, DisplayMode displayMode, const AppSettings& settings) {
    String svg;
    svg.reserve(512);

    if (view == "clock") {
        int hours = time.hours;
        if (settings.timeFormat == TimeFormat::AmPm) {
            hours %= 12;
            if (hours == 0) hours = 12;
        }
        bool showColon = separatorModeFor(settings, displayMode) != SeparatorMode::Pulse || ((time.seconds & 1) == 0);
        const int digitWidth = 6;
        const int spacing = 1;
        const int sepSpacingBefore = 3;
        const int sepSpacingAfter = 2;
        const int sepWidth = 1;
        const int hourDigits = hours >= 10 ? 2 : 1;
        const int totalWidth = (digitWidth * (hourDigits + 2)) + (spacing * (hourDigits >= 2 ? 2 : 1)) + sepSpacingBefore + sepSpacingAfter + sepWidth;
        int x = (32 - totalWidth) / 2;
        int y = 0;
        if (hours >= 10) {
            char h0 = (char)('0' + (hours / 10));
            appendGlyphSvg(svg, FONT_BIG, h0, x, y);
            x += digitWidth + spacing;
        }
        char h1 = (char)('0' + (hours % 10));
        appendGlyphSvg(svg, FONT_BIG, h1, x, y);
        x += digitWidth + sepSpacingBefore;
        if (showColon) {
            appendSvgCircle(svg, (float)x + 0.5f, 5.5f);
            appendSvgCircle(svg, (float)x + 0.5f, 10.5f);
        }
        x += sepWidth + sepSpacingAfter;
        char m0 = (char)('0' + (time.minutes / 10));
        appendGlyphSvg(svg, FONT_BIG, m0, x, y);
        x += digitWidth + spacing;
        char m1 = (char)('0' + (time.minutes % 10));
        appendGlyphSvg(svg, FONT_BIG, m1, x, y);
    } else if (view == "date") {
        svg += formatDateLines(dateStyle, date);
    } else if (view == "guest") {
        String text = guestShowSsid ? guestSsid : guestPassword;
        if (guestAvailable && text.length() > 0) {
            if (measureText(FONT_SMALL, text, 1, 2) <= 32) {
                appendCenteredTextSvg(svg, FONT_SMALL, text, 6);
            } else {
                int split = text.length() / 2;
                String first = text.substring(0, split);
                String second = text.substring(split);
                first.trim();
                second.trim();
                appendCenteredTextSvg(svg, FONT_SMALL, first, 2);
                appendCenteredTextSvg(svg, FONT_SMALL, second, 11);
            }
        }
    } else if (view == "stopwatch") {
        uint64_t total = stopwatchMs;
        uint32_t centisec = (uint32_t)((total % 1000ULL) / 10ULL);
        uint32_t totalSec = (uint32_t)(total / 1000ULL);
        const int digitWidth = 6;
        const int spacing = 1;
        const int sepWidth = 1;
        const int startY = 3;
        int x = 0;
        bool blink = stopwatchRunning && ((millis() / 500UL) % 2 == 0);
        if (totalSec < 100) {
            String s = String(totalSec);
            while (s.length() < 2) s = "0" + s;
            String c = String(centisec);
            while (c.length() < 2) c = "0" + c;
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, s[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, s[1], x, startY);
            x += digitWidth + spacing;
            appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 8.5f);
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, c[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, c[1], x, startY);
        } else if (totalSec < 600) {
            String m = String(totalSec / 60);
            String s = String(totalSec % 60);
            while (s.length() < 2) s = "0" + s;
            String d = String(centisec / 10);
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth * 2;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, m[0], x, startY);
            x += digitWidth + spacing;
            if (blink) {
                appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 4.5f);
                appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 6.5f);
            }
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, s[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, s[1], x, startY);
            x += digitWidth + spacing;
            appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 8.5f);
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, d[0], x, startY);
        } else if (totalSec < 6000) {
            String mm = String(totalSec / 60);
            while (mm.length() < 2) mm = "0" + mm;
            String ss = String(totalSec % 60);
            while (ss.length() < 2) ss = "0" + ss;
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[1], x, startY);
            x += digitWidth + spacing;
            if (blink) appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 8.5f);
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, ss[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, ss[1], x, startY);
        } else if (totalSec < 360000) {
            String hh = String(totalSec / 3600);
            while (hh.length() < 2) hh = "0" + hh;
            String mm = String((totalSec % 3600) / 60);
            while (mm.length() < 2) mm = "0" + mm;
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, hh[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, hh[1], x, startY);
            x += digitWidth + spacing;
            if (blink) {
                appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 4.5f);
                appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 6.5f);
            }
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[1], x, startY);
        } else {
            uint32_t days = totalSec / 86400U;
            if (days > 99U) days = 99U;
            String dd = String(days);
            while (dd.length() < 2) dd = "0" + dd;
            String hh = String((totalSec % 86400U) / 3600U);
            while (hh.length() < 2) hh = "0" + hh;
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, dd[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, dd[1], x, startY);
            x += digitWidth + spacing;
            appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 4.5f);
            appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 6.5f);
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, hh[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, hh[1], x, startY);
        }
    } else if (view == "countdown") {
        if (countdownExpired && (millis() % 1000UL) >= 750UL) {
            return svg;
        }
        uint32_t total = countdownMs;
        uint32_t totalSec = total / 1000UL;
        uint32_t minutes = totalSec / 60UL;
        uint32_t seconds = totalSec % 60UL;
        bool blink = countdownRunning && ((millis() / 500UL) % 2 == 0);
        const int digitWidth = 6;
        const int spacing = 1;
        const int sepWidth = 1;
        const int startY = 3;
        int x = 0;
        if (minutes < 100) {
            String mm = String(minutes);
            while (mm.length() < 2) mm = "0" + mm;
            String ss = String(seconds);
            while (ss.length() < 2) ss = "0" + ss;
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[1], x, startY);
            x += digitWidth + spacing;
            if (blink) {
                appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 4.5f);
                appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 6.5f);
            }
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, ss[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, ss[1], x, startY);
        } else {
            String hh = String(minutes / 60UL);
            while (hh.length() < 2) hh = "0" + hh;
            String mm = String(minutes % 60UL);
            while (mm.length() < 2) mm = "0" + mm;
            const int totalW = digitWidth * 4 + spacing * 3 + sepWidth;
            x = (32 - totalW) / 2;
            appendGlyphSvg(svg, FONT_MEDIUM, hh[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, hh[1], x, startY);
            x += digitWidth + spacing;
            appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 4.5f);
            appendSvgCircle(svg, (float)x + 0.5f, (float)startY + 6.5f);
            x += sepWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[0], x, startY);
            x += digitWidth + spacing;
            appendGlyphSvg(svg, FONT_MEDIUM, mm[1], x, startY);
        }
    }

    return svg;
}
}

// =============================================================================
// Construction
// =============================================================================

ClockApp::ClockApp()
    : _leds(MD_MAX72XX::FC16_HW, MAX7219_CS, MAX7219_NUM_MODULES)
    , _wifiManager(_settingsStore)
    , _wifiSync(_wifiManager, _timeProvider, _rtcClock, _settingsStore, _appSettings)
    , _display(_leds, _menuController, _timerController, _timeProvider, _settingsStore, _wifiManager)
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    , _menuBindings{_appSettings, _settingsStore, _display, _bellController,
                    _timeProvider, _wifiManager, _bellMode, _savedDisplayMode, _timeFormat, _nightMode,
                    _appSettings.transitionMode}
#else
    , _menuBindings{_appSettings, _settingsStore, _display, _bellController,
                    _timeProvider, _wifiManager, _bellMode, _savedDisplayMode, _timeFormat, _nightMode}
#endif
{
}

// =============================================================================
// Boot phases
// =============================================================================

void ClockApp::beginControllers() {
    _bellController.begin();
    _timeProvider.begin(_rtcClock, _wifiSync.getNtpClient());
    _menuController.begin(MENU_ITEMS, MENU_ITEM_COUNT);
    _guestWifi.begin();

    _menuController.setContext(&_menuBindings);
    _menuController.setSettingsStore(&_settingsStore);
    _display.setMenuBindings(&_menuBindings);
    _display.setRuntimeMode(&_displayMode);
    _display.setAppSettings(&_appSettings);
    _display.setDriftTimeModel(&_driftTimeModel);
    _display.setTimeFormat(&_timeFormat);
    _display.setDateStyle(&_activeDateStyle);
    _display.setGuestWifiController(&_guestWifi);
    _display.setNewYearController(&_newYearController);

    _wifiManager.setOtaDisplayCallback(handleOtaDisplay, this);
    _wifiManager.setTimeProvider(&_timeProvider);

    _wifiManager.setSaveCallback(handleSettingsSaved, this);
    _wifiManager.setReconnectResultCallback(handleReconnectResult, this);
    _wifiManager.setPreviewCallback(handleWebPreview, this);
    _wifiManager.setTimerStatusCallback(handleTimerStatus, this);

    // GuestWifi callback is wired in the .ino file via a trampoline
}

void ClockApp::wireTimerCallbacks(SavePresetFn savePreset,
                                  QueueAlertFn queueAlert,
                                  BellBusyFn   bellBusy,
                                  StopBellFn   stopBell) {
    _timerController.setCallbacks(savePreset, queueAlert, bellBusy, stopBell);
}

void ClockApp::wireTimerPersistenceCallbacks(CurrentEpochFn currentEpoch,
                                             SaveTargetEpochFn saveTargetEpoch,
                                             ClearTargetEpochFn clearTargetEpoch,
                                             SaveViewActiveFn saveViewActive,
                                             SaveUInt32Fn saveRemaining,
                                             ClearFn clearRemaining) {
    _timerController.setPersistenceCallbacks(currentEpoch, saveTargetEpoch,
                                             clearTargetEpoch, saveViewActive,
                                             saveRemaining, clearRemaining);
}

void ClockApp::wireStopwatchPersistenceCallbacks(SaveUInt64Fn saveElapsed,
                                                 ClearFn clearElapsed,
                                                 SaveTimeFn saveStartEpoch,
                                                 ClearFn clearStartEpoch,
                                                 SaveViewActiveFn saveViewActive) {
    _timerController.setStopwatchPersistenceCallbacks(saveElapsed, clearElapsed,
                                                      saveStartEpoch, clearStartEpoch,
                                                      saveViewActive);
}

void ClockApp::installTouchHandlers(OnTouchFn onPad1Press,
                                    OnTouchFn onPad8Press,
                                    OnTouchFn onPad4Release) {
    TouchPadConfig pad1;
    pad1.onPress = onPad1Press;
    _touchController.setHandler(1, pad1);

    TouchPadConfig pad4;
    pad4.onRelease = onPad4Release;
    _touchController.setHandler(4, pad4);

    TouchPadConfig pad8;
    pad8.onPress = onPad8Press;
    _touchController.setHandler(8, pad8);
}

void ClockApp::handleOtaDisplay(void* context, bool active, unsigned int progress, unsigned int total) {
    static_cast<ClockApp*>(context)->_display.showOtaUpdate(active, progress, total);
}

bool ClockApp::handleSettingsSaved(void* context,
                                   bool wifiChanged,
                                   bool tzChanged,
                                   bool manualTimeChanged,
                                   const String& wifiSsid,
                                   const String& wifiPassword) {
    return static_cast<ClockApp*>(context)->onSettingsSaved(wifiChanged, tzChanged, manualTimeChanged,
                                                            wifiSsid, wifiPassword);
}

void ClockApp::handleReconnectResult(void* context, bool success) {
    ClockApp* app = static_cast<ClockApp*>(context);
    LOGLN(success ? "Wi-Fi reconnect test finished" : "Wi-Fi reconnect test failed");
    app->reloadSettings();
    if (success) {
        app->_wifiSync.requestSync();
    }
}

void ClockApp::handleWebPreview(void* context, const String& field) {
    static_cast<ClockApp*>(context)->onWebPreview(field);
}

void ClockApp::initSerialAndPins() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    Serial.begin(SERIAL_BAUD);
    LOGLN("\nClock starting...");
    _wifiManager.setNetworkServiceConfig(MDNS_HOSTNAME, ARDUINO_OTA_PASSWORD);
}

void ClockApp::initDisplay() {
    _display.begin();
}

void ClockApp::initI2cAndRtc() {
    LOGLN("Initializing I2C for RTC...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_SPEED);

    if (_rtcClock.begin()) {
        LOGLN("RTC initialized successfully");
        _timeProvider.readRtc();
        ClockTime t = _rtcClock.getTime();
        LOGF("RTC time: %02d:%02d:%02d\n", t.hours, t.minutes, t.seconds);
    } else {
        LOGLN("RTC not available or not responding");
    }
}

void ClockApp::initCap1188() {
    LOGLN("Initializing CAP1188 touch sensor...");
    if (_touchController.begin()) {
        LOGLN("CAP1188 touch init OK");
    } else {
        LOGLN("CAP1188 not found");
    }
}

void ClockApp::loadSettings() {
    _appSettings = _settingsStore.load();
    _savedDisplayMode = _appSettings.displayMode;
    _activeDateStyle  = _appSettings.dateStyle;
    _temporaryDateStyle = _appSettings.dateStyle;
    _bellMode         = _appSettings.bellMode;
    _timeFormat       = _appSettings.timeFormat;
    _nightMode        = _appSettings.nightMode;
    syncDisplayModeSelection();
    syncDateStyleSelection();
#if DIGIT_TRANSITIONS
    digit_transition::set_transition_mode(_appSettings.transitionMode);
#endif
    LOG("Clock style loaded: ");
    LOGLN(displayModeLabel(_appSettings.displayMode));
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    LOG("Animation mode loaded: ");
    LOGLN(transitionModeLabel(_appSettings.transitionMode));
#endif
    LOG("Date style loaded: ");
    LOGLN(dateStyleLabel(_appSettings.dateStyle));
    LOG("Bell mode loaded: ");
    LOGLN((int)_bellMode);
    LOG("Time format loaded: ");
    LOGLN(timeFormatLabel(_appSettings.timeFormat));
    LOG("Night mode loaded: ");
    LOGLN(nightModeLabel(_appSettings.nightMode));
}

void ClockApp::loadTimerSettings() {
    uint8_t presetIndex = _settingsStore.loadCountdownPreset(COUNTDOWN_PRESET_COUNT);
    _timerController.begin(COUNTDOWN_PRESET_MINUTES, COUNTDOWN_PRESET_COUNT, presetIndex);
    time_t targetEpoch = _settingsStore.loadCountdownTargetEpoch();
    uint32_t remainingMs = _settingsStore.loadCountdownRemainingMs();
    bool countdownViewActive = _settingsStore.loadCountdownViewActive();
    _timerController.restoreCountdown(targetEpoch, countdownViewActive, remainingMs);
    LOGF("Countdown preset: %u min\n",
                  (unsigned)COUNTDOWN_PRESET_MINUTES[presetIndex]);

    uint64_t swElapsed = _settingsStore.loadStopwatchElapsed();
    time_t swStartEpoch = _settingsStore.loadStopwatchStartEpoch();
    bool swViewActive = _settingsStore.loadStopwatchViewActive();
    _timerController.restoreStopwatch(swElapsed, swStartEpoch, swViewActive);
}

void ClockApp::applyDisplayBrightness() {
    _display.loadBrightnessFromSettings();
    _nightModeController.begin(_nightMode);
    applyEffectiveDisplayBrightness();
}

void ClockApp::runDisplayTest() {
#if defined(DISPLAY_TEST) && DISPLAY_TEST > 0
    _display.runTest(DISPLAY_TEST);
    _display.setBrightness(_display.getBrightness());
#endif
}

void ClockApp::wifiBootSync() {
    _wifiSync.performBootSync();
}

void ClockApp::reloadSettings() {
    _appSettings = _settingsStore.load();
    _savedDisplayMode = _appSettings.displayMode;
    if (!_dateStyleOverrideActive) {
        _activeDateStyle = _appSettings.dateStyle;
        _temporaryDateStyle = _appSettings.dateStyle;
    }
    _bellMode         = _appSettings.bellMode;
    _timeFormat       = _appSettings.timeFormat;
    _nightMode        = _appSettings.nightMode;
    syncDisplayModeSelection();
    syncDateStyleSelection();
#if DIGIT_TRANSITIONS
    digit_transition::set_transition_mode(_appSettings.transitionMode);
#endif
    LOG("Clock style loaded: ");
    LOGLN(displayModeLabel(_appSettings.displayMode));
#if DIGIT_TRANSITIONS || SCREEN_TRANSITION
    LOG("Animation mode loaded: ");
    LOGLN(transitionModeLabel(_appSettings.transitionMode));
#endif
    LOG("Date style loaded: ");
    LOGLN(dateStyleLabel(_appSettings.dateStyle));
    LOG("Bell mode loaded: ");
    LOGLN((int)_bellMode);
    LOG("Time format loaded: ");
    LOGLN(timeFormatLabel(_appSettings.timeFormat));
    LOG("Night mode loaded: ");
    LOGLN(nightModeLabel(_appSettings.nightMode));
}

void ClockApp::applyManualTime() {
    if (!_rtcClock.available() || !_appSettings.manualTime.enabled) {
        return;
    }
    unsigned long manualEpoch = _appSettings.manualTime.epoch;
    if (manualEpoch == 0) {
        return;
    }

    LOGLN("Applying manual time setting...");
    _timeProvider.setRtcFromEpoch((time_t)manualEpoch);
    _timeProvider.readRtc();
    ClockTime t = _rtcClock.getTime();
    LOGF("RTC after manual set: %02d:%02d:%02d\n",
                  t.hours, t.minutes, t.seconds);
}

void ClockApp::render() {
    updateNewYearState();
    syncDisplayModeSelection();
    syncDateStyleSelection();
    applyEffectiveDisplayBrightness();

    if (_displayMode == DisplayMode::Drift) {
        if (_lastDisplayModeSeen != DisplayMode::Drift) {
            ClockTime time;
            if (_timeProvider.currentTime(time)) {
                _driftTimeModel.activate(time, millis());
                _lastDisplayModeSeen = DisplayMode::Drift;
            }
        }
    } else {
        _lastDisplayModeSeen = _displayMode;
    }

    _display.showTime();
}

// =============================================================================
// Per-tick services
// =============================================================================

void ClockApp::pollBootButton() {
    bool buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);

    if (buttonPressed && !_buttonWasPressed) {
        _buttonPressStart = millis();
        _buttonWasPressed = true;
    } else if (!buttonPressed && _buttonWasPressed) {
        _buttonWasPressed = false;

        LOG("Boot button short press: ");
        LOG(millis() - _buttonPressStart);
        LOGLN("ms - starting hotspot");

        _wifiManager.startHotspot();
    }
}

void ClockApp::pollTouch() {
    _touchController.update();
}

void ClockApp::tickWifiManager() {
    _wifiManager.loop();
}

void ClockApp::tickWifiSync() {
    _wifiSync.tick();
}

void ClockApp::tickRtc() {
    if (_rtcClock.available()) {
        _timeProvider.updateRtcTracking();
    }
}

void ClockApp::tickTimer() {
    _timerController.update();
}

void ClockApp::tickBell() {
    updateBellSchedule();
}

void ClockApp::tickGuestWifi() {
    if (_guestWifi.isDisabled()) {
        return;
    }

    int h = 0, m = 0, s = 0;
    if (!getCurrentClockTime(h, m, s)) {
        return;
    }

    ClockDate d;
    if (!_timeProvider.currentDate(d)) {
        return;
    }

    _guestWifi.tick(h, m, d.year, d.month, d.date);
}

void ClockApp::pollLongPress() {
    bool t4Pressed = _touchController.isPressed(4);

    if (!t4Pressed) {
        _t4LongPressHandled = false;
        return;
    }
    if (_t4LongPressHandled) return;

    uint32_t heldMs = _touchController.heldMs(4);
    if (heldMs < MENU_LONG_PRESS_MS) return;

    _t4LongPressHandled = true;

    if (_menuController.isActive()) {
        LOGLN("T4 1.5s: cancel & exit menu");
        if (_menuController.isEdit()) {
            _menuController.cancelEdit();
        }
        _menuController.exit();
        return;
    }

    LOGLN("T4 1.5s: enter menu");
    _menuController.enterBrowse();
}

void ClockApp::tickMenu() {
    _menuController.update();
}

void ClockApp::updateBellSchedule() {
    ClockTime realTime;
    ClockDate realDate;
    bool timeValid = updateNewYearState(&realTime, &realDate);
    ClockTime time = realTime;
    if (timeValid && _displayMode == DisplayMode::Drift) {
        unsigned long nowMs = millis();
        _driftTimeModel.update(time, nowMs);
        time = _driftTimeModel.displayTime(time, nowMs);
    }
    bool muteAutomatic = _nightModeController.shouldMuteAutomaticBell(realTime);

    if (_newYearController.hasMidnightBellRequest()) {
        if (!_timerController.isCountdownExpired() && !_bellController.isBusy()) {
            _bellController.queueNewYearAlert();
            _newYearController.resolveMidnightBellRequest();
        }
    }

    if (_newYearController.hasCountdownTickRequest()) {
        if (!_bellController.isBusy()) {
            _bellController.queueCountdownTickAlert();
            _newYearController.resolveCountdownTickRequest();
        }
    }

    if (_newYearController.hasCountdownSecondTickRequest()) {
        if (!_bellController.isBusy()) {
            _bellController.queueCountdownSecondTickAlert();
            _newYearController.resolveCountdownSecondTickRequest();
        }
    }

    if (_newYearController.hasCountdownTenSecRequest()) {
        if (!_bellController.isBusy()) {
            _bellController.queueCountdownTenSecAlert();
            _newYearController.resolveCountdownTenSecRequest();
        }
    }

    bool suppressScheduledStrike = _newYearController.isCelebrating() &&
                                   realTime.hours == 0 && realTime.minutes == 0 &&
                                   realTime.seconds <= 1;
    _bellController.update(time, timeValid, _bellMode,
                           _timerController.isCountdownExpired(),
                           muteAutomatic, suppressScheduledStrike);
}

bool ClockApp::updateNewYearState(ClockTime* timeOut, ClockDate* dateOut) {
    ClockTime time;
    ClockDate date;
    bool valid = _timeProvider.currentTime(time) && _timeProvider.currentDate(date);
    if (valid) {
        _newYearController.update(date, time, _timeProvider.milliseconds());
        if (timeOut) *timeOut = time;
        if (dateOut) *dateOut = date;
    }
    return valid;
}

void ClockApp::syncDisplayModeSelection() {
    if (_displayOverrideActive &&
        LAST_STYLE_TIMEOUT_MINUTES > 0) {
        unsigned long now = millis();
        if ((int32_t)(now - _displayOverrideExpiresAt) >= 0 ||
            _savedDisplayMode != _displayOverrideSourceMode) {
            _displayOverrideActive = false;
        }
    } else if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        _displayOverrideActive = false;
    }

    DisplayMode baseMode = _savedDisplayMode;
    if (isRandomDisplayMode(_savedDisplayMode)) {
        ClockDate currentDate;
        bool haveDate = _timeProvider.currentDate(currentDate);
        ClockTime currentTime;
        bool haveTime = _timeProvider.currentTime(currentTime);
        uint8_t currentSlot = haveTime
            ? (uint8_t)(currentTime.hours / randomStyleIntervalHours())
            : 0;

        if (!_randomDisplayModeValid) {
            _randomDisplayMode = pickRandomConcreteDisplayMode(DisplayMode::Rnd);
            _randomDisplayModeValid = true;
            if (haveDate && haveTime) {
                _randomDisplayDate = currentDate;
                _randomDisplayDateValid = true;
                _randomDisplayHourSlot = currentSlot;
            }
        }

        if (haveDate && haveTime) {
            if (!_randomDisplayDateValid) {
                _randomDisplayDate = currentDate;
                _randomDisplayDateValid = true;
                _randomDisplayHourSlot = currentSlot;
            } else if (!sameDate(currentDate, _randomDisplayDate) ||
                       currentSlot != _randomDisplayHourSlot) {
                _randomDisplayDate = currentDate;
                _randomDisplayHourSlot = currentSlot;
                _randomDisplayMode = pickRandomConcreteDisplayMode(_randomDisplayMode);
            }
        }

        baseMode = _randomDisplayMode;
    } else {
        _randomDisplayModeValid = false;
        _randomDisplayDateValid = false;
    }

    _displayMode = _displayOverrideActive ? _overrideDisplayMode : baseMode;
}

void ClockApp::syncDateStyleSelection() {
    if (_dateStyleOverrideActive &&
        LAST_STYLE_TIMEOUT_MINUTES > 0) {
        unsigned long now = millis();
        if ((int32_t)(now - _dateStyleOverrideExpiresAt) >= 0) {
            _dateStyleOverrideActive = false;
        }
    } else if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        _dateStyleOverrideActive = false;
    }

    _activeDateStyle = _dateStyleOverrideActive
        ? _temporaryDateStyle
        : _appSettings.dateStyle;
}

void ClockApp::cycleTemporaryDisplayMode(int direction) {
    if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        return;
    }

    syncDisplayModeSelection();
    int currentIndex = -1;
    for (uint8_t i = 0; i < QUICK_STYLE_POOL_COUNT; i++) {
        if (QUICK_STYLE_POOL[i] == _displayMode) {
            currentIndex = (int)i;
            break;
        }
    }
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    int nextIndex = (currentIndex + direction + QUICK_STYLE_POOL_COUNT) % QUICK_STYLE_POOL_COUNT;
    DisplayMode mode = QUICK_STYLE_POOL[nextIndex];
    if (_displayOverrideActive && mode == _overrideDisplayMode) {
        return;
    }

    _overrideDisplayMode = mode;
    _displayOverrideSourceMode = _savedDisplayMode;
    _displayOverrideActive = true;
    _displayOverrideExpiresAt =
        millis() + (unsigned long)LAST_STYLE_TIMEOUT_MINUTES * 60000UL;
    _displayMode = _overrideDisplayMode;
    LOG("Temporary clock style: ");
    LOGLN(displayModeLabel(mode));
}

void ClockApp::cycleTemporaryDateStyle(int direction) {
    if (LAST_STYLE_TIMEOUT_MINUTES <= 0) {
        return;
    }

    syncDateStyleSelection();
    int currentIndex = (int)_activeDateStyle;
    if (currentIndex < (int)DateStyle::Date || currentIndex > (int)DateStyle::Czod) {
        currentIndex = (int)DateStyle::Date;
    }

    int nextIndex = (currentIndex + direction + ((int)DateStyle::Czod + 1)) % ((int)DateStyle::Czod + 1);
    _temporaryDateStyle = (DateStyle)nextIndex;
    _dateStyleOverrideActive = true;
    _dateStyleOverrideExpiresAt =
        millis() + (unsigned long)LAST_STYLE_TIMEOUT_MINUTES * 60000UL;
    _activeDateStyle = _temporaryDateStyle;
    LOG("Temporary date style: ");
    LOGLN(dateStyleLabel(_temporaryDateStyle));
}

DisplayMode ClockApp::pickRandomConcreteDisplayMode(DisplayMode avoid) const {
    if (RANDOM_STYLE_POOL_COUNT == 0) {
        return DisplayMode::LargeDigitsOnly;
    }

    int avoidIndex = -1;
    for (uint8_t i = 0; i < RANDOM_STYLE_POOL_COUNT; i++) {
        if (RANDOM_STYLE_POOL[i] == avoid) {
            avoidIndex = (int)i;
            break;
        }
    }

    int selectableCount = (avoidIndex >= 0 && RANDOM_STYLE_POOL_COUNT > 1)
        ? RANDOM_STYLE_POOL_COUNT - 1
        : RANDOM_STYLE_POOL_COUNT;

    int pick = (int)(esp_random() % (uint32_t)selectableCount);
    if (avoidIndex >= 0 && RANDOM_STYLE_POOL_COUNT > 1 && pick >= avoidIndex) {
        pick++;
    }

    return RANDOM_STYLE_POOL[pick];
}

bool ClockApp::getCurrentClockTime(int& h, int& m, int& s) const {
    ClockTime t;
    bool ok = _timeProvider.currentTime(t);
    h = t.hours;
    m = t.minutes;
    s = t.seconds;
    return ok;
}

void ClockApp::applyEffectiveDisplayBrightness() {
    // While the user is editing the BRIGHT item, pin the LED to the current
    // user brightness so the selected value is visible on the whole screen.
    // This bypasses night-mode dimming and dark-mode suppression for the
    // duration of the preview; the normal path resumes on
    // save / cancel / exit / timeout.
    if (_menuController.isBrightPreviewActive()) {
        if (!_display.isEnabled()) {
            _display.setEnabled(true);
        }
        if (_display.getBrightness() != _display.getUserBrightness()) {
            _display.setBrightness(_display.getUserBrightness());
        }
        return;
    }

    // Sync the night-mode controller with the latest live state, then
    // let it decide the effective brightness for this tick. Avoid spamming
    // the LED driver by only writing when the value actually changes.
    _nightModeController.setMode(_nightMode);

    int h = 0, m = 0, s = 0;
    if (!getCurrentClockTime(h, m, s)) {
        return;
    }

    ClockTime now;
    now.hours = h;
    now.minutes = m;
    now.seconds = s;
    int8_t effective = _nightModeController.tick(now, _display.getUserBrightness());
    effective = _newYearController.boostedBrightness(effective, _display.getUserBrightness());
    bool enabled = !_nightModeController.isDisplaySuppressed() ||
                   _newYearController.shouldWakeDisplay();
    if (enabled != _display.isEnabled()) {
        _display.setEnabled(enabled);
    }
    if (effective != _display.getBrightness()) {
        _display.setBrightness(effective);
    }
}

// =============================================================================
// Callback handlers (invoked from .ino trampolines)
// =============================================================================

void ClockApp::onTouchLeft(uint8_t pad) {
    if (_menuController.isActive()) {
        onTouchMenuPrev(pad);
        return;
    }
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    if (_timerController.isCountdownExpired()) {
        _timerController.onLeft();
        return;
    }
    if (_timerController.isDateView()) {
        cycleTemporaryDateStyle(-1);
        return;
    }
    if (_timerController.isClockView()) {
        if (LAST_STYLE_TIMEOUT_MINUTES > 0) {
            cycleTemporaryDisplayMode(-1);
            return;
        }
    }
    _timerController.onLeft();
}

void ClockApp::onTouchRight(uint8_t pad) {
    if (_menuController.isActive()) {
        onTouchMenuNext(pad);
        return;
    }
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    if (_timerController.isDateView()) {
        cycleTemporaryDateStyle(1);
        return;
    }
    if (_timerController.isClockView()) {
        if (LAST_STYLE_TIMEOUT_MINUTES > 0) {
            cycleTemporaryDisplayMode(1);
            return;
        }
    }
    _timerController.onRight();
}

void ClockApp::onTouchMiddleShort(uint8_t pad) {
    if (_t4LongPressHandled) {
        return;
    }
    if (_menuController.isActive()) {
        onTouchMenuOk(pad);
        return;
    }
    _timerController.noteActivity();
    if (_nightModeController.consumeWakePress()) {
        return;
    }
    _nightModeController.noteUserActivity();
    _timerController.onMiddleShort();
}

void ClockApp::onTouchMenuPrev(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive()) return;
    _menuController.onPrev();
}

void ClockApp::onTouchMenuNext(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive()) return;
    _menuController.onNext();
}

void ClockApp::onTouchMenuOk(uint8_t pad) {
    (void)pad;
    if (!_menuController.isActive()) return;
    _menuController.onOk();
}

void ClockApp::wireGuestWifiCallback(TimerController::GuestWifiAvailableFn fn) {
    _timerController.setGuestWifiAvailableCallback(fn);
}

void ClockApp::configureTouchRepeat(uint8_t pad, OnTouchFn onRepeat, uint32_t initialDelayMs, uint32_t rateMs) {
    _touchController.setPadRepeat(pad, onRepeat, initialDelayMs, rateMs);
}

void ClockApp::onTouchLeftRepeat(uint8_t pad) {
    if (!_menuController.isActive()) return;
    if (_menuController.isEdit()) {
        _menuController.onPrev();
    }
}

void ClockApp::onTouchRightRepeat(uint8_t pad) {
    if (_menuController.isActive()) {
        if (_menuController.isEdit()) {
            _menuController.onNext();
        }
        return;
    }
    if (_timerController.isCountdownView()) {
        _timerController.noteActivity();
        _timerController.onRight();
    }
}

void ClockApp::saveCountdownPreset(uint8_t presetIndex) {
    _settingsStore.saveCountdownPreset(presetIndex);
}

bool ClockApp::currentEpoch(time_t& epoch) const {
    if (!_rtcClock.available()) {
        return false;
    }
    return _timeProvider.currentEpoch(epoch);
}

bool ClockApp::saveCountdownTargetEpoch(time_t targetEpoch) {
    return _settingsStore.saveCountdownTargetEpoch(targetEpoch);
}

bool ClockApp::clearCountdownTargetEpoch() {
    return _settingsStore.clearCountdownTargetEpoch();
}

bool ClockApp::saveCountdownViewActive(bool active) {
    return _settingsStore.saveCountdownViewActive(active);
}

bool ClockApp::saveCountdownRemainingMs(uint32_t remainingMs) {
    return _settingsStore.saveCountdownRemainingMs(remainingMs);
}

bool ClockApp::clearCountdownRemainingMs() {
    return _settingsStore.clearCountdownRemainingMs();
}

bool ClockApp::saveStopwatchElapsed(uint64_t elapsedMs) {
    return _settingsStore.saveStopwatchElapsed(elapsedMs);
}

bool ClockApp::clearStopwatchElapsed() {
    return _settingsStore.clearStopwatchElapsed();
}

bool ClockApp::saveStopwatchStartEpoch(time_t epoch) {
    return _settingsStore.saveStopwatchStartEpoch(epoch);
}

bool ClockApp::clearStopwatchStartEpoch() {
    return _settingsStore.clearStopwatchStartEpoch();
}

bool ClockApp::saveStopwatchViewActive(bool active) {
    return _settingsStore.saveStopwatchViewActive(active);
}

void ClockApp::queueBellAlert(uint8_t groups) {
    (void)groups;
    _bellController.queueCountdownAlert();
}

bool ClockApp::isBellBusy() const {
    return _bellController.isBusy();
}

void ClockApp::stopBell() {
    _bellController.stop();
}

// =============================================================================
// Live settings apply (called from config portal save callback)
// =============================================================================

bool ClockApp::onSettingsSaved(bool wifiChanged, bool tzChanged, bool manualTimeChanged, const String& wifiSsid, const String& wifiPassword) {
    LOGLN("Applying saved settings live...");

    int16_t oldTzOffset = _appSettings.timezone.offsetMinutes;
    bool oldManualEnabled = _appSettings.manualTime.enabled;
    reloadSettings();

    if (manualTimeChanged) {
        LOGLN("Applying manual time...");
        applyManualTime();
        if (_wifiManager.isHotspotActive()) {
            LOGLN("Manual time changed - reset hotspot timer");
            _wifiManager.resetHotspotTimer();
        }
    }

    if (tzChanged) {
        LOGLN("Applying timezone change...");
        NTPClient& ntp = _wifiSync.getNtpClient();
        ntp.setTimeOffset(_appSettings.timezone.offsetMinutes * 60);
        if (_rtcClock.available()) {
            ClockTime ct = _rtcClock.getTime();
            ClockDate cd = _rtcClock.getDate();
            struct tm tm;
            tm.tm_year = cd.year - 1900;
            tm.tm_mon = cd.month - 1;
            tm.tm_mday = cd.date;
            tm.tm_hour = ct.hours;
            tm.tm_min = ct.minutes;
            tm.tm_sec = ct.seconds;
            tm.tm_isdst = -1;
            time_t localEpoch = mktime(&tm);
            if (localEpoch > 0) {
                time_t utcEpoch = localEpoch - (oldTzOffset * 60);
                time_t newLocalEpoch = utcEpoch + (_appSettings.timezone.offsetMinutes * 60);
                _timeProvider.setRtcFromEpoch(newLocalEpoch);
            }
        }
    }

    if (oldManualEnabled && !_appSettings.manualTime.enabled) {
        LOGLN("Manual -> atomic, force NTP");
        _wifiSync.requestSync();
    }

    applyDisplayBrightness();

    if (wifiChanged) {
        LOGLN("WiFi changed, start bg test");
        if (!_wifiManager.startPendingNetworkReconnect(wifiSsid, wifiPassword, 15000)) {
            LOGLN("Unable to start pending Wi-Fi test");
            return false;
        }
    }

    return true;
}

void ClockApp::onWebPreview(const String& field) {
    if (field == "timer:left") {
        onTouchLeft(0);
        return;
    }
    if (field == "timer:middle") {
        onTouchMiddleShort(0);
        return;
    }
    if (field == "timer:right") {
        onTouchRight(0);
        return;
    }
    if (field == "brightness") {
        int8_t b = _settingsStore.loadBrightness(4);
        _display.setUserBrightness(b);
    } else if (field == "datestyle") {
        _timerController.showDateView();
    } else if (field == "bellmode") {
        ClockTime now;
        if (getCurrentClockTime(now.hours, now.minutes, now.seconds)) {
            _bellController.preview(_appSettings.bellMode, now, true);
        }
#if DIGIT_TRANSITIONS
    } else if (field == "anim") {
        digit_transition::set_transition_mode(_appSettings.transitionMode);
#endif
    } else {
        _timerController.showClockPreview();
    }
}

String ClockApp::timerStatusJson() const {
    ClockTime time;
    ClockDate date;
    bool timeValid = getCurrentClockTime(time.hours, time.minutes, time.seconds);
    bool dateValid = _timeProvider.currentDate(date);
    bool guestAvailable = _guestWifi.isTextAvailable();
    bool showSsid = false;
    if (guestAvailable) {
        unsigned long elapsed = millis() % ((GUEST_WIFI_SSID_SHOW_SECONDS + GUEST_WIFI_PASS_SHOW_SECONDS) * 1000UL);
        showSsid = elapsed < (unsigned long)GUEST_WIFI_SSID_SHOW_SECONDS * 1000UL;
    }

    String json = "{";
    json += "\"view\":";
    if (_timerController.isStopwatchView()) {
        appendJsonString(json, "stopwatch");
    } else if (_timerController.isCountdownExpired() || _timerController.isCountdownView()) {
        appendJsonString(json, "countdown");
    } else if (_timerController.isGuestWifiView()) {
        appendJsonString(json, "guest");
    } else if (_timerController.isDateView()) {
        appendJsonString(json, "date");
    } else {
        appendJsonString(json, "clock");
    }

    json += ",\"timeValid\":";
    json += timeValid ? "true" : "false";
    json += ",\"clockHours\":";
    json += time.hours;
    json += ",\"clockMinutes\":";
    json += time.minutes;
    json += ",\"clockSeconds\":";
    json += time.seconds;

    json += ",\"dateValid\":";
    json += dateValid ? "true" : "false";
    json += ",\"dateDay\":";
    json += date.day;
    json += ",\"dateDate\":";
    json += date.date;
    json += ",\"dateMonth\":";
    json += date.month;
    json += ",\"dateYear\":";
    json += date.year;
    json += ",\"dateStyle\":";
    json += (int)_activeDateStyle;

    json += ",\"guestAvailable\":";
    json += guestAvailable ? "true" : "false";
    json += ",\"guestShowSsid\":";
    json += showSsid ? "true" : "false";
    json += ",\"guestSsid\":";
    appendJsonString(json, guestAvailable ? _guestWifi.ssid() : "");
    json += ",\"guestPassword\":";
    appendJsonString(json, guestAvailable ? _guestWifi.password() : "");

    uint64_t stopwatchMs = _timerController.stopwatchMs();
    uint32_t countdownMs = _timerController.countdownMs();
    uint32_t countdownElapsed = _timerController.countdownElapsedSinceExpiryMs();
    String displaySvg = _display.snapshotSvg();

    json += ",\"stopwatchRunning\":";
    json += _timerController.stopwatchRunning() ? "true" : "false";
    json += ",\"stopwatchMs\":";
    appendJsonUInt64(json, stopwatchMs);

    json += ",\"countdownRunning\":";
    json += _timerController.countdownRunning() ? "true" : "false";
    json += ",\"countdownExpired\":";
    json += _timerController.isCountdownExpired() ? "true" : "false";
    json += ",\"countdownMs\":";
    json += String((unsigned long)countdownMs);
    json += ",\"countdownElapsedSinceExpiryMs\":";
    json += String((unsigned long)countdownElapsed);
    json += ",\"displaySvg\":";
    appendJsonString(json, displaySvg);
    json += "}";
    return json;
}

String ClockApp::handleTimerStatus(void* context) {
    return static_cast<ClockApp*>(context)->timerStatusJson();
}
