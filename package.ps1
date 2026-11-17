# Builds the DLL, runs the tests and puts together the ZIP that users
# download: build\RaceRoom-Triples-<version>.zip, plus its SHA256 checksum in
# a text file next to it. The version comes from source\version.h.
#
#   .\package.ps1              build, test, package
#   .\package.ps1 -NoSmokeTest skip the smoke test (it needs a graphics adapter)
param([switch]$NoSmokeTest)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

& .\build.ps1 -NoSmokeTest:$NoSmokeTest

$version = (Select-String -Path source\version.h -Pattern '#define RRT_VERSION "(.+)"').Matches[0].Groups[1].Value
$name = "RaceRoom-Triples-$version"
$stage = "build\$name"
$zip = "build\$name.zip"

# Start from an empty folder so nothing stale ends up in the ZIP.
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item build\d3d9.dll, package\RaceRoomTriple.ini, package\README.txt $stage
Copy-Item LICENSE "$stage\LICENSE.txt"
Copy-Item package\licenses $stage -Recurse

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content "build\$name.zip.sha256" -Encoding ascii

Write-Host "Packaged $zip"
Write-Host "SHA256   $hash"
