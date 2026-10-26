[CmdletBinding()]
param(
    [ValidateSet('Menu','Install','Update','Uninstall','CleanLogs')][string]$Mode = 'Menu',
    [string[]]$DiscordPath = @()
)
$ErrorActionPreference = 'Stop'

$script:RuntimeDirectory = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'version.dll')) -and
    (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'build/version.dll'))) {
    $script:RuntimeDirectory = Join-Path $PSScriptRoot 'build'
}

$script:DiscordProcessNames = @('Discord','DiscordPTB','DiscordCanary','DiscordDevelopment')
$script:Messages = @{
    en = @{
        Menu = "`nDiscorder-Proxy-Addon {0}`n1. Install / update`n2. Uninstall`n3. Set Discord path`n4. Edit proxy settings`n5. Delete proxy logs`n0. Exit"
        Select = 'Select an option'
        CustomPath = 'Discord folder or Discord.exe path'
        SettingsSaved = 'Restart Discord to apply settings.'
        UnknownChoice = 'Unknown option. Enter a number from 0 to 5.'
        ClosePrompt = 'Discord is running. End all Discord processes? Active calls will end. [y/N]'
        YesNo = 'Enter y (yes) or n (no). Press Enter to cancel.'
        Cancelled = 'Cancelled. Discord is still running.'
        Closed = 'Discord processes have been closed.'
        Closing = 'Closing Discord...'
        CloseFailed = 'Discord is still running after several attempts: {0}. Close it from the system tray and retry.'
        CloseDenied = 'Windows denied permission to close Discord: {0}. Close it manually, or run the installer with the same permissions as Discord.'
        ExistingInstall = 'An installation already exists in {0}. Uninstall it before replacing it.'
        Installed = 'TCP/UDP module installed: {0}'
        ManifestFormat = 'Unknown installation manifest format.'
        InvalidVersion = 'Invalid version in {0}. Expected a version such as 1.1.0.'
        InvalidDll = 'The package does not contain a valid add-on DLL.'
        VersionMismatch = 'The DLL version does not match [addon] version in the package INI. Extract a complete release package.'
        Downgrade = 'Version {0} is installed; installing older version {1} was blocked.'
        DllChanged = 'version.dll changed in {0}. Removal stopped to protect your files.'
        BackupName = 'Invalid backup file name.'
        BackupMissing = 'The original DLL backup is missing or changed. Restore the backup before uninstalling.'
        RollbackFailed = 'Update failed. Could not restore these files: {0}. Keep the backups and check write permissions before retrying.'
        Removed = 'Module removed; the previous DLL was restored if present: {0}'
        UpdateStopped = 'The installed DLL is missing or changed in {0}. Automatic update stopped.'
        AlreadyCurrent = 'The module is already up to date: {0}'
        Updated = 'Module updated; settings and original backup preserved: {0}'
        InvalidTracking = 'Ignoring an invalid installation tracking file.'
        NoExecutable = 'Discord executable not found: {0}'
        InvalidExecutable = 'Invalid executable header.'
        RequiresX64 = 'This package requires Windows x64 and Discord x64.'
        NotFound = 'Discord not found. Select its folder in option 3 or use: Discord-Proxy.bat Install "D:\Apps\Discord"'
        Targets = "Discord folders:`n{0}"
        MissingFile = 'Missing {0}. Extract the entire package first.'
        InstallComplete = 'Installation complete. Start Discord and keep your SOCKS5 proxy running with UDP enabled.'
        UninstallComplete = 'Removal complete. You can start Discord.'
        LogsRemoved = 'Proxy logs deleted.'
    }
    ru = @{
        Menu = "`nDiscorder-Proxy-Addon {0}`n1. Установить / обновить`n2. Удалить`n3. Указать путь к Discord`n4. Открыть настройки прокси`n5. Удалить логи прокси`n0. Выход"
        Select = 'Выберите пункт'
        CustomPath = 'Путь к папке Discord или к файлу Discord.exe'
        SettingsSaved = 'Перезапустите Discord, чтобы применить настройки.'
        UnknownChoice = 'Нет такого пункта. Введите число от 0 до 5.'
        ClosePrompt = 'Discord запущен. Завершить все его процессы? Текущие звонки будут прерваны. [д/Н]'
        YesNo = 'Введите д (да) или н (нет). Для отмены нажмите Enter.'
        Cancelled = 'Действие отменено. Discord продолжает работать.'
        Closed = 'Процессы Discord завершены.'
        Closing = 'Завершение процессов Discord...'
        CloseFailed = 'После нескольких попыток Discord продолжает работать: {0}. Закройте его через значок в системном трее и повторите попытку.'
        CloseDenied = 'Windows не разрешила завершить Discord: {0}. Закройте его вручную или запустите установщик с теми же правами, что и Discord.'
        ExistingInstall = 'В папке {0} уже есть установленный модуль. Удалите его перед повторной установкой.'
        Installed = 'Модуль TCP/UDP установлен: {0}'
        ManifestFormat = 'Неизвестный формат файла сведений об установке.'
        InvalidVersion = 'Некорректная версия в файле {0}. Ожидается номер вида 1.1.0.'
        InvalidDll = 'В пакете отсутствует корректная DLL модуля.'
        VersionMismatch = 'Версия DLL не совпадает с [addon] version в INI пакета. Распакуйте релиз целиком.'
        Downgrade = 'Установлена версия {0}; установка более старой версии {1} остановлена.'
        DllChanged = 'Файл version.dll в папке {0} изменён. Удаление остановлено, чтобы сохранить ваши файлы.'
        BackupName = 'Недопустимое имя резервной копии.'
        BackupMissing = 'Резервная копия исходной DLL отсутствует или изменена. Восстановите её перед удалением модуля.'
        RollbackFailed = 'Обновление не выполнено. Не удалось восстановить файлы: {0}. Сохраните резервные копии и проверьте права на запись перед повторной попыткой.'
        Removed = 'Модуль удалён; предыдущая DLL восстановлена, если она была сохранена: {0}'
        UpdateStopped = 'Установленная DLL в папке {0} отсутствует или изменена. Автоматическое обновление остановлено.'
        AlreadyCurrent = 'Уже установлена актуальная версия: {0}'
        Updated = 'Модуль обновлён; настройки и исходная резервная копия сохранены: {0}'
        InvalidTracking = 'Повреждённый файл со списком установок пропущен.'
        NoExecutable = 'Исполняемый файл Discord не найден: {0}'
        InvalidExecutable = 'Некорректный заголовок исполняемого файла.'
        RequiresX64 = 'Для этого пакета нужны Windows x64 и Discord x64.'
        NotFound = 'Discord не найден. Укажите его папку в пункте 3 или выполните: Discord-Proxy.bat Install "D:\Apps\Discord"'
        Targets = "Папки Discord:`n{0}"
        MissingFile = 'Отсутствует файл {0}. Сначала распакуйте пакет целиком.'
        InstallComplete = 'Установка завершена. Запустите Discord. SOCKS5-прокси должен работать с включённым UDP.'
        UninstallComplete = 'Удаление завершено. Можно запускать Discord.'
        LogsRemoved = 'Логи прокси удалены.'
    }
}

