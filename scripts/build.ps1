param(
    [string]$GameRoot,
    [switch]$SkipNative,
    [switch]$SkipSwf
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$workRoot = Join-Path $repoRoot 'work'
$distRoot = Join-Path $repoRoot 'dist'
$ffdecJar = Join-Path $repoRoot 'tools\vendor\ffdec\ffdec-cli.jar'
$version = '0.1.0'

function Write-Step {
    param([string]$Text)
    Write-Host "== $Text" -ForegroundColor Cyan
}

Write-Step 'Preparing directories'
foreach ($path in @($workRoot, $distRoot, (Join-Path $distRoot 'ba2\interface'))) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
}

$manifest = [ordered]@{
    product = '76Radio'
    version = $version
    builtAt = (Get-Date).ToUniversalTime().ToString('o')
    artifacts = @()
    surfaceModuleStatus = 'not-built'
}

Write-Step 'Building native targets'
if (-not $SkipNative) {
    Push-Location $repoRoot
    try {
        $configure = & cmake --preset msvc-release 2>&1
        if ($LASTEXITCODE -ne 0) {
            $configure | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }
            throw 'CMake configure failed'
        }
        $build = & cmake --build --preset msvc-release 2>&1
        if ($LASTEXITCODE -ne 0) {
            $build | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
            throw 'Native build failed'
        }
        $tests = & ctest --preset msvc-release 2>&1
        if ($LASTEXITCODE -ne 0) {
            $tests | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
            throw 'Native tests failed'
        }
        Write-Host '  native build and tests passed'
    } finally {
        Pop-Location
    }
}

Write-Step 'Building Scaleform archive'
if (-not $SkipSwf) {
    if (-not (Test-Path -LiteralPath $ffdecJar)) {
        throw 'FFDec is not available. Place ffdec-cli.jar under tools\vendor\ffdec.'
    }
    & (Join-Path $PSScriptRoot 'build-hud.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw 'HUD relay build failed'
    }
    $resolvedGameRoot = Resolve-GameRoot -GameRoot $GameRoot
    $interfaceArchive = Join-Path $resolvedGameRoot 'Data\SeventySix - Interface.ba2'
    if (-not (Test-Path -LiteralPath $interfaceArchive)) {
        throw "Interface archive not found at $interfaceArchive"
    }
    $extractRoot = Join-Path $workRoot 'extracted'
    $sourceSwf = Join-Path $extractRoot 'interface\newpipboy_radiopage.swf'
    $readme = Join-Path $extractRoot 'NOTICE.txt'
    if (-not (Test-Path -LiteralPath $sourceSwf)) {
        New-Item -ItemType Directory -Path (Join-Path $extractRoot 'interface') -Force | Out-Null
        & python (Join-Path $repoRoot 'tools\ba2\ba2inspect.py') $interfaceArchive extract newpipboy_radiopage.swf $extractRoot
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to extract the interface SWF'
        }
        Set-Content -LiteralPath $readme -Value 'Extracted from the locally installed Fallout 76 interface archive for build input only. Not redistributed.' -Encoding UTF8
    }
    $patchedSwf = Join-Path $repoRoot 'dist\ba2\interface\newpipboy_radiopage.swf'
    & java -jar $ffdecJar -importScript $sourceSwf $patchedSwf (Join-Path $repoRoot 'src\scaleform\pipboy') 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $patchedSwf)) {
        throw 'ActionScript recompilation failed'
    }
    $verifyRoot = Join-Path $workRoot 'verify_release'
    if (Test-Path -LiteralPath $verifyRoot) {
        Remove-Item -LiteralPath $verifyRoot -Recurse -Force
    }
    & java -jar $ffdecJar -export script $verifyRoot $patchedSwf 2>&1 | Out-Null
    $compiledScript = Join-Path $verifyRoot 'scripts\NewPipboy_RadioPage.as'
    if (-not (Test-Path -LiteralPath $compiledScript)) {
        throw 'Recompiled script verification failed'
    }
    if (-not (Select-String -LiteralPath $compiledScript -Pattern 'SharedHUDTools' -Quiet) -or
        -not (Select-String -LiteralPath $compiledScript -Pattern 'r76radio.state' -Quiet)) {
        throw 'Recompiled script is missing the current state and relay code'
    }
    Write-Host '  patched SWF verified'
} else {
    Write-Host '  skipped'
}

