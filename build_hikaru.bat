@echo off
setlocal

rem ===========================================================================
rem  build_hikaru.bat - compila o core de Sega Hikaru (Odin) para Karamelo
rem
rem  Baseado no emulador Odin de Reda Cherif-Touil (Tovarichtch) e Stefano Teso (Valkyrie).
rem  Licenca: GNU GPL-3.0
rem
rem  Este script compila o wrapper libretro em third_party\odin_libretro
rem  e gera cores\hikaru.dll e app\cores\hikaru.dll.
rem ===========================================================================

cd /d "%~dp0"

echo [HIKARU] Compilando cores/hikaru.dll via Ninja...
ninja -C third_party/odin/build hikaru
if errorlevel 1 (
    echo [FALHA] Falha na compilacao do core Sega Hikaru / Odin.
    exit /b 1
)

if not exist cores mkdir cores
if not exist app\cores mkdir app\cores

copy /y third_party\odin\build\hikaru.dll cores\hikaru.dll >nul
copy /y third_party\odin\build\hikaru.dll app\cores\hikaru.dll >nul
copy /y third_party\odin\build\share\odin\odin-games.json data\odin-games.json >nul
copy /y third_party\odin\build\share\odin\odin-games.json app\odin-games.json >nul
copy /y third_party\odin\build\share\odin\odin-games.json cores\odin-games.json >nul

echo [HIKARU] Sucesso! cores/hikaru.dll e app/cores/hikaru.dll atualizados.

endlocal
