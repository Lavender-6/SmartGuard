#include "oled_display.h"

#define OLED_LINE_CHARS 10   // textSize(2) 时每行最多 10 字符

bool OledDisplay::begin(uint8_t addr, uint8_t sda, uint8_t scl) {
    Wire.begin(sda, scl);
    Wire.setClock(400000);   // 400kHz，刷新更跟手

    // periphBegin=false：Wire 已由上面初始化，库内不再重来
    if (!_dev.begin(SSD1306_SWITCHCAPVCC, addr, true, false)) {
        return false;
    }
    _dev.clearDisplay();
    _dev.setTextSize(2);
    _dev.setTextColor(SSD1306_WHITE);
    _dev.display();
    return true;
}

void OledDisplay::showTwoLines(const char *l1, const char *l2) {
    char b1[OLED_LINE_CHARS + 1] = {0};
    char b2[OLED_LINE_CHARS + 1] = {0};
    strncpy(b1, l1, OLED_LINE_CHARS);
    strncpy(b2, l2, OLED_LINE_CHARS);

    _dev.clearDisplay();
    _dev.setTextSize(2);
    _dev.setTextColor(SSD1306_WHITE);
    _dev.setCursor(0, 16);
    _dev.println(b1);
    _dev.setCursor(0, 36);
    _dev.println(b2);
    _dev.display();
}

void OledDisplay::clear() {
    _dev.clearDisplay();
    _dev.display();
}

// 两行小字（1号字，每行≤21字符），和 IP 显示同字号
void OledDisplay::showTwoLinesSmall(const char *l1, const char *l2) {
    char b1[22] = {0};
    char b2[22] = {0};
    strncpy(b1, l1, 21);
    strncpy(b2, l2, 21);

    _dev.clearDisplay();
    _dev.setTextSize(1);
    _dev.setTextColor(SSD1306_WHITE);
    _dev.setCursor(0, 0);
    _dev.println(b1);
    _dev.setCursor(0, 14);
    _dev.println(b2);
    _dev.display();
}

// 三行小字（1号字，每行≤21字符），y=0/14/28
void OledDisplay::showThreeLinesSmall(const char *l1, const char *l2, const char *l3) {
    char b1[22] = {0};
    char b2[22] = {0};
    char b3[22] = {0};
    strncpy(b1, l1, 21);
    strncpy(b2, l2, 21);
    strncpy(b3, l3, 21);

    _dev.clearDisplay();
    _dev.setTextSize(1);
    _dev.setTextColor(SSD1306_WHITE);
    _dev.setCursor(0, 0);
    _dev.println(b1);
    _dev.setCursor(0, 14);
    _dev.println(b2);
    _dev.setCursor(0, 28);
    _dev.println(b3);
    _dev.display();
}
