@echo off
setlocal

rem ===========================================================================
rem  build_gopher64.bat - compila o core de N64 a partir do fonte vendorizado
rem
rem  Todos os outros cores vem prontos do buildbot da libretro via
rem  download_cores.ps1. O gopher64 nao: ele e compilado aqui, do fonte em
rem  third_party\gopher64, porque o projeto carrega correcoes proprias nele
rem  (a mais importante: o alinhamento da RDRAM em src\device\rdram.rs, que
rem  corrompia o heap ao descarregar o jogo - ver o commit d4ab32c).
rem
rem  Ate aqui isso era feito na mao, o que significava que a .dll corrigida
rem  existia so na maquina de quem compilou: cores\ e ignorado pelo git, entao
rem  o binario nao viaja no repositorio - so o fonte viaja. Este script e o
rem  elo que faltava entre os dois.
rem
rem  Rode depois de qualquer mudanca em third_party\gopher64, e antes de
rem  package_release.bat (que copia app\cores\*.dll para dentro do pacote).
rem ===========================================================================

cd /d "%~dp0"

where cargo >nul 2>&1
if errorlevel 1 (
    echo [FALHA] cargo nao encontrado no PATH. Instale o Rust: https://rustup.rs
    exit /b 1
)

if not exist "third_party\gopher64\Cargo.toml" (
    echo [FALHA] third_party\gopher64 nao encontrado.
    exit /b 1
)

echo [GOPHER64] Compilando ^(release^)... isso leva alguns minutos.
pushd "third_party\gopher64"
cargo build --release
set BUILD_RC=%errorlevel%
popd

if not "%BUILD_RC%"=="0" (
    echo.
    echo [FALHA] cargo build falhou.
    exit /b 1
)

set SRC_DLL=third_party\gopher64\target\release\gopher64.dll
if not exist "%SRC_DLL%" (
    echo [FALHA] %SRC_DLL% nao foi gerada.
    exit /b 1
)

if not exist cores mkdir cores
if not exist app\cores mkdir app\cores

rem Guarda a anterior uma unica vez, para dar caminho de volta se a nova regredir.
if exist "cores\n64_gopher.dll" if not exist "cores\n64_gopher.dll.bak" (
    copy /Y "cores\n64_gopher.dll" "cores\n64_gopher.dll.bak" >nul
    echo [GOPHER64] Core anterior guardado em cores\n64_gopher.dll.bak
)

copy /Y "%SRC_DLL%" "cores\n64_gopher.dll" >nul
copy /Y "%SRC_DLL%" "app\cores\n64_gopher.dll" >nul

echo.
echo [GOPHER64] Pronto:
echo    cores\n64_gopher.dll
echo    app\cores\n64_gopher.dll
echo.
echo    Lembre: package_release.bat copia app\cores\*.dll para o pacote,
echo    entao rode aquele depois deste para o core corrigido chegar no usuario.
endlocal
