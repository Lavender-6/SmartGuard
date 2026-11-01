@echo off
rem ============================================================
rem  一键烧录 STM32F103C8T6（WCH-Link，SWD 两线）
rem  用法：双击运行，或在 CLion 构建完成后运行
rem  放在工程根目录，会自动找最新的 .hex
rem ============================================================
chcp 65001 >nul
title STM32F103C8 一键烧录 (WCH-Link)

setlocal
cd /d "%~dp0"

set "OCD=C:\Users\lenovo\.eide\tools\openocd_7a1adfbec_mingw32\bin\openocd.exe"
set "SCRIPTS=C:\Users\lenovo\.eide\tools\openocd_7a1adfbec_mingw32\share\openocd\scripts"

if not exist "%OCD%" (
    echo [错误] 找不到 openocd.exe: %OCD%
    pause
    exit /b 1
)

rem ---- 自动找最新构建产物（优先 Debug，其次 Release）----
set "HEX="
for %%F in (cmake-build-debug\out\STM32F103C8_ESP8266_Init.hex cmake-build-release\out\STM32F103C8_ESP8266_Init.hex) do (
    if exist "%%F" set "HEX=%%F"
)

if "%HEX%"=="" (
    echo [错误] 找不到 .hex，先在 CLion 里构建一次（小锤子）
    pause
    exit /b 1
)

echo.
echo =========== STM32F103C8 一键烧录 ===========
echo 固件: %HEX%
echo 调试器: WCH-Link (CMSIS-DAP / SWD)
echo ============================================
echo.

"%OCD%" -s "%SCRIPTS%" -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c "program %HEX% verify reset exit"

if %errorlevel%==0 (
    echo.
    echo [成功] 烧录完成，芯片已复位运行！
) else (
    echo.
    echo [失败] 排查顺序：
    echo   1. WCH-Link 是否已切换到 ARM/SWD 模式（指示灯颜色）
    echo   2. CLK=PA14  DIO=PA13  GND 必须共地
    echo   3. 板子是否已上电
    echo   4. 芯片被读保护时：用 STM32CubeProgrammer 解锁 (RDP=AA)
)
echo.
pause
