#include "security_system.h"
#include "config.h"
#include <cstring>
#include <cmath>

void SecuritySystem::begin() {
    _stateMutex = xSemaphoreCreateMutex();

    _keypad.begin();
    _therm.begin(PIN_THERMISTOR);
    _hall.begin(PIN_HALL, HALL_ACTIVE_LOW);
    _lock.begin(PIN_SERVO, SERVO_CH);
    _rgb.begin(ONBOARD_RGB_PIN);              // 板载 RGB 状态灯
    _rgb.setStatusColor(0, 255, 0);           // 绿灯 = 环境正常
    if (!_oled.begin(OLED_I2C_ADDR, PIN_I2C_SDA, PIN_I2C_SCL)) {
        Serial.println("[oled] not found! check 0x3C/0x3D and wiring");
    }

    _st.mode = SysMode::Disarmed;
    _st.doorOpen = false;
    _st.doorUnlocked = false;
    _st.entryLen = 0;

    // 开机画面：顺带显示 WiFi 是否已连上（wifi.begin 在 setup 里先跑）
    _oled.showTwoLines("SmartGuard", "v1.0");
    delay(800);
    _oled.showTwoLinesSmall("SmartGuard boot", wifiLine().c_str());
    delay(600);

    // 上电直接进入密码输入阶段（明文显示），输对后开门，之后功能全开放
    _ctx = KeyCtx::EnterPwd;
    _pendingDisarm = false;
    _entryLen = 0; _entry[0] = 0;
    _oledLayout = 0;      // 强制下一帧重画三行版式
    showMessage("Password:", "", 0);
}

// ---------- 传感器采集（独立任务调用，写共享状态需持锁） ----------
void SecuritySystem::updateSensors() {
    float t = _therm.readFilteredC();

    // 门磁：按 HALL_SAMPLE_MS（10ms）高频采样 + 连续确认，比 100ms 主节奏灵敏很多
    static uint32_t lastHallMs = 0;
    if (millis() - lastHallMs >= HALL_SAMPLE_MS) {
        lastHallMs = millis();
        _hall.poll();
    }
    bool d = _hall.isDoorOpen();

    // 门磁诊断：DO 电平变化时打印原始值（排查"RGB 恒黄/恒绿"这类极性问题）
    static int lastLv = -1;
    static bool lastD = false;
    int lv = digitalRead(PIN_HALL);
    if (lv != lastLv || d != lastD) {
        lastLv = lv;
        lastD = d;
        Serial.printf("[hall] DO=%d -> %s (HALL_ACTIVE_LOW=%d)\n",
                      lv, d ? "OPEN" : "SHUT", HALL_ACTIVE_LOW);
    }

    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _st.tempC = t;
        _st.doorOpen = d;
        _st.entryLen = _entryLen;
        xSemaphoreGive(_stateMutex);
    }
}

// 加锁读取传感器值（逻辑/显示/网络任务用）
void SecuritySystem::snapshotSensors(float &tempC, bool &doorOpen) {
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        tempC = _st.tempC;
        doorOpen = _st.doorOpen;
        xSemaphoreGive(_stateMutex);
    } else {
        tempC = _st.tempC;      // 取锁失败也返回（32 位对齐读写是原子的）
        doorOpen = _st.doorOpen;
    }

    // 注意：这里返回的是**真实传感器值**，用于报警判定。
    // 紧急状态下对外显示的 50℃ 由 tempForDisplay() 处理，
    // 否则 50 > 45 会让高温判定在紧急态里无限触发。
}

/**
 * 对外显示用的温度（OLED / 上报事件 / 看板曲线）。
 * 紧急状态下固定 50℃，解除后恢复真实读数。
 */
float SecuritySystem::tempForDisplay() const {
    if (_st.mode == SysMode::Emergency) return TEMP_EMERGENCY_VALUE;
    return _st.tempC;
}

/**
 * 统一的上报出口：温度一律走 tempForDisplay()，
 * 保证紧急状态下所有事件里带的温度都是 50℃。
 */
void SecuritySystem::postWithTemp(const char *type) {
    if (!_net) return;
    bool doorOpen = false;
    float real = NAN;
    snapshotSensors(real, doorOpen);
    _net->postEvent(type, tempForDisplay(), doorOpen ? "open" : "closed", modeStr());
}

