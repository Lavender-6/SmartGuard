#pragma once
#include <Arduino.h>
#include "config.h"
#include "oled_display.h"
#include "keypad.h"
#include "thermistor.h"
#include "hall_sensor.h"
#include "servo_lock.h"
#include "onboard_rgb.h"
#include "esp32_wifi.h"

// ============================================================
//  安防系统核心：状态机 + 密码逻辑
//  模式：撤防（绿）/ 布防（黄）/ 报警（红闪）
//  按键：A 布防  B 撤防  C 改密码  D 查看
//        * 清空   # 确认
//        BOOT 键(GPIO0) → 舵机手动开/关锁 toggle
//  状态灯：板载 RGB（绿=正常 / 黄=门开 / 红=报警，已删分立彩灯和蜂鸣器）
//  通信：主控 ESP32 板载 WiFi（esp32_wifi.*），不再外接 ESP8266
// ============================================================

// 系统模式
//   Disarmed  撤防
//   Armed     布防
//   Emergency 紧急状态（唯一的"危险"状态）
//             触发来源有四个，行为完全一致：
//               ① STM32 长按（经看板下发 emergency）
//               ② 网页「紧急状态」按钮
//               ③ 温度超过阈值
//               ④ 布防状态下门被打开超时
//             表现：红灯闪 + 强制开门 + OLED 强制跳输密码页 + STM32 失控
//             解除：ESP 端输入正确密码（唯一途径）
enum class SysMode : uint8_t { Disarmed, Armed, Emergency };
enum class KeyCtx : uint8_t { None, EnterPwd, ChangeOld, ChangeNew1, ChangeNew2 };

struct SystemStatus {
    SysMode mode;
    bool doorOpen;
    float tempC;
    bool doorUnlocked;
    uint8_t entryLen;
};

class SecuritySystem {
public:
    void begin();
    void updateSensors();          // 传感器采集任务调用（100ms）
    void tick();                   // 主逻辑任务调用（20ms）
    void rgbTick();                // RGB 状态灯任务调用（20ms，驱动 WS2812 闪烁）
    void setNetwork(Esp32Wifi *net);   // 注入 WiFi（不传则不上报）
    void netTick();                // 网络任务调用（保活 + 定时温度上报）

    SystemStatus status() const {   // 跨任务读取，内部加锁
        SystemStatus s{};
        if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            s = _st;
            xSemaphoreGive(_stateMutex);
        }
        return s;
    }

private:
    // 模块实例
    Keypad       _keypad;
    Thermistor   _therm;
    HallSensor   _hall;
    ServoLock    _lock;
    OnboardRgb   _rgb;             // 状态灯：板载 WS2812（绿/黄/红）
    OledDisplay  _oled;
    Esp32Wifi   *_net = nullptr;   // Web 上报表（可为空）

    // 传感器共享状态互斥锁（采集任务写 / 逻辑·显示·网络任务读）
    SemaphoreHandle_t _stateMutex = nullptr;

    // 系统状态
    SystemStatus _st{};
    char _password[PASSWORD_LEN + 1] = DEFAULT_PASSWORD;
    char _entry[PASSWORD_LEN + 1] = {0};
    uint8_t _entryLen = 0;
    KeyCtx _ctx = KeyCtx::None;
    bool _pendingDisarm = false;
    char _newPwd[PASSWORD_LEN + 1] = {0};

    // 门开延时报警计时
    uint32_t _doorOpenSince = 0;
    bool _doorTiming = false;

    // 自动上锁
    uint32_t _unlockUntil = 0;

    // 上电密码验证：未输对密码前功能键锁定（只接受数字/清空/确认）
    bool _pwdVerified = false;

    // 临时消息
    char _msg1[17] = {0};
    char _msg2[17] = {0};
    uint32_t _msgUntil = 0;

    // OLED 显示缓存（内容不变不刷新，省 I2C 带宽）
    char _oledL1[17] = {0};
    char _oledL2[17] = {0};
    char _oledL3[22] = {0};      // 第三行小字（WiFi 状态）
    uint8_t _oledLayout = 0;     // 0=无关（等首刷） 1=两行大字 2=三行小字
    uint32_t _lastOledMs = 0;

    // 温度定时上报计时
    uint32_t _lastTempPostMs = 0;

    // 网页远程命令（netTick 轮询写入，tick 执行）
    //   0=无 1=开门 2=关门 3=紧急 4=布防 5=撤防
    //   紧急状态下 1/2 会被拒绝（必须本机输密码解除）
    volatile int _webCmd = 0;
    uint32_t _lastCmdPollMs = 0;
    uint32_t _cmdPollIntervalMs = 1000;   // 看板不可达时退避到 10s

    // 紧急状态：红灯闪 + 强制开门 + OLED 跳密码页 + 拒绝远程开锁
    void enterEmergency(const char *reason = "unknown");
    void clearEmergency();

    const char *modeStr() const;
    void snapshotSensors(float &tempC, bool &doorOpen);   // 加锁读取传感器值（真实值，用于报警判定）
    float tempForDisplay() const;                         // 对外显示温度（紧急态固定 50℃）
    void postWithTemp(const char *type);                  // 统一上报出口（温度走显示值）

    void handleKey(char k);
    void submitEntry();
    void doArm();
    void doDisarm();
    void triggerAlarm(const char *reason);
    void openDoor();
    void refreshLcd();
    void refreshLeds();
    String wifiLine() const;      // 第三行：WiFi 连接状态（连上=IP，未连=无网）
    void showMessage(const char *l1, const char *l2, uint32_t holdMs);
};