function Get-InstallerLanguage([string]$CultureName = [Globalization.CultureInfo]::CurrentUICulture.Name) {
    try {
        if ([Globalization.CultureInfo]::GetCultureInfo($CultureName).TwoLetterISOLanguageName -eq 'ru') { return 'ru' }
    } catch {}
    return 'en'
}

$script:Language = Get-InstallerLanguage

function Get-Text([string]$Key, [object[]]$Values = @()) {
    return $script:Messages[$script:Language][$Key] -f $Values
}

function Get-DiscordProcesses {
    Get-Process -Name $script:DiscordProcessNames -ErrorAction SilentlyContinue
}

function Confirm-DiscordClosed([ValidateRange(500,30000)][int]$TimeoutMilliseconds = 10000) {
    $processes = @(Get-DiscordProcesses)
    if (-not $processes.Count) { return $true }
    do {
        $answer = (Read-Host (Get-Text ClosePrompt)).Trim().ToLowerInvariant()
        if ($answer -in @('', 'n', 'no', 'н', 'нет')) {
            Write-Host (Get-Text Cancelled)
            return $false
        }
        if ($answer -in @('y', 'yes', 'д', 'да')) { break }
        Write-Host (Get-Text YesNo)
    } while ($true)

    Write-Host (Get-Text Closing)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $denied = @{}
    $quietChecks = 0
    while ($timer.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
        if ($processes.Count) {
            $quietChecks = 0
            foreach ($process in $processes) {
                try {
                    if ($process.Path -and $script:DiscordPath -notcontains $process.Path) { $script:DiscordPath += $process.Path }
                } catch {}
                try { Stop-Process -InputObject $process -Force -ErrorAction Stop }
                catch {
                    $exception = $_.Exception
                    $accessDenied = $_.CategoryInfo.Category -eq 'PermissionDenied'
                    while ($exception) {
                        if ($exception -is [UnauthorizedAccessException] -or
                            ($exception -is [ComponentModel.Win32Exception] -and $exception.NativeErrorCode -eq 5)) {
                            $accessDenied = $true
                        }
                        $exception = $exception.InnerException
                    }
                    if ($accessDenied) { $denied[$process.Id] = $true }
                }
            }
        } else {
            $quietChecks++
            if ($quietChecks -ge 5) {
                $processes = @(Get-DiscordProcesses)
                if (-not $processes.Count) {
                    Write-Host (Get-Text Closed)
                    return $true
                }
                $quietChecks = 0
                continue
            }
        }
        Start-Sleep -Milliseconds 100
        $processes = @(Get-DiscordProcesses)
    }
    $remaining = @(Get-DiscordProcesses)
    if ($remaining.Count) {
        $details = ($remaining | ForEach-Object { '{0} (PID {1})' -f $_.ProcessName, $_.Id }) -join ', '
        if (@($remaining | Where-Object { $denied.ContainsKey($_.Id) }).Count) { throw (Get-Text CloseDenied @($details)) }
        throw (Get-Text CloseFailed @($details))
    }
    Write-Host (Get-Text Closed)
    return $true
}


