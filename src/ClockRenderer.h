#ifndef CLOCK_RENDERER_H
#define CLOCK_RENDERER_H

#include <Arduino.h>

#include "AppSettings.h"
#include "RtcClock.h"

class Display;
class TimeProvider;

class ClockRenderer {
public:
    ClockRenderer() = default;

    // Two-phase init: called by Display::begin() after Display's own
    // members are constructed. Methods are no-ops (assert in debug)
    // before this is called.
    void init(Display& display, TimeProvider& timeProvider);
    void setTimeFormat(TimeFormat* timeFormat);

    // Top-level time-rendering entry points used by Display::showTime().
    void drawTime(int hours, int minutes, int seconds);
    void drawBigTime(int hours, int minutes, int seconds);
    void drawDriftTime(int hours, int minutes, int seconds);
    void drawWordTime(int hours, int minutes);
    void drawRomanTime(int hours, int minutes);
    void drawBinaryTime(int hours, int minutes, int seconds);
    void drawDateTime(ClockTime time);
    void drawDateView(DateStyle style);
    void drawSeconds(int seconds);
    void drawDeciseconds(int seconds, uint8_t deciseconds);
    // Renders a live clock preview in the chosen style. Used by the menu
    // Edit-mode preview phase (see MenuRenderer).
    void drawPreview(DisplayMode mode, ClockTime time);
    void setDriftStyleActive(bool active);

    // Used by Display to fetch the live deciseconds digit (0-9) for the decisecond display.
    uint8_t currentClockDeciseconds() const;

private:
    // Low-level drawing. Time digits are tabular; word/date text is proportional.
    int effectiveHours(int rawHours) const;
    void drawTimeDigit(uint8_t digit, int x, int y);
    void drawBigTimeDigit(uint8_t digit, int x, int y);
    void drawSeparator(int x, int y, int seconds);
    void drawBigSeparator(int x, int y, int seconds);
    void drawDriftSeparator(int x, int y, int seconds);
    void drawDriftApproxMarker(int x, int y);
    void drawSecDigit(uint8_t digit, int x, int y);
    int textWidth(const char* s, int cellW, int letterSpacing, int wordGap) const;
    void drawText(const char* s, int x, int y, bool small, int letterSpacing, int wordGap);
    void drawCenteredText(const char* s, int y, bool small, int letterSpacing, int wordGap);
    void drawWordTimeLegacy(int hours, int minutes);
    void drawBinaryRow(uint8_t value, int y);
    const char* hourWord(int hours) const;
    const char* minuteWord(int minutes) const;
    void buildMinutePhrase(int minutes, bool toHour, int letterSpacing, int wordGap, char* out, size_t outSize) const;

    // Date helpers.
    void drawMonthDayLine(int date, int month, int y);
    void drawDateStyleDate(const ClockDate& currentDate);
    void drawDateStyleYear(const ClockDate& currentDate);
    void drawDateStyleMoon(const ClockDate& currentDate);
    void drawDateStyleZod(const ClockDate& currentDate);
    void drawDateStyleCzod(const ClockDate& currentDate);
    static const char* getChineseZodiacAnimalCode(int year);
    static int dayOfYear(int year, int month, int day);
    static int isoWeeksInYear(int year);
    static bool isLeapYear(int year);
    static int weekdayMonday1(int year, int month, int day);
    static double getMoonAgeDays(int year, int month, int day);
    static double getMoonIlluminationPercent(double ageDays);
    static const char* getMoonState(double ageDays, double illuminationPercent);
    static int getChineseYearNumber(int year, int month, int day);
    static const char* getWesternZodiacSign(int month, int day);
    static const char* getWesternZodiacElement(const char* sign);
    static const char* getChineseZodiacAnimal(int year);
    static const char* getChineseZodiacElement(int year);
    void resetDriftState();
    void updateDriftState(int exactHours, int exactMinutes, unsigned long nowMs);
    int minuteOfDay(int hours, int minutes) const;
    int signedMinuteDelta(int fromMinute, int toMinute) const;
    int wrapMinuteOfDay(int minute) const;
    static int driftRandomRange(int minValue, int maxValue);
    static int driftHoldMsForLag(int lagMinutes);
    static int driftStepMinutes(int lagMinutes);
    void drawBigTimeInternal(int hours, int minutes, int seconds, bool driftMode, bool showApproxMarker);

    Display* _display = nullptr;
    TimeProvider* _timeProvider = nullptr;
    TimeFormat* _timeFormat = nullptr;
    bool _driftStyleActive = false;
    bool _driftInitialized = false;
    int _driftDisplayedMinute = 0;
    unsigned long _driftHoldStartedMs = 0;
    unsigned long _driftHoldDurationMs = 0;
};

#endif // CLOCK_RENDERER_H
