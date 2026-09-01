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
cl.exe /nologo /O2 /Oi /Ot /fp:fast /W3 /std:c++20 /EHsc ^
    /I include ^
    /I tests ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /Fobuild\ ^
    tests\test_main.cpp ^
    tests\test_config.cpp ^
    tests\test_archive.cpp ^
    tests\test_netplay.cpp ^
    tests\test_math_video.cpp ^
    tests\test_updater.cpp ^
    src\mister_math.cpp ^
    src\netplay_protocol.cpp ^
    src\archive_helper.cpp ^
    src\updater.cpp ^
    /link /OUT:build\mister_tests.exe /SUBSYSTEM:CONSOLE winhttp.lib shell32.lib user32.lib

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Unit test compilation failed.
    exit /b %ERRORLEVEL%
)

echo.
echo [RUNNING UNIT TESTS]...
build\mister_tests.exe
exit /b %ERRORLEVEL%