// ---------- 主逻辑（20ms 周期） ----------
void SecuritySystem::tick() {
    uint32_t now = millis();

    // 传感器值快照（采集任务在并发写，必须持锁读）
    float tempC = NAN;
    bool doorOpen = false;
    snapshotSensors(tempC, doorOpen);

    // 1) 键盘
    char k = _keypad.scan();
    if (k) handleKey(k);

    // 1.5) 远程命令（netTick 轮询写入，这里执行）
    //      1=开门 2=关门 3=紧急 4=布防 5=撤防 6=解除紧急
    //      紧急状态下拒绝开锁/关门指令，必须先在本机输密码解除
    if (_st.mode == SysMode::Emergency && (_webCmd == 1 || _webCmd == 2)) {
        if (_webCmd) {
            Serial.printf("[sys] remote cmd %d rejected: emergency state\n", _webCmd);
            showMessage("!!EMERGENCY!!", "enter pwd", 1000);
        }
        _webCmd = 0;
    }

    if (_webCmd == 1) {
        _webCmd = 0;
        if (!_st.doorUnlocked) openDoor();   // 开门 + 自动上锁 + Web 上报
    } else if (_webCmd == 2) {
        _webCmd = 0;
        if (_st.doorUnlocked) {
            _lock.lock();
            _st.doorUnlocked = false;
            showMessage("LOCKED", "web", 800);
        }
    } else if (_webCmd == 3) {
        _webCmd = 0;
        if (_st.mode != SysMode::Emergency) enterEmergency("web");   // 红灯闪 + 强制开门
    } else if (_webCmd == 4) {
        _webCmd = 0;
        if (_st.mode != SysMode::Armed && _st.mode != SysMode::Emergency) {
            if (doorOpen) showMessage("CLOSE DOOR", "web", 1200);
            else          doArm();
        }
    } else if (_webCmd == 5) {
        _webCmd = 0;
        if (_st.mode == SysMode::Armed) doDisarm();
    } else if (_webCmd == 6) {
        _webCmd = 0;
        // 网页「解除紧急」：仅在紧急状态下有效
        if (_st.mode == SysMode::Emergency) clearEmergency();
        else showMessage("NOT EMERG", "no need", 800);
    }

    // 2) 门磁 + 温度 → 报警判定
    if (_st.mode == SysMode::Armed) {
        if (doorOpen) {
            if (!_doorTiming) { _doorTiming = true; _doorOpenSince = now; }
            else if (now - _doorOpenSince > DOOR_OPEN_ALARM_MS) {
                triggerAlarm("door_open");
            }
        } else {
            _doorTiming = false;   // 及时关门解除倒计时
        }
    } else {
        _doorTiming = false;
    }

    if (!isnan(tempC) && tempC > TEMP_ALARM_THRESHOLD &&
        _st.mode != SysMode::Emergency) {
        char buf[24];
        snprintf(buf, sizeof(buf), "temp_high %.1f", tempC);
        triggerAlarm(buf);
    }

    // 3) 自动上锁已关闭：开门后舵机停在开门角度，不会自己转回
    //    （需要关门时按键盘 D 键，或网页「关门」按钮）
    // if (_st.doorUnlocked && now > _unlockUntil) { _lock.lock(); ... }

    // 4) 显示 / 指示
    refreshLeds();
    refreshLcd();
}

// ---------- 按键处理 ----------
void SecuritySystem::handleKey(char k) {
    // 传感器值快照（跨任务读取）
    float tempC = NAN;
    bool doorOpen = false;
    snapshotSensors(tempC, doorOpen);

    // 上电密码验证未通过前：只接受数字/清空/确认，功能键一律忽略
    if (!_pwdVerified) {
        bool isInputKey = (k >= '0' && k <= '9') || k == '*' || k == '#';
        if (!isInputKey) {
            showMessage("LOCKED", "enter pwd", 800);
            return;
        }
    }

    // 紧急状态下，任何输入都要求先输密码解除（功能键一律挡回）
    if (_st.mode == SysMode::Emergency && _ctx == KeyCtx::None) {
        if (k >= '0' && k <= '9') {
            _ctx = KeyCtx::EnterPwd;
            _pendingDisarm = false;
            _entryLen = 0; _entry[0] = 0;
        } else {
            showMessage("!!EMERGENCY!!", "enter pwd", 1000);
            return;
        }
    }

    switch (k) {
    case 'A':   // 布防
        if (_st.mode != SysMode::Armed) {
            if (doorOpen) {
                showMessage("CLOSE DOOR", "", 1200);
            } else {
                doArm();
            }
        }
        return;
    case 'B':   // 撤防（需密码）
        if (_st.mode == SysMode::Disarmed) {
            showMessage("DISARMED", "already", 1000);
        } else {
            _ctx = KeyCtx::EnterPwd;
            _pendingDisarm = true;
            _entryLen = 0; _entry[0] = 0;
        }
        return;
    case 'C':   // 修改密码
        _ctx = KeyCtx::ChangeOld;
        _entryLen = 0; _entry[0] = 0;
        return;
    case 'D':   // 开/关门 toggle（替代原 BOOT 键）
        if (_st.doorUnlocked) {
            _lock.lock();
            _st.doorUnlocked = false;
            showMessage("LOCKED", "", 800);
        } else {
            openDoor();   // 开门 + 5s 自动上锁 + Web 上报
        }
        return;
    case '*':   // 清空输入
        _entryLen = 0; _entry[0] = 0;
        return;
    case '#':   // 确认
        submitEntry();
        return;
    default:
        break;
    }

    // 数字键 → 密码输入
    if (k >= '0' && k <= '9' && _ctx != KeyCtx::None) {
        if (_entryLen < PASSWORD_LEN) {
            _entry[_entryLen++] = k;
            _entry[_entryLen] = 0;
        }
    }
}

