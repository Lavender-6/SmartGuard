#pragma once
#include <Arduino.h>

// ============================================================
//  引脚分配表（ESP32-S3）
//  原则：避开 0/3(strapping)、19/20(USB)、26-32(Flash)、
//        33-37(PSRAM 板子占用)、39-42(JTAG)、43/44(调试串口)、45/46(strapping)
// ============================================================

// ---- 矩阵键盘 4x4（行输出 / 列输入上拉）----
// 实物接线（老大确认）：
//   R1→GPIO12  R2→GPIO11  R3→GPIO10  R4→GPIO9
//   C1→GPIO8   C2→GPIO7   C3→GPIO6   C4→GPIO5
static const uint8_t KEYPAD_ROW_PINS[4] = {12, 11, 10, 9};  // R1 R2 R3 R4
static const uint8_t KEYPAD_COL_PINS[4] = {8, 7, 6, 5};     // C1 C2 C3 C4
static const char KEYMAP[4][4] = {
    {'*', '7', '4', '1'},   // R1 ← C1↔C4、C2↔C3 互换后：* 7 4 1
    {'0', '8', '5', '2'},   // R2 ← 0 8 5 2
    {'#', '9', '6', '3'},   // R3 ← # 9 6 3
    {'D', 'C', 'B', 'A'}    // R4 ← D 开/关门 · C 改密 · B 撤防 · A 布防
};

// ---- 板载 BOOT 按键（GPIO0）----
// 完整版已改为键盘 D 键开关门，BOOT 键仅自检版（rgb_main.cpp.bak）使用

// ---- 热敏电阻 NTC 10K（上拉式分压，NTC 下端接地）----
static const uint8_t PIN_THERMISTOR = 1;      // ADC1_CH0
#define NTC_BETA        3950.0f               // B 值（按器件手册改）
#define NTC_R25         10000.0f              // 25℃ 阻值
#define NTC_SERIES_R    10000.0f              // 分压上拉电阻
#define ADC_REF_V       3.3f
#define ADC_MAX_VAL     4095.0f

// ---- 霍尔门磁传感器（数字输出 DO）----
static const uint8_t PIN_HALL = 2;            // 数字输入
#define HALL_ACTIVE_LOW 1    // 1 = 检测到磁铁时输出低电平（多数模块如此，不对就改成 0）

// 门磁灵敏度调节（4x4 霍尔模块 / A3144 等数字量模块都适用）
//  HALL_CONFIRM_N ：新状态要连续采样 N 次一致才认可。越小越灵敏、越易误触发
//                   1 = 不平滑（10ms 直接响应）；2 = 20ms；3 = 30ms（更稳）
//  HALL_SAMPLE_MS ：采样周期，默认 10ms（比 100ms 主任务快 10 倍）
#define HALL_CONFIRM_N    2
#define HALL_SAMPLE_MS    10

// ---- 板载 BOOT 按键（ESP32-S3 DevKitC-1：GPIO0，按下为低电平）----
// 注意：GPIO0 是 strapping 脚，上电瞬间别按着（会进下载模式），运行时做普通输入没问题
static const uint8_t PIN_BOOT_KEY = 0;

// ---- 舵机（电子锁）----
static const uint8_t PIN_SERVO = 15;
#define SERVO_CH           0        // LEDC 通道
#define SERVO_LOCK_ANGLE   0        // 关门角度（lock）
#define SERVO_UNLOCK_ANGLE 90       // 开门角度（unlock）

// ---- 状态指示：改用板载 RGB 灯（GPIO48 WS2812），已删分立彩灯/蜂鸣器 ----
// 绿灯闪 = 环境正常；黄灯闪 = 门被打开；红灯闪 = 紧急/报警

// ---- OLED 0.96" SSD1306（I2C，128x64）----
static const uint8_t PIN_I2C_SDA = 13;
static const uint8_t PIN_I2C_SCL = 14;
#define OLED_I2C_ADDR 0x3C    // 0.96" 128x64 常见 0x3C；不亮先试 0x3D
#define OLED_WIDTH    128
#define OLED_HEIGHT   64      // 0.91" 的 128x32 屏改成 32

// 通信：只用 ESP32 板载 WiFi（esp32_wifi.*），不再外接 ESP8266 模块

// ---- 系统参数 ----
#define DEFAULT_PASSWORD   "1234"
#define PASSWORD_LEN       4
#define TEMP_ALARM_THRESHOLD 45.0f   // 温度报警阈值（℃）
// 紧急状态下温度读数的固定显示值：进入紧急后所有温度读数一律按 50℃ 处理
// （OLED、上报事件、看板折线图都显示 50），解除紧急后恢复真实传感器读数
#define TEMP_EMERGENCY_VALUE 50.0f
// 自动上锁：舵机开门后不再自动转回（改由键盘 D 键 / 网页「关门」手动关闭）
// #define UNLOCK_AUTO_LOCK_MS  5000
#define DOOR_OPEN_ALARM_MS   5000    // 布防后门开多久判定报警
