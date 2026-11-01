#include "keypad.h"
#include "config.h"

void Keypad::begin() {
    // 行 = 输入上拉；列 = 输出，逐列拉低扫描
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(KEYPAD_ROW_PINS[i], INPUT_PULLUP);
    }
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(KEYPAD_COL_PINS[i], OUTPUT);
        digitalWrite(KEYPAD_COL_PINS[i], HIGH);
    }
}

char Keypad::scan() {
    char key = 0;
    for (uint8_t c = 0; c < 4 && !key; c++) {
        // 当前列拉低，其余列拉高；被按的键把所在行拉低
        for (uint8_t j = 0; j < 4; j++) digitalWrite(KEYPAD_COL_PINS[j], j == c ? LOW : HIGH);
        delayMicroseconds(20);
        for (uint8_t r = 0; r < 4; r++) {
            if (digitalRead(KEYPAD_ROW_PINS[r]) == LOW) {
                key = KEYMAP[r][c];
                break;
            }
        }
    }
    for (uint8_t j = 0; j < 4; j++) digitalWrite(KEYPAD_COL_PINS[j], HIGH);  // 空闲

    if (key == 0) {
        _keyDown = false;
        _lastKey = 0;
        return 0;
    }
    if (!_keyDown || key != _lastKey) {
        _keyDown = true;
        _lastKey = key;
        return key;  // 每次新按下只返回一次
    }
    return 0;
}

bool Keypad::pressed(char c) {
    char k = scan();
    return k == c;
}
