@echo off
setlocal enabledelayedexpansion

cls
echo =======================================================
echo   MiSTer 4 ALL - Upload Automatico para o Servidor
echo =======================================================
echo.

cd /d "%~dp0"

set SERVER_IP=187.127.59.127
set SERVER_USER=root
set REMOTE_DIR=/data/downloads/
set LOCAL_ZIP=dist\MiSTer_4_ALL_v1.0_Win64.zip

if not exist "%LOCAL_ZIP%" (
    echo [1/2] Pacote nao encontrado. Gerando pacote primeiro...
    call package_release.bat
    if errorlevel 1 (
        echo [ERRO] Falha ao gerar o pacote. Upload cancelado.
        pause
        exit /b 1
    )
)

echo.
echo [2/2] Enviando %LOCAL_ZIP% para %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%...
echo.
scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%

if errorlevel 0 (
    echo.
    echo =======================================================
    echo   UPLOAD CONCLUIDO COM SUCESSO!
    echo =======================================================
    echo   Download disponivel em:
    echo   https://mister4all.com/downloads/MiSTer_4_ALL_v1.0_Win64.zip
    echo =======================================================
) else (
    echo.
    echo [ERRO] Falha na conexao ou autenticacao SSH.
)

echo.
pause
