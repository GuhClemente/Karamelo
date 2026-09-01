@echo off
setlocal

rem Dev-only build for the gameplay/preview-media recorder. Not part of the
rem shipped app - links the same core engine (core_runner.cpp and the pieces
rem it needs) plus headless_stubs.cpp instead of the real menu/netplay/osd/
rem retroachievements subsystems, since none of those affect a recording.

cd /d "%~dp0..\.."

call "%~dp0..\..\setup_msvc.bat"
if errorlevel 1 exit /b 1

if not exist build mkdir build
if not exist tools\record_gameplay\out mkdir tools\record_gameplay\out

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

cl.exe /nologo /MT /O2 /Oi /Ot /fp:fast /FS /W3 /std:c++20 /EHsc /GR- ^
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
    src\core_runner.cpp ^
    src\archive_helper.cpp ^
    tools\record_gameplay\headless_stubs.cpp ^
    tools\record_gameplay\record_gameplay.cpp ^
    third_party\rcheevos\src\*.c ^
    third_party\rcheevos\src\rapi\*.c ^
    third_party\rcheevos\src\rcheevos\*.c ^
    third_party\rcheevos\src\rhash\*.c ^
    build\unity.obj ^
    /link /OUT:tools\record_gameplay\out\record_gameplay.exe ^
    user32.lib gdi32.lib winmm.lib xinput.lib ws2_32.lib winhttp.lib shell32.lib opengl32.lib ole32.lib ^
    /SUBSYSTEM:CONSOLE

if errorlevel 1 (
    echo.
    echo [BUILD FAILED] Compilation errors occurred.
    exit /b 1
)

echo.
echo [BUILD SUCCESS] tools\record_gameplay\out\record_gameplay.exe
