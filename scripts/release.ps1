param(
    [string]$GameRoot,
    [string]$XScalSdk = $env:RADIO76_XSCAL_SDK_DIR
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$distRoot = Join-Path $repoRoot 'dist'

$buildArguments = @{}
if ($GameRoot) {
    $buildArguments.GameRoot = $GameRoot
}
if ($XScalSdk) {
    $buildArguments.XScalSdk = $XScalSdk
}

& (Join-Path $PSScriptRoot 'build.ps1') @buildArguments
& (Join-Path $PSScriptRoot 'package.ps1')

$manifest = Read-BuildManifest -RepoRoot $repoRoot
$version = $manifest.version
$modulePath = Join-Path $distRoot '76Radio.dll'
$packagePath = Join-Path $distRoot ('76Radio-' + $version + '.zip')
if (-not (Test-Path -LiteralPath $modulePath)) {
    throw 'dist\76Radio.dll is missing. The native build did not produce the xScal plugin.'
}
if (-not (Test-Path -LiteralPath ($modulePath + '.sig'))) {
    throw 'dist\76Radio.dll.sig is missing. Sign the plugin before a release.'
}
if (-not (Test-Path -LiteralPath $packagePath)) {
    throw "dist\76Radio-$version.zip is missing."
}

$moduleHash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
$packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash

Write-Section 'Release'
Write-Result ("version {0}" -f $version)
Write-Result ("module SHA-256  {0}" -f $moduleHash)
Write-Result ("package SHA-256 {0}" -f $packageHash)
Write-Result ("checksums       {0}" -f (Join-Path $distRoot 'SHA256SUMS.txt'))
Write-Result 'upload the zip and SHA256SUMS.txt, and do not rebuild afterwards, because the hashes change with every build'
