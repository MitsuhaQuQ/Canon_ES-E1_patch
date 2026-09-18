@echo off
setlocal EnableExtensions
cd /d "%~dp0"

call build.cmd
if errorlevel 1 exit /b 1

call build_patcher.cmd
if errorlevel 1 exit /b 1

call build_probe.cmd
if errorlevel 1 exit /b 1

echo.
echo Built EOSHOOKX.dll, EOS1V_Patcher.exe, and diagnostics
