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
rem PASSO 0: O que vai ser publicado tem que estar commitado e no GitHub
rem -------------------------------------------------------------
rem A release do GitHub (passo 4) aponta para o commit atual, e o site recebe
rem um binario compilado desta arvore. Com alteracao nao commitada, o que o
rem jogador baixa sai de um codigo que nao existe em lugar nenhum. Fica de fora
rem so o dist\version.json, que o proprio empacotamento regrava e e commitado
rem depois do deploy. Arquivos novos (nao rastreados) nao contam.
for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set CUR_VER=%%~v
if not "%~1"=="" if not "%~1"=="%CUR_VER%" (
    echo [ERRO] deploy_all.bat %~1 mudaria a versao ^(hoje %CUR_VER%^) sem commit.
    echo Antes, rode e commite a troca de versao:
    echo     powershell -File bump_version.ps1 %~1
    echo     git commit -am "chore: versao %~1" ^&^& git push
    echo Depois rode deploy_all.bat sem argumento.
    pause
    exit /b 1
)
git diff --quiet HEAD -- . ":(exclude)dist/version.json"
if errorlevel 1 (
    echo [ERRO] Ha alteracoes nao commitadas - deploy cancelado:
    git status --short --untracked-files=no -- . ":(exclude)dist/version.json"
    echo Commite e faca push antes de publicar.
    pause
    exit /b 1
)
git fetch -q origin
if errorlevel 1 (
    echo [ERRO] Nao consegui falar com o GitHub ^(git fetch^) - deploy cancelado.
    pause
    exit /b 1
)
set PUSHED=
for /f %%b in ('git branch -r --contains HEAD') do set PUSHED=1
if not defined PUSHED (
    echo [ERRO] O commit atual ainda nao esta no GitHub - rode git push antes.
    pause
    exit /b 1
)
echo [0/4] Tudo commitado e no GitHub ^(v%CUR_VER%^).
echo.

rem -------------------------------------------------------------
rem PASSO 1: Compilacao, Testes e Empacotamento Windows
rem -------------------------------------------------------------
rem package_release.bat's own step [1/4] already calls compile_port.bat
rem (compile + run the unit tests) before packaging.
echo [1/4] Compilando, testando e empacotando release Windows...
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
    echo [2/4] Compilando, testando e empacotando release Linux via WSL...
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
rem Inclusao de Release macOS (se previamente gerada em dist)
rem -------------------------------------------------------------
set LOCAL_MACOS_TAR=dist\Karamelo_v%APP_VER%_macOS_arm64.tar.gz
set LOCAL_MACOS_BIN=dist\Karamelo_mac
set MACOS_DEPLOY_FILES=
if exist "%LOCAL_MACOS_TAR%" (
    set MACOS_DEPLOY_FILES="%LOCAL_MACOS_TAR%" "%LOCAL_MACOS_BIN%"
)

rem -------------------------------------------------------------
rem PASSO 3: Upload via SCP para o Servidor VPS / Coolify
rem -------------------------------------------------------------
echo.
echo [3/4] Enviando pacotes (v%APP_VER%), executaveis e version.json para o servidor (%SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%)...
echo.

rem O pack de BIOS NAO sobe. Ele contem firmware de console, material
rem protegido por direito autoral - publica-lo em karamelo-emu.com e
rem redistribuicao, a mesma coisa que o README proibe para o repositorio.
rem O create_packs.ps1 continua gerando o arquivo em dist/ para uso local.
set EXTRA_FILES=

scp -o StrictHostKeyChecking=no "%LOCAL_ZIP%" "%LOCAL_EXE%" %LINUX_DEPLOY_FILES% %MACOS_DEPLOY_FILES% "%LOCAL_JSON%" "%~dp0data\ports.json" %EXTRA_FILES% %SERVER_USER%@%SERVER_IP%:%REMOTE_DIR%
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
    if exist "%LOCAL_MACOS_TAR%" (
    echo   - dist\Karamelo_mac
    echo   - %LOCAL_MACOS_TAR%
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
rem PASSO 4: Release no GitHub com os mesmos arquivos de dist
rem -------------------------------------------------------------
rem Sobe para github.com/GuhClemente/Karamelo/releases o zip, o exe e os
rem pacotes Linux/macOS que acabaram de ir para o site - nada e recompilado,
rem entao os dois lugares servem os mesmos bytes. Falha aqui so avisa: o site
rem ja foi publicado. Detalhes em tools\github_release.ps1.
echo.
echo [4/4] Publicando a release v%APP_VER% no GitHub...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\github_release.ps1" -Version %APP_VER%
if errorlevel 1 (
    echo   [AVISO] Release do GitHub nao publicada. Depois de resolver, rode:
    echo       powershell -File tools\github_release.ps1 -Version %APP_VER%
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
if exist "%LOCAL_MACOS_TAR%" (
echo   - %LOCAL_MACOS_BIN%
echo   - %LOCAL_MACOS_TAR%
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
if exist "%LOCAL_MACOS_TAR%" (
echo   - https://karamelo-emu.com/downloads/Karamelo_mac
echo   - https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_macOS_arm64.tar.gz
)
echo.
rem -------------------------------------------------------------
rem Notificar no Discord (Resumo em #anuncios + Links dos Pacotes Completos em #downloads)
rem -------------------------------------------------------------
if exist "c:\github\Server-Discord-Karamelo\notify_deploy.bat" (
    echo.
    echo Notificando servidor Discord...
    call "c:\github\Server-Discord-Karamelo\notify_deploy.bat" --json "%LOCAL_JSON%"
)

echo =====================================================================
echo.
pause
