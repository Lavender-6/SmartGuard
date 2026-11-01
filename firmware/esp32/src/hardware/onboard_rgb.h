#pragma once
#include <Arduino.h>

// ============================================================
//  板载 RGB 灯（ESP32-S3-DevKitC-1：GPIO48 接 WS2812）
//  两种模式：
//    循环模式：红→绿→蓝→黄→青→紫→白，每色淡入→保持→淡出（自检演示）
//    状态模式：锁定单色（绿=正常/黄=门开/红=报警），闪烁节奏不变
// ============================================================

// 1 = 板载 WS2812（用官方 DevKitC-1，GPIO48）
// 0 = 分立 RGB 灯珠（共阴极，三个 PWM 脚，改下面三个引脚）
#define ONBOARD_RGB_WS2812 1

#if ONBOARD_RGB_WS2812
  #ifdef RGB_BUILTIN
    #define ONBOARD_RGB_PIN RGB_BUILTIN   // core 变体定义的板载 RGB 脚
  #else
    #define ONBOARD_RGB_PIN 48
  #endif
#else
  #define PIN_RGB_R 47
  #define PIN_RGB_G 21
  #define PIN_RGB_B 20
#endif

class OnboardRgb {
public:
    void begin(uint8_t pin);
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void off();
    void cycleUpdate(uint32_t nowMs);   // 主循环调用，做循环闪烁
    void setColorHoldMs(uint32_t ms);   // 每种颜色总时长（默认 800ms）
    void setBrightness(uint8_t level);  // 0-255，128 = 50% 亮度

    // ---- 状态灯模式 ----
    void setStatusColor(uint8_t r, uint8_t g, uint8_t b);  // 锁定颜色（节奏不变）：绿=正常/黄=门开/红=报警
    void cycleColors();                                     // 回到七色循环模式

private:
    uint8_t _pin = ONBOARD_RGB_PIN;
    uint32_t _colorHoldMs = 800;
    uint8_t _brightness = 128;          // 全局亮度系数（默认 50%）
    static const uint8_t COLORS[7][3];
    static const uint8_t COLOR_COUNT = 7;
    uint32_t _fadeMs = 200;
    bool _statusMode = false;           // true = 锁定 _statusColor
    uint8_t _statusColor[3] = {0, 255, 0};
};
