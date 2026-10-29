param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$baseUrl = 'https://raw.githubusercontent.com/GitCrazy-wc/hudmodloader/main/HUDTools/scripts'

$files = [ordered]@{
    'SharedHUDTools.as' = 'src\scaleform\hud\SharedHUDTools.as'
    'Shared\HUDModes.as' = 'src\scaleform\hud\Shared\HUDModes.as'
    'Shared\AS3\Data\BSUIDataManager.as' = 'src\scaleform\hud\Shared\AS3\Data\BSUIDataManager.as'
    'Shared\AS3\Data\BSUIEventDispatcherBackend.as' = 'src\scaleform\hud\Shared\AS3\Data\BSUIEventDispatcherBackend.as'
    'Shared\AS3\Data\FromClientDataEvent.as' = 'src\scaleform\hud\Shared\AS3\Data\FromClientDataEvent.as'
    'Shared\AS3\Data\TestProviderLoader.as' = 'src\scaleform\hud\Shared\AS3\Data\TestProviderLoader.as'
    'Shared\AS3\Data\UIDataFromClient.as' = 'src\scaleform\hud\Shared\AS3\Data\UIDataFromClient.as'
    'Shared\AS3\Data\UIDataShuttleConnector.as' = 'src\scaleform\hud\Shared\AS3\Data\UIDataShuttleConnector.as'
    'Shared\AS3\Data\UIDataShuttleTestConnector.as' = 'src\scaleform\hud\Shared\AS3\Data\UIDataShuttleTestConnector.as'
    'Shared\AS3\Data\UsesEventDispatcherBackend.as' = 'src\scaleform\hud\Shared\AS3\Data\UsesEventDispatcherBackend.as'
    'Shared\AS3\Events\CustomEvent.as' = 'src\scaleform\hud\Shared\AS3\Events\CustomEvent.as'
    'com\adobe\serialization\json\JSONDecoder.as' = 'src\scaleform\hud\com\adobe\serialization\json\JSONDecoder.as'
    'com\adobe\serialization\json\JSONParseError.as' = 'src\scaleform\hud\com\adobe\serialization\json\JSONParseError.as'
    'com\adobe\serialization\json\JSONToken.as' = 'src\scaleform\hud\com\adobe\serialization\json\JSONToken.as'
    'com\adobe\serialization\json\JSONTokenType.as' = 'src\scaleform\hud\com\adobe\serialization\json\JSONTokenType.as'
    'com\adobe\serialization\json\JSONTokenizer.as' = 'src\scaleform\hud\com\adobe\serialization\json\JSONTokenizer.as'
}

$downloaded = 0
foreach ($entry in $files.GetEnumerator()) {
    $target = Join-Path $repoRoot $entry.Value
    if (-not $Force -and (Test-Path -LiteralPath $target) -and
        (Get-Item -LiteralPath $target).Length -gt 0) {
        continue
    }

    $directory = Split-Path -Parent $target
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $relative = $entry.Key -replace '\\', '/'
    Write-Host "  $relative"
    try {
        Invoke-WebRequest -Uri "$baseUrl/$relative" -OutFile $target -UseBasicParsing
    } catch {
        if (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Force
        }
        throw "Failed to download $relative. $($_.Exception.Message)"
    }

    $text = Get-Content -LiteralPath $target -Raw
    if ($text -notmatch '^\s*package\b') {
        Remove-Item -LiteralPath $target -Force
        throw "Downloaded $relative is not ActionScript source"
    }
    $downloaded++
}

if ($downloaded -eq 0) {
    Write-Host '  dependency sources already present'
} else {
    Write-Host "  fetched $downloaded file(s)"
}
