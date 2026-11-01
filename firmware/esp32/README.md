# 智能安防门禁系统（第 15 组课设）

基于 **ESP32-S3 + PlatformIO(Arduino)** 的传感器课程设计工程。

> **当前固件模式：完整安防系统**（矩阵键盘 + 门磁 + 舵机 + 状态灯 + WiFi 上报，FreeRTOS 5 任务）。
> OLED 两行大字：密码输入 / 状态屏；板载 RGB 状态灯：绿=正常关门 / 黄=门开 / 红=报警；
> 键盘 **D 键** 手动开关门；报警（布防门开超时 / 超温≥45℃）红灯闪 + 强制开门。
> 自检版备份在 `src/software/rgb_main.cpp.bak`，按文末「切回自检模式」可回退。

**WiFi 用法**：账号密码在 `include/wifi_config.h`（当前已配 SSID=`123`）→ 烧录 → 串口监视器（115200）会打印 IP → 手机/电脑连同一 WiFi，浏览器打开 `http://<设备IP>/` 看状态页，`/json` 出 JSON 数据。掉线会自动重连。

> **通信方案变更**：项目改用 **ESP32-S3 板载 WiFi**（`esp32_wifi.*`），**不再使用外接 ESP8266 模块**（抽选分配表中该项弃用）。板载 WiFi 功能更强（能直接当 Web 服务器），答辩时说明"主控自带 WiFi 已覆盖透传需求"即可。

8 个抽选模块中使用情况：

| # | 模块 | 在本项目中的角色 |
|---|------|------------------|
| 1 | ESP32 主控 | 系统核心，FreeRTOS 多任务 + 板载 WiFi |
| 2 | 热敏电阻测温 | 环境温度监测，超温报警（火灾隐患） |
| 3 | 霍尔磁环检测 | 门磁传感器，检测门开/关 |
| 4 | 矩阵键盘 | 密码输入、布防/撤防/改密 |
| 5 | 舵机 | 电子门锁，密码正确开锁，5s 自动上锁；键盘 D 键手动 toggle |
| 6 | ~~多位彩灯~~ | **已改为板载 RGB 状态灯**：绿闪=正常 / 黄闪=门开 / 红闪=报警（GPIO48 WS2812） |
| 7 | ~~ESP8266 无线串口~~ | **已弃用**，改由主控板载 WiFi 承担通信 |
| 8 | OLED 0.96" 显示屏（SSD1306，I2C） | 状态、温度、密码输入提示 |

## 打开工程

1. VSCode 安装 **PlatformIO IDE** 插件
2. `PIO Home → Open Project`，选择本目录（`platformio.ini` 所在层）
3. 左下角点 ✓（Build）编译，→ 烧录，插头图标打开串口监视器（115200）

> 板子型号默认 `esp32-s3-devkitc-1`，绝大多数 S3 板子通用；
> 如果你的板子插上电脑**不出现 CH340/CP210x 串口**（原生 USB），
> 打开 `platformio.ini` 里注释掉的 `build_flags` 三行。

## 引脚分配

| 信号 | ESP32-S3 引脚 | 备注 |
|------|--------------|------|
| 矩阵键盘 行 R1-R4 | GPIO 5 / 6 / 7 / 8 | 输出，逐列扫描 |
| 矩阵键盘 列 C1-C4 | GPIO 9 / 10 / 11 / 12 | 输入上拉 |
| 热敏电阻 | GPIO 2 | ADC1_CH1，上拉分压 |
| 霍尔门磁 DO | GPIO 1 | 数字输入 |
| 舵机信号 | GPIO 15 | LEDC PWM 50Hz，外部 5V 供电 + 共地 |
| BOOT 按键 | GPIO 0 | 板载按键（仅自检版用于控舵机；完整版由键盘 D 键替代） |
| 板载 RGB 状态灯 | GPIO 48 | WS2812，绿=正常 / 黄=门开 / 红=报警 |
| OLED 0.96" SSD1306 | SDA=GPIO13, SCL=GPIO14 | I2C，地址默认 0x3C（不亮试 0x3D） |

