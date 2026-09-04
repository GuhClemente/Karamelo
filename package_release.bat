@echo off
setlocal enabledelayedexpansion

cls
echo =======================================================
echo   MiSTer 4 ALL v1.0 - Packaging ^& Release Generator
echo =======================================================
echo.

cd /d "%~dp0"

if "%~1" neq "" (
    echo [0/4] Configurando versao de release para %~1...
    powershell -NoProfile -ExecutionPolicy Bypass -File .\bump_version.ps1 %~1
    echo.
)

for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set APP_BASE=MiSTer_4_ALL
set APP_EXE=%APP_BASE%_v%APP_VER%.exe
set DIST_NAME=%APP_BASE%_v%APP_VER%_Win64
set DIST_DIR=%~dp0dist\%DIST_NAME%

echo =======================================================
echo   MiSTer 4 ALL v%APP_VER% - Packaging ^& Release Generator
echo =======================================================
echo.

rem 1. Compile clean release binary
echo [1/4] Compilando executavel de producao com /MT (Static CRT)...
call "%~dp0compile_port.bat"
if errorlevel 1 (
    echo [ERRO] Falha na compilacao. O pacote nao pode ser criado.
    exit /b 1
)

rem 2. Prepare distribution directories

echo.
echo [2/4] Estruturando pacote de distribuicao em dist\%DIST_NAME%...
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\cores"
mkdir "%DIST_DIR%\Wallpapers"
mkdir "%DIST_DIR%\saves"
mkdir "%DIST_DIR%\screenshots"
mkdir "%DIST_DIR%\Config"

rem ---------------------------------------------------------------
rem Create BIOS directory with the real, per-system, MD5-verified guide
rem (packaging\bios-guide\BIOS_NECESSARIOS.txt is the tracked source of
rem truth - edit it there, not here, next time a core's BIOS requirements
rem change. It lives outside bios/roms because .gitignore blanket-ignores
rem any directory literally named "bios" or "roms" anywhere in the repo.)
rem ---------------------------------------------------------------
mkdir "%DIST_DIR%\bios"
copy /Y "%~dp0packaging\bios-guide\BIOS_NECESSARIOS.txt" "%DIST_DIR%\bios\BIOS_NECESSARIOS.txt" >nul

rem ---------------------------------------------------------------
rem Create all 35 ROM system folders with user instructions
rem ---------------------------------------------------------------
set ROMS_DIR=%DIST_DIR%\roms
mkdir "%ROMS_DIR%"
for %%S in (
    Arcade
    Atari2600 Atari5200 Atari7800 Jaguar Lynx
    NES SNES Nintendo64 GameBoy GBA NDS 3DS GameCube
    MasterSystem Genesis MegaCD 32X Saturn Dreamcast
    PlayStation PlayStation2 PSP
    NeoGeo NGP
    TurboGrafx16 PCFX
    3DO ColecoVision WonderSwan
    DOS MSX Amiga C64 ZXSpectrum
) do (
    mkdir "%ROMS_DIR%\%%S"
    copy /Y "%~dp0packaging\rom-folder-guides\%%S\LEIA-ME.txt" "%ROMS_DIR%\%%S\LEIA-ME.txt" >nul 2>&1
)

rem 3. Copy binaries and assets (Engines and wallpapers only - NO ROMs or BIOSes)
echo.
echo [3/4] Copiando executavel unico, motores e recursos...
copy /Y "app\%APP_EXE%" "%DIST_DIR%\MiSTer_4_ALL.exe" >nul
copy /Y "app\cores\*.dll" "%DIST_DIR%\cores\" >nul 2>&1
copy /Y "app\Wallpapers\*.*" "%DIST_DIR%\Wallpapers\" >nul 2>&1

