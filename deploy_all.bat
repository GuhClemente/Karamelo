@echo off
setlocal enabledelayedexpansion

cls
echo =====================================================================
echo   Karamelo - Pipeline All-in-One: Build, Test, Package ^& Deploy
echo =====================================================================
echo.

cd /d "%~dp0"

rem Endereco, usuario e diretorio remoto do servidor ficam em deploy_env.bat,
rem que esta fora do controle de versao. Sem ele o deploy nao roda: copie o
rem deploy_env.example.bat e preencha. Quem preferir pode so exportar
rem SERVER_IP/SERVER_USER/REMOTE_DIR no ambiente - deploy_env.bat nao
rem sobrescreve o que ja estiver definido.
if not exist "%~dp0deploy_env.bat" (
    echo [ERRO] deploy_env.bat nao encontrado.
    echo Copie deploy_env.example.bat para deploy_env.bat e preencha o servidor.
    pause
    exit /b 1
)
call "%~dp0deploy_env.bat"
set LOCAL_ZIP=dist\Karamelo_v1.0_Win64.zip
set LOCAL_EXE=dist\Karamelo.exe
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
set LOCAL_ZIP=dist\Karamelo_v%APP_VER%_Win64.zip
set LOCAL_EXE=dist\Karamelo.exe
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

set EXTRA_FILES=
if exist "%~dp0dist\Karamelo_Pack_BIOS.zip" set EXTRA_FILES="%~dp0dist\Karamelo_Pack_BIOS.zip"

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" "%LOCAL_JSON%" "%~dp0fix_server.sh" %EXTRA_FILES% %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
if errorlevel 1 (
    echo.
    echo [AVISO] Falha no upload via SCP - verifique se a chave SSH esta configurada.
    echo Os arquivos estao prontos na pasta dist\ para upload manual no Coolify:
    echo   - dist\version.json
    echo   - dist\Karamelo.exe
    echo   - %LOCAL_ZIP%
    if exist "dist\Karamelo_Pack_BIOS.zip" echo   - dist\Karamelo_Pack_BIOS.zip
    echo.
) else (
    echo.
    echo Sincronizando com containers do Coolify e ajustando permissoes...
    ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "chmod +x %REMOTE_DIR%fix_server.sh && %REMOTE_DIR%fix_server.sh"
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
if exist "dist\Karamelo_Pack_BIOS.zip" echo   - dist\Karamelo_Pack_BIOS.zip
echo.
echo   Links Oficiais:
echo   - https://karamelo-emu.com/downloads/version.json
echo   - https://karamelo-emu.com/downloads/Karamelo.exe
echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Win64.zip
if exist "dist\Karamelo_Pack_BIOS.zip" echo   - https://karamelo-emu.com/downloads/Karamelo_Pack_BIOS.zip
echo.
echo =====================================================================
echo.
pause
