$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Test-GameRoot {
    param([string]$Path)
    if (-not $Path) {
        return $false
    }
    return (Test-Path -LiteralPath (Join-Path $Path 'Fallout76.exe'))
}

function Get-SteamRoots {
    $roots = @()
    $keys = @(
        'HKCU:\Software\Valve\Steam',
        'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
        'HKLM:\SOFTWARE\Valve\Steam'
    )
    foreach ($key in $keys) {
        foreach ($name in @('SteamPath', 'InstallPath')) {
            try {
                $value = (Get-ItemProperty -LiteralPath $key -ErrorAction Stop).$name
                if (-not $value) {
                    continue
                }
                $resolved = Resolve-Path -LiteralPath $value -ErrorAction SilentlyContinue
                $path = if ($resolved) { $resolved.Path } else { [string]$value }
                if ($roots -notcontains $path) {
                    $roots += $path
                }
            } catch { }
        }
    }
    return $roots
}

function Get-SteamLibraries {
    param([string]$SteamRoot)
    $libraries = @()
    $vdf = Join-Path $SteamRoot 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        $text = Get-Content -LiteralPath $vdf -Raw -ErrorAction SilentlyContinue
        if ($text) {
            foreach ($match in [regex]::Matches($text, '"path"\s+"([^"]+)"')) {
                $library = $match.Groups[1].Value -replace '\\\\', '\'
                if ($libraries -notcontains $library) {
                    $libraries += $library
                }
            }
        }
    }
    if ($libraries -notcontains $SteamRoot) {
        $libraries += $SteamRoot
    }
    return $libraries
}

function Get-SteamFalloutCandidates {
    $candidates = @()
    foreach ($root in @(Get-SteamRoots)) {
        $steamRoot = Resolve-Path -LiteralPath $root -ErrorAction SilentlyContinue
        if (-not $steamRoot) {
            continue
        }
        foreach ($library in @(Get-SteamLibraries -SteamRoot $steamRoot.Path)) {
            $libraryPath = Resolve-Path -LiteralPath $library -ErrorAction SilentlyContinue
            if (-not $libraryPath) {
                continue
            }
            $steamApps = Join-Path $libraryPath.Path 'steamapps'
            $manifest = Join-Path $steamApps 'appmanifest_1151340.acf'
            if (Test-Path -LiteralPath $manifest) {
                $text = Get-Content -LiteralPath $manifest -Raw -ErrorAction SilentlyContinue
                if ($text) {
                    $installdir = [regex]::Match($text, '"installdir"\s+"([^"]+)"')
                    if ($installdir.Success) {
                        $candidate = Join-Path (Join-Path $steamApps 'common') $installdir.Groups[1].Value
                        if ($candidates -notcontains $candidate) {
                            $candidates += $candidate
                        }
                    }
                }
            }
            $fallback = Join-Path $steamApps 'common\Fallout76'
            if ($candidates -notcontains $fallback) {
                $candidates += $fallback
            }
        }
    }
    return $candidates
}

function Get-UninstallFalloutCandidates {
    $candidates = @()
    $keys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($key in $keys) {
        try {
            foreach ($entry in @(Get-ItemProperty -Path $key -ErrorAction Stop)) {
                if ($entry.DisplayName -eq 'Fallout 76' -and $entry.InstallLocation) {
                    $candidates += ([string]$entry.InstallLocation).Trim('"')
                }
            }
        } catch { }
    }
    return $candidates
}

function Get-XboxFalloutCandidates {
    $candidates = @()
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        try {
            if ($drive.DriveType -ne [System.IO.DriveType]::Fixed -or -not $drive.IsReady) {
                continue
            }
            $root = $drive.RootDirectory.FullName
            $candidates += (Join-Path $root 'XboxGames\Fallout 76\Content')
            $candidates += (Join-Path $root 'Program Files\ModifiableWindowsApps\Fallout 76')
        } catch { }
    }
    return $candidates
}

function Get-BethesdaFalloutCandidates {
    $candidates = @()
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $base) {
            continue
        }
        $candidates += (Join-Path $base 'Bethesda.net Launcher\games\Fallout76')
    }
    return $candidates
}

