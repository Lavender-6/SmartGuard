@echo off
chcp 936 >nul
title SmartGuard 看板服务 一键启动
setlocal enabledelayedexpansion

set "APPDIR=E:\workbuddy\2026-10-23-20-33-28\SmartGuard_Web"
set "PORT=8080"

cd /d "%APPDIR%"
if errorlevel 1 (
    echo [错误] 找不到看板目录：%APPDIR%
    pause
    exit /b 1
)

echo ============================================
echo   SmartGuard 智能门禁看板 一键启动
echo   目录：%APPDIR%
echo ============================================
echo.

REM ---------- 1. 检查 Node.js ----------
where node >nul 2>nul
if errorlevel 1 (
    echo [错误] 没找到 Node.js，请先安装：https://nodejs.org
    echo        装完后重新双击本文件即可。
    pause
    exit /b 1
)
for /f "tokens=*" %%v in ('node -v') do echo [OK] Node.js %%v

REM ---------- 2. 结束占用 8080 的旧进程（关键：旧版服务必须先杀掉） ----------
set "KILLED=0"
for /f "tokens=5" %%p in ('netstat -ano ^| findstr ":%PORT% " ^| findstr "LISTENING"') do (
    if not "%%p"=="0" (
        echo [..] 发现占用 %PORT% 端口的旧进程 PID=%%p，正在结束...
        taskkill /F /PID %%p >nul 2>nul
        if not errorlevel 1 set "KILLED=1"
    )
)
if "!KILLED!"=="1" (
    echo [OK] 旧看板进程已结束
    timeout /t 1 /nobreak >nul
) else (
    echo [OK] %PORT% 端口空闲
)

REM ---------- 3. 启动最新版服务（独立窗口，关掉即停止） ----------
echo [..] 正在启动最新版看板服务...
start "SmartGuard 看板服务 (最新版)" cmd /k "chcp 936 >nul && cd /d %APPDIR% && node server.js"

REM ---------- 4. 等待端口就绪 ----------
set "READY=0"
for /l %%i in (1,1,20) do (
    if "!READY!"=="0" (
        timeout /t 1 /nobreak >nul
        netstat -ano | findstr ":%PORT% " | findstr "LISTENING" >nul
        if not errorlevel 1 set "READY=1"
    )
)

if "!READY!"=="0" (
    echo.
    echo [警告] 10 秒内没等到 %PORT% 端口监听，请查看弹出的服务窗口报错信息。
    pause
    exit /b 1
)
echo [OK] 服务已监听 %PORT% 端口

REM ---------- 5. 自检 /api/mode（STM32 短按开门靠它） ----------
echo.
echo [自检] GET /api/mode?device=stm32
set "MODEJSON="
for /f "delims=" %%r in ('curl -s -m 3 "http://127.0.0.1:%PORT%/api/mode?device=stm32" 2^>nul') do set "MODEJSON=%%r"

if "!MODEJSON!"=="" (
    echo   [失败] 接口无响应，请确认服务窗口没有报错。
) else (
    echo   响应：!MODEJSON!
    echo !MODEJSON! | findstr /C:"\"mode\"" >nul
    if errorlevel 1 (
        echo   [失败] 响应里没有 "mode" 字段 —— 说明跑的还是旧版 server.js！
        echo          请确认文件修改时间为最新，或手动检查 %APPDIR%\server.js
    ) else (
        echo   [OK] 接口正常，STM32 短按开门可用
    )
)

REM ---------- 6. 打开浏览器（带刷新参数，绕开旧页面缓存） ----------
echo.
echo [..] 打开看板页面...
start "" "http://localhost:%PORT%/?t=%RANDOM%"

echo.
echo ============================================
echo   启动完成。关掉"SmartGuard 看板服务"窗口即停止服务。
echo   本窗口可以关闭。
echo ============================================
timeout /t 5 /nobreak >nul
exit /b 0
