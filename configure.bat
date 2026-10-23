@echo off
:: Configures the Ninja/MSVC build into .\build. Works from any checkout path:
:: the Visual Studio installation is located with vswhere instead of a hardcoded
:: drive letter, and CMake falls back to the copy that ships with Visual Studio.
setlocal
set "ROOT=%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [configure] Visual Studio not found. Install Visual Studio 2022 with the C++ workload.
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [configure] No Visual Studio installation with the C++ workload was found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

set "CMAKE=cmake"
where cmake >nul 2>nul || set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if /i not "%CMAKE%"=="cmake" if not exist "%CMAKE%" (
    echo [configure] CMake was not found.
    exit /b 1
)

:: Prefer the Ninja that ships with Visual Studio: the cached generator path is
:: used by later `cmake --build` calls, which do not necessarily inherit the
:: PATH changes vcvars made here.
set "NINJA=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA%" set "NINJA=ninja"

cd /d "%ROOT%"
"%CMAKE%" -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_MAKE_PROGRAM="%NINJA%"
exit /b %errorlevel%
