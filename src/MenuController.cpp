#include "MenuController.h"

#include "Config.h"
#include "SettingsStore.h"

void MenuController::begin(const MenuItem* items, uint8_t itemCount) {
    _items = items;
    _itemCount = itemCount;
    exit();
}

void MenuController::setContext(void* ctx) {
    _ctx = ctx;
}

void MenuController::setSettingsStore(SettingsStore* store) {
    _settingsStore = store;
}

void MenuController::enterBrowse() {
    if (!_items || _itemCount == 0) return;

    if (_settingsStore) {
        _index = _settingsStore->loadMenuIndex(_itemCount, 0);
    } else if (_index >= _itemCount) {
        _index = 0;
    }
    _state = MenuState::Browse;
    _editValue = _items[_index].getValue(_ctx);
    _originalValue = _editValue;
    markActivity();
    LOGF("Menu: BROWSE on %s (idx=%u)\n", _items[_index].name, _index);
}

void MenuController::enterEdit() {
    if (!_items || _itemCount == 0 || _state != MenuState::Browse) return;

    _state = MenuState::Edit;
    _editValue = _items[_index].getValue(_ctx);
    _originalValue = _editValue;
    _brightPreviewActive = (strcmp(_items[_index].name, "BRIGHT") == 0);
    markActivity();
    previewCurrent();
    LOGF("Menu: EDIT %s (start=%d)\n",
                  _items[_index].name, (int)_editValue);
}

void MenuController::saveEdit() {
    if (!_items || _itemCount == 0 || _state != MenuState::Edit) return;

    const MenuItem& item = _items[_index];
    if (item.commitValue) item.commitValue(_ctx, _editValue);
    _originalValue = _editValue;

    if (item.editCommit && item.editCommit(_ctx, _editValue)) {
        _editValue = item.getValue ? item.getValue(_ctx) : _editValue;
        _originalValue = _editValue;
        _brightPreviewActive = false;
        previewCurrent();
        markActivity();
        return;
    }

    _brightPreviewActive = false;
    LOGF("Menu: SAVED %s = %d\n", item.name, (int)_editValue);
    _state = MenuState::Browse;
    markActivity();
}

void MenuController::cancelEdit() {
    if (!_items || _itemCount == 0 || _state != MenuState::Edit) return;

    const MenuItem& item = _items[_index];
    LOGF("Menu: CANCEL %s (revert %d -> %d)\n",
                  item.name, (int)_editValue, (int)_originalValue);
    if (item.previewValue) item.previewValue(_ctx, _originalValue);
    _editValue = _originalValue;
    _brightPreviewActive = false;
    if (item.cancelValue) item.cancelValue(_ctx);
    _state = MenuState::Browse;
    markActivity();
}

void MenuController::exit() {
    if (_state != MenuState::Off) {
        LOGLN("Menu: EXIT to NORMAL");
    }
    if (_items && _index < _itemCount) {
        if (_items[_index].cancelValue) {
            _items[_index].cancelValue(_ctx);
        }
        if (_settingsStore) {
            _settingsStore->saveMenuIndex(_index);
        }
    }
    _state = MenuState::Off;
    _editValue = 0;
    _originalValue = 0;
    _lastActivityMs = 0;
    _blinkPhase = MenuBlinkPhase::NameIntro;
    _blinkOn = true;
    _lastBlinkMs = 0;
    _nameIntroBlanks = 0;
    _brightPreviewActive = false;
}

void MenuController::update() {
    if (_state == MenuState::Off || !_items || _itemCount == 0) return;

    uint32_t now = millis();
    uint32_t timeoutMs = (_state == MenuState::Edit)
        ? MENU_TIMEOUT_LONG_SECONDS * 1000UL
        : MENU_TIMEOUT_SHORT_SECONDS * 1000UL;
    if (now - _lastActivityMs >= timeoutMs) {
        LOGLN("Menu: auto-timeout");
        if (_state == MenuState::Edit) {
            const MenuItem& item = _items[_index];
            if (item.previewValue) item.previewValue(_ctx, _originalValue);
        }
        exit();
        return;
    }

    if (_state == MenuState::Edit) {
        uint32_t blinkDuration = _blinkOn ? BLINK_ON_MS : BLINK_OFF_MS;
        if (now - _lastBlinkMs < blinkDuration) {
            return;
        }

        if (_blinkPhase == MenuBlinkPhase::NameIntro) {
            if (_blinkOn) {
                _blinkOn = false;
            } else {
                // Count completed blank pulses; the value name is shown
                // for two full on/off cycles before the preview takes over.
                _nameIntroBlanks++;
                if (_nameIntroBlanks >= 2) {
                    _blinkPhase = MenuBlinkPhase::ClockPreview;
                    _blinkOn = true;
                    _nameIntroBlanks = 0;
                } else {
                    _blinkOn = true;
                }
            }
        } else {
            _blinkOn = !_blinkOn;
        }
        _lastBlinkMs = now;
    }
}

void MenuController::onPrev() {
    if (!_items || _itemCount == 0 || _state == MenuState::Off) return;

    if (_state == MenuState::Browse) {
        _index = (_index + _itemCount - 1) % _itemCount;
        markActivity();
        LOGF("Menu: prev -> item %u (%s)\n", _index, _items[_index].name);
        return;
    }

    int16_t& value = _editValue;
    const MenuItem& item = _items[_index];
    value--;
    if (value < item.minValue) value = item.maxValue;
    markActivity();
    previewCurrent();
    LOGF("Menu edit: %s = %d\n", item.name, value);
}

void MenuController::onNext() {
    if (!_items || _itemCount == 0 || _state == MenuState::Off) return;

    if (_state == MenuState::Browse) {
        _index = (_index + 1) % _itemCount;
        markActivity();
        LOGF("Menu: next -> item %u (%s)\n", _index, _items[_index].name);
        return;
    }

    int16_t& value = _editValue;
    const MenuItem& item = _items[_index];
    value++;
    if (value > item.maxValue) value = item.minValue;
    markActivity();
    previewCurrent();
    LOGF("Menu edit: %s = %d\n", item.name, value);
}

void MenuController::onOk() {
    if (_state == MenuState::Browse) {
        enterEdit();
    } else if (_state == MenuState::Edit) {
        saveEdit();
    }
}

bool MenuController::isActive() const {
    return _state != MenuState::Off;
}

bool MenuController::isBrowse() const {
    return _state == MenuState::Browse;
}

bool MenuController::isEdit() const {
    return _state == MenuState::Edit;
}

bool MenuController::blinkOn() const {
    return _blinkOn;
}

MenuBlinkPhase MenuController::blinkPhase() const {
    return _blinkPhase;
}

uint8_t MenuController::index() const {
    return _index;
}

int16_t MenuController::editValue() const {
    return _editValue;
}

const MenuItem& MenuController::currentItem() const {
    return _items[_index];
}

void MenuController::previewCurrent() {
    const MenuItem& item = _items[_index];
    if (item.previewValue) item.previewValue(_ctx, _editValue);
    if (item.previewAction) item.previewAction(_ctx, _editValue);
}

void MenuController::markActivity() {
    _lastActivityMs = millis();
    _blinkPhase = MenuBlinkPhase::NameIntro;
    _blinkOn = true;
    _lastBlinkMs = _lastActivityMs;
    _nameIntroBlanks = 0;
}