Write-Step 'Packing BA2 archive'
$staleFont = Join-Path $distRoot 'ba2\interface\fonts_ja.swf'
if (Test-Path -LiteralPath $staleFont) {
    Remove-Item -LiteralPath $staleFont -Force
}
$hudSwf = Join-Path $repoRoot 'assets\hud\76RadioHud.swf'
if (-not (Test-Path -LiteralPath $hudSwf)) {
    throw 'HUD relay SWF missing. Run scripts\build-hud.ps1 to produce assets\hud\76RadioHud.swf.'
}
Copy-Item -LiteralPath $hudSwf -Destination (Join-Path $distRoot 'ba2\interface\76RadioHud.swf') -Force
$ba2Path = Join-Path $distRoot '76Radio.ba2'
& python (Join-Path $repoRoot 'tools\ba2\ba2pack.py') pack (Join-Path $distRoot 'ba2') $ba2Path --include newpipboy_radiopage --include 76RadioHud --verify-root (Join-Path $distRoot 'ba2')
if ($LASTEXITCODE -ne 0) {
    throw 'BA2 packaging failed'
}
$manifest.artifacts += [pscustomobject]@{
    kind = 'archive'
    name = '76Radio.ba2'
    relativeTarget = 'Data\76Radio.ba2'
    sha256 = (Get-FileHash -LiteralPath $ba2Path -Algorithm SHA256).Hash
    requiresArchiveList = $true
}
$hudLoosePath = Join-Path $distRoot '76RadioHud.swf'
Copy-Item -LiteralPath $hudSwf -Destination $hudLoosePath -Force
$manifest.artifacts += [pscustomobject]@{
    kind = 'hud-relay'
    name = '76RadioHud.swf'
    relativeTarget = 'Data\interface\76RadioHud.swf'
    sha256 = (Get-FileHash -LiteralPath $hudLoosePath -Algorithm SHA256).Hash
}

$nativeBin = Join-Path $repoRoot 'build\msvc-release\bin\Release'
$surfaceModulePath = Join-Path $nativeBin 'version.dll'
if (Test-Path -LiteralPath $surfaceModulePath) {
    Copy-Item -LiteralPath $surfaceModulePath -Destination (Join-Path $distRoot 'version.dll') -Force
    $manifest.artifacts += [pscustomobject]@{
        kind = 'surface-module'
        name = 'version.dll'
        relativeTarget = 'version.dll'
        sha256 = (Get-FileHash -LiteralPath (Join-Path $distRoot 'version.dll') -Algorithm SHA256).Hash
    }
    $manifest.surfaceModuleStatus = 'built'
} else {
    $manifest.surfaceModuleStatus = 'not-built'
}

$settingsTemplate = Join-Path $repoRoot 'config\76Radio.ini'
if (-not (Test-Path -LiteralPath $settingsTemplate)) {
    throw 'config\76Radio.ini is missing'
}
Copy-Item -LiteralPath $settingsTemplate -Destination (Join-Path $distRoot '76Radio.ini') -Force
$manifest.settingsTemplate = '76Radio.ini'

$stationsTemplate = Join-Path $repoRoot 'config\stations.example.json'
if (-not (Test-Path -LiteralPath $stationsTemplate)) {
    throw 'config\stations.example.json is missing'
}
Copy-Item -LiteralPath $stationsTemplate -Destination (Join-Path $distRoot 'stations.json') -Force

$manifestPath = Join-Path $distRoot 'build-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ''
Write-Host 'Build complete' -ForegroundColor Green
Write-Host "  archive      $ba2Path"
Write-Host "  manifest     $manifestPath"
if ($manifest.surfaceModuleStatus -ne 'built') {
    Write-Host '  surface module not built' -ForegroundColor Yellow
}
