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

set APP_BASE=MiSTer_Flavor
set APP_VER=v2.0
set APP_EXE=%APP_BASE%_%APP_VER%.exe

rem Close any running instances so the linker does not fail with LNK1104 (file in use)
taskkill /F /IM "%APP_EXE%" >nul 2>&1
taskkill /F /IM "%APP_BASE%.exe" >nul 2>&1

rc.exe /nologo /I include /fobuild\resource.res src\resource.rc

rem libchdr on its own, at /W0. It is vendored third-party code we do not edit,
rem and at /W3 it emits ~30 conversion warnings that scroll our own off screen.
echo [libchdr]
cl.exe /nologo /c /O2 /W0 /FS ^
    /D_CRT_SECURE_NO_WARNINGS /DZSTD_DISABLE_ASM ^
    /I third_party\libchdr\include /I third_party\libchdr ^
    /Fobuild\ ^
    third_party\libchdr\unity.c
if errorlevel 1 (
    echo [BUILD FAILED] libchdr failed to compile.
    exit /b 1
)

rem /Fobuild\ keeps the .obj files out of the source tree.
rem /std:c++20 enables modern C++20 standard with concepts, spans and optimizer enhancements.
rem /Oi enables compiler intrinsic functions (SSE2/AVX vectorization).
rem /Ot favors maximum CPU execution speed.
rem /fp:fast enables high-performance floating-point math for CRT shaders and resamplers.

cl.exe /nologo /O2 /Oi /Ot /fp:fast /FS /W3 /std:c++20 /EHsc /Zi ^
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
    src\main_win32.cpp ^
    third_party\rcheevos\src\*.c ^
    third_party\rcheevos\src\rapi\*.c ^
    third_party\rcheevos\src\rcheevos\*.c ^
    third_party\rcheevos\src\rhash\*.c ^
    build\resource.res ^
    build\unity.obj ^
    /link /OUT:app\%APP_EXE% ^
    user32.lib gdi32.lib winmm.lib xinput.lib ws2_32.lib winhttp.lib opengl32.lib dwmapi.lib ^
    /SUBSYSTEM:WINDOWS /DEBUG /MAP:build\%APP_BASE%.map /OPT:REF /OPT:ICF

if %ERRORLEVEL% equ 0 (
    echo.
    echo [BUILD SUCCESS] app\%APP_EXE%
    echo.
    echo ==================================================
    echo [RUNNING AUTOMATED UNIT TESTS]
    echo ==================================================
    rem Delegate instead of keeping a second copy of the recipe. The copy that
    rem lived here had gone stale: it never linked the production sources the
    rem tests call, so it failed to build while run_tests.bat passed 17/17.
    rem Absolute path: vcvars64.bat changes the working directory, so a bare
    rem name is not found by the time we get here.
    call "%~dp0run_tests.bat"
    if errorlevel 1 (
        echo [BUILD FAILED] Unit tests failed.
        exit /b 1
    )
) else (
    echo.
    echo [BUILD FAILED] Compilation errors occurred.
)

exit /b %ERRORLEVEL%
