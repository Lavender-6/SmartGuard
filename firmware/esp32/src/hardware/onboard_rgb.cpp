#include "onboard_rgb.h"
#include "config.h"
#include <esp32-hal-rgb-led.h>   // core 板载 RGB 驱动（WS2812，走 RMT 外设）

// arduino-esp32 新版 core 的板载 WS2812 驱动叫 neopixelWrite（2.x 叫 rgbLedWrite）
// 注意：3.x 的 core 里 ESP_ARDUINO_VERSION_MAJOR 仍报 2，不能靠版本宏判断，
// 直接用本 core 实际存在的名字。若换旧 core 报未声明，把下一行改成 rgbLedWrite 即可。
#define RGB_WRITE(p, r, g, b) neopixelWrite((p), (r), (g), (b))

// 红 绿 蓝 黄 青 紫 白
const uint8_t OnboardRgb::COLORS[7][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
    {255, 255, 0}, {0, 255, 255}, {255, 0, 255}, {255, 255, 255}
};

void OnboardRgb::begin(uint8_t pin) {
    _pin = pin;
#if ONBOARD_RGB_WS2812
    // WS2812：core 自带 neopixelWrite（内部走 RMT，不占 CPU）
    off();
#else
    // 分立 RGB：三路 LEDC PWM
    ledcSetup(1, 5000, 8); ledcAttachPin(PIN_RGB_R, 1);
    ledcSetup(2, 5000, 8); ledcAttachPin(PIN_RGB_G, 2);
    ledcSetup(3, 5000, 8); ledcAttachPin(PIN_RGB_B, 3);
    off();
#endif
}

void OnboardRgb::setColor(uint8_t r, uint8_t g, uint8_t b) {
    // 统一乘亮度系数（默认 128 = 50%）
    uint8_t rr = (uint8_t)((uint16_t)r * _brightness / 255);
    uint8_t gg = (uint8_t)((uint16_t)g * _brightness / 255);
    uint8_t bb = (uint8_t)((uint16_t)b * _brightness / 255);
#if ONBOARD_RGB_WS2812
    RGB_WRITE(_pin, rr, gg, bb);   // WS2812：内部按 GRB 位流输出
#else
    ledcWrite(1, rr);
    ledcWrite(2, gg);
    ledcWrite(3, bb);
#endif
}

void OnboardRgb::off() {
    setColor(0, 0, 0);
}

void OnboardRgb::setColorHoldMs(uint32_t ms) {
    _colorHoldMs = ms;
}

void OnboardRgb::setBrightness(uint8_t level) {
    _brightness = level;
}

// ---- 状态灯模式 ----
void OnboardRgb::setStatusColor(uint8_t r, uint8_t g, uint8_t b) {
    _statusColor[0] = r;
    _statusColor[1] = g;
    _statusColor[2] = b;
    _statusMode = true;
}

void OnboardRgb::cycleColors() {
    _statusMode = false;
}

void OnboardRgb::cycleUpdate(uint32_t nowMs) {
    uint32_t period = _colorHoldMs;                     // 每种颜色总时长
    uint32_t phase = nowMs % period;                    // 相位 0~period

    // 状态模式锁定单色，闪烁节奏（淡入→保持→淡出）完全不变
    const uint8_t *c = _statusMode ? _statusColor
                                   : COLORS[(nowMs / period) % COLOR_COUNT];
    uint8_t r = 0, g = 0, b = 0;

    // 周期：淡入(0~fade) → 保持(fade~period-fade) → 淡出(period-fade~period)
    if (phase < _fadeMs) {
        float t = (float)phase / _fadeMs;
        r = (uint8_t)(c[0] * t); g = (uint8_t)(c[1] * t); b = (uint8_t)(c[2] * t);
    } else if (phase >= period - _fadeMs) {
        float t = (float)(period - phase) / _fadeMs;
        r = (uint8_t)(c[0] * t); g = (uint8_t)(c[1] * t); b = (uint8_t)(c[2] * t);
    } else {
        r = c[0]; g = c[1]; b = c[2];
    }

    setColor(r, g, b);
}
