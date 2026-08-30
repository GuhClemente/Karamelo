@echo off
setlocal enabledelayedexpansion

cls 

rem Run from wherever this file lives, so the project can be moved or cloned
rem anywhere instead of being pinned to a static path.
cd /d "%~dp0"

set "EXE="
if exist "app\MiSTer_Flavor_v2.0.exe" set "EXE=app\MiSTer_Flavor_v2.0.exe"
if not defined EXE if exist "app\MiSTer_Flavor.exe" set "EXE=app\MiSTer_Flavor.exe"
if not defined EXE (
    for %%F in (app\MiSTer_Flavor*.exe) do (
        set "EXE=%%F"
    )
)
if not defined EXE if exist "app\MiSTer_Win32.exe" set "EXE=app\MiSTer_Win32.exe"

if not defined EXE (
    echo [ERROR] Executable not found in app\ directory.
    echo Please run compile_port.bat first to build the project.
    exit /b 1
)

echo [RUNNING] %EXE%...
start "" "%EXE%" %*

exit /b 0
