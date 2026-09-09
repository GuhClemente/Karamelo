@echo off
setlocal enabledelayedexpansion

cls
echo =======================================================
echo   Karamelo - Upload Automatico para o Servidor
echo =======================================================
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
for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set LOCAL_ZIP=dist\Karamelo_v%APP_VER%_Win64.zip
set LOCAL_EXE=dist\Karamelo.exe
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
set LOCAL_ZIP=dist\Karamelo_v%APP_VER%_Win64.zip

echo.
echo [2/2] Enviando arquivos para %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%...
echo   - %LOCAL_ZIP%
echo   - %LOCAL_EXE%
echo   - %LOCAL_JSON%
set EXTRA_FILES=
if exist "%~dp0dist\Karamelo_Pack_BIOS.zip" (
    set EXTRA_FILES="%~dp0dist\Karamelo_Pack_BIOS.zip"
    echo   - dist\Karamelo_Pack_BIOS.zip
)

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" "%LOCAL_JSON%" "%~dp0fix_server.sh" %EXTRA_FILES% %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%

if errorlevel 1 (
    echo.
    echo [ERRO] Falha no upload via SCP - verifique a conexao e chave SSH.
) else (
    echo.
    echo Sincronizando com containers do Coolify e ajustando permissoes...
    ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "chmod +x %REMOTE_DIR%fix_server.sh && %REMOTE_DIR%fix_server.sh"
    echo.
    echo =======================================================
    echo   UPLOAD E PUBLICACAO CONCLUIDOS COM SUCESSO!
    echo =======================================================
    echo   Downloads disponiveis em:
    echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Win64.zip
    echo   - https://karamelo-emu.com/downloads/Karamelo.exe
    echo   - https://karamelo-emu.com/downloads/version.json
    if exist "%~dp0dist\Karamelo_Pack_BIOS.zip" echo   - https://karamelo-emu.com/downloads/Karamelo_Pack_BIOS.zip
    echo =======================================================
)

echo.
pause

