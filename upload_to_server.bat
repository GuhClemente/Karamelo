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
set LOCAL_LINUX_TAR=dist\Karamelo_v%APP_VER%_Linux64.tar.gz
set LOCAL_LINUX_BIN=dist\Karamelo_linux
set LINUX_DEPLOY_FILES=
if exist "%LOCAL_LINUX_TAR%" set LINUX_DEPLOY_FILES="%LOCAL_LINUX_TAR%" "%LOCAL_LINUX_BIN%"

echo.
echo [2/2] Enviando arquivos para %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%...
echo   - %LOCAL_ZIP%
echo   - %LOCAL_EXE%
echo   - %LOCAL_JSON%
if exist "%LOCAL_LINUX_TAR%" (
echo   - %LOCAL_LINUX_BIN%
echo   - %LOCAL_LINUX_TAR%
)
rem O pack de BIOS NAO sobe. Ele contem firmware de console, material
rem protegido por direito autoral - publica-lo em karamelo-emu.com e
rem redistribuicao, a mesma coisa que o README proibe para o repositorio.
rem O create_packs.ps1 continua gerando o arquivo em dist/ para uso local.
set EXTRA_FILES=

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" %LINUX_DEPLOY_FILES% "%LOCAL_JSON%" %EXTRA_FILES% %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
rem O script de deploy vai para /root/, nunca para %REMOTE_DIR%: aquela pasta e
rem servida publicamente, e o fix_server.sh acabou exposto em /downloads/.
scp -o StrictHostKeyChecking=no "%~dp0fix_server.sh" %SERVER_USER%@%SERVER_IP%:/root/fix_server.sh

if errorlevel 1 (
    echo.
    echo [ERRO] Falha no upload via SCP - verifique a conexao e chave SSH.
) else (
    echo.
    echo Sincronizando com containers do Coolify e ajustando permissoes...
    ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "sed -i 's/\r$//' /root/fix_server.sh && chmod +x /root/fix_server.sh && /root/fix_server.sh"
    echo.
    echo =======================================================
    echo   UPLOAD E PUBLICACAO CONCLUIDOS COM SUCESSO!
    echo =======================================================
    echo   Downloads disponiveis em:
    echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Win64.zip
    echo   - https://karamelo-emu.com/downloads/Karamelo.exe
    echo   - https://karamelo-emu.com/downloads/version.json
    if exist "%LOCAL_LINUX_TAR%" (
    echo   - https://karamelo-emu.com/downloads/Karamelo_linux
    echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Linux64.tar.gz
    )
    echo =======================================================
)

echo.
pause

