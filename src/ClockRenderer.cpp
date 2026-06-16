#include "ClockRenderer.h"

#include "Config.h"
#include "Display.h"
#include "RtcClock.h"
#include "TimeProvider.h"
#include "fonts.h"

#include <math.h>
#include <cstring>
#include <esp_system.h>

namespace {
struct WordSegment {
    const char* text;
    bool small;
};

struct MonthDay {
    uint8_t month;
    uint8_t day;
};

struct MoonAnchorCache {
    bool initialized = false;
    uint16_t count = 0;
    uint16_t offsets[1024] = {};
};

// Exact Chinese New Year dates for 2001-2100.
// Outside this range we fall back to the lightweight approximation.
static const MonthDay CHINESE_NEW_YEAR_DATES_2001_2100[] = {
    {1, 24}, {2, 12}, {2, 1},  {1, 22}, {2, 9},  {1, 29}, {2, 18}, {2, 7},  {1, 26}, {2, 14},
    {2, 3},  {1, 23}, {2, 10}, {1, 31}, {2, 19}, {2, 8},  {1, 28}, {2, 16}, {2, 5},  {1, 25},
    {2, 12}, {2, 1},  {1, 22}, {2, 10}, {1, 29}, {2, 17}, {2, 6},  {1, 26}, {2, 13}, {2, 3},
    {1, 23}, {2, 11}, {1, 31}, {2, 19}, {2, 8},  {1, 28}, {2, 15}, {2, 4},  {1, 24}, {2, 12},
    {2, 1},  {1, 22}, {2, 10}, {1, 30}, {2, 17}, {2, 6},  {1, 26}, {2, 14}, {2, 2},  {1, 23},
    {2, 11}, {2, 1},  {2, 19}, {2, 8},  {1, 28}, {2, 15}, {2, 4},  {1, 24}, {2, 12}, {2, 2},
    {1, 21}, {2, 9},  {1, 29}, {2, 17}, {2, 5},  {1, 26}, {2, 14}, {2, 3},  {1, 23}, {2, 11},
    {1, 31}, {2, 19}, {2, 7},  {1, 27}, {2, 15}, {2, 5},  {1, 24}, {2, 12}, {2, 2},  {1, 22},
    {2, 9},  {1, 29}, {2, 17}, {2, 6},  {1, 26}, {2, 14}, {2, 3},  {1, 24}, {2, 10}, {1, 30},
    {2, 18}, {2, 7},  {1, 27}, {2, 15}, {2, 5},  {1, 25}, {2, 12}, {2, 1},  {1, 21}, {2, 9}
};

static MoonAnchorCache gMoonAnchorCache;
static constexpr int MOON_ANCHOR_START_YEAR = 2026;
static constexpr int MOON_ANCHOR_START_MONTH = 6;
static constexpr int MOON_ANCHOR_START_DAY = 8;
static constexpr int MOON_ANCHOR_END_YEAR = 2100;
static constexpr int MOON_ANCHOR_END_MONTH = 12;
static constexpr int MOON_ANCHOR_END_DAY = 31;
static constexpr double MOON_SYNODIC_MONTH = 29.530588853;
static constexpr double MOON_NEW_MOON_REF_JD = 2451550.09765;

static int clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static uint8_t fontIndex(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 10);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c == '%') return 36;
    if (c == '-') return 37;
    if (c == '+') return 38;
    if (c == 'o') return 39;
    if (c == '^') return 40;
    if (c == '@') return 41;
    if (c == 'v') return 42;
    return 0;
}

static void smallGlyphBounds(char c, int& left, int& right) {
    if (c == ' ') {
        left = 0;
        right = -1;
        return;
    }

    if (c == '-') {
        left = 0;
        right = 2;
        return;
    }

    uint8_t i = fontIndex(c);
    left = 4;
    right = -1;

    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < SEC_FONT_HEIGHT; row++) {
            if (FONT_SMALL[i][row][col]) {
                if (col < left) left = col;
                if (col > right) right = col;
            }
        }
    }
}

int segmentWidth(const WordSegment& segment) {
    return Display::textWidth(segment.text, segment.small, 1, 2);
}

int lineWidth(const WordSegment* segments, uint8_t count) {
    int width = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) {
            width += 2;
        }
        width += segmentWidth(segments[i]);
    }
    return width;
}

void drawWordLine(Display& display, const WordSegment* segments, uint8_t count, int y) {
    int width = lineWidth(segments, count);
    int x = (COLS_PER_ROW - width) / 2;

    for (uint8_t i = 0; i < count; i++) {
        if (i > 0) {
            x += 2;
        }
        display.drawText(segments[i].text, x, y, segments[i].small, 1, 2);
        x += segmentWidth(segments[i]);
    }
}

bool lineFits(const WordSegment* segments, uint8_t count) {
    return lineWidth(segments, count) <= COLS_PER_ROW;
}

void drawCenteredTextWithSpacingFallback(Display& display,
                                         const char* text,
                                         int y,
                                         bool small) {
    int width = Display::textWidth(text, small, 1, 2);
    int x = (COLS_PER_ROW - width) / 2;
    display.drawText(text, x, y, small, 1, 2);
}

