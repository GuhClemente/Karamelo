@echo off
setlocal enabledelayedexpansion

cls 

rem Run from wherever this file lives, so the project can be moved or cloned
rem anywhere instead of being pinned to a static path.
cd /d "%~dp0"

set "EXE="
rem Nao existe nem nunca existiu uma v1.0; a busca abaixo ja acha qualquer
rem Karamelo*.exe que o build tenha gerado.
if not defined EXE if exist "app\Karamelo.exe" set "EXE=app\Karamelo.exe"
if not defined EXE (
    for %%F in (app\Karamelo*.exe) do (
        set "EXE=%%F"
    )
)

if not defined EXE (
    echo [ERROR] Executable not found in app\ directory.
    echo Please run compile_port.bat first to build the project.
    exit /b 1
)

echo [RUNNING] %EXE%...
start "" "%EXE%" %*

exit /b 0
