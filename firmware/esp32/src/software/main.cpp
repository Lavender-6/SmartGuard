#include <Arduino.h>
#include "security_system.h"
#include "esp32_wifi.h"
#include "wifi_config.h"

// ============================================================
//  入口文件：FreeRTOS 多任务架构（完整安防系统）
//    taskSensors  100ms  热敏 + 门磁采集（写共享状态，持锁）
//    taskLogic     20ms  键盘 + BOOT 键 + 状态机 + OLED 主逻辑
//    taskRgb       20ms  板载 RGB 状态灯（绿=正常 / 黄=门开 / 红=报警）
//    taskNetwork  500ms  WiFi 保活 + Web 看板上报
//    taskMonitor    5s  系统管理：堆内存/运行状态巡检
//  核分工：采集/逻辑/RGB → core1；网络/监控 → core0
//  按键：矩阵键盘布防/撤防/改密；BOOT 键(GPIO0) 手动 toggle 舵机
// ============================================================

SecuritySystem sys;
Esp32Wifi wifi;

void taskSensors(void *pv) {
    (void)pv;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        sys.updateSensors();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
    }
}

void taskLogic(void *pv) {
    (void)pv;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        sys.tick();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(20));
    }
}

void taskRgb(void *pv) {
    (void)pv;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        sys.rgbTick();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(20));
    }
}

void taskNetwork(void *pv) {
    (void)pv;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        sys.netTick();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
    }
}

void taskMonitor(void *pv) {
    (void)pv;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        SystemStatus st = sys.status();
        const char *modeStr = st.mode == SysMode::Armed ? "armed" :
                              (st.mode == SysMode::Emergency ? "emergency" : "disarmed");
        Serial.printf("[mon] up=%lus heap=%u min=%u mode=%s temp=%.1f door=%s\n",
                      (unsigned long)(millis() / 1000),
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      modeStr, st.tempC, st.doorOpen ? "OPEN" : "SHUT");
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[SmartGuard] booting...");

    wifi.begin(WIFI_SSID, WIFI_PASS);
    sys.setNetwork(&wifi);
    sys.begin();

    xTaskCreatePinnedToCore(taskSensors, "sensors", 4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(taskLogic,   "logic",   4096, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(taskRgb,     "rgb",     2048, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(taskNetwork, "network", 8192, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(taskMonitor, "monitor", 4096, nullptr, 1, nullptr, 0);

    Serial.printf("[SmartGuard] 5 tasks started, heap=%u\n", ESP.getFreeHeap());
}

void loop() {
    // 主循环退位为心跳，全部工作由 FreeRTOS 任务承担
    vTaskDelay(pdMS_TO_TICKS(1000));
}
