#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "config.h"

// ============================================================
//  0.96" OLED（SSD1306，I2C，128x64）
//  依赖 PlatformIO 自动安装的 Adafruit SSD1306 / GFX 库
//  showTwoLines：两行大字（每行≤10字符）
//  showTwoLinesSmall：两行小字（每行≤21字符），与 IP 显示同字号
//  showThreeLinesSmall：三行小字（每行≤21字符），y=0/14/28
// ============================================================
class OledDisplay {
public:
    // 返回 false 说明屏幕没找到（查地址/接线）
    bool begin(uint8_t addr, uint8_t sda, uint8_t scl);
    void showTwoLines(const char *l1, const char *l2);       // 两行大字（每行≤10字符）
    void showTwoLinesSmall(const char *l1, const char *l2);  // 两行小字（每行≤21字符）
    void showThreeLinesSmall(const char *l1, const char *l2, const char *l3); // 三行小字（每行≤21字符）
    void clear();

private:
    Adafruit_SSD1306 _dev{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
};
