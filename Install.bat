@echo off
setlocal EnableExtensions

set "BASE=%~dp0"
set "TARGET=%~1"

if not defined TARGET (
    if exist "%BASE%Memory.exe" if exist "%BASE%Remote.exe" set "TARGET=%BASE%"
)

if not defined TARGET (
    echo EOS-1V Windows 11 compatibility installer
    echo.
    echo Drag the Canon "EOS LINK ES-E1" folder onto this BAT file,
    echo or copy this BAT and the patch files into that folder and run it there.
    echo.
    pause
    exit /b 2
)

if not exist "%TARGET%\Memory.exe" (
    echo ERROR: Memory.exe was not found in:
    echo "%TARGET%"
    echo Select the complete Canon "EOS LINK ES-E1" folder.
    pause
    exit /b 3
)

set "REMOTE_SRC=%BASE%Remote_hooked.exe"
set "DRIVER_SRC=%BASE%Eos1v_hooked.drv"
set "DLL_SRC=%BASE%EOSHOOKX.dll"

if not exist "%REMOTE_SRC%" set "REMOTE_SRC=%BASE%runtime\Remote.exe"
if not exist "%DRIVER_SRC%" set "DRIVER_SRC=%BASE%runtime\Eos1v.drv"
if not exist "%DLL_SRC%" set "DLL_SRC=%BASE%runtime\EOSHOOKX.dll"

if not exist "%REMOTE_SRC%" goto :check_installed_without_sources
if not exist "%DRIVER_SRC%" goto :check_installed_without_sources
if not exist "%DLL_SRC%" goto :check_installed_without_sources

call :install_one "Remote.exe" "%REMOTE_SRC%" "Remote.original.exe"
if errorlevel 1 goto :failed

call :install_one "Eos1v.drv" "%DRIVER_SRC%" "Eos1v.original.drv"
if errorlevel 1 goto :failed

if exist "%TARGET%\EOSHOOKX.dll" (
    fc /b "%TARGET%\EOSHOOKX.dll" "%DLL_SRC%" >nul 2>nul
    if not errorlevel 1 goto :dll_ready
)
copy /y "%DLL_SRC%" "%TARGET%\EOSHOOKX.dll" >nul
if errorlevel 1 goto :failed

:dll_ready
call :cleanup_temporary_files
echo.
echo Installation completed successfully:
echo "%TARGET%"
echo.
echo Before starting Remote.exe, put the camera in PC mode.
exit /b 0

:check_installed_without_sources
call :current_install_is_valid
if errorlevel 1 goto :missing_patch
echo Remote.exe, Eos1v.drv, and EOSHOOKX.dll are already patched.
call :cleanup_temporary_files
goto :dll_ready

:install_one
set "CURRENT=%TARGET%\%~1"
set "PATCHED=%~2"
set "BACKUP=%TARGET%\%~3"

if exist "%CURRENT%" (
    fc /b "%CURRENT%" "%PATCHED%" >nul 2>nul
    if not errorlevel 1 (
        echo %~1 is already patched.
        exit /b 0
    )
    if not exist "%BACKUP%" (
        move /y "%CURRENT%" "%BACKUP%" >nul
        if errorlevel 1 exit /b 1
        echo Backed up %~1 as %~3.
    ) else (
        echo Preserving existing backup %~3.
    )
) else (
    if not exist "%BACKUP%" (
        echo ERROR: Neither %~1 nor %~3 exists in the target directory.
        exit /b 1
    )
)

copy /y "%PATCHED%" "%CURRENT%" >nul
if errorlevel 1 exit /b 1
echo Installed patched %~1.
exit /b 0

:cleanup_temporary_files
if exist "%TARGET%\Remote_hooked.exe" del /f /q "%TARGET%\Remote_hooked.exe" >nul 2>nul
if exist "%TARGET%\Eos1v_hooked.drv" del /f /q "%TARGET%\Eos1v_hooked.drv" >nul 2>nul
exit /b 0

:current_install_is_valid
if not exist "%TARGET%\Remote.exe" exit /b 1
if not exist "%TARGET%\Eos1v.drv" exit /b 1
if not exist "%TARGET%\EOSHOOKX.dll" exit /b 1
certutil -hashfile "%TARGET%\Remote.exe" SHA256 2>nul | find /i "0A8DAC759136C55D89CA139649C1950D6F7B96F3031B55853A121F1CF900D6B0" >nul
if errorlevel 1 exit /b 1
certutil -hashfile "%TARGET%\Eos1v.drv" SHA256 2>nul | find /i "F497B4E975462478FFA7366D067F0316085FA5FE312F86A50205F1E84A407CED" >nul
if errorlevel 1 exit /b 1
certutil -hashfile "%TARGET%\EOSHOOKX.dll" SHA256 2>nul | find /i "99F83B1DFAD538F42EE057DF267974871827EB8DBE6CB54FC482F40C69303091" >nul
if errorlevel 1 exit /b 1
exit /b 0

:missing_patch
echo ERROR: The patch files were not found beside this BAT or in its runtime folder.
pause
exit /b 4

:failed
echo.
echo ERROR: Installation failed. Close Remote.exe and Memory.exe, then try again.
echo If the Canon directory is under Program Files, run this BAT as administrator.
pause
exit /b 5
