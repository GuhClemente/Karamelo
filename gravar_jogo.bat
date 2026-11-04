@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

rem ============================================================================
rem  Karamelo Emulador - Gravador Universal de Gameplay (Qualquer Sistema/Core)
rem  Aceleração por Hardware NVIDIA NVENC + Áudio Estúdio 48.000 Hz
rem ============================================================================

set "ROOT_DIR=%~dp0"
set "RECORDER=%ROOT_DIR%tools\record_gameplay\out\record_gameplay.exe"
set "OUT_DEFAULT=%ROOT_DIR%videos_gravados"

if not exist "%RECORDER%" (
    echo [ERRO] Gravador nao encontrado: %RECORDER%
    echo Compilando o gravador agora...
    call "%ROOT_DIR%tools\record_gameplay\build.bat"
    if errorlevel 1 (
        echo [ERRO] Falha ao compilar record_gameplay.exe.
        pause
        exit /b 1
    )
)

rem ----------------------------------------------------------------------------
rem  Modo Parâmetros CLI:
rem  gravar_jogo.bat <ROM> [DURACAO_SEG] [CORE_DLL] [PASTA_SAIDA] [FILTRO: 0=Raw|1=CRT] [MACRO]
rem ----------------------------------------------------------------------------
if not "%~1"=="" (
    set "ROM_PATH=%~1"
    set "DURATION=%~2"
    if "!DURATION!"=="" set "DURATION=90"

    set "CORE_DLL=%~3"
    set "OUT_DIR=%~4"
    if "!OUT_DIR!"=="" set "OUT_DIR=%OUT_DEFAULT%"

    set "FILTER_MODE=%~5"
    if "!FILTER_MODE!"=="" set "FILTER_MODE=0"

    set "MACRO_SPEC=%~6"
    if "!MACRO_SPEC!"=="" set "MACRO_SPEC=none"

    for %%F in ("!ROM_PATH!") do set "BASE_NAME=%%~nF"

    rem Auto-detect core if not supplied
    if "!CORE_DLL!"=="" (
        call :DETECT_CORE "!ROM_PATH!"
    )
    goto :EXECUTE
)

rem ----------------------------------------------------------------------------
rem  Modo Wizard Interativo
rem ----------------------------------------------------------------------------
cls
echo ============================================================================
echo   🎬 KARAMELO - GRAVADOR UNIVERSAL DE GAMEPLAY (TODOS OS SISTEMAS)
echo ============================================================================
echo.

echo [1/5] Selecione a ROM / Jogo:
echo ----------------------------------------------------------------------------
echo Arraste e solte o arquivo da ROM aqui, ou digite o caminho completo:
echo.
set /p "USER_INPUT=Arquivo da ROM: "
set "USER_INPUT=%USER_INPUT:"=%"

if not exist "%USER_INPUT%" (
    echo [ERRO] Arquivo nao encontrado: %USER_INPUT%
    pause
    exit /b 1
)

set "ROM_PATH=%USER_INPUT%"
for %%F in ("!ROM_PATH!") do (
    set "BASE_NAME=%%~nF"
    set "ROM_EXT=%%~xF"
)

rem Auto-detect core
call :DETECT_CORE "!ROM_PATH!"

echo.
echo [2/5] Motor / Core de Emulacao Detectado:
echo ----------------------------------------------------------------------------
echo Core sugerido: !CORE_DLL!
echo.
echo Deseja usar este core ou escolher outro?
echo   [1] Usar core detectado (!CORE_NAME!)
echo   [2] Digitar outro nome de core em cores\
echo   [3] Listar todos os cores disponiveis
echo.
set /p "CORE_OPTION=Opcao [1-3, padrao 1]: "
if "%CORE_OPTION%"=="2" (
    set /p "CUSTOM_CORE=Nome da DLL em cores\ (ex: snes.dll): "
    set "CORE_DLL=%ROOT_DIR%cores\!CUSTOM_CORE!"
) else if "%CORE_OPTION%"=="3" (
    echo.
    echo Cores disponiveis em cores\:
    dir /b "%ROOT_DIR%cores\*.dll"
    echo.
    set /p "CUSTOM_CORE=Nome da DLL (ex: genesis.dll): "
    set "CORE_DLL=%ROOT_DIR%cores\!CUSTOM_CORE!"
)

