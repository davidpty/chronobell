#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

// Numeric font metrics
#define TIME_FONT_BIG_HEIGHT       16
#define TIME_FONT_MEDIUM_HEIGHT    10
#define TIME_FONT_SPACING          1
#define TIME_SEP_WIDTH             1

// Small font metrics
#define SEC_FONT_HEIGHT            5
#define SEC_FONT_SPACING           1

extern const uint8_t FONT_BIG[43][TIME_FONT_BIG_HEIGHT][6];
extern const uint8_t FONT_MEDIUM[44][TIME_FONT_MEDIUM_HEIGHT][6];
extern const uint8_t FONT_SMALL[43][SEC_FONT_HEIGHT][4];

#endif
