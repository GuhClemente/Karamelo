@echo off
setlocal enabledelayedexpansion

cls
echo =====================================================================
echo   MiSTer 4 ALL - Pipeline All-in-One: Build, Test, Package ^& Deploy
echo =====================================================================
echo.

cd /d "%~dp0"

set SERVER_IP=187.127.59.127
set SERVER_USER=root
set REMOTE_DIR=/data/downloads/
set LOCAL_ZIP=dist\MiSTer_4_ALL_v1.0_Win64.zip
set LOCAL_EXE=dist\MiSTer_4_ALL.exe
set LOCAL_JSON=dist\version.json

rem -------------------------------------------------------------
rem PASSO 1: Compilacao, Testes e Empacotamento
rem -------------------------------------------------------------
rem package_release.bat's own step [1/4] already calls compile_port.bat
rem (compile + run the unit tests) before packaging - calling it again here
rem first just recompiled and re-ran the whole test suite a second time,
rem invisibly, since compile_port.bat's own "cls" wiped the first run off
rem the screen before anyone could see it had already happened.
echo [1/2] Compilando, testando e empacotando a release...
call "%~dp0package_release.bat" %*
if errorlevel 1 (
    echo.
    echo [FALHA] Erro na compilacao, testes ou empacotamento. Deploy cancelado.
    pause
    exit /b 1
)

for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set LOCAL_ZIP=dist\MiSTer_4_ALL_v%APP_VER%_Win64.zip
set LOCAL_EXE=dist\MiSTer_4_ALL.exe
set LOCAL_JSON=dist\version.json

if not exist "%LOCAL_ZIP%" (
    echo.
    echo [FALHA] Arquivo %LOCAL_ZIP% nao foi encontrado.
    pause
    exit /b 1
)

rem -------------------------------------------------------------
rem PASSO 2: Upload via SCP para o Servidor VPS / Coolify
rem -------------------------------------------------------------
echo.
echo [2/2] Enviando pacote (v%APP_VER%), executavel e version.json para o servidor (%SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%)...
echo.

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" "%LOCAL_JSON%" %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
if errorlevel 1 (
    echo.
    echo [AVISO] Falha no upload via SCP - verifique se a chave SSH esta configurada.
    echo Os arquivos estao prontos na pasta dist\ para upload manual no Coolify:
    echo   - dist\version.json
    echo   - dist\MiSTer_4_ALL.exe
    echo   - %LOCAL_ZIP%
    echo.
)

rem -------------------------------------------------------------
rem SUCESSO TOTAL
rem -------------------------------------------------------------
echo.
echo =====================================================================
echo   [PIPELINE CONCLUIDO COM SUCESSO - VERSAO v%APP_VER%]
echo =====================================================================
echo.
echo   Arquivos Gerados na pasta dist\:
echo   - %LOCAL_JSON%
echo   - %LOCAL_EXE%
echo   - %LOCAL_ZIP%
echo.
echo   Links Oficiais:
echo   - https://mister4all.com/downloads/version.json
echo   - https://mister4all.com/downloads/MiSTer_4_ALL.exe
echo   - https://mister4all.com/downloads/MiSTer_4_ALL_v%APP_VER%_Win64.zip
echo.
echo =====================================================================
echo.
pause
