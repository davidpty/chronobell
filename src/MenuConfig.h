#ifndef MENU_CONFIG_H
#define MENU_CONFIG_H

#include <Arduino.h>
#include "AppSettings.h"
#include "MenuController.h"

extern MenuItem MENU_ITEMS[];
extern const uint8_t MENU_ITEM_COUNT;
extern StyleConfig g_stylePending;

extern uint8_t g_settimeStep;
extern uint8_t  g_setHour;
extern uint8_t  g_setMin;
extern uint8_t  g_setSec;
extern uint8_t  g_setDay;
extern uint8_t  g_setMonth;
extern uint16_t g_setYear;

extern uint8_t g_alarmStep;
extern uint8_t g_alarmHour;
extern uint8_t g_alarmMin;

uint8_t styleMenuStep();
DisplayMode styleMenuPreviewMode();
bool styleMenuIsEditing();
bool styleMenuInfoPreviewActive();

#endif // MENU_CONFIG_H
