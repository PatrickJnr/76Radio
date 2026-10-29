param(
    [string]$FlexHome
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $FlexHome) {
    $FlexHome = Join-Path $repoRoot 'work\flex'
}
$FlexHome = (Resolve-Path -LiteralPath $FlexHome).Path
$mxmlc = Join-Path $FlexHome 'bin\mxmlc.bat'
$playerglobal = Join-Path $FlexHome 'frameworks\libs\player\32.0\playerglobal.swc'
if (-not (Test-Path -LiteralPath $mxmlc)) {
    throw "Flex SDK not found at $FlexHome. Set -FlexHome to its directory."
}
if (-not (Test-Path -LiteralPath $playerglobal)) {
    throw "playerglobal.swc is missing at $playerglobal"
}

$sharedTools = Join-Path $repoRoot 'src\scaleform\hud\SharedHUDTools.as'
if (-not (Test-Path -LiteralPath $sharedTools)) {
    & (Join-Path $PSScriptRoot 'fetch-hud-deps.ps1')
}

$config = Join-Path $FlexHome 'frameworks\flex-config.xml'
$text = [System.IO.File]::ReadAllText($config)
if ($text -match '\{playerglobalHome\}' -or $text -match '\{targetPlayerMajorVersion\}') {
    $playerRoot = (Join-Path $FlexHome 'frameworks\libs\player') -replace '\\', '/'
    $text = $text.Replace('{playerglobalHome}', $playerRoot)
    $text = $text.Replace('{targetPlayerMajorVersion}.{targetPlayerMinorVersion}', '32.0')
    [System.IO.File]::WriteAllText($config, $text, (New-Object System.Text.UTF8Encoding($false)))
}

$source = Join-Path $repoRoot 'src\scaleform\hud\Radio76Hud.as'
$output = Join-Path $repoRoot 'assets\hud\76RadioHud.swf'
New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
$env:FLEX_HOME = $FlexHome
& cmd /c "`"$mxmlc`" -target-player=32.0 -swf-version=14 -use-network=false -static-link-runtime-shared-libraries=true -output=`"$output`" `"$source`""
if ($LASTEXITCODE -ne 0) {
    throw 'HUD relay SWF compilation failed'
}
$bytes = [System.IO.File]::ReadAllBytes($output)
if ($bytes.Length -lt 8 -or $bytes[3] -ne 14) {
    throw "HUD relay SWF has an unexpected version byte: $($bytes[3])"
}

$ffdecJar = Join-Path $repoRoot 'tools\vendor\ffdec\ffdec-cli.jar'
if (Test-Path -LiteralPath $ffdecJar) {
    $verifyRoot = Join-Path $repoRoot 'work\verify_hud'
    if (Test-Path -LiteralPath $verifyRoot) {
        Remove-Item -LiteralPath $verifyRoot -Recurse -Force
    }
    & java -jar $ffdecJar -export script $verifyRoot $output 2>&1 | Out-Null
    $exported = Join-Path $verifyRoot 'scripts\Radio76Hud.as'
    if (-not (Test-Path -LiteralPath $exported)) {
        throw 'HUD relay SWF does not expose the Radio76Hud class'
    }
    if (-not (Select-String -LiteralPath $exported -Pattern 'modStorage' -Quiet)) {
        throw 'HUD relay SWF is missing the modStorage access'
    }
    Write-Host '  relay class and bridge access verified'
}

Write-Host "built $output ($($bytes.Length) bytes, SWF version $($bytes[3]))"
