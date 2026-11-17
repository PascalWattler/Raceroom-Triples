# Builds build\d3d9.dll and runs the tests. Needs Zig (0.15.2 is the tested
# version) on the PATH, or its path in the environment variable ZIG_BIN.
#
#   .\build.ps1              build, run the parser tests and the smoke test
#   .\build.ps1 -NoSmokeTest skip the smoke test (it needs a graphics adapter)
param([switch]$NoSmokeTest)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$zig = if ($env:ZIG_BIN) { $env:ZIG_BIN } else { 'zig' }
$warnings = '-std=c++17', '-Wall', '-Wextra', '-Werror'
New-Item -ItemType Directory -Force build | Out-Null

# Runs a program and stops the script if it reports failure.
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

# Parser and rule tests.
Invoke-Checked $zig (@('c++') + $warnings + 'source/config-test.cpp', '-o', 'build/config-test.exe')
Invoke-Checked 'build\config-test.exe' @('package\RaceRoomTriple.ini')

# The DLL, always for 64-bit Windows.
Invoke-Checked $zig (@('c++', '-target', 'x86_64-windows-gnu', '-shared', '-O2') + $warnings +
    'source/proxy.cpp', 'source/version.rc', 'source/exports.def', '-o', 'build/d3d9.dll', '-ldxguid', '-luuid')

# The smoke test loads the fresh DLL with the example INI next to it and
# drives it through real Direct3D, without the game.
Invoke-Checked $zig (@('c++', '-target', 'x86_64-windows-gnu', '-municode', '-O2') + $warnings +
    'source/smoke-test.cpp', '-o', 'build/smoke-test.exe')
if (-not $NoSmokeTest) {
    Copy-Item package\RaceRoomTriple.ini build\RaceRoomTriple.ini -Force
    Invoke-Checked 'build\smoke-test.exe' @((Resolve-Path 'build\d3d9.dll').Path)
}

Write-Host "Built build\d3d9.dll"
