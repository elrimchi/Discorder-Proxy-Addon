@echo off
setlocal
cd /d "%~dp0"
if defined VCToolsInstallDir goto compiler_ready
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
 echo Install Visual Studio Build Tools with Desktop development with C++.
 exit /b 1
)
for /f "usebackq delims=" %%V in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vsroot=%%V"
if not defined vsroot exit /b 1
call "%vsroot%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
:compiler_ready
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
 echo Use an x64 Visual Studio developer command prompt.
 exit /b 1
)
if not exist build mkdir build
cl /nologo /TC /O2 /MT /W3 /DWIN32_LEAN_AND_MEAN /Ivendor\minhook\include /Ivendor\minhook\src /c vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde64.c /Fobuild\
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fobuild\exports.obj src\exports.asm
if errorlevel 1 exit /b 1
rc /nologo /fobuild\version.res src\version.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /D_WIN32_WINNT=0x0602 /Ivendor\minhook\include /LD src\proxy.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj build\exports.obj build\version.res /Fobuild\proxy.obj /link /DEF:src\exports.def /OUT:build\version.dll /IMPLIB:build\version.lib ws2_32.lib
if errorlevel 1 exit /b 1
copy /y discord-proxy.ini build\discord-proxy.ini >nul
if errorlevel 1 exit /b 1
del /q build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj build\exports.obj build\proxy.obj build\version.res build\version.lib build\version.exp >nul 2>nul
echo DLL build completed.
exit /b 0
