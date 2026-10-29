@echo off
setlocal

cd /d "%~dp0"

call "%~dp0setup_msvc.bat"
if errorlevel 1 exit /b 1

if not exist build mkdir build
if not exist build\tests mkdir build\tests

rem Production sources are linked in on purpose. The suite used to compile only
rem tests\*.cpp and every test kept its own private copy of the logic, so the
rem whole thing was green while testing code that never shipped.
echo [BUILDING UNIT TESTS]...
rem lzxd.c is third-party: /W0 on its own, as in compile.bat. Everything
rem else is ours and builds with /WX - a warning fails the build.
cl.exe /nologo /c /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /I third_party\libmspack /Fobuild\tests\ third_party\libmspack\lzxd.c
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
cl.exe /nologo /O2 /Oi /Ot /fp:fast /W3 /WX /std:c++20 /EHsc ^
    /I include ^
    /I tests ^
    /I third_party\libmspack ^
    /I third_party\SDL3\include ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /Fobuild\ ^
    tests\test_main.cpp ^
    tests\test_config.cpp ^
    tests\test_archive.cpp ^
    tests\test_netplay.cpp ^
    tests\test_math_video.cpp ^
    tests\test_updater.cpp ^
    tests\test_crash_reporter.cpp ^
    tests\test_x360_dump.cpp ^
    tests\test_port_catalog.cpp ^
    tests\test_input_mouse.cpp ^
    tests\test_cloud_live.cpp ^
    src\karamelo_math.cpp ^
    src\netplay_protocol.cpp ^
    src\archive_helper.cpp ^
    src\updater.cpp ^
    src\crash_reporter.cpp ^
    src\x360_dump.cpp ^
    src\port_catalog.cpp ^
    src\json_mini.cpp ^
    src\input_map.cpp ^
    src\gamepad_sdl.cpp ^
    src\charrom.cpp ^
    src\cloud_sync.cpp ^
    src\web_admin.cpp ^
    src\voice_chat.cpp ^
    src\tray_manager.cpp ^
    src\desktop_chat.cpp ^
    build\tests\lzxd.obj ^
    build\sdl3\SDL3-static.lib ^
    /link /OUT:build\karamelo_tests.exe /SUBSYSTEM:CONSOLE winhttp.lib ws2_32.lib crypt32.lib shell32.lib user32.lib gdi32.lib dwmapi.lib winmm.lib bcrypt.lib ole32.lib oleaut32.lib imm32.lib setupapi.lib version.lib advapi32.lib

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Unit test compilation failed.
    exit /b %ERRORLEVEL%
)

echo.
echo [RUNNING UNIT TESTS]...
build\karamelo_tests.exe
exit /b %ERRORLEVEL%
