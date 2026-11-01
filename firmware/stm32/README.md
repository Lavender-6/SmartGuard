# STM32F103C8T6 + ESP8266-01S + 按键 —— CLion 初始工程

现成的裸机（无 RTOS）起步工程：**CMake + Ninja + arm-none-eabi-gcc** 构建，
**WCH-Link（SWD 两线）** 烧录，已在 CLion 2024+ 上验证可编译。

---

## 1. 快速开始

```text
1) 用 CLion「Open」打开本目录（含 CMakeLists.txt 的这一层）
   → 已带 CMakePresets.json（Debug / Release），CLion 会自动识别并配置
2) 改 App/Inc/wifi_config.h：WiFi 名称、密码、电脑 IP
3) Build（Ctrl+F9）→ 产物：cmake-build-debug/out/*.hex / *.bin
4) 按 tools/flash.md 用 WCH-Link 烧录，或 CLion 里配 OpenOCD 直接下载调试
```

命令行构建：

```bat
set PATH=E:\ST\STM32CubeCLT_1.18.0\Ninja\bin;E:\ST\STM32CubeCLT_1.18.0\GNU-tools-for-STM32\bin;%PATH%
E:\ST\STM32CubeCLT_1.18.0\CMake\bin\cmake.exe --preset Debug
E:\ST\STM32CubeCLT_1.18.0\CMake\bin\cmake.exe --build --preset Debug
```

首次实测占用：**Flash 9.5KB / 64KB（15%）、RAM 2.9KB / 20KB（14%）**。

---

## 2. 接线表

### 2.1 WCH-Link 烧录（SWD）

| WCH-Link | STM32 |
|---|---|
| CLK | **PA14** |
| DIO | **PA13** |
| GND | GND（必须接） |
| 3V3 | 3V3（可选） |

### 2.2 ESP8266-01S（接 USART2）

| ESP8266 | STM32 | 说明 |
|---|---|---|
| VCC / 3V3 | **独立 3.3V 电源** | ⚠️ 不要用 Blue Pill 的 3V3，带不动 |
| GND | GND | 必须与 STM32 共地 |
| TXD | **PA3**（USART2_RX） | 交叉接 |
| RXD | **PA2**（USART2_TX） | 交叉接 |
| CH_PD (EN) | **PB0** | 高电平使能，代码自动拉高 |
| RST | **PB1** | 低电平复位，代码开机复位一次 |

### 2.3 其它

| 器件 | STM32 |
|---|---|
| 按键（一端接 GND，一端接脚） | **PB12**（内部上拉，按下为低） |
| 调试串口 USB-TTL 的 RX | **PA9**（USART1_TX），115200 |
| 板载 LED | PC13（低电平点亮，做心跳） |

---

## 3. 程序默认行为

| 动作 | 结果 |
|---|---|
| 上电 | LED 闪 3 下 → 初始化 ESP8266 → 连 WiFi → 打印本机 IP → PING 电脑 |
| 主循环 | 10ms 一拍；LED 每 500ms 翻转（心跳） |
| **按键短按** | 串口打印 `[key] short press`，并向网页 `POST /api/event` 发一条 `key_press` |
| **按键长按 1s** | 重新连 WiFi |
| 掉线 | 每 30s PING 一次电脑，不通就自动重连 |

串口（USART1，115200）会输出类似：

```
==========================================
 STM32F103C8T6 + ESP8266 初始工程
 SYSCLK = 72000000 Hz
==========================================
[esp] AT ok
[esp] joining AP: my_hotspot ...
[esp] local ip = 192.168.43.88
[esp] ping 192.168.43.27 ... OK
[net] POST boot -> OK
[key] short press
[net] POST key_press -> OK
```

---

## 4. 目录结构

```
STM32F103C8-ESP8266-Init/
├── CMakeLists.txt              构建脚本（源文件自动 GLOB，新增 .c 无需改）
├── CMakePresets.json           Debug / Release 预设（CLion 自动识别）
├── cmake/
│   ├── arm-none-eabi-gcc.cmake 交叉编译工具链（自动找 CubeCLT / EIDE / PATH）
│   ├── STM32F103C8Tx_FLASH.ld  链接脚本：64K Flash / 20K RAM
│   └── startup_stm32f103xb.s   启动文件（向量表 + 复制 .data + 清 .bss）
├── Core/                       芯片级（换芯片只动这里）
│   ├── Inc/stm32f103_regs.h    寄存器定义（RCC/GPIO/USART/Flash/SysTick/NVIC）
│   ├── Inc/system_stm32f103.h
│   ├── Inc/usart.h             USART1 调试 + USART2 接 ESP8266
│   └── Src/  system_stm32f103.c（时钟 72MHz、delay_ms/us）、usart.c
├── App/                        应用级（你的业务代码）
│   ├── Inc/wifi_config.h       ★ 改 WiFi 账号密码、服务器 IP
│   ├── Inc/esp8266.h           AT 指令驱动
│   ├── Inc/key.h               按键（短按 / 长按）
│   └── Src/  main.c  esp8266.c  key.c
└── tools/
    ├── flash.md                WCH-Link 烧录 / CLion 调试 / 排错
    └── wchlink_stm32f103.cfg   OpenOCD 配置
```

---

## 5. 怎么往下加功能

| 想加的东西 | 怎么做 |
|---|---|
| 新外设（ADC / PWM / I2C / SPI） | 在 `Core/Inc/stm32f103_regs.h` 加外设结构体（照 RM0008 地址），再在 `App/` 写驱动 |
| 新的周期任务 | 主循环里用 `tick`（10ms 单位）做计数判断，别用 `delay_ms` 卡住 |
| 多发几种网络事件 | 直接调 `esp8266_http_post(WEB_HOST, WEB_PORT, "/api/event", json)` |
| 换成 RTOS | 把 `main` 里的循环拆成任务即可，驱动都是阻塞式、可直接在任务里调 |

---

## 6. 踩坑提示

1. **ESP8266 供电是最大坑**：必须独立 3.3V ≥500mA 电源，共地。供电不足表现为模块反复重启、AT 无响应。
2. **CH_PD 不拉高模块不工作**，本工程用 PB0 控制，上电自动拉高。
3. **USART 接线要交叉**：STM32 的 TX 接模块 RX，反之亦然。
4. **WCH-Link 模式**：默认若是 RISC-V 模式需切到 ARM/SWD，否则 OpenOCD 找不到目标。
5. **改了 `wifi_config.h` 一定要重新编译**，只烧旧 hex 不会生效。