function Get-RunningFalloutCandidates {
    $candidates = @()
    foreach ($process in @(Get-Process -Name 'Fallout76' -ErrorAction SilentlyContinue)) {
        try {
            if ($process.Path) {
                $candidates += (Split-Path -Parent $process.Path)
            }
        } catch { }
    }
    return $candidates
}

function Resolve-GameRoot {
    param([string]$GameRoot)
    if ($GameRoot) {
        $resolved = Resolve-Path -LiteralPath $GameRoot -ErrorAction SilentlyContinue
        if (-not $resolved) {
            throw "Fallout 76 folder not found: $GameRoot"
        }
        if (-not (Test-GameRoot -Path $resolved.Path)) {
            throw "Fallout76.exe was not found in $($resolved.Path). Pass -GameRoot with the folder that contains Fallout76.exe."
        }
        return $resolved.Path
    }

    $sources = @(
        { Get-SteamFalloutCandidates },
        { Get-UninstallFalloutCandidates },
        { Get-BethesdaFalloutCandidates },
        { Get-XboxFalloutCandidates },
        { Get-RunningFalloutCandidates },
        { @('C:\Program Files (x86)\Steam\steamapps\common\Fallout76', 'C:\Program Files\Steam\steamapps\common\Fallout76') }
    )

    $seen = @()
    $searched = @()
    foreach ($source in $sources) {
        foreach ($candidate in @(& $source)) {
            if (-not $candidate) {
                continue
            }
            if ($seen -contains $candidate) {
                continue
            }
            $seen += $candidate
            if (-not (Test-Path -LiteralPath $candidate)) {
                continue
            }
            $searched += $candidate
            if (Test-GameRoot -Path $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    if ($searched.Count -eq 0) {
        $message = 'Fallout 76 installation not found. No Steam library, Bethesda.net folder or XboxGames folder with Fallout76.exe was found.'
    } else {
        $lines = @($searched | ForEach-Object { '  ' + $_ })
        $message = "Fallout 76 installation not found. Folders checked, no Fallout76.exe:`n" + ($lines -join "`n")
    }
    throw ($message + "`nPass -GameRoot with the folder that contains Fallout76.exe.")
}

function Assert-GameRoot {
    param([string]$GameRoot)
    $exe = Join-Path $GameRoot 'Fallout76.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Fallout76.exe not found in $GameRoot"
    }
    $data = Join-Path $GameRoot 'Data'
    if (-not (Test-Path -LiteralPath $data)) {
        throw "Data folder not found in $GameRoot"
    }
    return $GameRoot
}

function Get-GameProcesses {
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in @('Fallout76', 'Fallout76.exe') })
}

function Assert-GameStopped {
    $processes = Get-GameProcesses
    if ($processes.Count -gt 0) {
        throw 'Fallout 76 is running. Close the game before changing installed files.'
    }
}

function Get-FileMarkers {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    return [System.Text.Encoding]::ASCII.GetString($bytes)
}

function Get-ExtenderKind {
    param([string]$Path)
    $text = Get-FileMarkers -Path $Path
    if ($text.Length -eq 0) {
        return 'absent'
    }
    if ($text -match 'XScalPlugin_Query' -or $text -match 'xScal') {
        return 'xscal'
    }
    if ($text -match 'zfe' -or $text -match 'ZFE') {
        return 'zfe'
    }
    if ($text -match 'ReShade' -or $text -match 'reshade') {
        return 'reshade'
    }
    if ($text -match 'SFE' -or $text -match 'Scaleform') {
        return 'sfe'
    }
    return 'unknown'
}

function Get-ProxyInventory {
    param([string]$GameRoot)
    $candidates = @('dxgi.dll','d3d11.dll','d3d12.dll','version.dll','winmm.dll','dinput8.dll','XINPUT1_3.dll','uxtheme.dll')
    $present = @()
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $GameRoot $candidate)) {
            $present += $candidate
        }
    }
    return $present
}

function Get-ExecutableVersion {
    param([string]$GameRoot)
    $exe = Join-Path $GameRoot 'Fallout76.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        return $null
    }
    $info = (Get-Item -LiteralPath $exe).VersionInfo
    if ($info.FileVersion) {
        return ($info.FileVersion -replace '\s', '')
    }
    return $info.ProductVersion
}

