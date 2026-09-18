@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe was not found. Install Visual Studio Build Tools with Desktop development with C++.
    exit /b 1
)

set "VSROOTFILE=%TEMP%\eos1v_vsroot_%RANDOM%_%RANDOM%.txt"
"%VSWHERE%" -all -products * -property installationPath > "%VSROOTFILE%"
set /p VSROOT=<"%VSROOTFILE%"
del /q "%VSROOTFILE%" >nul 2>nul
if not defined VSROOT (
    echo ERROR: A Visual Studio installation was not found.
    exit /b 1
)

if not exist "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo ERROR: Visual Studio C++ build tools were not found.
    exit /b 1
)

call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

cl /nologo /O2 /W4 /GS /guard:cf /MT eos_probe.c /link /machine:x64 /subsystem:console,6.01 /dynamicbase /nxcompat /guard:cf /out:eos_probe.exe setupapi.lib winusb.lib kernel32.lib
if errorlevel 1 exit /b 1

echo Built eos_probe.exe
