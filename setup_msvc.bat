@echo off
rem ---------------------------------------------------------------------------
rem Locates and initialises the MSVC x64 toolchain.
rem
rem Shared by compile_port.bat and run_tests.bat so there is one definition. Two
rem copies of a build recipe drift apart - that already happened here once, when
rem the duplicated test recipe stopped linking the sources the tests needed.
rem
rem No setlocal on purpose: the environment it sets up must survive back to the
rem caller.
rem
rem Written without parenthesised blocks around anything holding a Program Files
rem path. cmd expands variables BEFORE it parses a block, so the ")" inside
rem "Program Files (x86)" closes the block early and the whole script dies with
rem "\Microsoft was unexpected at this time". Labels and gotos avoid it.
rem ---------------------------------------------------------------------------

rem Already initialised? Nothing to do.
where cl.exe >nul 2>&1
if not errorlevel 1 exit /b 0

set "_SM_CWD=%CD%"
set "_SM_PF86=%ProgramFiles(x86)%"
set "_SM_PF=%ProgramFiles%"
set "_SM_VCVARS="

rem vcvarsall.bat shells out to vswhere.exe by bare name. With the Installer
rem directory off PATH that prints
rem     'vswhere.exe' is not recognized as an internal or external command
rem before falling back. Adding it removes the noise and lets the detection
rem below use vswhere properly.
set "_SM_VSWHERE=%_SM_PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%_SM_VSWHERE%" goto :no_vswhere
set "PATH=%_SM_PF86%\Microsoft Visual Studio\Installer;%PATH%"

rem Ask vswhere where Visual Studio actually is, so this works with 2019, 2022,
rem Community, Professional or Build Tools instead of one hardcoded path.
for /f "usebackq tokens=*" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do call :try_vs "%%i"

:no_vswhere

rem Fallbacks for machines without vswhere, newest first.
if not defined _SM_VCVARS call :try_vs "%_SM_PF%\Microsoft Visual Studio\2022\BuildTools"
if not defined _SM_VCVARS call :try_vs "%_SM_PF%\Microsoft Visual Studio\2022\Community"
if not defined _SM_VCVARS call :try_vs "%_SM_PF%\Microsoft Visual Studio\2022\Professional"
if not defined _SM_VCVARS call :try_vs "%_SM_PF86%\Microsoft Visual Studio\2019\BuildTools"
if not defined _SM_VCVARS call :try_vs "%_SM_PF86%\Microsoft Visual Studio\2019\Community"
if not defined _SM_VCVARS call :try_vs "%_SM_PF86%\Microsoft Visual Studio\2019\Professional"

if defined _SM_VCVARS goto :found
echo [ERROR] Visual Studio with the C++ x64 tools was not found.
echo         Install "Desktop development with C++" or the Build Tools.
exit /b 1

:found
call "%_SM_VCVARS%" >nul 2>&1

rem vcvars changes the working directory; put it back for the caller.
cd /d "%_SM_CWD%"

where cl.exe >nul 2>&1
if not errorlevel 1 exit /b 0

echo [ERROR] Could not initialise the MSVC x64 environment.
echo         Tried: %_SM_VCVARS%
exit /b 1

:try_vs
if defined _SM_VCVARS goto :eof
if exist "%~1\VC\Auxiliary\Build\vcvars64.bat" set "_SM_VCVARS=%~1\VC\Auxiliary\Build\vcvars64.bat"
goto :eof
