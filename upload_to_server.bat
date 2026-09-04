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
for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set LOCAL_ZIP=dist\MiSTer_4_ALL_v%APP_VER%_Win64.zip
set LOCAL_EXE=dist\MiSTer_4_ALL.exe
set LOCAL_JSON=dist\version.json

if not exist "%LOCAL_ZIP%" (
    echo [1/2] Pacote nao encontrado. Gerando pacote primeiro...
    call "%~dp0package_release.bat"
    if errorlevel 1 (
        echo [ERRO] Falha ao gerar o pacote. Upload cancelado.
        pause
        exit /b 1
    )
)

for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set LOCAL_ZIP=dist\MiSTer_4_ALL_v%APP_VER%_Win64.zip

echo.
echo [2/2] Enviando arquivos para %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%...
echo   - %LOCAL_ZIP%
echo   - %LOCAL_EXE%
echo   - %LOCAL_JSON%
echo.
scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" "%LOCAL_JSON%" "%~dp0fix_server.sh" %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%

if errorlevel 0 (
    echo.
    echo Sincronizando com containers do Coolify e ajustando permissoes...
    ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "chmod +x %REMOTE_DIR%fix_server.sh && %REMOTE_DIR%fix_server.sh"
    echo.
    echo =======================================================
    echo   UPLOAD E PUBLICACAO CONCLUIDOS COM SUCESSO!
    echo =======================================================
    echo   Downloads disponiveis em:
    echo   - https://mister4all.com/downloads/MiSTer_4_ALL_v%APP_VER%_Win64.zip
    echo   - https://mister4all.com/downloads/MiSTer_4_ALL.exe
    echo   - https://mister4all.com/downloads/version.json
    echo =======================================================
) else (
    echo.
    echo [ERRO] Falha na conexao ou autenticacao SSH.
)

echo.
pause

