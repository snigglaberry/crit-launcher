@echo off
rem Crit Launcher Native - MSVC build (run from a Developer Command Prompt or let
rem this script locate vcvars64.bat).
setlocal enabledelayedexpansion
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
    if defined VSDIR if exist "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat"
  )
)
where cl >nul 2>nul
if errorlevel 1 (
  echo error: cl.exe not found. Install Visual Studio 2022 Build Tools with the C++ workload.
  exit /b 1
)

if not exist build mkdir build
setlocal
set SRC=src\main.cpp src\app\app.cpp src\app\shell.cpp src\app\home.cpp src\app\minecraft.cpp
set SRC=%SRC% src\app\overlays.cpp src\app\page_instances.cpp src\app\page_accounts.cpp
set SRC=%SRC% src\app\page_settings.cpp src\app\page_misc.cpp src\ui\framework.cpp
set SRC=%SRC% src\render\canvas.cpp src\render\font.cpp src\render\svg.cpp
set SRC=%SRC% src\core\util.cpp src\core\zip.cpp src\platform\json.cpp src\platform\win_platform.cpp

cl /nologo /std:c++17 /O2 /EHsc /W3 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ^
   /I src /I third_party\stb /DCRIT_USE_STB %SRC% ^
   /Fo:build\ /Fe:build\CritLauncher.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest ^
   user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib shlwapi.lib d3d11.lib dxgi.lib d3dcompiler.lib ^
   winhttp.lib version.lib dwmapi.lib bcrypt.lib crypt32.lib psapi.lib
if errorlevel 1 exit /b 1
echo.
echo built build\CritLauncher.exe
