#pragma once
#include <Arduino.h>

// ============================================================
//  霍尔门磁传感器（数字输出）
//  磁铁靠近 = 门关闭；磁铁离开 = 门打开
//  灵敏度/稳定性由 HALL_CONFIRM_N（连续采样确认）+ HALL_DEBOUNCE_MS（消抖）控制
// ============================================================
class HallSensor {
public:
    void begin(uint8_t pin, bool activeLow);
    void poll();         // 高频调用（10ms）：采样 + 确认 + 消抖，更新内部状态
    bool isDoorOpen() const { return _open; }   // true = 门被打开（用 poll() 后的结果）
    bool rawOpen() const;                       // 不做任何滤波的瞬时判断（调试用）
    bool changed();      // 状态是否刚发生变化（边沿，读取即清除）

private:
    uint8_t _pin = 0;
    bool _activeLow = true;
    bool _open = false;      // 滤波后的门开状态
    bool _lastOpen = false;  // changed() 用
    bool _pending = false;   // 待确认的新状态
    uint8_t _hit = 0;        // 连续命中次数
    uint32_t _lastFlipMs = 0;
};
