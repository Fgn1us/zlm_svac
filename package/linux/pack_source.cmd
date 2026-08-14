@echo off
rem ============================================================
rem  ZLMediaKit SVAC fork - Windows cmd 打包源码
rem  内部调用 Git Bash 执行 pack_source.sh
rem  用法: pack_source.cmd [-v VERSION]
rem  等价于 Linux 下的: bash pack_source.sh -v VERSION
rem ============================================================
chcp 65001 >nul
setlocal

set "SH=%~dp0pack_source.sh"
set "BASH="

where bash.exe >nul 2>nul
if %errorlevel%==0 set "BASH=bash.exe"
if defined BASH goto :found

if exist "C:\Program Files\Git\bin\bash.exe" set "BASH=C:\Program Files\Git\bin\bash.exe"
if defined BASH goto :found

if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if defined BASH goto :found

if exist "%LOCALAPPDATA%\Programs\Git\bin\bash.exe" set "BASH=%LOCALAPPDATA%\Programs\Git\bin\bash.exe"
if defined BASH goto :found

echo [ERR] 未找到 Git Bash(bash.exe)，请安装 Git for Windows: https://git-scm.com/
exit /b 1

:found
"%BASH%" "%SH%" %*
exit /b %errorlevel%
