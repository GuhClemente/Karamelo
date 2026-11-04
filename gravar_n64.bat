@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

rem ============================================================================
rem  Karamelo Emulador - Gravador de Gameplay Padronizado de Nintendo 64
rem  Aceleração por Hardware NVIDIA NVENC + Áudio Estúdio 48.000 Hz
rem ============================================================================

set "ROOT_DIR=%~dp0"
set "RECORDER=%ROOT_DIR%tools\record_gameplay\out\record_gameplay.exe"
set "OUT_DEFAULT=%ROOT_DIR%videos_comparativo_n64"

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
rem  gravar_n64.bat <ROM> [DURACAO_SEG] [CORE: 1=parallel|2=mupen|3=gopher] [UPSCALE: 0|1|2] [NOME_SAIDA] [MACRO]
rem ----------------------------------------------------------------------------
if not "%~1"=="" (
    set "ROM_PATH=%~1"
    set "DURATION=%~2"
    if "!DURATION!"=="" set "DURATION=90"

    set "CORE_CHOICE=%~3"
    if "!CORE_CHOICE!"=="" set "CORE_CHOICE=1"
    if "!CORE_CHOICE!"=="1" set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll"
    if "!CORE_CHOICE!"=="parallel" set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll"
    if "!CORE_CHOICE!"=="2" set "CORE_DLL=%ROOT_DIR%cores\n64_mupen.dll"
    if "!CORE_CHOICE!"=="mupen" set "CORE_DLL=%ROOT_DIR%cores\n64_mupen.dll"
    if "!CORE_CHOICE!"=="3" set "CORE_DLL=%ROOT_DIR%cores\n64_gopher.dll"
    if "!CORE_CHOICE!"=="gopher" set "CORE_DLL=%ROOT_DIR%cores\n64_gopher.dll"
    if not exist "!CORE_DLL!" set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll"

    set "UPSCALE=%~4"
    if "!UPSCALE!"=="" set "UPSCALE=0"

    set "BASE_NAME=%~5"
    if "!BASE_NAME!"=="" (
        for %%F in ("!ROM_PATH!") do set "BASE_NAME=%%~nF"
    )

    set "MACRO_SPEC=%~6"
    if "!MACRO_SPEC!"=="" set "MACRO_SPEC=none"

    set "OUT_DIR=%OUT_DEFAULT%"
    goto :EXECUTE
)

rem ----------------------------------------------------------------------------
rem  Modo Wizard Interativo
rem ----------------------------------------------------------------------------
cls
echo ============================================================================
echo   🎬 KARAMELO - GRAVADOR DE GAMEPLAY NINTENDO 64 (NVENC + 48 kHz)
echo ============================================================================
echo.

echo [1/5] Selecione a ROM de Nintendo 64:
echo ----------------------------------------------------------------------------
echo Digite o caminho da ROM, arraste e solte o arquivo aqui, ou escolha da lista:
echo.

set idx=0
for %%f in ("%ROOT_DIR%roms\Nintendo64\*.z64" "%ROOT_DIR%roms\Nintendo64\*.n64" "%ROOT_DIR%roms\Nintendo64\*.v64") do (
    set /a idx+=1
    set "ROM_ITEM_!idx!=%%f"
    echo   [!idx!] %%~nxf
)

echo.
set /p "USER_INPUT=Escolha um numero [1-%idx%] ou arraste uma ROM: "
set "USER_INPUT=%USER_INPUT:"=%"

if defined ROM_ITEM_%USER_INPUT% (
    set "ROM_PATH=!ROM_ITEM_%USER_INPUT%!"
) else if exist "%USER_INPUT%" (
    set "ROM_PATH=%USER_INPUT%"
) else (
    echo [ERRO] ROM nao encontrada: %USER_INPUT%
    pause
    exit /b 1
)

for %%F in ("!ROM_PATH!") do set "AUTO_NAME=%%~nF"
echo.
echo ROM Selecionada: !ROM_PATH!
echo.

echo [2/5] Escolha o Motor / Core de Emulacao:
echo ----------------------------------------------------------------------------
echo   [1] ParaLLEl N64 (Dynarec + Angrylion/Glide64) [Padrao]
echo   [2] Mupen64Plus-Next (GLideN64 Nativo)
echo   [3] Gopher64 (Vulkan Parallel-RDP com Upscale Horizontal)
echo.
set /p "CORE_SELECT=Opcao [1-3, padrao 1]: "
if "%CORE_SELECT%"=="2" (
    set "CORE_DLL=%ROOT_DIR%cores\n64_mupen.dll"
    set "CORE_TAG=Mupen"
) else if "%CORE_SELECT%"=="3" (
    set "CORE_DLL=%ROOT_DIR%cores\n64_gopher.dll"
    set "CORE_TAG=Gopher"
) else (
    set "CORE_DLL=%ROOT_DIR%cores\n64_parallel.dll"
    set "CORE_TAG=Parallel"
)

echo.
echo [3/5] Upscaling Horizontal (especifico para Gopher64):
echo ----------------------------------------------------------------------------
echo   [0] 1x Desligado (320x240 nativo) [Padrao]
echo   [1] 2x Horizontal (640x240)
echo   [2] 4x Ultra Horizontal (1280x240)
echo.
set /p "UPSCALE=Opcao [0-2, padrao 0]: "
if "%UPSCALE%"=="" set "UPSCALE=0"
if "%UPSCALE%"=="1" set "CORE_TAG=!CORE_TAG!_2x_640"
if "%UPSCALE%"=="2" set "CORE_TAG=!CORE_TAG!_4x_1280"

echo.
echo [4/5] Duracao da Gravacao (segundos):
echo ----------------------------------------------------------------------------
set /p "DURATION=Duracao em segundos [padrao 90]: "
if "%DURATION%"=="" set "DURATION=90"

echo.
echo [5/5] Comandos de Botoes / Macro Cronometrada (Opcional):
echo ----------------------------------------------------------------------------
echo Digite os segundos e botoes a pressionar para passar telas/menus.
echo Exemplos:
echo   2-5:A           (Pressiona A entre 2s e 5s)
echo   2-5:A;8-10:START (Pressiona A de 2-5s e START de 8-10s)
echo   Deixe em branco para usar a liberacao automatica padrao.
echo.
set /p "MACRO_SPEC=Comandos de botoes [Enter para padrao]: "
if "%MACRO_SPEC%"=="" set "MACRO_SPEC=none"

set "OUT_DIR=%OUT_DEFAULT%"
set "BASE_NAME=!AUTO_NAME!_!CORE_TAG!"

:EXECUTE
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

cls
echo ============================================================================
echo   INICIANDO GRAVACAO DE NINTENDO 64
echo ============================================================================
echo  ROM:      %ROM_PATH%
echo  Core:     %CORE_DLL%
echo  Duracao:  %DURATION% segundos
echo  Upscale:  %UPSCALE%
echo  Macro:    %MACRO_SPEC%
echo  Saida:    %OUT_DIR%\%BASE_NAME%.mp4
echo ============================================================================
echo.

"%RECORDER%" "%CORE_DLL%" "%ROM_PATH%" %DURATION% "%OUT_DIR%" "%BASE_NAME%" %UPSCALE% 0 1 1 "%MACRO_SPEC%"

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

