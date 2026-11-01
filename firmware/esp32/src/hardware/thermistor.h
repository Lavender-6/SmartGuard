#pragma once
#include <Arduino.h>

// ============================================================
//  NTC 热敏电阻测温（分压 + Steinhart 公式换算）
// ============================================================
class Thermistor {
public:
    void begin(uint8_t pin);
    float readC();              // 单次读取（℃）
    float readFilteredC();      // 8 次滑动平均

private:
    uint8_t _pin;
    float _buf[8] = {0};
    uint8_t _idx = 0;
    bool _filled = false;
};
