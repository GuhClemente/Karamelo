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
rem PASSO 1 & 2: Compilacao C++20 (/MT) e Testes Unitarios
rem -------------------------------------------------------------
echo [1/3] Compilando motor C++20 com /MT e executando testes unitarios...
call compile_port.bat
if errorlevel 1 (
    echo.
    echo [FALHA] Erro na compilacao ou testes. Deploy cancelado.
    pause
    exit /b 1
)

rem -------------------------------------------------------------
rem PASSO 3: Empacotamento Limpo da Distribuicao
rem -------------------------------------------------------------
echo.
echo [2/3] Gerando pacote oficial de distribuicao...
call package_release.bat %*
if errorlevel 1 (
    echo.
    echo [FALHA] Erro ao empacotar a release. Deploy cancelado.
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
rem PASSO 4: Upload via SCP para o Servidor VPS / Coolify
rem -------------------------------------------------------------
echo.
echo [3/3] Enviando pacote (v%APP_VER%), executavel e version.json para o servidor (%SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%)...
echo.

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" "%LOCAL_JSON%" %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
if errorlevel 1 (
    echo.
    echo [AVISO] Falha no upload via SCP (caso nao esteja configurado com chave SSH).
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