function Get-ProxyHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Get-IniValue([string]$Text, [string]$Section, [string]$Key) {
    $active = $false
    foreach ($line in ($Text -split '\r?\n')) {
        if ($line -match '^\s*\[([^\]]+)\]') { $active = $Matches[1].Trim() -ieq $Section }
        elseif ($active -and $line -match '^\s*([^;#=]+?)\s*=(.*)$' -and $Matches[1].Trim() -ieq $Key) {
            return $Matches[2].Trim()
        }
    }
    return $null
}

function Set-IniValue([string]$Text, [string]$Section, [string]$Key, [string]$Value) {
    $newline = "`n"
    if ($Text.Contains("`r`n")) { $newline = "`r`n" }
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $active = $false; $foundSection = $false; $written = $false
    foreach ($line in ($Text.TrimEnd("`r", "`n") -split '\r?\n')) {
        if ($line -match '^\s*\[([^\]]+)\]') {
            if ($active -and -not $written) { $lines.Add("$Key=$Value"); $written = $true }
            $active = $Matches[1].Trim() -ieq $Section
            if ($active) { $foundSection = $true }
        }
        if ($active -and $line -match '^\s*([^;#=]+?)\s*=' -and $Matches[1].Trim() -ieq $Key) {
            if (-not $written) { $lines.Add("$Key=$Value"); $written = $true }
        } else { $lines.Add($line) }
    }
    if (-not $foundSection) { $lines.Add(''); $lines.Add("[$Section]") }
    if (-not $written) { $lines.Add("$Key=$Value") }
    return ($lines -join $newline) + $newline
}

function Get-ConfigVersion([string]$Path) {
    $value = Get-IniValue ([IO.File]::ReadAllText($Path)) 'addon' 'version'
    if ($null -eq $value) { return $null }
    if ($value -notmatch '^\d+\.\d+\.\d+$') { throw (Get-Text InvalidVersion @($Path)) }
    try { [void][version]$value } catch { throw (Get-Text InvalidVersion @($Path)) }
    return $value
}