static void appendRomanNumeral(int value, char* out, size_t outSize) {
    if (outSize == 0) {
        return;
    }

    out[0] = '\0';

    if (value <= 0) {
        out[0] = 'N';
        if (outSize > 1) {
            out[1] = '\0';
        }
        return;
    }

    struct RomanMap {
        int value;
        const char* numeral;
    };
    static const RomanMap MAP[] = {
        {1000, "M"},
        {900,  "CM"},
        {500,  "D"},
        {400,  "CD"},
        {100,  "C"},
        {90,   "XC"},
        {50,   "L"},
        {40,   "XL"},
        {10,   "X"},
        {9,    "IX"},
        {5,    "V"},
        {4,    "IV"},
        {1,    "I"},
    };

    size_t pos = 0;
    for (const RomanMap& entry : MAP) {
        while (value >= entry.value) {
            size_t len = strlen(entry.numeral);
            if (pos + len >= outSize) {
                out[pos] = '\0';
                return;
            }
            memcpy(out + pos, entry.numeral, len);
            pos += len;
            value -= entry.value;
        }
    }

    out[pos] = '\0';
}

static int clampMonth(int month) {
    if (month < 1) return 1;
    if (month > 12) return 12;
    return month;
}

static int daysInMonth(int month, int year) {
    static const int DAYS[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    month = clampMonth(month);
    if (month == 2) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return DAYS[month - 1];
}

static int weekdaySunday1(int year, int month, int day) {
    if (month < 3) {
        year--;
    }
    static const int T[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int w = (year + year / 4 - year / 100 + year / 400 + T[month - 1] + day) % 7;
    return (w < 0) ? (w + 7) : w;
}

static int64_t daysFromCivilLocal(int year, int month, int day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + (unsigned)day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static double julianDayFromCivil(int year, int month, int day) {
    return 2440587.5 + (double)daysFromCivilLocal(year, month, day);
}

static int64_t julianDayNumberFromCivil(int year, int month, int day) {
    return (int64_t)floor(julianDayFromCivil(year, month, day) + 0.5);
}

static double normalizeDegrees(double degrees) {
    double value = fmod(degrees, 360.0);
    if (value < 0.0) {
        value += 360.0;
    }
    return value;
}

static double sinDegrees(double degrees) {
    static const double kPi = 3.14159265358979323846;
    return sin(normalizeDegrees(degrees) * (kPi / 180.0));
}

static bool chineseNewYearDateForYear(int year, int& month, int& day) {
    if (year < 2001 || year > 2100) {
        return false;
    }

    const MonthDay& entry = CHINESE_NEW_YEAR_DATES_2001_2100[year - 2001];
    month = entry.month;
    day = entry.day;
    return true;
}

static double moonExactAgeDays(int year, int month, int day);
static double moonMeanAgeDays(int year, int month, int day);
static double moonAgeFromForwardAnchors(int year, int month, int day);
static double newMoonJdeForK(double k);

static int64_t moonAnchorStartDayNumber() {
    return daysFromCivilLocal(MOON_ANCHOR_START_YEAR, MOON_ANCHOR_START_MONTH, MOON_ANCHOR_START_DAY);
}

static int64_t moonAnchorEndDayNumber() {
    return daysFromCivilLocal(MOON_ANCHOR_END_YEAR, MOON_ANCHOR_END_MONTH, MOON_ANCHOR_END_DAY);
}

static int64_t moonAnchorJdnForJde(double jde) {
    return (int64_t)floor(jde + 0.5);
}

static void buildMoonAnchorCache() {
    if (gMoonAnchorCache.initialized) {
        return;
    }
    gMoonAnchorCache.initialized = true;

    const int64_t startDay = moonAnchorStartDayNumber();
    const int64_t endDay = moonAnchorEndDayNumber();
    const double startJd = julianDayFromCivil(MOON_ANCHOR_START_YEAR, MOON_ANCHOR_START_MONTH, MOON_ANCHOR_START_DAY) + 0.5;

    double kEstimate = floor((startJd - MOON_NEW_MOON_REF_JD) / MOON_SYNODIC_MONTH);
    int firstK = (int)kEstimate;

    while (moonAnchorJdnForJde(newMoonJdeForK((double)firstK)) < startDay) {
        firstK++;
    }

    for (int k = firstK; gMoonAnchorCache.count < (sizeof(gMoonAnchorCache.offsets) / sizeof(gMoonAnchorCache.offsets[0])); k++) {
        double candidateJde = newMoonJdeForK((double)k);
        int64_t candidateDay = moonAnchorJdnForJde(candidateJde);
        if (candidateDay > endDay) {
            break;
        }
        if (candidateDay >= startDay) {
            gMoonAnchorCache.offsets[gMoonAnchorCache.count++] = (uint16_t)(candidateDay - startDay);
        }
    }
}

static double moonAgeFromForwardAnchors(int year, int month, int day) {
    buildMoonAnchorCache();

    const int64_t currentDay = daysFromCivilLocal(year, month, day);
    const int64_t startDay = moonAnchorStartDayNumber();
    const int64_t endDay = moonAnchorEndDayNumber();

    if (currentDay < startDay) {
        return moonExactAgeDays(year, month, day);
    }
    if (currentDay > endDay || gMoonAnchorCache.count == 0) {
        return moonMeanAgeDays(year, month, day);
    }

    uint16_t targetOffset = (uint16_t)(currentDay - startDay);
    if (targetOffset < gMoonAnchorCache.offsets[0]) {
        return moonExactAgeDays(year, month, day);
    }

    int lo = 0;
    int hi = (int)gMoonAnchorCache.count;
    while (lo < hi) {
        int mid = lo + ((hi - lo) / 2);
        if (gMoonAnchorCache.offsets[mid] <= targetOffset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo <= 0) {
        return moonExactAgeDays(year, month, day);
    }

    uint16_t anchorOffset = gMoonAnchorCache.offsets[lo - 1];
    return (double)(targetOffset - anchorOffset);
}

static double newMoonJdeForK(double k) {
    double t = k / 1236.85;
    double t2 = t * t;
    double t3 = t2 * t;
    double t4 = t3 * t;

    double jde = MOON_NEW_MOON_REF_JD
        + MOON_SYNODIC_MONTH * k
        + 0.0001337 * t2
        - 0.000000150 * t3
        + 0.00000000073 * t4;

    double e = 1.0 - 0.002516 * t - 0.0000074 * t2;

    double m = 2.5534 + 29.10535669 * k - 0.0000218 * t2 - 0.00000011 * t3;
    double mprime = 201.5643 + 385.81693528 * k + 0.0107438 * t2 + 0.00001239 * t3 - 0.000000058 * t4;
    double f = 160.7108 + 390.67050284 * k - 0.0016341 * t2 - 0.00000227 * t3 + 0.000000011 * t4;
    double omega = 124.7746 - 1.56375580 * k + 0.0020691 * t2 + 0.00000215 * t3;

    jde += -0.40720 * sinDegrees(mprime);
    jde += 0.17241 * e * sinDegrees(m);
    jde += 0.01608 * sinDegrees(2.0 * mprime);
    jde += 0.01039 * sinDegrees(2.0 * f);
    jde += 0.00739 * e * sinDegrees(mprime - m);
    jde += -0.00514 * e * sinDegrees(mprime + m);
    jde += 0.00208 * e * e * sinDegrees(2.0 * m);
    jde += -0.00111 * sinDegrees(mprime - 2.0 * f);
    jde += -0.00057 * sinDegrees(mprime + 2.0 * f);
    jde += 0.00056 * e * sinDegrees(2.0 * mprime + m);
    jde += -0.00042 * sinDegrees(3.0 * mprime);
    jde += 0.00042 * e * sinDegrees(m + 2.0 * f);
    jde += 0.00038 * e * sinDegrees(m - 2.0 * f);
    jde += -0.00024 * e * sinDegrees(2.0 * mprime - m);
    jde += -0.00017 * sinDegrees(omega);
    jde += -0.00007 * sinDegrees(mprime + 2.0 * m);
    jde += 0.00004 * sinDegrees(2.0 * mprime - 2.0 * f);
    jde += 0.00004 * sinDegrees(3.0 * m);
    jde += 0.00003 * sinDegrees(mprime + m - 2.0 * f);
    jde += 0.00003 * sinDegrees(2.0 * mprime + 2.0 * f);
    jde += -0.00003 * sinDegrees(mprime + m + 2.0 * f);
    jde += 0.00003 * sinDegrees(mprime - m + 2.0 * f);
    jde += -0.00002 * sinDegrees(mprime - m - 2.0 * f);
    jde += -0.00002 * sinDegrees(3.0 * mprime + m);
    jde += 0.00002 * sinDegrees(4.0 * mprime);

    jde += 0.000325 * sinDegrees(299.77 + 0.107408 * k - 0.009173 * t2);
    jde += 0.000165 * sinDegrees(251.88 + 0.016321 * k);
    jde += 0.000164 * sinDegrees(251.83 + 26.651886 * k);
    jde += 0.000126 * sinDegrees(349.42 + 36.412478 * k);
    jde += 0.000110 * sinDegrees(84.66 + 18.206239 * k);
    jde += 0.000062 * sinDegrees(141.74 + 53.303771 * k);
    jde += 0.000060 * sinDegrees(207.14 + 2.453732 * k);
    jde += 0.000056 * sinDegrees(154.84 + 7.306860 * k);
    jde += 0.000047 * sinDegrees(34.52 + 27.261239 * k);
    jde += 0.000042 * sinDegrees(207.19 + 0.121824 * k);
    jde += 0.000040 * sinDegrees(291.34 + 1.844379 * k);
    jde += 0.000037 * sinDegrees(161.72 + 24.198154 * k);
    jde += 0.000035 * sinDegrees(239.56 + 25.513099 * k);
    jde += 0.000023 * sinDegrees(331.55 + 3.592518 * k);

    return jde;
}

static double moonMeanAgeDays(int year, int month, int day) {
    double jd = julianDayFromCivil(year, month, day) + 0.5;
    double age = fmod(jd - MOON_NEW_MOON_REF_JD, MOON_SYNODIC_MONTH);
    if (age < 0.0) {
        age += MOON_SYNODIC_MONTH;
    }
    return age;
}

static double moonExactAgeDays(int year, int month, int day) {
    double jd = julianDayFromCivil(year, month, day) + 0.5;
    double kEstimate = floor((jd - MOON_NEW_MOON_REF_JD) / MOON_SYNODIC_MONTH);
    double best = -1.0;

    for (int offset = -2; offset <= 2; offset++) {
        double candidate = newMoonJdeForK(kEstimate + offset);
        if (candidate <= jd && candidate > best) {
            best = candidate;
        }
    }

    if (best < 0.0) {
        return moonMeanAgeDays(year, month, day);
    }
    return jd - best;
}

static int chineseZodiacYearForDate(int year, int month, int day) {
    int cnyMonth = 0;
    int cnyDay = 0;
    if (chineseNewYearDateForYear(year, cnyMonth, cnyDay)) {
        if (month < cnyMonth || (month == cnyMonth && day < cnyDay)) {
            return year - 1;
        }
        return year;
    }

    if (month < 2 || (month == 2 && day < 4)) {
        return year - 1;
    }
    return year;
}
} // namespace

void ClockRenderer::init(Display& display, TimeProvider& timeProvider) {
    _display = &display;
    _timeProvider = &timeProvider;
}

void ClockRenderer::setTimeFormat(TimeFormat* timeFormat) {
    _timeFormat = timeFormat;
}

// =============================================================================
// Time rendering
// =============================================================================

int ClockRenderer::effectiveHours(int rawHours) const {
    if (_timeFormat && *_timeFormat == TimeFormat::AmPm) {
        int h = rawHours % 12;
        if (h <= 0) h += 12;
        return h;
    }
    return rawHours;
}

void ClockRenderer::drawPreview(DisplayMode mode, ClockTime time) {
    int hours = effectiveHours(time.hours);
    setDriftStyleActive(mode == DisplayMode::Drift);
    switch (mode) {
        case DisplayMode::TimeWithSeconds:
            drawTime(hours, time.minutes, time.seconds);
            drawSeconds(time.seconds);
            break;
        case DisplayMode::TimeWithDeciseconds:
            drawTime(hours, time.minutes, time.seconds);
            drawDeciseconds(time.seconds, currentClockDeciseconds());
            break;
        case DisplayMode::Word:
            drawWordTime(hours, time.minutes);
            break;
        case DisplayMode::Roma:
            drawRomanTime(time.hours, time.minutes);
            break;
        case DisplayMode::Bin:
            drawBinaryTime(time.hours, time.minutes, time.seconds);
            break;
        case DisplayMode::Drift:
            drawDriftTime(hours, time.minutes, time.seconds, 0);
            break;
        case DisplayMode::Rnd:
            _display->drawCenteredBigText("RND", 0);
            break;
        case DisplayMode::TimeWithDate:
            drawDateTime(time);
            break;
        case DisplayMode::LargeDigitsOnly:
        default:
            drawBigTime(hours, time.minutes, time.seconds);
            break;
    }
}

void ClockRenderer::setDriftStyleActive(bool active) {
    _driftStyleActive = active;
}

void ClockRenderer::drawBigTimeInternal(int hours, int minutes, int seconds, bool driftMode, int offsetMinutes) {
    hours = effectiveHours(hours);
    int digitWidth = 6;
    int spacing = 1;
    int sepWidth = 1;

    int numHourDigits = (hours >= 10) ? 2 : 1;
    int totalDigits = numHourDigits + 2;
    int totalWidth = (digitWidth * totalDigits) + (spacing * (totalDigits - 1)) + sepWidth;
    int startX = (COLS_PER_ROW - totalWidth) / 2;
    int startY = 0;

    int x = startX;
    if (hours >= 10) {
        drawBigTimeDigit(hours / 10, x, startY);
        x += digitWidth + spacing;
    }
    drawBigTimeDigit(hours % 10, x, startY);
    x += digitWidth + spacing;

    int sepX = x;
    if (driftMode) {
        drawDriftSeparator(sepX, startY, seconds, offsetMinutes);
    } else {
        drawBigSeparator(sepX, startY, seconds);
    }
    x += sepWidth + spacing;

    drawBigTimeDigit(minutes / 10, x, startY);
    x += digitWidth + spacing;
    drawBigTimeDigit(minutes % 10, x, startY);
}

void ClockRenderer::drawDriftTime(int hours, int minutes, int seconds, int offsetMinutes) {
    drawBigTimeInternal(hours, minutes, seconds, true, offsetMinutes);
}

void ClockRenderer::drawTime(int hours, int minutes, int seconds) {
    hours = effectiveHours(hours);
    int digitWidth = 6;
    int numHourDigits = (hours >= 10) ? 2 : 1;
    int totalDigits = numHourDigits + 2;
    int totalWidth = (digitWidth * totalDigits) + (TIME_FONT_SPACING * (totalDigits - 1)) + TIME_SEP_WIDTH;
    int startX = (COLS_PER_ROW - totalWidth) / 2;
    int startY = 0;

    int x = startX;
    if (hours >= 10) {
        drawTimeDigit(hours / 10, x, startY);
        x += digitWidth + TIME_FONT_SPACING;
    }
    drawTimeDigit(hours % 10, x, startY);
    x += digitWidth + TIME_FONT_SPACING;

    int sepX = x;
    drawSeparator(sepX, startY, seconds);
    x += TIME_SEP_WIDTH + TIME_FONT_SPACING;

    drawTimeDigit(minutes / 10, x, startY);
    x += digitWidth + TIME_FONT_SPACING;
    drawTimeDigit(minutes % 10, x, startY);
}

void ClockRenderer::drawBigTime(int hours, int minutes, int seconds) {
    drawBigTimeInternal(hours, minutes, seconds, false, 0);
}

void ClockRenderer::drawDriftSeparator(int x, int y, int seconds, int offsetMinutes) {
    (void)seconds;
    int absOffset = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
    int empty = DRIFT_SEP_GAP_NEAR;
#if DRIFT_SEPARATOR_INDICATOR
    if (absOffset >= DRIFT_MAX_OFFSET_MINUTES / 3) {
        empty = DRIFT_SEP_GAP_FAR;
    } else if (absOffset >= DRIFT_MAX_OFFSET_MINUTES / 10) {
        empty = DRIFT_SEP_GAP_MID;
    }
#endif
    int ctc = empty + 1;
    int topY = y + 7 - (ctc / 2);
    int bottomY = topY + ctc;
    if (topY < y) topY = y;
    if (bottomY >= y + TIME_FONT_BIG_HEIGHT) bottomY = y + TIME_FONT_BIG_HEIGHT - 1;
    _display->setPixel(x, topY, true);
    _display->setPixel(x, bottomY, true);
}

void ClockRenderer::drawWordTimeLegacy(int hours, int minutes) {
    hours = effectiveHours(hours);
    const int letterSpacing = 1;
    const int wordGap = 2;

    if (minutes == 0) {
        drawCenteredText(hourWord(hours), 0, false, letterSpacing, wordGap);
        drawCenteredText("OCLOCK", 11, true, letterSpacing, wordGap);
        return;
    }

    bool toHour = minutes > 30;
    int minuteValue = toHour ? (60 - minutes) : minutes;
    int targetHour = toHour ? (hours + 1) : hours;

    char minuteLine[20];
    buildMinutePhrase(minuteValue, toHour, letterSpacing, wordGap, minuteLine, sizeof(minuteLine));
    drawCenteredText(minuteLine, 0, true, letterSpacing, wordGap);
    drawCenteredText(hourWord(targetHour), 6, false, letterSpacing, wordGap);
}

void ClockRenderer::drawWordTime(int hours, int minutes) {
    hours = effectiveHours(hours);

    if (minutes == 0) {
        const WordSegment hourLine[] = {
            {hourWord(hours), false},
        };
        const WordSegment clockLine[] = {
            {"OCLOCK", true},
        };
        drawWordLine(*_display, hourLine, 1, 0);
        drawWordLine(*_display, clockLine, 1, 11);
        return;
    }

    bool toHour = minutes > 30;
    int minuteValue = toHour ? (60 - minutes) : minutes;
    int targetHour = toHour ? (hours + 1) : hours;
    const char* connector = toHour ? "TO" : "PAST";

    WordSegment line1A[] = {
        {minuteWord(minuteValue), true},
        {connector, true},
    };
    WordSegment line2Hour[] = {
        {hourWord(targetHour), false},
    };

    if (lineFits(line1A, 2) && lineFits(line2Hour, 1)) {
        drawWordLine(*_display, line1A, 2, 0);
        drawWordLine(*_display, line2Hour, 1, 6);
        return;
    }

    WordSegment line1Minute[] = {
        {minuteWord(minuteValue), true},
    };
    WordSegment line2Horizontal[] = {
        {connector, true},
        {hourWord(targetHour), false},
    };

    if (lineFits(line1Minute, 1) && lineFits(line2Horizontal, 2)) {
        drawWordLine(*_display, line1Minute, 1, 0);
        drawWordLine(*_display, line2Horizontal, 2, 6);
        return;
    }

    // Final fallback keeps the older word renderer behavior, which may
    // still introduce numeric fragments for the longest phrases.
    drawWordTimeLegacy(hours, minutes);
}

void ClockRenderer::drawRomanTime(int hours, int minutes) {
    hours = effectiveHours(hours);

    char hourBuf[16];
    char minuteBuf[16];
    appendRomanNumeral(hours, hourBuf, sizeof(hourBuf));

    if (minutes == 0) {
        _display->drawCenteredBigText(hourBuf, 0);
        return;
    }

    appendRomanNumeral(minutes, minuteBuf, sizeof(minuteBuf));
    _display->drawCenteredMediumText(hourBuf, 0);
    _display->drawCenteredSmallText(minuteBuf, 11);
}

void ClockRenderer::drawBinaryTime(int hours, int minutes, int seconds) {
    (void)seconds;
    drawBinaryRow((uint8_t)hours, 0);
    drawBinaryRow((uint8_t)minutes, 6);
    drawBinaryRow((uint8_t)seconds, 12);
}

void ClockRenderer::drawDateTime(ClockTime time) {
    int hours = effectiveHours(time.hours);
    drawTime(hours, time.minutes, time.seconds);

    ClockDate currentDate;
    if (_timeProvider->currentDate(currentDate)) {
        drawMonthDayLine(currentDate.date, currentDate.month, 11);
    } else {
        _display->drawCenteredSmallText("NO DATE", 11);
    }
}

void ClockRenderer::drawTimeDigit(uint8_t digit, int x, int y) {
    if (digit > 9) return;

    for (int row = 0; row < TIME_FONT_MEDIUM_HEIGHT; row++) {
        for (int col = 0; col < 6; col++) {
            if (FONT_MEDIUM[digit][row][col]) {
                _display->setPixel(x + col, y + row, true);
            }
        }
    }
}

void ClockRenderer::drawBigTimeDigit(uint8_t digit, int x, int y) {
    if (digit > 9) return;

    for (int row = 0; row < TIME_FONT_BIG_HEIGHT; row++) {
        for (int col = 0; col < 6; col++) {
            if (FONT_BIG[digit][row][col]) {
                _display->setPixel(x + col, y + row, true);
            }
        }
    }
}

void ClockRenderer::drawBinaryRow(uint8_t value, int y) {
    for (int bit = 5; bit >= 0; bit--) {
        int cellX = 1 + ((5 - bit) * 5);
        bool on = ((value >> bit) & 0x01) != 0;
        for (int dy = 0; dy < 4; dy++) {
            for (int dx = 0; dx < 5; dx++) {
                bool pixel = on
                    ? (dx >= 1 && dx <= 3 && dy <= 2)
                    : (dx == 2 && dy == 1);
                if (pixel) {
                    _display->setPixel(cellX + dx, y + dy, true);
                }
            }
        }
    }
}

void ClockRenderer::drawSeparator(int x, int y, int seconds) {
    (void)seconds;
    _display->setPixel(x, y + 4, true);
    _display->setPixel(x, y + 6, true);
}

void ClockRenderer::drawBigSeparator(int x, int y, int seconds) {
    (void)seconds;
    _display->setPixel(x, y + 6, true);
    _display->setPixel(x, y + 9, true);
}

void ClockRenderer::drawSeconds(int seconds) {
    int digitWidth = 4;
    int totalWidth = (digitWidth * 2) + SEC_FONT_SPACING;
    int startX = (COLS_PER_ROW - totalWidth) / 2 + 1;
    int startY = 11;

    drawSecDigit(seconds / 10, startX, startY);
    drawSecDigit(seconds % 10, startX + digitWidth + SEC_FONT_SPACING, startY);
}

void ClockRenderer::drawDeciseconds(int seconds, uint8_t deciseconds) {
    int digitWidth = 4;
    int dotWidth = 1;
    int totalWidth = (digitWidth * 3) + (SEC_FONT_SPACING * 2) + dotWidth + (SEC_FONT_SPACING * 2);
    int startX = (COLS_PER_ROW - totalWidth) / 2 + 1;
    int startY = 11;
    int x = startX;

    drawSecDigit(seconds / 10, x, startY);
    x += digitWidth + SEC_FONT_SPACING;
    drawSecDigit(seconds % 10, x, startY);
    x += digitWidth + SEC_FONT_SPACING;

    _display->setPixel(x, startY + SEC_FONT_HEIGHT - 1, true);
    x += dotWidth + SEC_FONT_SPACING;

    drawSecDigit(deciseconds % 10, x, startY);
}

void ClockRenderer::drawSecDigit(uint8_t digit, int x, int y) {
    if (digit > 9) return;

    for (int row = 0; row < SEC_FONT_HEIGHT; row++) {
        for (int col = 0; col < 4; col++) {
            if (FONT_SMALL[digit][row][col]) {
                _display->setPixel(x + col, y + row, true);
            }
        }
    }
}

int ClockRenderer::textWidth(const char* s, int cellW, int letterSpacing, int wordGap) const {
    return Display::textWidth(s, cellW <= 4, letterSpacing, wordGap);
}

void ClockRenderer::drawText(const char* s, int x, int y, bool small, int letterSpacing, int wordGap) {
    _display->drawText(s, x, y, small, letterSpacing, wordGap);
}

void ClockRenderer::drawCenteredText(const char* s, int y, bool small, int letterSpacing, int wordGap) {
    int w = Display::textWidth(s, small, letterSpacing, wordGap);
    int x = (COLS_PER_ROW - w) / 2;
    drawText(s, x, y, small, letterSpacing, wordGap);
}

const char* ClockRenderer::hourWord(int hours) const {
    static const char* const HOURS[] = {
        "TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE",
        "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN"
    };
    int h = hours % 12;
    if (h < 0) h += 12;
    return HOURS[h];
}

const char* ClockRenderer::minuteWord(int minutes) const {
    static const char* const MINUTES[] = {
        "",
        "ONE", "TWO", "THREE", "FOUR", "FIVE",
        "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
        "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "QUARTER",
        "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN", "TWENTY",
        "TWENTYONE", "TWENTYTWO", "TWENTYTHREE", "TWENTYFOUR", "TWENTYFIVE",
        "TWENTYSIX", "TWENTYSEVEN", "TWENTYEIGHT", "TWENTYNINE", "HALF"
    };
    if (minutes < 1 || minutes > 30) {
        return "";
    }
    return MINUTES[minutes];
}

void ClockRenderer::buildMinutePhrase(int minutes, bool toHour, int letterSpacing, int wordGap, char* out, size_t outSize) const {
    const char* direction = toHour ? "TO" : "PAST";

    char wordPhrase[20];
    snprintf(wordPhrase, sizeof(wordPhrase), "%s %s", minuteWord(minutes), direction);
    if (textWidth(wordPhrase, 4, letterSpacing, wordGap) <= COLS_PER_ROW) {
        snprintf(out, outSize, "%s", wordPhrase);
        return;
    }

    snprintf(out, outSize, "%d %s", minutes, direction);
}

// =============================================================================
// Date peek
// =============================================================================

void ClockRenderer::drawMonthDayLine(int date, int month, int y) {
    static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (month < 1 || month > 12) month = 1;

    const char* monthName = MONTHS[month - 1];
    char line[16];
    snprintf(line, sizeof(line), "%s %d", monthName, date);
    drawCenteredTextWithSpacingFallback(*_display, line, y, true);
}

int ClockRenderer::dayOfYear(int year, int month, int day) {
    static const int MONTH_OFFSETS[] = {
        0, 31, 59, 90, 120, 151,
        181, 212, 243, 273, 304, 334
    };

    month = clampMonth(month);
    day = (day < 1) ? 1 : day;
    int doy = MONTH_OFFSETS[month - 1] + day;
    if (month > 2 && isLeapYear(year)) {
        doy++;
    }
    return doy;
}

bool ClockRenderer::isLeapYear(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

int ClockRenderer::weekdayMonday1(int year, int month, int day) {
    int weekday0 = weekdaySunday1(year, month, day);
    return (weekday0 == 0) ? 7 : weekday0;
}

int ClockRenderer::isoWeeksInYear(int year) {
    int jan1 = weekdayMonday1(year, 1, 1);
    if (jan1 == 4) {
        return 53;
    }
    if (jan1 == 3 && isLeapYear(year)) {
        return 53;
    }
    return 52;
}

double ClockRenderer::getMoonAgeDays(int year, int month, int day) {
    if (year < MOON_ANCHOR_START_YEAR) {
        return moonExactAgeDays(year, month, day);
    }
    if (year <= MOON_ANCHOR_END_YEAR) {
        return moonAgeFromForwardAnchors(year, month, day);
    }
    return moonMeanAgeDays(year, month, day);
}

double ClockRenderer::getMoonIlluminationPercent(double ageDays) {
    static const double kPi = 3.14159265358979323846;
    double phase = ageDays / MOON_SYNODIC_MONTH;
    double illumination = (1.0 - cos(2.0 * kPi * phase)) * 50.0;
    if (illumination < 0.0) illumination = 0.0;
    if (illumination > 100.0) illumination = 100.0;
    return illumination;
}

const char* ClockRenderer::getMoonState(double ageDays, double illuminationPercent) {
    if (illuminationPercent <= 2.0) {
        return "NEW";
    }
    if (illuminationPercent >= 98.0) {
        return "FULL";
    }
    return (ageDays < (MOON_SYNODIC_MONTH / 2.0)) ? "WAXING" : "WANING";
}

static char moonStateGlyph(const char* state) {
    if (strcmp(state, "WAXING") == 0) return '^';
    if (strcmp(state, "WANING") == 0) return 'v';
    if (strcmp(state, "FULL") == 0) return '@';
    return 'o';
}

int ClockRenderer::getChineseYearNumber(int year, int month, int day) {
    return chineseZodiacYearForDate(year, month, day) + 2698;
}

const char* ClockRenderer::getWesternZodiacSign(int month, int day) {
    struct Range {
        int month;
        int day;
        const char* sign;
    };
    static const Range RANGES[] = {
        {1, 20, "CAPRICORN"},
        {2, 19, "AQUARIUS"},
        {3, 20, "PISCES"},
        {4, 19, "ARIES"},
        {5, 20, "TAURUS"},
        {6, 20, "GEMINI"},
        {7, 22, "CANCER"},
        {8, 22, "LEO"},
        {9, 22, "VIRGO"},
        {10, 22, "LIBRA"},
        {11, 21, "SCORPIO"},
        {12, 21, "SAGITTARIUS"},
    };

    if (month < 1 || month > 12) {
        month = 1;
        day = 1;
    }

    for (int i = 11; i >= 0; i--) {
        if (month > RANGES[i].month || (month == RANGES[i].month && day >= RANGES[i].day)) {
            return RANGES[i].sign;
        }
    }
    return "CAPRICORN";
}

const char* ClockRenderer::getWesternZodiacElement(const char* sign) {
    if (strcmp(sign, "ARIES") == 0) return "FIRE";
    if (strcmp(sign, "TAURUS") == 0) return "EARTH";
    if (strcmp(sign, "GEMINI") == 0) return "AIR";
    if (strcmp(sign, "CANCER") == 0) return "WATER";
    if (strcmp(sign, "LEO") == 0) return "FIRE";
    if (strcmp(sign, "VIRGO") == 0) return "EARTH";
    if (strcmp(sign, "LIBRA") == 0) return "AIR";
    if (strcmp(sign, "SCORPIO") == 0) return "WATER";
    if (strcmp(sign, "SAGITTARIUS") == 0) return "FIRE";
    if (strcmp(sign, "CAPRICORN") == 0) return "EARTH";
    if (strcmp(sign, "AQUARIUS") == 0) return "AIR";
    return "WATER";
}

static const char* westernZodiacDisplayLabel(const char* sign) {
    if (strcmp(sign, "SCORPIO") == 0) return "SCORP";
    if (strcmp(sign, "SAGITTARIUS") == 0) return "SAGIT";
    if (strcmp(sign, "CAPRICORN") == 0) return "CAPRI";
    if (strcmp(sign, "AQUARIUS") == 0) return "AQUAR";
    return sign;
}

const char* ClockRenderer::getChineseZodiacAnimal(int year) {
    static const char* const ANIMALS[] = {
        "RAT", "OX", "TIGER", "RABBIT", "DRAGON", "SNAKE",
        "HORSE", "GOAT", "MONKEY", "ROOSTER", "DOG", "PIG"
    };
    int index = (year - 4) % 12;
    if (index < 0) index += 12;
    return ANIMALS[index];
}

const char* ClockRenderer::getChineseZodiacElement(int year) {
    static const char* const ELEMENTS[] = {
        "WOOD", "WOOD", "FIRE", "FIRE", "EARTH", "EARTH",
        "METAL", "METAL", "WATER", "WATER"
    };
    int index = (year - 4) % 10;
    if (index < 0) index += 10;
    return ELEMENTS[index];
}

const char* ClockRenderer::getChineseZodiacAnimalCode(int year) {
    static const char* const CODES[] = {
        "RAT", "OX", "TIGER", "RABBIT", "DRGN", "SNAKE",
        "HORSE", "GOAT", "MNKY", "RSTR", "DOG", "PIG"
    };
    int index = (year - 4) % 12;
    if (index < 0) index += 12;
    return CODES[index];
}

void ClockRenderer::drawDateStyleDate(const ClockDate& currentDate) {
    static const char* const WEEKDAYS[] = {"SUNDAY", "MONDAY", "TUESDAY", "WED", "THU", "FRIDAY", "SAT"};

    int day = currentDate.day;
    int date = currentDate.date;
    int month = currentDate.month;

    if (day < 1 || day > 7) day = 1;
    if (month < 1 || month > 12) month = 1;

    _display->drawCenteredSmallText(WEEKDAYS[day - 1], 0);
    drawMonthDayLine(date, month, 11);
}

void ClockRenderer::drawDateStyleYear(const ClockDate& currentDate) {
    char buf[20];
    int doy = dayOfYear(currentDate.year, currentDate.month, currentDate.date);

    snprintf(buf, sizeof(buf), "%d", currentDate.year);
    _display->drawCenteredSmallText(buf, 0);
    snprintf(buf, sizeof(buf), "DAY %d", doy);
    _display->drawCenteredSmallText(buf, 11);
}

void ClockRenderer::drawDateStyleMoon(const ClockDate& currentDate) {
    double age = getMoonAgeDays(currentDate.year, currentDate.month, currentDate.date);
    double illumination = getMoonIlluminationPercent(age);
    const char* state = getMoonState(age, illumination);

    char line1[20];
    snprintf(line1, sizeof(line1), "MOON %c", moonStateGlyph(state));
    _display->drawCenteredSmallText(line1, 0);

    char line2[20];
    if (age < MOON_SYNODIC_MONTH / 2.0) {
        if (illumination >= 98.0) {
            snprintf(line2, sizeof(line2), "FULL");
        } else {
            int days = (int)(MOON_SYNODIC_MONTH / 2.0 - age + 0.5);
            snprintf(line2, sizeof(line2), "FULL IN %d", days);
        }
    } else {
        if (illumination <= 2.0) {
            snprintf(line2, sizeof(line2), "NEW");
        } else {
            int days = (int)(MOON_SYNODIC_MONTH - age + 0.5);
            snprintf(line2, sizeof(line2), "NEW IN %d", days);
        }
    }
    _display->drawCenteredSmallText(line2, 11);
}

void ClockRenderer::drawDateStyleZod(const ClockDate& currentDate) {
    const char* sign = getWesternZodiacSign(currentDate.month, currentDate.date);
    const char* element = getWesternZodiacElement(sign);
    _display->drawCenteredSmallText(westernZodiacDisplayLabel(sign), 0);
    _display->drawCenteredSmallText(element, 11);
}

void ClockRenderer::drawDateStyleCzod(const ClockDate& currentDate) {
    int zodiacYear = chineseZodiacYearForDate(currentDate.year, currentDate.month, currentDate.date);
    const char* animal = getChineseZodiacAnimal(zodiacYear);
    const char* animalCode = getChineseZodiacAnimalCode(zodiacYear);
    const char* element = getChineseZodiacElement(zodiacYear);

    _display->drawCenteredSmallText((Display::textWidth(animal, true, 1, 2) <= COLS_PER_ROW) ? animal : animalCode, 0);
    _display->drawCenteredSmallText(element, 11);
}

void ClockRenderer::drawDateView(DateStyle style) {
    ClockDate currentDate;
    if (!_timeProvider->currentDate(currentDate)) {
        _display->drawCenteredSmallText("NO", 0);
        _display->drawCenteredSmallText("DATE", 11);
        return;
    }

    switch (style) {
        case DateStyle::Year:
            drawDateStyleYear(currentDate);
            break;
        case DateStyle::Moon:
            drawDateStyleMoon(currentDate);
            break;
        case DateStyle::Zod:
            drawDateStyleZod(currentDate);
            break;
        case DateStyle::Czod:
            drawDateStyleCzod(currentDate);
            break;
        case DateStyle::Date:
        default:
            drawDateStyleDate(currentDate);
            break;
    }
}

uint8_t ClockRenderer::currentClockDeciseconds() const {
    return (uint8_t)((_timeProvider->milliseconds() / 100) % 10);
}
