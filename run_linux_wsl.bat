@echo off
setlocal

cd /d "%~dp0"

echo [WSL] Executando Karamelo (Linux nativo via WSLg)...
start "" wsl.exe -d Ubuntu-24.04 --cd "%~dp0" ./app/Karamelo %*
