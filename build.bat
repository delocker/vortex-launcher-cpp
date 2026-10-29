@echo off
setlocal
rem ===========================================================================
rem  Build Vortex Minecraft Launcher (C++ port) - MSVC x64, no third-party deps
rem  Requires Visual Studio 2017+ or Build Tools with the "Desktop C++" workload.
rem ===========================================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: vswhere.exe not found. Install Visual Studio C++ Build Tools,
    echo        or call vcvars64.bat manually and run the cl/rc lines below.
    exit /b 1
)

set "VS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if "%VS%"=="" (
    echo error: no C++ toolset found by vswhere. Install the "Desktop development with C++" workload.
    exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rc /nologo /fo app.res res\app.rc                 || exit /b 1
if not exist obj mkdir obj
cl /nologo /EHsc /std:c++17 /W3 /O2 /MT /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    src\app.cpp src\http.cpp src\zip.cpp ^
    /Fe:VLauncher_cpp.exe /Fo:obj\ ^
    /link /SUBSYSTEM:WINDOWS app.res ^
    winhttp.lib crypt32.lib advapi32.lib user32.lib gdi32.lib comctl32.lib || exit /b 1

echo.
echo BUILD OK: VLauncher_cpp.exe
exit /b 0