// ---------- 密码提交 ----------
void SecuritySystem::submitEntry() {
    bool ok = (_entryLen == PASSWORD_LEN) && (strcmp(_entry, _password) == 0);

    switch (_ctx) {
    case KeyCtx::EnterPwd:
        if (ok) {
            _pwdVerified = true;   // 上电密码验证通过，功能全开放
            if (_pendingDisarm || _st.mode == SysMode::Emergency) doDisarm();   // 输密码解除紧急
            else openDoor();
        } else {
            showMessage("WRONG PW", "", 1200);
        }
        _ctx = KeyCtx::None; _pendingDisarm = false;
        _entryLen = 0; _entry[0] = 0;
        break;

    case KeyCtx::ChangeOld:
        if (ok) {
            _ctx = KeyCtx::ChangeNew1;
        } else {
            showMessage("WRONG PW", "", 1200);
            _ctx = KeyCtx::None;
        }
        _entryLen = 0; _entry[0] = 0;
        break;

    case KeyCtx::ChangeNew1:
        strncpy(_newPwd, _entry, PASSWORD_LEN);
        _newPwd[PASSWORD_LEN] = 0;
        _ctx = KeyCtx::ChangeNew2;
        _entryLen = 0; _entry[0] = 0;
        break;

    case KeyCtx::ChangeNew2:
        if (strcmp(_entry, _newPwd) == 0) {
            strncpy(_password, _newPwd, PASSWORD_LEN);
            showMessage("PW CHANGED", "", 1500);
        } else {
            showMessage("Mismatch!", "keep old", 1500);
        }
        _ctx = KeyCtx::None;
        _entryLen = 0; _entry[0] = 0;
        break;

    default:
        break;
    }
}

// ---------- 模式切换 ----------
// 事件产生点：布防/撤防/报警/开锁，同时上报 Web 看板
void SecuritySystem::doArm() {
    _st.mode = SysMode::Armed;
    _lock.lock();
    _st.doorUnlocked = false;
    showMessage("ARMED", "watch...", 1500);
    postWithTemp("armed");
}

void SecuritySystem::doDisarm() {
    bool wasEmergency = (_st.mode == SysMode::Emergency);
    _st.mode = SysMode::Disarmed;
    _doorTiming = false;

    // 从紧急态撤出来时，把门锁也收回去：
    //   紧急是"强制开门"进来的，解除后如果门还开着，等于紧急结束了但大门敞着。
    //   上锁同时清 doorUnlocked，保证 ESP32 内部状态和物理舵机一致。
    if (wasEmergency && _st.doorUnlocked) {
        _lock.lock();
        _st.doorUnlocked = false;
    }

    refreshLeds();                   // 立刻把红灯换掉（绿/黄）
    showMessage("DISARMED", _st.doorUnlocked ? "door open" : "door locked", 1500);
    postWithTemp("disarmed");        // 事件带 mode=disarmed，看板立刻知道紧急解除了
}

// 传感器触发的紧急（高温 / 布防下开门），与 STM32、网页两条路径殊途同归，
// 最终都落到 enterEmergency()：红灯闪 + 强制开门 + OLED 跳密码页 + STM32 失控。
void SecuritySystem::triggerAlarm(const char *reason) {
    if (_st.mode == SysMode::Emergency) return;   // 已在紧急，不重复触发
    Serial.printf("[sys] emergency by sensor: %s\n", reason);
    enterEmergency(reason);
}