if not exist "!CORE_DLL!" (
    echo [ERRO] Core nao encontrado: !CORE_DLL!
    pause
    exit /b 1
)

echo.
echo [3/5] Duracao da Gravacao (segundos):
echo ----------------------------------------------------------------------------
set /p "DURATION=Duracao em segundos [padrao 90]: "
if "%DURATION%"=="" set "DURATION=90"

echo.
echo [4/5] Filtro Visual:
echo ----------------------------------------------------------------------------
echo   [0] Raw / Pixel-Art Puro (Nativo) [Padrao]
echo   [1] CRT Scanlines / Shaders de Tubo
echo.
set /p "FILTER_MODE=Opcao [0-1, padrao 0]: "
if "%FILTER_MODE%"=="" set "FILTER_MODE=0"

echo.
echo [5/5] Comandos de Botoes / Macro Cronometrada (Opcional):
echo ----------------------------------------------------------------------------
echo Digite os segundos e botoes a pressionar para passar avisos ou menus.
echo Exemplos:
echo   2-5:A             (Pressiona A entre 2s e 5s)
echo   2-4:A;8-10:START   (Pressiona A de 2-4s e START de 8-10s)
echo   Deixe em branco se o jogo tem modo Attract nativo ou se nao precisa de input.
echo.
set /p "MACRO_SPEC=Comandos de botoes [Enter para nenhum]: "
if "%MACRO_SPEC%"=="" set "MACRO_SPEC=none"

set "OUT_DIR=%OUT_DEFAULT%"

:EXECUTE
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

cls
echo ============================================================================
echo   INICIANDO GRAVACAO UNIVERSAL DE GAMEPLAY
echo ============================================================================
echo  ROM:      %ROM_PATH%
echo  Core:     %CORE_DLL%
echo  Duracao:  %DURATION% segundos
echo  Filtro:   %FILTER_MODE%
echo  Macro:    %MACRO_SPEC%
echo  Saida:    %OUT_DIR%\%BASE_NAME%.mp4
echo ============================================================================
echo.

"%RECORDER%" "%CORE_DLL%" "%ROM_PATH%" %DURATION% "%OUT_DIR%" "%BASE_NAME%" 0 %FILTER_MODE% 1 1 "%MACRO_SPEC%"

echo.
if exist "%OUT_DIR%\%BASE_NAME%.mp4" (
    echo ============================================================================
    echo   [SUCESSO] Video gravado com sucesso!
    echo   Arquivo: %OUT_DIR%\%BASE_NAME%.mp4
    echo ============================================================================
    echo.
    set /p "OPEN_CHOICE=Deseja abrir a pasta do video? [S/N, padrao S]: "
    if /i "!OPEN_CHOICE!" NEQ "n" explorer.exe /select,"%OUT_DIR%\%BASE_NAME%.mp4"
) else (
    echo [ERRO] Ocorreu uma falha na gravacao do video.
    pause
)
endlocal
exit /b 0

rem ----------------------------------------------------------------------------
rem  Subrotina de Deteccao de Core por Extensao
rem ----------------------------------------------------------------------------
:DETECT_CORE
set "EXT=%~x1"
set "EXT=!EXT:.=!"

