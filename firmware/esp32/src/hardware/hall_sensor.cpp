#include "hall_sensor.h"
#include "config.h"

void HallSensor::begin(uint8_t pin, bool activeLow) {
    _pin = pin;
    _activeLow = activeLow;
    pinMode(_pin, INPUT_PULLUP);
    _open = rawOpen();       // 上电先取一次真实状态
    _lastOpen = _open;
}

// 瞬时判断（不做滤波）
bool HallSensor::rawOpen() const {
    int level = digitalRead(_pin);
    // 磁铁靠近 = 门关闭；磁铁离开 = 门打开
    if (_activeLow) return level == HIGH;
    return level == LOW;
}

// 10ms 采样一次：
//   新电平要连续 HALL_CONFIRM_N 次一致才认可 → 抗干扰（手靠近/磁铁抖动不误触发）
//   认可后保持该状态，直到再次满足确认条件（无需额外等待，响应 = N × HALL_SAMPLE_MS）
void HallSensor::poll() {
    bool now = rawOpen();

    // 与当前已认可状态一致 → 清空待确认计数
    if (now == _open) {
        _hit = 0;
        return;
    }

    // 与待确认状态不同 → 从 1 重新计数；相同 → 累加
    if (now != _pending) {
        _pending = now;
        _hit = 1;
    } else if (_hit < 255) {
        _hit++;
    }
    if (_hit < HALL_CONFIRM_N) return;      // 还没攒够次数

    _open = now;
    _hit = 0;
    _lastFlipMs = millis();
}

bool HallSensor::changed() {
    if (_open != _lastOpen) {
        _lastOpen = _open;
        return true;
    }
    return false;
}
