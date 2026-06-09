#ifndef MENU_CONTROLLER_H
#define MENU_CONTROLLER_H

#include <Arduino.h>

class SettingsStore;

enum class MenuState : uint8_t {
    Off = 0,
    Browse = 1,
    Edit = 2
};

enum class MenuBlinkPhase : uint8_t {
    // Value label (e.g. "TALL") is being shown to introduce a new selection.
    // Blinks twice (on, off, on, off) over ~2 s, then transitions to ClockPreview.
    NameIntro = 0,
    // Live clock preview in the chosen style, blinking on/off continuously.
    // Only used by the STYLE menu item; other items stay in NameIntro
    // semantics and just blink the value via blinkOn().
    ClockPreview = 1
};

struct MenuItem {
    const char* name;
    int16_t minValue;
    int16_t maxValue;
    int16_t (*getValue)(void* ctx);
    void (*previewValue)(void* ctx, int16_t value);
    void (*commitValue)(void* ctx, int16_t value);
    void (*previewAction)(void* ctx, int16_t value);
};

class MenuController {
public:
    void begin(const MenuItem* items, uint8_t itemCount);
    void setContext(void* ctx);
    void setSettingsStore(SettingsStore* store);
    void enterBrowse();
    void enterEdit();
    void saveEdit();
    void cancelEdit();
    void exit();
    void update();
    void onPrev();
    void onNext();
    void onOk();

    bool isActive() const;
    bool isBrowse() const;
    bool isEdit() const;
    bool blinkOn() const;
    MenuBlinkPhase blinkPhase() const;
    uint8_t index() const;
    int16_t editValue() const;
    const MenuItem& currentItem() const;

    // True while the user is editing the BRIGHT item. The orchestrator uses
    // this to bypass night-mode brightness dimming for the duration of
    // the preview, so the user can see the selected brightness on the
    // whole screen. Cleared on save, cancel, exit, and timeout.
    bool isBrightPreviewActive() const { return _brightPreviewActive; }

private:
    void previewCurrent();
    void markActivity();

    const MenuItem* _items = nullptr;
    uint8_t _itemCount = 0;
    void* _ctx = nullptr;
    SettingsStore* _settingsStore = nullptr;
    MenuState _state = MenuState::Off;
    uint8_t _index = 0;
    int16_t _editValue = 0;
    int16_t _originalValue = 0;
    uint32_t _lastActivityMs = 0;
    MenuBlinkPhase _blinkPhase = MenuBlinkPhase::NameIntro;
    bool _blinkOn = true;
    uint32_t _lastBlinkMs = 0;
    uint8_t _nameIntroBlanks = 0;
    bool _brightPreviewActive = false;
};

#endif
