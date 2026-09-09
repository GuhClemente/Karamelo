@echo off
setlocal
cls

echo =======================================================
echo   Karamelo - Correcao Definitiva de Download
echo =======================================================
echo.
echo [1/2] Enviando script de correcao para o servidor...
scp -o StrictHostKeyChecking=no "%~dp0fix_server.sh" root@187.127.59.127:/root/fix_server.sh
if errorlevel 1 (
    echo [ERRO] Falha ao enviar o script via SCP.
    pause
    exit /b 1
)

echo.
echo [2/2] Executando correcao no servidor...
ssh -o StrictHostKeyChecking=no root@187.127.59.127 "chmod +x /root/fix_server.sh && /root/fix_server.sh"

echo.
echo =======================================================
echo   Processo finalizado!
echo =======================================================
echo.
pause
