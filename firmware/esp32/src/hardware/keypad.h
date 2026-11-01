#pragma once
#include <Arduino.h>

// ============================================================
//  4x4 矩阵键盘（行列扫描法，软件消抖）
//  返回：按键字符，无键返回 0
// ============================================================
class Keypad {
public:
    void begin();
    char scan();          // 每次循环调用，按下返回一次字符
    bool pressed(char c); // 查询某个键是否刚按下（边沿触发）

private:
    char _lastKey = 0;
    bool _keyDown = false;
    uint32_t _debounceMs = 20;
};
