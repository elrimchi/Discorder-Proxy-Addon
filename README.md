[English](README.md) · [Русский](README.ru.md)

# Discorder-Proxy-Addon

A Windows x64 add-on that routes Discord network traffic, including voice and video calls, through a local SOCKS5 proxy. It works with any VPN client or proxy app that provides a SOCKS5 endpoint with UDP support. The default address is `127.0.0.1:10808`.

The add-on runs inside Discord processes and does not start separate background programs or services. You can close the installer window after installation. All add-on settings are stored in `discord-proxy.ini` beside `Discord.exe` in the `app-*` folder.

This is a third-party tool and is not affiliated with Discord. Start your SOCKS5 proxy separately.

## Requirements

- Windows x64 and Discord x64.
- A local SOCKS5 proxy with UDP enabled. Voice/video features may not work without UDP.

## Install

1. Download the ZIP from [Releases](../../releases) and extract it.
2. If your proxy uses a different address or port, edit `host` and `port` in `discord-proxy.ini`.
3. Run `Discord-Proxy.bat`. If Discord is running, the script asks to end its processes. Confirm to continue; active calls will end.
4. Choose Install / update.
5. Start Discord.

The menu also lets you remove the add-on, select the Discord folder, edit the configuration, and delete logs. If a Discord update installs into a new `app-*` folder, run the installer again.

## Update

Download the new release, extract it into a separate folder, and run `Discord-Proxy.bat`. Choose Install / update. Your proxy address, port, and other settings are preserved. Any new options missing from your INI are added with their default values.

Downgrades are blocked. If an update fails, the installer restores the previous DLL and INI.

## Settings and logs

Use `discord-proxy.ini` to change the proxy address (`host`, `port`), enable or disable TCP and UDP, and configure proxying for Chromium.

The installer also stores a list of Discord installation folders in `%LOCALAPPDATA%\DiscordProcessProxy\installations.json`. That file contains no proxy settings.

If the Discord folder has no `discord-proxy.ini`, the installer creates it from the file beside the installer. Existing values are preserved: editing the INI beside the installer does not replace settings already installed in Discord. After installation, edit the file in the Discord folder; menu option 4 opens that file. Restart Discord to apply the changes.

For a proxy that requires authentication, set `username` and `password` in the `[proxy]` section:

```ini
[proxy]
username=
password=
```

Both fields are empty by default, so the connection uses no authentication. To enable authentication, fill in both fields and restart Discord. Each value must be 1 to 255 UTF-8 bytes long. These settings are preserved during updates.

With credentials configured, the DLL handles authentication for TCP and UDP. The `chromium_proxy` option is not applied in this mode, and hostnames are resolved through system DNS.

Logging is off by default (`log=0`). Set `log=1` to write a shared log at `%LOCALAPPDATA%\DiscordProcessProxy\discord-proxy.log`. The file is capped at 2 MiB and does not contain message contents.

## Manual build

You need Visual Studio Build Tools with **Desktop development with C++** and Python 3. Run `release.cmd` from the project root to build the DLL and create a ZIP with the version in its name, such as `Discorder-Proxy-Addon-v1.1.1.zip`. Attach this ZIP to your GitHub release. The `Discorder-Proxy-Addon` folder contains the extracted installer and DLL.

To build and package separately, run `native-dll/build.cmd`, then `python native-dll/package.py`.

Before releasing a new version, update `[addon] version` in `native-dll/discord-proxy.ini` and the version fields in `native-dll/src/version.rc`, then rebuild the DLL. The tag must match the package version, for example `v1.1.1`.

GitHub Actions builds the Windows version. When you publish a `v*` tag, the workflow attaches a ZIP to a pre-release.

Source and build scripts are in [native-dll](native-dll/). The [MinHook license](native-dll/vendor/minhook/LICENSE.txt) is included with the source and release package.