function Get-AddonDllVersion([string]$Path) {
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    if ($info.InternalName -ne 'DiscordProcessProxy') { throw (Get-Text InvalidDll) }
    return '{0}.{1}.{2}' -f $info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart
}

function Get-PackageVersion([string]$SourceDll, [string]$SourceIni) {
    $version = Get-ConfigVersion $SourceIni
    if (-not $version -or $version -ne (Get-AddonDllVersion $SourceDll)) { throw (Get-Text VersionMismatch) }
    return $version
}

function Merge-AddonConfig([string]$Text, [string]$Defaults, [string]$Version) {
    $active = $false
    foreach ($line in ($Defaults -split '\r?\n')) {
        if ($line -match '^\s*\[([^\]]+)\]') { $active = $Matches[1].Trim() -ieq 'proxy' }
        elseif ($active -and $line -match '^\s*([^;#=]+?)\s*=(.*)$') {
            $key = $Matches[1].Trim(); $value = $Matches[2].Trim()
            if ($null -eq (Get-IniValue $Text 'proxy' $key)) { $Text = Set-IniValue $Text 'proxy' $key $value }
        }
    }
    if ((Get-IniValue $Text 'addon' 'version') -ne $Version) { $Text = Set-IniValue $Text 'addon' 'version' $Version }
    return $Text
}

function Write-AtomicBytes([string]$Path, [byte[]]$Bytes) {
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    $backup = $temporary + '.bak'
    try {
        [IO.File]::WriteAllBytes($temporary, $Bytes)
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, $backup) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally {
        foreach ($file in @($temporary, $backup)) {
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue }
        }
    }
}

function Save-InstallRecord([string]$Path, $Record) {
    $json = $Record | ConvertTo-Json
    Write-AtomicBytes $Path ([Text.Encoding]::UTF8.GetBytes($json))
}

