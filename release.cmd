@echo off
setlocal
pushd "%~dp0native-dll" || exit /b 1
call build.cmd
if errorlevel 1 goto failed
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command ". .\Manage-Install.ps1; [void](Get-PackageVersion (Join-Path $PWD 'build\version.dll') (Join-Path $PWD 'discord-proxy.ini'))"
if errorlevel 1 goto failed
python package.py
if errorlevel 1 goto failed
popd
exit /b 0
:failed
set "result=%errorlevel%"
popd
exit /b %result%