引脚集中在 `include/config.h`，接线与模块实际不符时**只改这一个文件**。

## 功能定义

**上电流程**：上电直接进入密码输入界面（**明文显示**输入的数字），输对密码开门（舵机 90°），之后所有功能开放；未输对前功能键锁定，只接受数字 / `*` 清空 / `#` 确认。

键盘功能分配：

| 键 | 功能 |
|----|------|
| `0`–`9` | 密码输入 |
| **A** | 布防（门必须关闭；布防中开门有 5s 倒计时，超时报警） |
| **B** | 撤防（需输入密码） |
| **C** | 修改密码（旧密码 → 新密码 → 再输一次） |
| **D** | 开/关门 toggle（按一下开门 90°，再按一下关门 0°，替代 BOOT 键） |
| `*` | 清空当前输入 |
| `#` | 确认提交 |

- **任意状态输对密码**：开锁，5 秒后自动上锁
- **报警**（门开超时 / 温度 > 45℃）：板载 RGB 红灯闪烁 + 强制开门，输入密码解除（Web 上报见「通信模块说明」）
- **状态灯**：板载 RGB 绿灯闪=正常关门；黄灯闪=门被打开；红灯闪=报警
- 默认密码 `1234`

## 代码结构

```
include/              公共配置（引脚、系统参数、WiFi 账号）
  config.h            引脚与系统参数（改这里）
  wifi_config.h       WiFi 账号（+ 预留 Web 服务器地址）

src/hardware/         硬件驱动（每个模块一对 .h/.cpp）
  keypad.*            4x4 矩阵键盘扫描
  thermistor.*        NTC 分压 + Steinhart 换算
  hall_sensor.*       门磁检测
  servo_lock.*        舵机 PWM 驱动（10 位分辨率，S3 最高 14）
  oled_display.*      OLED 0.96" SSD1306（I2C，Adafruit 库，支持两/三行小字）
  onboard_rgb.*       板载 WS2812（GPIO48，neopixelWrite；状态灯模式 + 七色循环）
  esp32_wifi.*        ESP32 板载 WiFi + 状态网页

src/software/         软件逻辑
  main.cpp            入口 + 当前自检模式
  security_system.*   状态机 + 密码逻辑（完整系统核心）
  system_main.cpp.bak 完整系统版入口备份
```

## FreeRTOS 任务划分

固件跑在 FreeRTOS 上（Arduino 核心自带），按“采集 / 显示 / 执行 / 通信 / 管理”分任务，互不阻塞：

| 任务 | 周期 | 核 | 职责 |
|------|------|----|------|
| taskSensors | 100ms | 1 | 热敏 + 门磁采集，写共享状态（持互斥锁） |
| taskLogic | 20ms | 1 | 键盘扫描、状态机、OLED 刷新（完整系统） |
| taskDisplay | 200ms | 1 | OLED 三行刷新，内容变化才刷 + RGB 状态色决策（自检固件） |
| taskRgb | 20ms | 1 | 板载 RGB 状态灯驱动（绿/黄/红，节奏固定只换色） |
| taskBootKey | 20ms | 1 | BOOT 键轮询去抖，toggle 舵机（自检固件） |
| taskNetwork | 500ms | 0 | WiFi 保活、状态网页、Web 看板上报 |
| taskMonitor | 5s | 0 | 系统管理：堆内存/最低水位/传感器健康巡检 |

关键点（答辩可讲）：

- **共享状态全部走互斥锁**（`g_stateMutex` / `SecuritySystem::_stateMutex`），采集任务写、其余任务读，`status()` 内部也加锁
- **核分工**：WiFi 协议栈在 core0，网络/监控任务放 core0；采集/显示/逻辑放 core1
- **周期任务用 `vTaskDelayUntil`**，固定频率不受任务耗时影响
- **传感器健康巡检**：30s 读不到有效温度，monitor 任务串口告警查线
- 主循环（`loop`）退位为心跳，不干实际活