rem Optional: compress the distributed exe with UPX (open source, MIT
rem license - https://upx.github.io), only our own exe, never the third-
rem party core DLLs (see the audit discussion on why those shouldn't be
rem touched). This is compression, not real anti-reverse-engineering - UPX
rem is trivially reversed with "upx -d" - but it's free, well-known, and
rem does shrink the download. Entirely optional: skips itself cleanly if
rem upx.exe isn't on PATH, no install attempted here.
where upx >nul 2>&1
if not errorlevel 1 (
    echo    Comprimindo MiSTer_4_ALL.exe com UPX...
    upx --best --lzma "%DIST_DIR%\MiSTer_4_ALL.exe" >nul
) else (
    echo    UPX nao encontrado no PATH - pulando compressao ^(opcional^).
    echo    Baixe em https://upx.github.io se quiser essa etapa ativa.
)

rem Safety verification: ensure no copyrighted rom/bios files were copied
del /s /q "%DIST_DIR%\*.bin" "%DIST_DIR%\*.iso" "%DIST_DIR%\*.cue" "%DIST_DIR%\*.chd" "%DIST_DIR%\*.nes" "%DIST_DIR%\*.sfc" "%DIST_DIR%\*.smc" "%DIST_DIR%\*.md" "%DIST_DIR%\*.gen" "%DIST_DIR%\*.z64" "%DIST_DIR%\*.n64" "%DIST_DIR%\*.gba" "%DIST_DIR%\*.gb" "%DIST_DIR%\*.gbc" "%DIST_DIR%\*.nds" "%DIST_DIR%\*.3ds" "%DIST_DIR%\*.gcm" "%DIST_DIR%\*.cso" "%DIST_DIR%\*.pbp" >nul 2>&1

rem Create README with controls & guide
(
echo =====================================================================
echo   MiSTer 4 ALL v%APP_VER% - Windows x64 (C++20 Nativo)
echo   Site: https://mister4all.com ^| YouTube: @GuhClemente
echo =====================================================================
echo.
echo 1. COMO JOGAR:
echo    - Coloque suas ROMs/ISOs na pasta correspondente dentro de "roms\"
echo      (ex: roms\SNES, roms\Genesis, roms\PlayStation, etc.)
echo    - Execute "MiSTer_4_ALL.exe"
echo    - Use as setas do teclado ou o controle XInput para navegar e jogar
echo.
echo 2. CONTROLES PADRAO NO TECLADO:
echo    - Abrir / Fechar Menu OSD: F1 ou ESC
echo    - Navegar: Setas Direcionais
echo    - Confirmar / Entrar: ENTER ou TECLA Z
echo    - Cancelar / Voltar: BACKSPACE ou TECLA X
echo    - Teclado remapeavel em: Settings ^> Controller
echo.
echo 3. CONTROLE GAMEPAD (XINPUT / XBOX / PLAYSTATION):
echo    - Plug ^& Play automatico ao conectar.
echo    - Abrir Menu: BOTAO GUIDE / HOME ou SELECT + START
echo    - Confirmar: BOTAO A
echo    - Voltar: BOTAO B
echo.
echo 4. SISTEMAS NATIVOS DISPONIVEIS (35 Sistemas / 39 Cores):
echo    - Nintendo: NES, SNES, N64, Game Boy, GBA, NDS, 3DS, GameCube
echo    - Sega: Master System, Genesis/Mega Drive, Mega CD, 32X, Saturn, Dreamcast
echo    - Sony: PlayStation 1, PlayStation 2, PSP
echo    - SNK / Arcade: Neo Geo AES/MVS, Neo Geo CD, Neo Geo Pocket, FBNeo, MAME
echo    - Atari: 2600, 5200, 7800, Jaguar, Lynx
echo    - Computadores / Outros: DOSBox Pure, Amiga, C64, MSX, ZX Spectrum,
echo      PC Engine / TG-16, PC-FX, 3DO, ColecoVision, WonderSwan.
echo.
echo 5. AVISO LEGAL / LEGAL DISCLAIMER:
echo    - O MiSTer 4 ALL e um projeto de preservacao e codigo aberto (Open Source).
echo    - Esta distribuicao NAO CONTEM nenhum arquivo de BIOS protegida ou ROM de jogo.
echo    - O usuario deve utilizar seus proprios backups de jogos e BIOS legalmente adquiridos.
echo.
echo Desenvolvido com paixao para a comunidade retro gaming.
) > "%DIST_DIR%\LEIAME.txt"

rem 4. Create ZIP package
echo.
echo [4/4] Compactando pacote final em dist\%DIST_NAME%.zip...
powershell -NoProfile -Command "Compress-Archive -Path '%DIST_DIR%' -DestinationPath '%~dp0dist\%DIST_NAME%.zip' -Force"

echo.
echo =======================================================
echo   PACOTE GERADO COM SUCESSO
echo =======================================================
echo   Arquivo: dist\%DIST_NAME%.zip
echo.
powershell -NoProfile -Command "$f = Get-Item '%~dp0dist\%DIST_NAME%.zip'; Write-Host ('   Tamanho: ' + [math]::Round($f.Length / 1MB, 2) + ' MB (' + $f.Length + ' bytes)'); $hash = Get-FileHash $f.FullName -Algorithm SHA256; Write-Host ('   SHA-256: ' + $hash.Hash)"

rem 5. Prepare Standalone Executable & version.json for Auto-Updater
copy /Y "app\%APP_EXE%" "dist\MiSTer_4_ALL.exe" >nul
powershell -NoProfile -Command "$exe = Get-Item 'dist\MiSTer_4_ALL.exe'; $exeHash = (Get-FileHash $exe.FullName -Algorithm SHA256).Hash; $date = (Get-Date -Format 'yyyy-MM-dd'); $json = @{ version = '%APP_VER%'; title = 'MiSTer 4 ALL v%APP_VER%'; release_date = $date; notes = 'Lancamento oficial do MiSTer 4 ALL com 35 sistemas nativos e Auto-Update.'; exe_url = 'https://mister4all.com/downloads/MiSTer_4_ALL.exe'; exe_size = $exe.Length; exe_sha256 = $exeHash; zip_url = 'https://mister4all.com/downloads/MiSTer_4_ALL_v%APP_VER%_Win64.zip'; force_full_package = $false } | ConvertTo-Json -Depth 4; Set-Content -Path 'dist\version.json' -Value $json -Encoding UTF8"

echo   Updater: dist\MiSTer_4_ALL.exe
echo   Manifest: dist\version.json
echo =======================================================
echo.
