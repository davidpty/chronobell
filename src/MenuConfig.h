#ifndef MENU_CONFIG_H
#define MENU_CONFIG_H

#include <Arduino.h>
#include "AppSettings.h"
#include "MenuController.h"

extern MenuItem MENU_ITEMS[];
extern const uint8_t MENU_ITEM_COUNT;
extern InfoLineMode g_stylePendingInfoLineMode;

extern uint8_t g_settimeStep;
extern uint8_t  g_setHour;
extern uint8_t  g_setMin;
extern uint8_t  g_setSec;
extern uint8_t  g_setDay;
extern uint8_t  g_setMonth;
extern uint16_t g_setYear;

uint8_t styleMenuStep();
DisplayMode styleMenuPreviewMode();
InfoLineMode styleMenuPendingInfoLineMode();
SeparatorMode styleMenuPendingSeparatorMode();
DriftSeparatorMode styleMenuPendingDriftSeparatorMode();
bool styleMenuIsEditing();
bool styleMenuInfoPreviewActive();

#endif // MENU_CONFIG_H
