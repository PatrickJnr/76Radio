$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build\ci'

Push-Location $repoRoot
try {
    cmake -S . -B $buildRoot -A x64
    if ($LASTEXITCODE -ne 0) {
        throw 'CMake configure failed'
    }

    cmake --build $buildRoot --config Release
    if ($LASTEXITCODE -ne 0) {
        throw 'Native build failed'
    }

    ctest --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw 'Native tests failed'
    }

    powershell -ExecutionPolicy Bypass -File tests\ini-tests.ps1
    if ($LASTEXITCODE -ne 0) {
        throw 'Archive list tests failed'
    }
} finally {
    Pop-Location
}