## 分工建议（3 人）

| 成员 | 模块 | 交付 |
|------|------|------|
| A | thermistor + hall_sensor | 分压电路、标定曲线、采集代码 |
| B | keypad + oled_display + onboard_rgb | 人机交互、显示界面、状态灯 |
| C | servo_lock + esp32_wifi + security_system | 执行、联网、主逻辑 |

## 通信模块说明

**方案：ESP32-S3 板载 WiFi**（`esp32_wifi.*`），不再使用外接 ESP8266 模块。

- 已实现：Station 连路由、掉线自动重连、设备状态网页（`http://<IP>/`）、`/json` 数据接口
- 数据看板：配套网站 `../SmartGuard_Web`（Node 服务），接收 `POST /api/event` 并在网页展示
- **网页远程开关门**：看板页「门锁控制」两个按钮 → `POST /api/cmd {action:"open"|"close"}` 暂存命令
  → 设备网络任务每 1s `GET /api/cmd` 取走执行（一次性消费，命令 10s 过期；看板不可达时轮询自动退避到 10s）
- 上报已接入：`esp32_wifi.postEvent(type, temp, door, mode)`；温度每 10~15s 自动上报一次，
  布防/撤防/报警/开锁四个事件实时上报（完整系统在 `security_system.cpp` 的四个事件点）
- **使用前必改**：`include/wifi_config.h` 的 `WEB_HOST` 改成跑看板那台电脑的局域网 IP（`ipconfig` 查 IPv4）
- 答辩口径：主控自带 WiFi 已满足无线通信需求，功能比外接串口 WiFi 模块更完整（可同时做 Web 服务），故未使用该模块

## 切回自检模式

当前 `main.cpp` 是完整安防系统；想回自检（状态灯 + WiFi + BOOT 键控舵机，不接键盘/门磁也能跑）：

1. 把 `src/software/main.cpp` 改名为 `system_main.cpp.bak`
2. 把 `src/software/rgb_main.cpp.bak` 改名为 `main.cpp`
3. 重新编译烧录

## 常见问题

0. **板载 RGB 不亮/太亮/颜色不对** → 确认你的板子 GPIO48 确实接着 WS2812（官方 DevKitC-1 是）；
   亮度在 `src/hardware/onboard_rgb.h` 的 `_brightness`（默认 128 = 50%，全亮改 255）；
   状态灯模式锁定单色（绿/黄/红），想回到七色循环演示调 `rgb.cycleColors()`。
1. **编译报错 `esp32-s3-devkitc-1` 找不到板子** → PlatformIO 插件首次编译会自动下载平台（几分钟），等它跑完。
2. **烧录后串口无输出** → 检查波特率 115200；原生 USB 板子打开 `platformio.ini` 的 `build_flags`。
3. **OLED 不亮/串口打印 `[oled] not found`** → I2C 地址不对：把 `include/config.h` 里 `OLED_I2C_ADDR` 在 `0x3C` / `0x3D` 之间换；再查 SDA/SCL 是否接反（多数模块标了 SDA/SCL）。首次编译会自动下载 Adafruit 库，需要等一会。
4. **舵机一直抖/不动** → ① 信号线接 GPIO 15；② 电源用独立 5V 并与 ESP32 共地（舵机电流大，别用主控 3.3V/5V 脚供电）；
   ③ 本工程 LEDC 用 10 位分辨率（ESP32-S3 最高 14 位，超限 ledcSetup 会静默失败、PWM 无输出）。
5. **BOOT 按键没反应**（仅自检版适用）→ 按键就是 GPIO0；上电瞬间别按着（strapping 脚，会进下载模式）；串口看有没有 `[boot] servo -> ...` 打印。完整版的开关门在键盘 **D 键**。
6. **温度读数明显偏低/偏高** → 模块分压电阻或 B 值和代码假设不符：改 `include/config.h` 的 `NTC_SERIES_R`（分压电阻，常见 10K）和 `NTC_BETA`（常见 3950）；串口会打印实际温度便于对表。
