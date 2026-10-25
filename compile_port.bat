@echo off
setlocal

cls

rem Build from wherever this file lives, so the project can be moved or cloned
rem anywhere instead of being pinned to c:\Karamelo.
cd /d "%~dp0"

call "%~dp0setup_msvc.bat"
if errorlevel 1 exit /b 1

if not exist build mkdir build
if not exist app mkdir app

set APP_BASE=Karamelo
for /f "tokens=3" %%v in ('findstr /c:"#define APP_VERSION " include\app_info.h') do set APP_VER=%%~v
set APP_EXE=%APP_BASE%_v%APP_VER%.exe

rem Close any running instances so the linker does not fail with LNK1104 (file in use)
taskkill /F /IM "%APP_EXE%" >nul 2>&1
taskkill /F /IM "%APP_BASE%.exe" >nul 2>&1

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

rem libmspack's lzxd.c, also vendored and also at /W0: at /W3 it emits two
rem C4244 conversion warnings in code we do not edit. Our own sources build
rem with /WX below, so the build output has to be warning-free to begin with.
echo [libmspack]
cl.exe /nologo /c /MT /O2 /W0 /FS ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /I third_party\libmspack ^
    /Fobuild\ ^
    third_party\libmspack\lzxd.c
if errorlevel 1 (
    echo [BUILD FAILED] libmspack failed to compile.
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

cl.exe /nologo /MT /O2 /Oi /Ot /fp:fast /FS /W3 /WX /std:c++20 /EHsc /GR- /guard:cf /Zi ^
    /I include ^
    /I third_party\rcheevos\include ^
    /I third_party\rcheevos\src ^
    /I third_party\libchdr\include ^
    /I third_party\libchdr ^
    /I third_party\SDL3\include ^
    /I third_party\Vulkan-Headers\include ^
    /I third_party\libmspack ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /DRC_CLIENT_SUPPORTS_HASH ^
    /DZSTD_DISABLE_ASM ^
    /Fobuild\ ^
    src\karamelo_math.cpp ^
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
    src\port_catalog.cpp ^
    src\json_mini.cpp ^
    src\x360_dump.cpp ^
    build\lzxd.obj ^
    src\crash_reporter.cpp ^
    src\main_win32.cpp ^
    third_party\rcheevos\src\*.c ^
    third_party\rcheevos\src\rapi\*.c ^
    third_party\rcheevos\src\rcheevos\*.c ^
    third_party\rcheevos\src\rhash\*.c ^
    build\resource.res ^
    build\unity.obj ^
    build\sdl3\SDL3-static.lib ^
    /link /OUT:app\%APP_EXE% ^
    user32.lib gdi32.lib winmm.lib ws2_32.lib winhttp.lib shell32.lib opengl32.lib dwmapi.lib ole32.lib ^
    kernel32.lib imm32.lib oleaut32.lib version.lib uuid.lib advapi32.lib setupapi.lib ^
    d3d11.lib d3dcompiler.lib bcrypt.lib ^
    /MANIFEST:EMBED /MANIFESTINPUT:src\karamelo.manifest ^
    /SUBSYSTEM:WINDOWS /DEBUG /PDBALTPATH:%%_PDB%% /GUARD:CF /MAP:build\%APP_BASE%.map /OPT:REF /OPT:ICF

if errorlevel 1 (
    echo.
    echo [BUILD FAILED] Compilation errors occurred.
    exit /b 1
)

echo.
echo [BUILD SUCCESS] app\%APP_EXE%
rem app\%APP_BASE%.exe (o que run_port.bat abre) so e atualizado no fim, depois
rem dos testes de unidade e da bateria de cores. Um build que deixa algum core
rem sem abrir nao substitui o executavel que estava funcionando.
if not exist app\cores mkdir app\cores
rem A pasta cores\ na raiz e um segundo deposito dos mesmos motores, e esta
rem copia despeja ela por cima de app\cores\ a cada compilacao. Ela nunca
rem apaga nada, so sobrescreve - entao core removido de app\cores\ reaparece
rem no build seguinte. Foi assim que os 25 MB de cores mortos sobreviveram a
rem uma release inteira: foram apagados de app\cores\, o commit registrou a
rem remocao nos documentos, e a compilacao seguinte trouxe todos de volta sem
rem dizer nada.
rem
rem O aviso abaixo nao impede a copia - quem depende dela para popular uma
rem checkout novo continua sendo atendido. Ele so tira o silencio: se a raiz
rem tem um .dll que app\cores\ nao tem, alguem precisa saber que ele acabou de
rem voltar.
if exist cores\*.dll (
    for %%F in (cores\*.dll) do (
        if not exist "app\cores\%%~nxF" echo   [AVISO] cores\%%~nxF nao estava em app\cores\ e foi copiado de volta.
    )
    copy /Y cores\*.dll app\cores\ >nul 2>&1
)

rem Gerar version.json automatico
powershell -NoProfile -Command "$size = (Get-Item 'app\%APP_EXE%').Length; $date = (Get-Date -Format 'yyyy-MM-dd'); $json = @{ version = '%APP_VER%'; title = 'Karamelo v%APP_VER%'; release_date = $date; notes = 'Versao de producao Karamelo'; exe_url = 'https://karamelo-emu.com/downloads/Karamelo.exe'; exe_size = $size; exe_sha256 = ''; zip_url = 'https://karamelo-emu.com/downloads/Karamelo_v%APP_VER%_Win64.zip'; force_full_package = $false } | ConvertTo-Json -Depth 4; Set-Content -Path 'app\version.json' -Value $json -Encoding UTF8"
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

rem Bateria de cores: cada core com uma ROM real do seu sistema, carregado,
rem rodando ate entregar video, desligado e carregado de novo - pelo mesmo
rem caminho do menu. Uma vez no driver padrao (Auto) e outra no driver que o
rem jogador tem salvo em app\Config\karamelo.cfg, porque foi justamente num
rem driver diferente do padrao que um core falhou sem nenhum teste acusar.
rem O Nintendo 64 roda nos tres cores (ParaLLEl, Mupen64Plus, Gopher64).
rem Qualquer core que recuse, trave ou derrube o processo reprova o build.
echo.
echo ==================================================
echo [RUNNING CORE BATTERY]
echo ==================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\stress_cores.ps1" -Cycles 1 -Driver 0
if errorlevel 1 (
    echo [BUILD FAILED] Bateria de cores reprovou no driver Auto - veja a lista acima.
    exit /b 1
)
set "USER_DRIVER="
for /f "tokens=2 delims==" %%d in ('findstr /b /c:"driver=" app\Config\karamelo.cfg 2^>nul') do set "USER_DRIVER=%%d"
if defined USER_DRIVER if not "%USER_DRIVER%"=="0" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\stress_cores.ps1" -Cycles 1 -Driver %USER_DRIVER%
    if errorlevel 1 (
        echo [BUILD FAILED] Bateria de cores reprovou no driver %USER_DRIVER% da sua configuracao.
        exit /b 1
    )
)

copy /Y "app\%APP_EXE%" "app\%APP_BASE%.exe" >nul 2>&1
echo [BUILD OK] app\%APP_BASE%.exe atualizado - testes de unidade e bateria de cores passaram.
exit /b 0