function Get-XScalState {
    param([string]$GameRoot)
    $dxgi = Join-Path $GameRoot 'dxgi.dll'
    $log = Join-Path $GameRoot 'xscal.log'
    $plugins = Join-Path $GameRoot 'Data\XSCAL\Plugins'
    $addressLibrary = Join-Path $GameRoot 'Data\XSCAL\AddressLibrary'
    $result = [ordered]@{
        dxgiPresent = Test-Path -LiteralPath $dxgi
        extenderKind = Get-ExtenderKind -Path $dxgi
        logPresent = Test-Path -LiteralPath $log
        pluginsDirectory = $plugins
        pluginsDirectoryPresent = Test-Path -LiteralPath $plugins
        addressLibraryDirectory = Test-Path -LiteralPath $addressLibrary
        version = $null
        extenderVersion = $null
        gameVersion = $null
        addressLibraryLoaded = $false
        executableVersion = Get-ExecutableVersion -GameRoot $GameRoot
        addressLibraryFile = $null
        addressLibraryMatchesExecutable = $false
        proxyInventory = @(Get-ProxyInventory -GameRoot $GameRoot)
    }
    if ($result.addressLibraryDirectory -and $result.executableVersion) {
        $prefix = 'version-' + ($result.executableVersion -replace '\.', '-') + '-'
        $files = @(Get-ChildItem -LiteralPath $addressLibrary -Filter ($prefix + '*.bin') -ErrorAction SilentlyContinue)
        if ($files.Count -gt 0) {
            $result.addressLibraryFile = $files[0].Name
            $result.addressLibraryMatchesExecutable = $true
        }
    }
    if ($result.logPresent) {
        $head = Get-Content -LiteralPath $log -TotalCount 12 -ErrorAction SilentlyContinue
        foreach ($line in $head) {
            if ($line -match 'xScal v([0-9.]+)') {
                $result.version = $Matches[1]
                $result.extenderVersion = $Matches[1]
            }
            if ($line -match 'runtime = ([0-9.]+)') {
                $result.gameVersion = $Matches[1]
            }
            if ($line -match 'Address Library: loaded') {
                $result.addressLibraryLoaded = $true
            }
        }
    }
    return [pscustomobject]$result
}

