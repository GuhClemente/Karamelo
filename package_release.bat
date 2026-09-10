@echo off
setlocal enabledelayedexpansion

cls
echo =======================================================
echo   Karamelo v1.0 - Packaging ^& Release Generator
echo =======================================================
echo.

cd /d "%~dp0"

if "%~1" neq "" (
    echo [0/4] Configurando versao de release para %~1...
    powershell -NoProfile -ExecutionPolicy Bypass -File .\bump_version.ps1 %~1
    echo.
)

for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set APP_BASE=Karamelo
set APP_EXE=%APP_BASE%_v%APP_VER%.exe
set DIST_NAME=%APP_BASE%_v%APP_VER%_Win64
set DIST_DIR=%~dp0dist\%DIST_NAME%

echo =======================================================
echo   Karamelo v%APP_VER% - Packaging ^& Release Generator
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
mkdir "%DIST_DIR%\ports"
copy /Y "%~dp0packaging\ports-guide\LEIA-ME.txt" "%DIST_DIR%\ports\LEIA-ME.txt" >nul

rem ---------------------------------------------------------------
rem Create BIOS directory with the real, per-system, MD5-verified guide
rem (packaging\bios-guide\BIOS_NECESSARIOS.txt is the tracked source of
rem truth - edit it there, not here, next time a core's BIOS requirements
rem change. It lives outside bios/roms because .gitignore blanket-ignores
rem any directory literally named "bios" or "roms" anywhere in the repo.)
rem ---------------------------------------------------------------
mkdir "%DIST_DIR%\bios"
copy /Y "%~dp0packaging\bios-guide\BIOS_NECESSARIOS.txt" "%DIST_DIR%\bios\BIOS_NECESSARIOS.txt" >nul
if exist "%~dp0docs\GUIA_COMPLETO_BIOS.md" copy /Y "%~dp0docs\GUIA_COMPLETO_BIOS.md" "%DIST_DIR%\bios\GUIA_COMPLETO_BIOS.md" >nul

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
copy /Y "app\%APP_EXE%" "%DIST_DIR%\Karamelo.exe" >nul
copy /Y "app\cores\*.dll" "%DIST_DIR%\cores\" >nul 2>&1
rem So o .raw entra no pacote. O app le exclusivamente .raw (ScanCustomWallpapers
rem em src/menu.cpp aceita essa extensao e mais nenhuma), entao os .jpg de origem
rem sao 86 MB que ninguem abre - tres vezes o peso dos cores mortos que sairam
rem nesta mesma versao. Eles continuam versionados no repositorio, que e onde
rem servem para alguma coisa: gerar o .raw de novo se a resolucao mudar.
copy /Y "app\Wallpapers\*.raw" "%DIST_DIR%\Wallpapers\" >nul 2>&1

rem Optional: compress the distributed exe with UPX (open source, MIT
rem license - https://upx.github.io), only our own exe, never the third-
rem party core DLLs (see the audit discussion on why those shouldn't be
rem touched). This is compression, not real anti-reverse-engineering - UPX
rem is trivially reversed with "upx -d" - but it's free, well-known, and
rem does shrink the download. Entirely optional: skips itself cleanly if
rem upx.exe isn't on PATH, no install attempted here.
where upx >nul 2>&1
if not errorlevel 1 (
    echo    Comprimindo Karamelo.exe com UPX...
    upx --best --lzma "%DIST_DIR%\Karamelo.exe" >nul
) else (
    echo    UPX nao encontrado no PATH - pulando compressao ^(opcional^).
    echo    Baixe em https://upx.github.io se quiser essa etapa ativa.
)

rem Safety verification: ensure no copyrighted rom/bios files were copied into roms or cores
del /s /q "%ROMS_DIR%\*.bin" "%ROMS_DIR%\*.iso" "%ROMS_DIR%\*.cue" "%ROMS_DIR%\*.chd" "%ROMS_DIR%\*.nes" "%ROMS_DIR%\*.sfc" "%ROMS_DIR%\*.smc" "%ROMS_DIR%\*.md" "%ROMS_DIR%\*.gen" "%ROMS_DIR%\*.z64" "%ROMS_DIR%\*.n64" "%ROMS_DIR%\*.gba" "%ROMS_DIR%\*.gb" "%ROMS_DIR%\*.gbc" "%ROMS_DIR%\*.nds" "%ROMS_DIR%\*.3ds" "%ROMS_DIR%\*.gcm" "%ROMS_DIR%\*.cso" "%ROMS_DIR%\*.pbp" >nul 2>&1
del /s /q "%DIST_DIR%\cores\*.bin" "%DIST_DIR%\cores\*.iso" >nul 2>&1

rem Create README with controls & user guides
copy /Y "%~dp0packaging\release-guide\LEIAME.txt" "%DIST_DIR%\LEIAME.txt" >nul
rem zlib (SDL3), BSD-3 (libchdr) e MIT (rcheevos, miniz) exigem que o aviso de
rem copyright acompanhe a redistribuicao binaria - sem estes dois arquivos o
rem pacote fica irregular.
copy /Y "%~dp0LICENSE.md" "%DIST_DIR%\LICENSE.md" >nul
copy /Y "%~dp0THIRD-PARTY-NOTICES.md" "%DIST_DIR%\THIRD-PARTY-NOTICES.md" >nul
copy /Y "%~dp0packaging\ports-guide\LEIA-ME.txt" "%DIST_DIR%\ports\LEIA-ME.txt" >nul
copy /Y "%~dp0packaging\bios-guide\BIOS_NECESSARIOS.txt" "%DIST_DIR%\bios\BIOS_NECESSARIOS.txt" >nul
if exist "%~dp0docs\GUIA_COMPLETO_BIOS.md" (
    copy /Y "%~dp0docs\GUIA_COMPLETO_BIOS.md" "%DIST_DIR%\bios\GUIA_COMPLETO_BIOS.md" >nul
    copy /Y "%~dp0docs\GUIA_COMPLETO_BIOS.md" "%DIST_DIR%\bios\GUIA_COMPLETO_BIOS.txt" >nul
)

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
copy /Y "app\%APP_EXE%" "dist\Karamelo.exe" >nul
powershell -NoProfile -Command "$exe = Get-Item 'dist\Karamelo.exe'; $exeHash = (Get-FileHash $exe.FullName -Algorithm SHA256).Hash; $date = (Get-Date -Format 'yyyy-MM-dd'); $json = @{ version = '%APP_VER%'; title = 'Karamelo v%APP_VER%'; release_date = $date; notes = 'Lancamento oficial do Karamelo com 35 sistemas nativos e Auto-Update.'; exe_url = 'https://karamelo-emu.com/downloads/Karamelo.exe'; exe_size = $exe.Length; exe_sha256 = $exeHash; zip_url = 'https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Win64.zip'; force_full_package = $false } | ConvertTo-Json -Depth 4; Set-Content -Path 'dist\version.json' -Value $json -Encoding UTF8"

echo   Updater: dist\Karamelo.exe
echo   Manifest: dist\version.json
echo.
echo   [LEMBRETE] dist\version.json e versionado de proposito: e a copia que
echo   o auto-update le no GitHub quando karamelo-emu.com nao responde.
echo   Sem commit + push dele, o fallback anuncia a versao anterior:
echo       git add dist/version.json ^&^& git commit -m "release: v%APP_VER%" ^&^& git push
echo =======================================================
echo.