void SecuritySystem::openDoor() {
    _lock.unlock();
    _st.doorUnlocked = true;
    // 自动上锁已取消：舵机保持开门角度，直到按 D 键 / 网页「关门」/ 重新布防
    // _unlockUntil = millis() + UNLOCK_AUTO_LOCK_MS;
    showMessage("UNLOCKED", "5s lock", 1500);
    postWithTemp("door_unlocked");
}

// ---------- 紧急状态 ----------
// 四个触发来源（行为完全一致）：
//   1) STM32 长按 1s  → 看板下发 emergency 指令
//   2) 网页「紧急状态」按钮 → 同上
//   3) 温度超过 TEMP_ALARM_THRESHOLD → triggerAlarm("temp_high ...")
//   4) 布防状态下门被打开超时       → triggerAlarm("door_open")
// 表现：板载 RGB 红灯闪 + 舵机强制开门 + OLED 强制跳输密码页 + 拒绝远程开锁
// 解除：只有 ESP 端键盘输入正确密码（或网页「解除紧急」按钮）
void SecuritySystem::enterEmergency(const char *reason) {
    _st.mode = SysMode::Emergency;

    // 强制开门（不论原来锁没锁）
    _lock.unlock();
    _st.doorUnlocked = true;

    // OLED 强制跳到输密码页：清掉当前上下文，让用户能立刻输密码
    _ctx = KeyCtx::EnterPwd;
    _pendingDisarm = false;
    _entryLen = 0; _entry[0] = 0;
    _oledLayout = 0;                 // 强制下一帧重画（账号密码页版式）
    showMessage("!!EMERGENCY!!", "enter pwd", 0);

    refreshLeds();                   // 立刻切红灯
    _doorTiming = false;             // 取消门开计时，避免叠加第二条报警

    Serial.printf("[sys] EMERGENCY (%s): forced unlock, waiting for password\n", reason);
    if (_net) {
        bool doorOpen = false;
        float real = NAN;
        snapshotSensors(real, doorOpen);
        // 事件类型带来源，看板据此区分是 STM32、网页还是传感器触发
        // 此时 _st.mode 已是 Emergency，温度会按显示值报 50℃
        String type = String("emergency:") + reason;
        _net->postEvent(type.c_str(), tempForDisplay(),
                        doorOpen ? "open" : "closed", "emergency");
    }
}

void SecuritySystem::clearEmergency() {
    if (_st.mode != SysMode::Emergency) return;
    _st.mode = SysMode::Disarmed;
    _ctx = KeyCtx::None;             // 退出密码输入态
    _entryLen = 0; _entry[0] = 0;
    showMessage("EMERGENCY", "cleared", 1200);
    refreshLeds();
    Serial.println("[sys] EMERGENCY cleared");
}

// ---------- 指示与显示 ----------
// 状态灯（板载 RGB，闪烁节奏固定，只换色）：
//   紧急 → 红闪；门开 → 黄闪；其余(正常) → 绿闪
void SecuritySystem::refreshLeds() {
    if (_st.mode == SysMode::Emergency)    _rgb.setStatusColor(255, 0, 0);
    else if (_st.doorOpen)                 _rgb.setStatusColor(255, 255, 0);
    else                                   _rgb.setStatusColor(0, 255, 0);
}

// RGB 状态灯驱动（独立任务 20ms 调用）
void SecuritySystem::rgbTick() {
    _rgb.cycleUpdate(millis());
}