function Get-CustomIniPath {
    param([string]$GameRoot)
    $documents = [Environment]::GetFolderPath('MyDocuments')
    $candidates = @(
        (Join-Path $documents 'My Games\Fallout 76\Fallout76Custom.ini'),
        'C:\Users\' + $env:USERNAME + '\Documents\My Games\Fallout 76\Fallout76Custom.ini',
        (Join-Path $GameRoot 'Fallout76Custom.ini')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Read-IniText {
    param([string]$Path)
    return [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
}

function Write-IniText {
    param([string]$Path, [string]$Text)
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-ArchiveSectionRange {
    param([string]$Text)

    $header = [regex]::Match($Text, "(?m)^[\uFEFF \t]*\[[ \t]*Archive[ \t]*\][ \t]*\r?\n?")
    if (-not $header.Success) {
        return $null
    }
    $start = $header.Index + $header.Length
    $remainder = $Text.Substring($start)
    $next = [regex]::Match($remainder, '(?m)^[ \t]*\[')
    $end = $Text.Length
    if ($next.Success) {
        $end = $start + $next.Index
    }
    return [pscustomobject]@{ Start = $start; End = $end }
}

function Get-ArchiveKeyMatch {
    param([string]$Text)
    $range = Get-ArchiveSectionRange -Text $Text
    if ($null -eq $range) {
        return $null
    }
    $body = $Text.Substring($range.Start, $range.End - $range.Start)
    $key = [regex]::Match($body, '(?m)^[ \t]*sResourceArchive2List[ \t]*=[^\r\n]*')
    if (-not $key.Success) {
        return $null
    }
    return [pscustomobject]@{ Index = $range.Start + $key.Index; Length = $key.Length; Line = $key.Value }
}

function Get-ArchiveListMatch {
    param([string]$IniPath)
    $text = Read-IniText -Path $IniPath
    return Get-ArchiveKeyMatch -Text $text
}

function Get-ArchiveListValues {
    param([string]$Line)
    $separator = $Line.IndexOf('=')
    if ($separator -lt 0) {
        return @()
    }
    $value = $Line.Substring($separator + 1)
    if ($value.Trim().Length -eq 0) {
        return @()
    }
    return @($value.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
}

function Get-ArchiveListCount {
    param([string]$IniPath, [string]$ArchiveName)
    $match = Get-ArchiveListMatch -IniPath $IniPath
    if ($null -eq $match) {
        return 0
    }
    return @(@(Get-ArchiveListValues -Line $match.Line) | Where-Object { $_ -ieq $ArchiveName }).Count
}

function Add-ArchiveListEntry {
    param([string]$IniPath, [string]$ArchiveName)
    $text = Read-IniText -Path $IniPath
    $match = Get-ArchiveKeyMatch -Text $text
    if ($null -eq $match) {
        $newline = "`n"
        if ($text.Contains("`r`n")) {
            $newline = "`r`n"
        }
        if ($text.Length -gt 0 -and -not $text.EndsWith($newline)) {
            $text += $newline
        }
        $text += $newline + '[Archive]' + $newline + "sResourceArchive2List = $ArchiveName" + $newline
        Write-IniText -Path $IniPath -Text $text
        return 'added'
    }
    $values = @(Get-ArchiveListValues -Line $match.Line)
    if (@($values | Where-Object { $_ -ieq $ArchiveName }).Count -gt 0) {
        return 'present'
    }
    $joined = @($values + @($ArchiveName)) -join ', '
    $replacement = 'sResourceArchive2List = ' + $joined
    $text = $text.Substring(0, $match.Index) + $replacement + $text.Substring($match.Index + $match.Length)
    Write-IniText -Path $IniPath -Text $text
    return 'added'
}

function Remove-ArchiveListEntry {
    param([string]$IniPath, [string]$ArchiveName)
    $text = Read-IniText -Path $IniPath
    $match = Get-ArchiveKeyMatch -Text $text
    if ($null -eq $match) {
        return 'absent'
    }
    $values = @(Get-ArchiveListValues -Line $match.Line)
    $kept = @($values | Where-Object { $_ -ine $ArchiveName })
    if ($kept.Count -eq $values.Count) {
        return 'absent'
    }
    $replacement = 'sResourceArchive2List = '
    if ($kept.Count -gt 0) {
        $replacement = 'sResourceArchive2List = ' + ($kept -join ', ')
    }
    $text = $text.Substring(0, $match.Index) + $replacement + $text.Substring($match.Index + $match.Length)
    Write-IniText -Path $IniPath -Text $text
    return 'removed'
}

function Get-ArchiveListEntries {
    param([string]$IniPath)
    $match = Get-ArchiveListMatch -IniPath $IniPath
    if ($null -eq $match) {
        return @()
    }
    return @(Get-ArchiveListValues -Line $match.Line)
}

function Get-IniTextHash {
    param([string]$IniPath)
    $bytes = [System.IO.File]::ReadAllBytes($IniPath)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
    } finally {
        $sha.Dispose()
    }
}

function Get-FileHashOrNull {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-ManifestPath {
    param([string]$GameRoot)
    return (Join-Path $GameRoot 'Data\XSCAL\76Radio\deployment.json')
}

function Read-DeploymentManifest {
    param([string]$GameRoot)
    $path = Get-ManifestPath -GameRoot $GameRoot
    if (-not (Test-Path -LiteralPath $path)) {
        return $null
    }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function Save-DeploymentManifest {
    param([string]$GameRoot, $Manifest)
    $path = Get-ManifestPath -GameRoot $GameRoot
    $directory = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

function Read-BuildManifest {
    param([string]$RepoRoot)
    $path = Join-Path $RepoRoot 'dist\build-manifest.json'
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Build manifest not found at $path. Run scripts\build.ps1 first."
    }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function Write-Section {
    param([string]$Text)
    Write-Host ""
    Write-Host $Text -ForegroundColor Cyan
}

function Write-Result {
    param([string]$Text, [string]$Level = 'info')
    switch ($Level) {
        'ok' { Write-Host "  [ok]   $Text" -ForegroundColor Green }
        'warn' { Write-Host "  [warn] $Text" -ForegroundColor Yellow }
        'fail' { Write-Host "  [fail] $Text" -ForegroundColor Red }
        default { Write-Host "  [info] $Text" }
    }
}
