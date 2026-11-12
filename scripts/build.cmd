@echo off
rem Windows build: MSVC (Visual Studio or Build Tools with "Desktop development with C++") + CMake 3.25 or newer.
rem Optional environment variables:
rem   CMAKE      full path of cmake.exe (default: the one on PATH, else the one shipped with Visual Studio)
rem   BUILD_DIR  build folder (default: <repo>\build)
rem Extra arguments are passed to the CMake configure step, e.g. scripts\build.cmd -DLAUNCHER_VENDORED=OFF
setlocal
set "ROOT=%~dp0.."
if not defined BUILD_DIR set "BUILD_DIR=%ROOT%\build"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio or the Build Tools with the "Desktop development with C++" workload.
    exit /b 1
)
set "VSDIR="
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "usebackq delims=" %%i in (`.\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
popd
if not defined VSDIR (
    echo No Visual Studio with the C++ x64 tools was found. Install the "Desktop development with C++" workload.
    exit /b 1
)
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1

if not defined CMAKE set "CMAKE=cmake"
"%CMAKE%" --version >nul 2>nul
if errorlevel 1 (
    echo cmake not found. Install CMake 3.25 or newer and put it on PATH, or set CMAKE to the full path of cmake.exe.
    exit /b 1
)

rem Ninja (shipped with the Visual Studio C++ tools) rebuilds everything that includes a changed header reliably; the older
rem "NMake Makefiles" generator works with every Visual Studio / CMake combination and is the fallback.
set "GEN=NMake Makefiles"
where ninja >nul 2>nul
if not errorlevel 1 (
    set "GEN=Ninja"
) else if exist "%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" (
    set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
    set "GEN=Ninja"
)
"%CMAKE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%BUILD_DIR%"
exit /b %errorlevel%