if /i "!EXT!"=="z64" ( set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll" & set "CORE_NAME=ParaLLEl N64" & exit /b 0 )
if /i "!EXT!"=="n64" ( set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll" & set "CORE_NAME=ParaLLEl N64" & exit /b 0 )
if /i "!EXT!"=="v64" ( set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll" & set "CORE_NAME=ParaLLEl N64" & exit /b 0 )
if /i "!EXT!"=="sfc" ( set "CORE_DLL=%ROOT_DIR%cores\snes.dll" & set "CORE_NAME=SNES (Snes9x)" & exit /b 0 )
if /i "!EXT!"=="smc" ( set "CORE_DLL=%ROOT_DIR%cores\snes.dll" & set "CORE_NAME=SNES (Snes9x)" & exit /b 0 )
if /i "!EXT!"=="nes" ( set "CORE_DLL=%ROOT_DIR%cores\nes.dll" & set "CORE_NAME=NES (FCEUmm)" & exit /b 0 )
if /i "!EXT!"=="md"  ( set "CORE_DLL=%ROOT_DIR%cores\genesis.dll" & set "CORE_NAME=Mega Drive (Genesis Plus GX)" & exit /b 0 )
if /i "!EXT!"=="gen" ( set "CORE_DLL=%ROOT_DIR%cores\genesis.dll" & set "CORE_NAME=Mega Drive (Genesis Plus GX)" & exit /b 0 )
if /i "!EXT!"=="smd" ( set "CORE_DLL=%ROOT_DIR%cores\genesis.dll" & set "CORE_NAME=Mega Drive (Genesis Plus GX)" & exit /b 0 )
if /i "!EXT!"=="gba" ( set "CORE_DLL=%ROOT_DIR%cores\gba.dll" & set "CORE_NAME=Game Boy Advance (mGBA)" & exit /b 0 )
if /i "!EXT!"=="gb"  ( set "CORE_DLL=%ROOT_DIR%cores\gb.dll" & set "CORE_NAME=Game Boy (Gambatte)" & exit /b 0 )
if /i "!EXT!"=="gbc" ( set "CORE_DLL=%ROOT_DIR%cores\gb.dll" & set "CORE_NAME=Game Boy Color (Gambatte)" & exit /b 0 )
if /i "!EXT!"=="pce" ( set "CORE_DLL=%ROOT_DIR%cores\pce.dll" & set "CORE_NAME=PC Engine (Beetle PCE)" & exit /b 0 )
if /i "!EXT!"=="a26" ( set "CORE_DLL=%ROOT_DIR%cores\atari2600.dll" & set "CORE_NAME=Atari 2600 (Stella)" & exit /b 0 )
if /i "!EXT!"=="a78" ( set "CORE_DLL=%ROOT_DIR%cores\atari7800.dll" & set "CORE_NAME=Atari 7800 (ProSystem)" & exit /b 0 )
if /i "!EXT!"=="col" ( set "CORE_DLL=%ROOT_DIR%cores\coleco.dll" & set "CORE_NAME=ColecoVision (Gearcoleco)" & exit /b 0 )
if /i "!EXT!"=="ws"  ( set "CORE_DLL=%ROOT_DIR%cores\wswan.dll" & set "CORE_NAME=WonderSwan (Mednafen)" & exit /b 0 )
if /i "!EXT!"=="wsc" ( set "CORE_DLL=%ROOT_DIR%cores\wswan.dll" & set "CORE_NAME=WonderSwan Color (Mednafen)" & exit /b 0 )
if /i "!EXT!"=="ngp" ( set "CORE_DLL=%ROOT_DIR%cores\ngp.dll" & set "CORE_NAME=Neo Geo Pocket (Beetle NeoPop)" & exit /b 0 )
if /i "!EXT!"=="ngc" ( set "CORE_DLL=%ROOT_DIR%cores\ngp.dll" & set "CORE_NAME=Neo Geo Pocket Color (Beetle NeoPop)" & exit /b 0 )
if /i "!EXT!"=="chd" ( set "CORE_DLL=%ROOT_DIR%cores\psx.dll" & set "CORE_NAME=PlayStation (SwanStation)" & exit /b 0 )
if /i "!EXT!"=="cue" ( set "CORE_DLL=%ROOT_DIR%cores\psx.dll" & set "CORE_NAME=PlayStation (SwanStation)" & exit /b 0 )

rem Padrao de fallback
set "CORE_DLL=%ROOT_DIR%cores\snes.dll"
set "CORE_NAME=SNES (Padrao)"
exit /b 0

