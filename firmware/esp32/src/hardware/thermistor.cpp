#include "thermistor.h"
#include "config.h"
#include <cmath>

void Thermistor::begin(uint8_t pin) {
    _pin = pin;
    analogReadResolution(12);           // 0~4095
    analogSetPinAttenuation(_pin, ADC_11db);
}

float Thermistor::readC() {
    int adc = analogRead(_pin);
    if (adc <= 0 || adc >= (int)ADC_MAX_VAL) return NAN;  // 开路/短路保护

    float v = adc / ADC_MAX_VAL * ADC_REF_V;
    // 上拉式分压：3.3V — R_series — ADC节点 — NTC — GND
    float rNTC = NTC_SERIES_R * v / (ADC_REF_V - v);
    if (rNTC <= 0) return NAN;

    float lnR = logf(rNTC / NTC_R25);
    float invT = 1.0f / 298.15f + lnR / NTC_BETA;         // B 值简化公式
    return 1.0f / invT - 273.15f;
}

float Thermistor::readFilteredC() {
    float v = readC();
    if (!isnan(v)) {                       // 坏采样不进缓冲，避免 NAN 污染平均值
        _buf[_idx] = v;
        _idx = (_idx + 1) % 8;
        if (_idx == 0) _filled = true;
    }
    float sum = 0;
    uint8_t n = 0;
    uint8_t count = _filled ? 8 : _idx;
    for (uint8_t i = 0; i < count; i++) {
        if (!isnan(_buf[i])) { sum += _buf[i]; n++; }
    }
    return (n > 0) ? sum / n : NAN;
}
