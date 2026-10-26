@echo off
setlocal
pushd "%~dp0" || exit /b 1
if "%~1"=="" (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage-Install.ps1" -Mode Menu
) else (
  if "%~2"=="" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage-Install.ps1" -Mode "%~1"
  ) else (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage-Install.ps1" -Mode "%~1" -DiscordPath "%~2"
  )
)
set "result=%errorlevel%"
popd
exit /b %result%
