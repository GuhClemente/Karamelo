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

rem SDL3, vendored as source under third_party\SDL3 (dev-sdl3 migration branch) and
rem built once as a static lib into build\sdl3\ - cached there across runs since
rem compile_port.bat never wipes build\, same as every .obj file below. Building
rem it fresh takes a couple of minutes (270+ translation units); linking against
rem the cached SDL3-static.lib afterward costs nothing extra.
if not exist build\sdl3\SDL3-static.lib (
    echo [SDL3] Nenhum build em cache - configurando e compilando do source ^(so na primeira vez^)...
    cmake -S third_party\SDL3 -B build\sdl3 -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
        -DSDL_STATIC=ON ^
        -DSDL_SHARED=OFF ^
        -DSDL_TEST_LIBRARY=OFF ^
        -DSDL_EXAMPLES=OFF ^
        -DSDL_DISABLE_INSTALL=ON ^
        -DSDL_DISABLE_INSTALL_DOCS=ON
    if errorlevel 1 (
        echo [BUILD FAILED] SDL3 cmake configure falhou.
        exit /b 1
    )
    cmake --build build\sdl3 --config Release
    if errorlevel 1 (
        echo [BUILD FAILED] SDL3 falhou ao compilar.
        exit /b 1
    )
)

rem /Fobuild\ keeps the .obj files out of the source tree.
rem /MT statically links the C/C++ runtime so the exe runs on any Windows machine with 0 dependencies.
rem /std:c++20 enables modern C++20 standard with concepts, spans and optimizer enhancements.
rem /Oi enables compiler intrinsic functions (SSE2/AVX vectorization).
rem /Ot favors maximum CPU execution speed.
rem /fp:fast enables high-performance floating-point math for CRT shaders and resamplers.
rem /GR- disables RTTI - nothing in this codebase uses dynamic_cast/typeid (confirmed by
rem grep before adding this), so this is free: it drops the type-name strings and vtable
rem metadata RTTI would otherwise leave sitting in the binary for anyone to read.
rem /guard:cf (paired with /GUARD:CF at link) adds Control Flow Guard instrumentation -
rem free hardening against control-flow-hijacking exploits, no separate tool required.

cl.exe /nologo /MT /O2 /Oi /Ot /fp:fast /FS /W3 /std:c++20 /EHsc /GR- /guard:cf /Zi ^
    /I include ^
    /I third_party\rcheevos\include ^
    /I third_party\rcheevos\src ^
    /I third_party\libchdr\include ^
    /I third_party\libchdr ^
    /I third_party\SDL3\include ^
    /I third_party\Vulkan-Headers\include ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /DRC_CLIENT_SUPPORTS_HASH ^
    /DZSTD_DISABLE_ASM ^
    /Fobuild\ ^
    src\mister_math.cpp ^
    src\input_map.cpp ^
    src\gamepad_sdl.cpp ^
    src\hw_render.cpp ^
    src\hw_render_vulkan.cpp ^
    src\hw_render_d3d11.cpp ^
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
    build\sdl3\SDL3-static.lib ^
    /link /OUT:app\%APP_EXE% ^
    user32.lib gdi32.lib winmm.lib xinput.lib ws2_32.lib winhttp.lib shell32.lib opengl32.lib dwmapi.lib ole32.lib ^
    kernel32.lib imm32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib dinput8.lib ^
    d3d11.lib d3dcompiler.lib ^
    /SUBSYSTEM:WINDOWS /DEBUG /PDBALTPATH:%%_PDB%% /GUARD:CF /MAP:build\%APP_BASE%.map /OPT:REF /OPT:ICF

if errorlevel 1 (
    echo.
    echo [BUILD FAILED] Compilation errors occurred.
    exit /b 1
)

echo.
echo [BUILD SUCCESS] app\%APP_EXE%
copy /Y "app\%APP_EXE%" "app\%APP_BASE%.exe" >nul 2>&1
if not exist app\cores mkdir app\cores
if exist cores\*.dll copy /Y cores\*.dll app\cores\ >nul 2>&1

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