function Set-AddonTarget([string]$Directory, [string]$SourceDll, [string]$SourceIni, $Record = $null) {
    $version = Get-PackageVersion $SourceDll $SourceIni
    $dll = Join-Path $Directory 'version.dll'
    $ini = Join-Path $Directory 'discord-proxy.ini'
    $manifest = Join-Path $Directory 'discord-proxy-install.json'
    $updating = $null -ne $Record
    $newHash = Get-ProxyHash $SourceDll
    $configExisted = Test-Path -LiteralPath $ini
    $configBytes = $null
    if ($configExisted) { $configBytes = [IO.File]::ReadAllBytes($ini) }
    $defaults = [IO.File]::ReadAllText($SourceIni)
    $text = $defaults
    if ($configExisted) { $text = [IO.File]::ReadAllText($ini) }
    $merged = Merge-AddonConfig $text $defaults $version
    $ownedConfig = -not $configExisted
    $olderBuild = $null
    if ($updating) {
        if (-not (Test-Path -LiteralPath $dll) -or (Get-ProxyHash $dll) -ne $Record.DllHash) {
            throw (Get-Text UpdateStopped @($Directory))
        }
        $installedVersion = $Record.Version
        if (-not $installedVersion) {
            try { $installedVersion = Get-AddonDllVersion $dll }
            catch { if ($configExisted) { $installedVersion = Get-ConfigVersion $ini } }
        }
        if ($installedVersion -and [version]$version -lt [version]$installedVersion) {
            throw (Get-Text Downgrade @($installedVersion, $version))
        }
        $ownedConfig = $ownedConfig -or ((Get-ProxyHash $ini) -eq $Record.IniHash)
        $olderBuild = $Record.PreviousBuildName
    } else {
        $Record = [pscustomobject]@{ Format = 1; ConfigCreated = (-not $configExisted); DllHash = ''; IniHash = ''; BackupName = ''; BackupHash = '' }
    }
    $replaceDll = -not $updating -or $Record.DllHash -ne $newHash
    $stage = Join-Path $Directory ('discord-proxy-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    $backup = $null; $dllWritten = $false; $configWritten = $false
    try {
        if ($replaceDll) {
            Copy-Item -LiteralPath $SourceDll -Destination $stage
            if ((Get-ProxyHash $stage) -ne $newHash) { throw (Get-Text InvalidDll) }
            if (Test-Path -LiteralPath $dll) {
                $prefix = 'version.dll.before-discord-proxy-'
                if ($updating) { $prefix = 'version.dll.previous-build-' }
                $backupName = $prefix + [Guid]::NewGuid().ToString('N')
                $backup = Join-Path $Directory $backupName
                if (-not $updating) {
                    $Record.BackupName = $backupName
                    $Record.BackupHash = Get-ProxyHash $dll
                }
                [IO.File]::Replace($stage, $dll, $backup)
                $dllWritten = $true
                if ($updating) { $Record | Add-Member -NotePropertyName PreviousBuildName -NotePropertyValue $backupName -Force }
            } else { [IO.File]::Move($stage, $dll); $dllWritten = $true }
        }
        if (-not $configExisted -or $merged -cne $text) {
            # UTF-16 lets the Windows INI API read non-ASCII settings and comments.
            $encoding = [Text.Encoding]::Unicode
            Write-AtomicBytes $ini ([byte[]]($encoding.GetPreamble() + $encoding.GetBytes($merged)))
            $configWritten = $true
        }
        if ($ownedConfig -or -not $updating) { $Record.IniHash = Get-ProxyHash $ini }
        if (-not $configExisted) { $Record.ConfigCreated = $true }
        $Record.DllHash = $newHash
        $Record | Add-Member -NotePropertyName Version -NotePropertyValue $version -Force
        Save-InstallRecord $manifest $Record
    } catch {
        $failure = $_
        $restoreErrors = @()
        if ($configWritten) {
            try {
                if ($configExisted) { Write-AtomicBytes $ini $configBytes }
                else { Remove-Item -LiteralPath $ini }
            } catch { $restoreErrors += $ini }
        }
        if ($dllWritten) {
            try {
                if ($backup) {
                    [IO.File]::Replace($backup, $dll, ($stage + '.rollback'))
                    Remove-Item -LiteralPath ($stage + '.rollback') -Force -ErrorAction SilentlyContinue
                }
                else { Remove-Item -LiteralPath $dll }
            } catch { $restoreErrors += $dll }
        }
        if ($restoreErrors.Count) { throw (Get-Text RollbackFailed @($restoreErrors -join ', ')) }
        throw $failure
    } finally {
        if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Force -ErrorAction SilentlyContinue }
    }
    if ($replaceDll -and $olderBuild -match '^version\.dll\.previous-build-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath (Join-Path $Directory $olderBuild) -Force -ErrorAction SilentlyContinue
    }
    if (-not $updating) { Write-Host (Get-Text Installed @($Directory)) }
    elseif ($replaceDll) { Write-Host (Get-Text Updated @($Directory)) }
    else { Write-Host (Get-Text AlreadyCurrent @($Directory)) }
}

function Install-ProxyTarget([string]$Directory, [string]$SourceDll, [string]$SourceIni) {
    if (Test-Path -LiteralPath (Join-Path $Directory 'discord-proxy-install.json')) { throw (Get-Text ExistingInstall @($Directory)) }
    Set-AddonTarget $Directory $SourceDll $SourceIni
}

