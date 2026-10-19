@echo off
setlocal

cd /d "%~dp0"

echo ==============================================================================
echo   Karamelo Emulador - Bateria de Teste de Todos os Cores e Sistemas
echo ==============================================================================
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\stress_cores.ps1" %*
exit /b %ERRORLEVEL%
