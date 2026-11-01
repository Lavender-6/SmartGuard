# 烧录与调试说明（WCH-Link + SWD 两线）

## 一、接线

| WCH-Link 引脚 | STM32F103C8T6 | 说明 |
|---|---|---|
| **CLK** | **PA14**（SWDCLK） | 时钟线 |
| **DIO** | **PA13**（SWDIO） | 数据线 |
| **GND** | GND | **必须接**，否则连不上 |
| **3V3** | 3V3 | 可接可不接（板子自己 USB 供电时不必接） |

> WCH-Link 出厂默认可能是「RISC-V 模式」，需要长按上面的按钮切换成 **ARM/SWD 模式**（指示灯颜色会变）。
> 如果 OpenOCD 报 `Error: no device found`，八成就是模式不对或 CLK/DIO 接反。

## 二、OpenOCD 烧录（命令行）

本机 OpenOCD 位置：`C:/Users/lenovo/.eide/tools/openocd_7a1adfbec_mingw32/bin/openocd.exe`

在工程根目录执行（把 hex 换成你的产物路径）：

```bat
set OCD=C:\Users\lenovo\.eide\tools\openocd_7a1adfbec_mingw32\bin\openocd.exe
set SCRIPTS=C:\Users\lenovo\.eide\tools\openocd_7a1adfbec_mingw32\share\openocd\scripts

"%OCD%" -s "%SCRIPTS%" ^
        -f interface/cmsis-dap.cfg ^
        -f target/stm32f1x.cfg ^
        -c "program cmake-build-debug/out/STM32F103C8_ESP8266_Init.hex verify reset exit"
```

出现 `** Programming Finished **` / `** Verified OK **` 就是成功了。

**只想擦除：**
```bat
"%OCD%" -s "%SCRIPTS%" -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c "init; reset halt; flash erase_sector 0 0 63; shutdown"
```

**读保护导致连不上时**（`Error: init mode failed`）：用 STM32CubeProgrammer 的 `OB` 页面把 RDP 设为 AA，或用 OpenOCD：
```bat
... -c "init; stm32f1x unlock 0; reset halt; shutdown"
```

## 三、CLion 里调试

CLion 不能直接认 WCH-Link，需要 GDB Server：

1. `Settings → Build, Execution, Deployment → Embedded Development`（或 Debugger → GDB Server）
2. 添加 **OpenOCD** 服务器：
   - Executable：`C:\Users\lenovo\.eide\tools\openocd_7a1adfbec_mingw32\bin\openocd.exe`
   - Arguments：`-s C:\Users\lenovo\.eide\tools\openocd_7a1adfbec_mingw32\share\openocd\scripts -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg`
   - GDB：`E:\ST\STM32CubeCLT_1.18.0\GNU-tools-for-STM32\bin\arm-none-eabi-gdb.exe`
3. Run/Debug Configuration 里选 **OpenOCD Download & Run**，Target 选 `cmake-build-debug/STM32F103C8_ESP8266_Init.elf`

启动后会自动烧录 + 停在 main，可以打断点、看变量。

## 四、串口看日志

- **USART1 = PA9(TX) / PA10(RX)**，115200 8N1
- 用 USB-TTL：TTL 的 **RX 接 PA9**，GND 接 GND（只收不发的话 TX 可以不接）
- 串口助手（或 CLion 的 Serial Monitor）打开对应 COM 口即可看到 `[esp] ...` 日志

## 五、常见问题

| 现象 | 原因 / 处理 |
|---|---|
| OpenOCD: `no device found` | WCH-Link 不在 SWD 模式 / CLK·DIO 接反 / 没共地 |
| 连上后 `target not halted` | 芯片处于低功耗或被读保护，加 `reset_config none` 试试 |
| 串口无输出 | PA9 接的是 TTL 的 RX（不是 TX）；波特率 115200；GND 共地 |
| ESP8266 只回乱码 | 波特率不对（老固件 9600/74880），或供电不足导致不断重启 |
| `AT` 一直无响应 | CH_PD(PB0) 没拉高 / 3.3V 供电不足（ESP 峰值 300mA）/ TX·RX 交叉接错 |
