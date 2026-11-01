#pragma once
// ============================================================
//  联网配置（ESP32 板载 WiFi + Web 看板上报）
// ============================================================

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// Web 数据看板服务器地址（跑 SmartGuard_Web 的那台电脑的局域网 IP）
// 查看方法：看板电脑上运行 ipconfig，找 "IPv4 地址"，例如 192.168.43.52
#define WEB_HOST "192.168.137.1"
#define WEB_PORT 8080
