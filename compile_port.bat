@echo off
setlocal

cls

rem Build from wherever this file lives, so the project can be moved or cloned
rem anywhere instead of being pinned to c:\SaborMister.
cd /d "%~dp0"

call "%~dp0setup_msvc.bat"
if errorlevel 1 exit /b 1

if not exist build mkdir build
if not exist app mkdir app

set APP_BASE=MiSTer_4_ALL
for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set APP_EXE=%APP_BASE%_v%APP_VER%.exe

rem Close any running instances so the linker does not fail with LNK1104 (file in use)
taskkill /F /IM "%APP_EXE%" >nul 2>&1
taskkill /F /IM "%APP_BASE%.exe" >nul 2>&1
taskkill /F /IM "MiSTer_Flavor*.exe" >nul 2>&1

rc.exe /nologo /I include /fobuild\resource.res src\resource.rc

rem libchdr on its own, at /W0. It is vendored third-party code we do not edit,
rem and at /W3 it emits ~30 conversion warnings that scroll our own off screen.
echo [libchdr]
cl.exe /nologo /c /MT /O2 /W0 /FS ^
    /D_CRT_SECURE_NO_WARNINGS /DZSTD_DISABLE_ASM ^
    /I third_party\libchdr\include /I third_party\libchdr ^
    /Fobuild\ ^
    third_party\libchdr\unity.c
if errorlevel 1 (
    echo [BUILD FAILED] libchdr failed to compile.
    exit /b 1
)

rem /Fobuild\ keeps the .obj files out of the source tree.
rem /MT statically links the C/C++ runtime so the exe runs on any Windows machine with 0 dependencies.
rem /std:c++20 enables modern C++20 standard with concepts, spans and optimizer enhancements.
rem /Oi enables compiler intrinsic functions (SSE2/AVX vectorization).
rem /Ot favors maximum CPU execution speed.
rem /fp:fast enables high-performance floating-point math for CRT shaders and resamplers.

cl.exe /nologo /MT /O2 /Oi /Ot /fp:fast /FS /W3 /std:c++20 /EHsc /Zi ^
    /I include ^
    /I third_party\rcheevos\include ^
    /I third_party\rcheevos\src ^
    /I third_party\libchdr\include ^
    /I third_party\libchdr ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /DRC_CLIENT_SUPPORTS_HASH ^
    /DZSTD_DISABLE_ASM ^
    /Fobuild\ ^
    src\mister_math.cpp ^
    src\input_map.cpp ^
    src\hw_render.cpp ^
    src\chd_reader.cpp ^
    src\netplay_protocol.cpp ^
    src\charrom.cpp ^
    src\osd.cpp ^
    src\menu.cpp ^
    src\core_runner.cpp ^
    src\archive_helper.cpp ^
    src\netplay.cpp ^
    src\retroachievements.cpp ^
    src\updater.cpp ^
    src\port_runner.cpp ^
    src\main_win32.cpp ^
    third_party\rcheevos\src\*.c ^
    third_party\rcheevos\src\rapi\*.c ^
    third_party\rcheevos\src\rcheevos\*.c ^
    third_party\rcheevos\src\rhash\*.c ^
    build\resource.res ^
    build\unity.obj ^
    /link /OUT:app\%APP_EXE% ^
    user32.lib gdi32.lib winmm.lib xinput.lib ws2_32.lib winhttp.lib shell32.lib opengl32.lib dwmapi.lib ole32.lib ^
    /SUBSYSTEM:WINDOWS /DEBUG /MAP:build\%APP_BASE%.map /OPT:REF /OPT:ICF

if errorlevel 1 (
    echo.
    echo [BUILD FAILED] Compilation errors occurred.
    exit /b 1
)

echo.
echo [BUILD SUCCESS] app\%APP_EXE%
copy /Y "app\%APP_EXE%" "app\%APP_BASE%.exe" >nul 2>&1

rem Gerar version.json automatico
powershell -NoProfile -Command "$size = (Get-Item 'app\%APP_EXE%').Length; $date = (Get-Date -Format 'yyyy-MM-dd'); $json = @{ version = '%APP_VER%'; title = 'MiSTer 4 ALL v%APP_VER%'; release_date = $date; notes = 'Versao de producao MiSTer 4 ALL'; exe_url = 'https://mister4all.com/downloads/MiSTer_4_ALL.exe'; exe_size = $size; exe_sha256 = ''; zip_url = 'https://mister4all.com/downloads/MiSTer_4_ALL_v%APP_VER%_Win64.zip'; force_full_package = $false } | ConvertTo-Json -Depth 4; Set-Content -Path 'app\version.json' -Value $json -Encoding UTF8"
echo [VERSION.JSON] Gerado em app\version.json (v%APP_VER%)
echo.
echo ==================================================
echo [RUNNING AUTOMATED UNIT TESTS]
echo ==================================================
call "%~dp0run_tests.bat"
if errorlevel 1 (
    echo [BUILD FAILED] Unit tests failed.
    exit /b 1
)

exit /b 0
