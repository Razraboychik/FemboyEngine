@echo off
:: Builds everything in .\build (or the targets passed after the script, e.g.
:: `build.bat --target femboy_tests`). Configures first when there is no build
:: tree yet, then copies the SFML runtime DLLs next to the executables.
setlocal
set "ROOT=%~dp0"

if not exist "%ROOT%build\CMakeCache.txt" (
    call "%ROOT%configure.bat"
    if errorlevel 1 exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto after_vs
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
:after_vs

set "CMAKE=cmake"
where cmake >nul 2>nul || set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if /i not "%CMAKE%"=="cmake" if not exist "%CMAKE%" (
    echo [build] CMake was not found.
    exit /b 1
)

cd /d "%ROOT%"
"%CMAKE%" --build build %*
if errorlevel 1 exit /b %errorlevel%

copy /y "libs\SFML-3.1.0\bin\*.dll" build\ >nul 2>&1
exit /b 0
