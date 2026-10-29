param(
    [string]$GameRoot,
    [string]$ApiKey = $env:VT_API_KEY,
    [switch]$UploadToVirusTotal
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-RepoRoot
$distRoot = Join-Path $repoRoot 'dist'

if ($UploadToVirusTotal -and -not $ApiKey) {
    Write-Section 'VirusTotal'
    Write-Result 'VT_API_KEY is not set, so nothing was built or uploaded' 'fail'
    Write-Result 'store it once with: setx VT_API_KEY "your key"' 'info'
    exit 1
}

$buildArguments = @{}
if ($GameRoot) {
    $buildArguments.GameRoot = $GameRoot
}

& (Join-Path $PSScriptRoot 'build.ps1') @buildArguments
& (Join-Path $PSScriptRoot 'package.ps1')

$manifest = Read-BuildManifest -RepoRoot $repoRoot
$version = $manifest.version
$modulePath = Join-Path $distRoot 'version.dll'
$packagePath = Join-Path $distRoot ('76Radio-' + $version + '.zip')
if (-not (Test-Path -LiteralPath $modulePath)) {
    throw 'dist\version.dll is missing. The native build did not produce the surface module.'
}

$hash = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash.ToLower()
$hashUpper = $hash.ToUpper()
$reportUrl = "https://www.virustotal.com/gui/file/$hash"

$uploaded = $false
Write-Section 'VirusTotal'
if ($UploadToVirusTotal) {
    try {
        $boundary = [Guid]::NewGuid().ToString()
        $fileName = [IO.Path]::GetFileName($modulePath)
        $prefix = "--$boundary`r`nContent-Disposition: form-data; name=`"file`"; filename=`"$fileName`"`r`nContent-Type: application/octet-stream`r`n`r`n"
        $suffix = "`r`n--$boundary--`r`n"
        $prefixBytes = [Text.Encoding]::UTF8.GetBytes($prefix)
        $suffixBytes = [Text.Encoding]::UTF8.GetBytes($suffix)
        $fileBytes = [IO.File]::ReadAllBytes($modulePath)
        $body = New-Object byte[] ($prefixBytes.Length + $fileBytes.Length + $suffixBytes.Length)
        [Array]::Copy($prefixBytes, 0, $body, 0, $prefixBytes.Length)
        [Array]::Copy($fileBytes, 0, $body, $prefixBytes.Length, $fileBytes.Length)
        [Array]::Copy($suffixBytes, 0, $body, $prefixBytes.Length + $fileBytes.Length, $suffixBytes.Length)

        Invoke-RestMethod -Uri 'https://www.virustotal.com/api/v3/files' -Method Post -Headers @{ 'x-apikey' = $ApiKey } -ContentType "multipart/form-data; boundary=$boundary" -Body $body | Out-Null
        $uploaded = $true
        Write-Result ("uploaded version.dll, SHA-256 {0}" -f $hashUpper) 'ok'
        Write-Result 'one API request used, open the report after a minute to read the detections'
        Write-Result "report $reportUrl"
    } catch {
        Write-Result ("VirusTotal upload failed: {0}" -f $_.Exception.Message) 'fail'
        exit 1
    }
} else {
    Write-Result 'no upload requested, the daily API quota was not touched' 'warn'
    Write-Result 'upload dist\version.dll at https://www.virustotal.com/gui/home/upload'
    Write-Result 'or set VT_API_KEY and run the script with -UploadToVirusTotal'
    Write-Result "the report will live at $reportUrl"
}

$readmeSnippet = 'The {0} build has SHA-256 `{1}`, and a [VirusTotal report]({2}) is available for that exact file. Check that the report hash matches the `version.dll` you downloaded. A scan is not a guarantee of safety, which is why the source is published and can be rebuilt.' -f $version, $hashUpper, $reportUrl
$nexusSnippet = 'The {0} build has SHA-256 [font=Courier New]{1}[/font], and a [url={2}]VirusTotal report[/url] is available for that exact file. Check that the report hash matches the [font=Courier New]version.dll[/font] you downloaded. A scan is not a guarantee of safety, which is why the source is published and can be rebuilt.' -f $version, $hashUpper, $reportUrl
$packageHash = if (Test-Path -LiteralPath $packagePath) { (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToUpper() } else { '' }

$lines = @(
    "76Radio $version",
    "Generated $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm UTC'))",
    '',
    "version.dll  $hashUpper",
    "package      $packageHash",
    "report       $reportUrl"
)
if ($uploaded) {
    $lines += 'scan         upload requested, read the report for the detection counts'
} else {
    $lines += 'scan         not uploaded by this run'
}
$lines += @(
    '',
    'README.md paragraph',
    $readmeSnippet,
    '',
    'Nexus description paragraph',
    $nexusSnippet,
    '',
    'Replace the matching paragraph in README.md and release\nexus\description.bbcode, add the detection count and the date checked, then commit and tag.'
)
$reportPath = Join-Path $distRoot 'virustotal.txt'
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($reportPath, (($lines -join "`n") + "`n"), $encoding)

Write-Section 'Release'
Write-Result ("module SHA-256 {0}" -f $hashUpper)
Write-Result ("package SHA-256 {0}" -f $packageHash)
Write-Result "snippets $reportPath"
if (-not $uploaded) {
    Write-Host '  VirusTotal still needs the upload and the README update before a release.' -ForegroundColor Yellow
}