function Uninstall-ProxyTarget([string]$Directory) {
    $manifest = Join-Path $Directory 'discord-proxy-install.json'
    if (-not (Test-Path -LiteralPath $manifest)) { return }
    $record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($record.Format -ne 1) { throw (Get-Text ManifestFormat) }
    $dll = Join-Path $Directory 'version.dll'
    $ini = Join-Path $Directory 'discord-proxy.ini'
    if ((Test-Path -LiteralPath $dll) -and (Get-ProxyHash $dll) -ne $record.DllHash) {
        throw (Get-Text DllChanged @($Directory))
    }
    $backup = $null
    if ($record.BackupName) {
        if ($record.BackupName -notmatch '^version\.dll\.before-discord-proxy-[a-f0-9]{32}$') { throw (Get-Text BackupName) }
        $backup = Join-Path $Directory $record.BackupName
        if (-not (Test-Path -LiteralPath $backup) -or (Get-ProxyHash $backup) -ne $record.BackupHash) {
            throw (Get-Text BackupMissing)
        }
    }
    if (Test-Path -LiteralPath $dll) { Remove-Item -LiteralPath $dll }
    if ($backup) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($backup)
        if ($info.InternalName -eq 'DiscordProcessProxy' -and $info.ProductName -in @('Discorder-Proxy-Addon (unofficial)', 'Discord Process Proxy (unofficial)')) {
            # A manually installed older build is ours too. Restoring it would
            # silently enable the proxy again after the user chose Uninstall.
            Remove-Item -LiteralPath $backup
        } else { Move-Item -LiteralPath $backup -Destination $dll }
    }
    if ($record.ConfigCreated -ne $false -and (Test-Path -LiteralPath $ini) -and (Get-ProxyHash $ini) -eq $record.IniHash) { Remove-Item -LiteralPath $ini }
    Remove-Item -LiteralPath $manifest
    if ($record.PreviousBuildName -match '^version\.dll\.previous-build-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath (Join-Path $Directory $record.PreviousBuildName) -Force -ErrorAction SilentlyContinue
    }
    Write-Host (Get-Text Removed @($Directory))
}

function Update-ProxyTarget([string]$Directory, [string]$SourceDll, [string]$SourceIni) {
    $manifest = Join-Path $Directory 'discord-proxy-install.json'
    if (-not (Test-Path -LiteralPath $manifest)) {
        Install-ProxyTarget $Directory $SourceDll $SourceIni
        return
    }
    $record = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($record.Format -ne 1) { throw (Get-Text ManifestFormat) }
    Set-AddonTarget $Directory $SourceDll $SourceIni $record
}

