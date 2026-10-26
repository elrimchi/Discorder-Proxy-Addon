@echo off
setlocal
echo(
echo(  ____  ___ ____   ____ ___  ____  ____  _____ ____
echo( ^|  _ \^|_ _/ ___^| / ___/ _ \^|  _ \^|  _ \^| ____^|  _ \
echo( ^| ^| ^| ^|^| ^|\___ \^| ^|  ^| ^| ^| ^| ^|_) ^| ^| ^| ^|  _^| ^| ^|_) ^|
echo( ^| ^|_^| ^|^| ^| ___) ^| ^|__^| ^|_^| ^|  _ ^<^| ^|_^| ^| ^|___^|  _ ^<
echo( ^|____/^|___^|____/ \____\___/^|_^| \_\____/^|_____^|_^| \_\
echo(                 P R O X Y   A D D O N
echo(
echo(  https://github.com/elrimchi/Discorder-Proxy-Addon
echo(
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
