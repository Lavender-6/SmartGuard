#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ============================================================
//  ESP32-S3 板载 WiFi（Station 联网 + 状态网页 + 数据上报）
//  用法：begin(ssid, pass) → loop() 里保活 + 处理网页请求
//  网页：http://<设备IP>/  状态页；/json  JSON 数据
//  上报：postEvent(type, temp, door, mode) → POST 到 SmartGuard_Web
// ============================================================
class Esp32Wifi {
public:
    void begin(const char *ssid, const char *pass, uint32_t timeoutMs = 15000);
    void loop();

    bool isConnected() const;
    String ip() const;
    int rssi() const;

    // 向 Web 看板上报一条事件（断网时直接返回 false，不阻塞）
    bool postEvent(const char *type, float temp = NAN,
                   const char *door = nullptr, const char *mode = nullptr);

    // 取一条网页/STM32 下发的门锁命令：
    //   "open" / "close" / "emergency"（紧急状态：红灯闪 + 强制开门）
    //   ""（连上了但没命令）/ "err"（请求失败）
    String pollCommand();

private:
    WebServer _server{80};
    char _ssid[33] = {0};
    char _pass[65] = {0};
    uint32_t _lastRetry = 0;

    void handleRoot();
    void handleJson();
};
