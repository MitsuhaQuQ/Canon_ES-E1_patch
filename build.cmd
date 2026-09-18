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

call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 exit /b 1

cl /nologo /c /O2 /W4 /GS /guard:cf /MT /DUNICODE /D_UNICODE eosbridge.c
if errorlevel 1 exit /b 1
link /nologo /dll /machine:x86 /subsystem:windows,6.01 /dynamicbase /nxcompat /guard:cf /def:eosbridge.def /out:EOSHOOKX.dll eosbridge.obj setupapi.lib winusb.lib advapi32.lib kernel32.lib user32.lib
if errorlevel 1 exit /b 1

cl /nologo /c /O2 /W4 /GS /guard:cf /MT smoke_test.c
if errorlevel 1 exit /b 1
link /nologo /machine:x86 /subsystem:console,6.01 /entry:mainCRTStartup /out:smoke_test.exe smoke_test.obj kernel32.lib
if errorlevel 1 exit /b 1

cl /nologo /O2 /W4 /GS /guard:cf /MT bridge_device_test.c /link /machine:x86 /subsystem:console,6.01 /dynamicbase /nxcompat /guard:cf /out:bridge_device_test.exe kernel32.lib
if errorlevel 1 exit /b 1

echo Built EOSHOOKX.dll and bridge tests