void SecuritySystem::refreshLcd() {
    char l1[17], l2[17];

    // 传感器值快照（采集任务在并发写）
    float tempC = NAN;
    bool doorOpen = false;
    snapshotSensors(tempC, doorOpen);
    tempC = tempForDisplay();        // 紧急状态下显示固定 50℃

    // 1) 密码输入界面优先（明文显示输入的数字，不隐藏）
    if (_ctx != KeyCtx::None) {
        char code[PASSWORD_LEN + 1] = "    ";
        for (uint8_t i = 0; i < _entryLen; i++) code[i] = _entry[i];
        const char *prompt = "Password:";
        if (_ctx == KeyCtx::ChangeOld) prompt = "Old pwd:";
        else if (_ctx == KeyCtx::ChangeNew1) prompt = "New pwd:";
        else if (_ctx == KeyCtx::ChangeNew2) prompt = "Repeat:";
        snprintf(l1, sizeof(l1), "%s", prompt);
        snprintf(l2, sizeof(l2), "%s", code);
    }
    // 2) 临时消息
    else if (millis() < _msgUntil) {
        snprintf(l1, sizeof(l1), "%s", _msg1);
        snprintf(l2, sizeof(l2), "%s", _msg2);
    }
    // 3) 主状态屏
    else {
        const char *modeStr = (_st.mode == SysMode::Armed)     ? "ARMED" :
                              (_st.mode == SysMode::Emergency) ? "EMERG" : "SAFE";
        snprintf(l1, sizeof(l1), "T%sC",
                 isnan(tempC) ? "--.-" : String(tempC, 1).c_str());
        snprintf(l2, sizeof(l2), "%s %s", modeStr, doorOpen ? "OPEN" : "SHUT");
    }

    // 第三行小字：WiFi 连接状态（连上显示 IP，未连显示 NO WIFI）
    char l3[22];
    snprintf(l3, sizeof(l3), "%s", wifiLine().c_str());

    // 内容没变就不刷（OLED 每次刷新要传整帧，占 I2C 带宽）
    bool same = (!strcmp(l1, _oledL1) && !strcmp(l2, _oledL2) &&
                 !strcmp(l3, _oledL3));
    if (same && _oledLayout == 2) return;
    if (millis() - _lastOledMs < 150) return;   // 限速 ~6fps
    _lastOledMs = millis();
    strncpy(_oledL1, l1, 16); _oledL1[16] = 0;
    strncpy(_oledL2, l2, 16); _oledL2[16] = 0;
    strncpy(_oledL3, l3, 21); _oledL3[21] = 0;
    _oledLayout = 2;
    _oled.showThreeLinesSmall(_oledL1, _oledL2, _oledL3);
}

// 第三行文本：WiFi 状态
//   已连接 → "WiFi 192.168.x.x"（IP 最长 15 字符，+5 前缀 = 20，正好 21 字符上限）
//   未连接 → "WiFi: NO LINK"（后台每 5s 自动重连，连上后本行自动变 IP）
String SecuritySystem::wifiLine() const {
    if (!_net) return String("WiFi: OFF");
    if (!_net->isConnected()) return String("WiFi: NO LINK");
    return String("WiFi ") + _net->ip();
}

void SecuritySystem::showMessage(const char *l1, const char *l2, uint32_t holdMs) {
    strncpy(_msg1, l1, 16); _msg1[16] = 0;
    strncpy(_msg2, l2, 16); _msg2[16] = 0;
    _msgUntil = holdMs ? millis() + holdMs : 0xFFFFFFFF;
}

// ---------- 网络 ----------
void SecuritySystem::setNetwork(Esp32Wifi *net) {
    _net = net;
}

const char *SecuritySystem::modeStr() const {
    switch (_st.mode) {
        case SysMode::Armed:     return "armed";
        case SysMode::Emergency: return "emergency";
        default:                 return "disarmed";
    }
}

void SecuritySystem::netTick() {
    if (!_net) return;
    _net->loop();

    uint32_t now = millis();

    // 每 1s 取一次网页下发的门锁命令（看板 POST /api/cmd → 设备 GET 取走）
    // 看板不可达时退避到 10s，避免 HTTP 超时拖累温度上报
    if (now - _lastCmdPollMs > _cmdPollIntervalMs) {
        _lastCmdPollMs = now;
        String cmd = _net->pollCommand();
        if (cmd == "err") {
            _cmdPollIntervalMs = 10000;
        } else {
            _cmdPollIntervalMs = 1000;
            // 命令 → _webCmd 编码（对应 tick() 里的分发）
            if      (cmd == "open")            _webCmd = 1;
            else if (cmd == "close")           _webCmd = 2;
            else if (cmd == "emergency")       _webCmd = 3;
            else if (cmd == "arm")             _webCmd = 4;
            else if (cmd == "disarm")          _webCmd = 5;
            else if (cmd == "clear_emergency") _webCmd = 6;
            else if (cmd.length())             Serial.printf("[cmd] unknown: %s\n", cmd.c_str());
        }
    }

    // 每 15s 上报一次温度（看板折线图数据源）
    if (now - _lastTempPostMs > 15000) {
        float tempC = NAN;
        bool doorOpen = false;
        snapshotSensors(tempC, doorOpen);
        tempC = tempForDisplay();        // 紧急状态下上报 50℃
        if (!isnan(tempC)) {
            _lastTempPostMs = now;
            _net->postEvent("temp", tempC, doorOpen ? "open" : "closed", modeStr());
        }
    }
}
