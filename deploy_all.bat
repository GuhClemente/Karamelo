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
echo [2/3] Gerando pacote oficial de distribuicao (dist\MiSTer_4_ALL_v1.0_Win64.zip)...
call package_release.bat
if errorlevel 1 (
    echo.
    echo [FALHA] Erro ao empacotar a release. Deploy cancelado.
    pause
    exit /b 1
)

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
echo [3/3] Enviando pacote para o servidor (%SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%)...
echo.

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
if errorlevel 1 (
    echo.
    echo [FALHA] Erro no upload via SCP para o servidor.
    pause
    exit /b 1
)

rem -------------------------------------------------------------
rem SUCESSO TOTAL
rem -------------------------------------------------------------
echo.
echo =====================================================================
echo   [PIPELINE CONCLUIDO COM SUCESSO!]
echo =====================================================================
echo.
echo   Arquivo Publicado: %LOCAL_ZIP%
echo   Destino no Servidor: %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
echo   Link Oficial de Download:
echo   -> https://mister4all.com/downloads/MiSTer_4_ALL_v1.0_Win64.zip
echo.
echo =====================================================================
echo.
pause
