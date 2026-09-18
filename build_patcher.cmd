@echo off
setlocal EnableExtensions EnableDelayedExpansion
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

if not exist EOSHOOKX.dll (
    echo ERROR: EOSHOOKX.dll must be built before the patcher so it can be embedded.
    exit /b 1
)

set "BRIDGEHASH="
for /f "tokens=*" %%H in ('certutil -hashfile EOSHOOKX.dll SHA256 ^| findstr /R /I "^[0-9A-F][0-9A-F]*$"') do set "BRIDGEHASH=%%H"
if not defined BRIDGEHASH (
    echo ERROR: Could not calculate EOSHOOKX.dll SHA-256.
    exit /b 1
)
>bridge_hash.h echo #ifndef EOS1V_BRIDGE_HASH_H
>>bridge_hash.h echo #define EOS1V_BRIDGE_HASH_H
>>bridge_hash.h echo #define EOS1V_EXPECTED_BRIDGE_HASH L"!BRIDGEHASH!"
>>bridge_hash.h echo #endif

rc /nologo eos1v_patcher.rc
if errorlevel 1 exit /b 1

cl /nologo /O2 /W4 /GS /guard:cf /MT /DUNICODE /D_UNICODE eos1v_patcher.c eos1v_patcher.res /link /machine:x64 /subsystem:console,6.01 /dynamicbase /nxcompat /guard:cf /out:EOS1V_Patcher.exe bcrypt.lib user32.lib
if errorlevel 1 exit /b 1

echo Built EOS1V_Patcher.exe