function Get-DiscordTargets([string[]]$Candidates) {
    $names = $script:DiscordProcessNames
    $roots = @($Candidates)
    foreach ($name in $names) { $roots += Join-Path $env:LOCALAPPDATA $name }
    foreach ($process in @(Get-Process -Name $names -ErrorAction SilentlyContinue)) {
        if ($process.Path) { $roots += $process.Path }
    }
    foreach ($hive in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
                        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
                        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')) {
        foreach ($entry in @(Get-ItemProperty -Path "$hive\*" -ErrorAction SilentlyContinue)) {
            if ($entry.DisplayName -match '^Discord(?:\s+(?:PTB|Canary|Development))?$') {
                if ($entry.InstallLocation) { $roots += $entry.InstallLocation }
                if ($entry.DisplayIcon -match '^"?(.+?\.exe)') { $roots += $Matches[1] }
            }
        }
    }
    $tracking = Join-Path $env:LOCALAPPDATA 'DiscordProcessProxy\installations.json'
    if (Test-Path -LiteralPath $tracking) {
        try { $roots += @(Get-Content -LiteralPath $tracking -Raw | ConvertFrom-Json) } catch { Write-Warning (Get-Text InvalidTracking) }
    }
    $found = foreach ($candidate in ($roots | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $item = Get-Item -LiteralPath $candidate
        $base = if ($item.PSIsContainer) { $item.FullName } else { $item.DirectoryName }
        $children = @(Get-ChildItem -LiteralPath $base -Directory -Filter 'app-*' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
        $directories = if ($children.Count) { $children } else { @($base) }
        if ($children.Count -and (Test-Path -LiteralPath (Join-Path $base 'discord-proxy-install.json'))) { $directories += $base }
        foreach ($directory in $directories) {
            foreach ($name in $names) {
                if (Test-Path -LiteralPath (Join-Path $directory ($name + '.exe')) -PathType Leaf) {
                    [IO.Path]::GetFullPath($directory); break
                }
            }
        }
    }
    $found | Sort-Object -Unique
}

function Assert-X64Discord([string]$Directory) {
    $exe = Get-ChildItem -LiteralPath $Directory -File | Where-Object { $_.Name -match '^Discord(?:PTB|Canary|Development)?\.exe$' } | Select-Object -First 1
    if (-not $exe) { throw (Get-Text NoExecutable @($Directory)) }
    $stream = [IO.File]::OpenRead($exe.FullName)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw (Get-Text InvalidExecutable) }
        $stream.Position = 0x3C; $offset = $reader.ReadUInt32(); $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw (Get-Text RequiresX64) }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Remove-ProxyLogs([string[]]$Directories) {
    foreach ($directory in $Directories) {
        Get-ChildItem -LiteralPath $directory -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^discord-proxy-(?:[0-9]+)\.log$' -or $_.Name -eq 'discord-proxy.log' } |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction Continue }
    }
    $shared = Join-Path $env:LOCALAPPDATA 'DiscordProcessProxy\discord-proxy.log'
    if (Test-Path -LiteralPath $shared) { Remove-Item -LiteralPath $shared -Force }
}

function Invoke-ProxyInstall([string]$Action) {
    $targets = @(Get-DiscordTargets $DiscordPath)
    if (-not $targets.Count) { throw (Get-Text NotFound) }
    Write-Host (Get-Text Targets @($targets -join "`n"))
    if ($Action -eq 'CleanLogs') { Remove-ProxyLogs $targets; Write-Host (Get-Text LogsRemoved); return }
    if (-not (Confirm-DiscordClosed)) { return }
    foreach ($target in $targets) { Assert-X64Discord $target }
    if ($Action -in @('Install','Update')) {
        if (-not (Test-Path -LiteralPath (Join-Path $script:RuntimeDirectory 'version.dll'))) { throw (Get-Text MissingFile @('version.dll')) }
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'discord-proxy.ini'))) { throw (Get-Text MissingFile @('discord-proxy.ini')) }
        foreach ($target in $targets) {
            Update-ProxyTarget $target (Join-Path $script:RuntimeDirectory 'version.dll') (Join-Path $PSScriptRoot 'discord-proxy.ini')
        }
        $folder = Join-Path $env:LOCALAPPDATA 'DiscordProcessProxy'
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        ConvertTo-Json -InputObject @($targets) | Set-Content -LiteralPath (Join-Path $folder 'installations.json') -Encoding UTF8
    } else {
        foreach ($target in $targets) { Uninstall-ProxyTarget $target }
        $tracking = Join-Path $env:LOCALAPPDATA 'DiscordProcessProxy\installations.json'
        if (Test-Path -LiteralPath $tracking) { Remove-Item -LiteralPath $tracking }
    }
    Remove-ProxyLogs $targets
    if ($Action -in @('Install','Update')) { Write-Host (Get-Text InstallComplete) }
    else { Write-Host (Get-Text UninstallComplete) }
}

function Show-ProxyMenu {
    while ($true) {
        Write-Host (Get-Text Menu @((Get-ConfigVersion (Join-Path $PSScriptRoot 'discord-proxy.ini'))))
        $choice = Read-Host (Get-Text Select)
        if ($choice -eq '0') { return }
        try {
            switch ($choice) {
                '1' { Invoke-ProxyInstall 'Install' }
                '2' { Invoke-ProxyInstall 'Uninstall' }
                '3' { $script:DiscordPath = @((Read-Host (Get-Text CustomPath)).Trim('"')) }
                '4' {
                    $configs = @(Get-DiscordTargets $DiscordPath | ForEach-Object { Join-Path $_ 'discord-proxy.ini' } | Where-Object { Test-Path -LiteralPath $_ })
                    if (-not $configs.Count) { $configs = @(Join-Path $PSScriptRoot 'discord-proxy.ini') }
                    foreach ($config in $configs) { Start-Process notepad.exe -ArgumentList ('"' + $config + '"') -Wait }
                    Write-Host (Get-Text SettingsSaved)
                }
                '5' { Invoke-ProxyInstall 'CleanLogs' }
                default { Write-Host (Get-Text UnknownChoice) }
            }
        } catch { Write-Host $_ -ForegroundColor Red }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        if (-not (Confirm-DiscordClosed)) { exit 0 }
        if ($Mode -eq 'Menu') { Show-ProxyMenu } else { Invoke-ProxyInstall $Mode }
    } catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
}
