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
rem O caminho real do pacote so pode ser montado depois que APP_VERSION for
rem lido do app_info.h, o que acontece mais abaixo. Aqui havia um
rem dist\Karamelo_v1.0_Win64.zip fixo, sobrescrito logo em seguida e nunca
rem usado - inofensivo na execucao, mas fazia qualquer leitor acreditar que a
rem release era a 1.0.
set LOCAL_EXE=dist\Karamelo.exe
set LOCAL_JSON=dist\version.json

rem -------------------------------------------------------------
rem PASSO 1: Compilacao, Testes e Empacotamento Windows
rem -------------------------------------------------------------
rem package_release.bat's own step [1/4] already calls compile_port.bat
rem (compile + run the unit tests) before packaging.
echo [1/3] Compilando, testando e empacotando release Windows...
call "%~dp0package_release.bat" %*
if errorlevel 1 (
    echo.
    echo [FALHA] Erro na compilacao, testes ou empacotamento Windows. Deploy cancelado.
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
rem PASSO 2: Compilacao, Testes e Empacotamento Linux (WSL)
rem -------------------------------------------------------------
set LOCAL_LINUX_TAR=dist\Karamelo_v%APP_VER%_Linux64.tar.gz
set LOCAL_LINUX_BIN=dist\Karamelo_linux
set LINUX_DEPLOY_FILES=

where wsl >nul 2>&1
if not errorlevel 1 (
    echo.
    echo [2/3] Compilando, testando e empacotando release Linux via WSL...
    wsl bash -c "cd '$(wslpath '%~dp0')' && sed -i 's/\r$//' package_linux.sh && chmod +x package_linux.sh && ./package_linux.sh"
    if errorlevel 1 (
        echo.
        echo [AVISO] Falha ao gerar release Linux no WSL. Continuando deploy somente com Windows...
    ) else (
        if exist "%LOCAL_LINUX_TAR%" (
            set LINUX_DEPLOY_FILES="%LOCAL_LINUX_TAR%" "%LOCAL_LINUX_BIN%"
        )
    )
) else (
    echo.
    echo [AVISO] WSL nao encontrado no sistema. Pulando empacotamento Linux...
)

rem -------------------------------------------------------------
rem PASSO 3: Upload via SCP para o Servidor VPS / Coolify
rem -------------------------------------------------------------
echo.
echo [3/3] Enviando pacotes (v%APP_VER%), executaveis e version.json para o servidor (%SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%)...
echo.

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
    echo [AVISO] Falha no upload via SCP - verifique se a chave SSH esta configurada.
    echo Os arquivos estao prontos na pasta dist\ para upload manual no Coolify:
    echo   - dist\version.json
    echo   - dist\Karamelo.exe
    echo   - %LOCAL_ZIP%
    if exist "%LOCAL_LINUX_TAR%" (
    echo   - dist\Karamelo_linux
    echo   - %LOCAL_LINUX_TAR%
    )
    echo.
) else (
    echo.
    echo Sincronizando com containers do Coolify e ajustando permissoes...
    rem O sed remove CR do script antes de executar. Um .sh que sai do checkout
    rem com CRLF nao roda no Linux - o shebang vira "/bin/bash\r" e o erro e um
    rem enigmatico "cannot execute: required file not found", com os arquivos ja
    rem no servidor e os downloads em 404. O .gitattributes impede que volte, isto
    rem aqui garante que nem uma copia antiga do script derrube a publicacao.
    ssh -o StrictHostKeyChecking=no %SERVER_USER%@%SERVER_IP% "sed -i 's/\r$//' /root/fix_server.sh && chmod +x /root/fix_server.sh && /root/fix_server.sh"
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
if exist "%LOCAL_LINUX_TAR%" (
echo   - %LOCAL_LINUX_BIN%
echo   - %LOCAL_LINUX_TAR%
)
echo.
echo   Links Oficiais:
echo   - https://karamelo-emu.com/downloads/version.json
echo   - https://karamelo-emu.com/downloads/Karamelo.exe
echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Win64.zip
if exist "%LOCAL_LINUX_TAR%" (
echo   - https://karamelo-emu.com/downloads/Karamelo_linux
echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Linux64.tar.gz
)
echo.
echo =====================================================================
echo.
pause
