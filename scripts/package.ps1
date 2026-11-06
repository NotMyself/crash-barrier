#Requires -Version 7
# Builds the downloadable package: dist\CrashBarrier-<version>-x64.zip and dist\SHA256SUMS.txt.
# The zip is flat: DLL + PDB + test host + the install scripts + README (from docs/INSTALL.md) + LICENSE.
param(
    [string] $Version = '',
    [string] $Bin = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\release'),
    [string] $Out = (Join-Path (Split-Path $PSScriptRoot -Parent) 'dist')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $Version) { $Version = & "$PSScriptRoot\version.ps1" }

$name = "CrashBarrier-$Version-x64"
$stage = Join-Path $Out $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item $stage -ItemType Directory -Force | Out-Null

$files = @(
    "$Bin\CrashBarrier_x64.dll", "$Bin\CrashBarrier_x64.pdb", "$Bin\asiohost_test.exe",
    "$PSScriptRoot\install.ps1", "$PSScriptRoot\uninstall.ps1", "$PSScriptRoot\verify-asio-registry.ps1",
    "$PSScriptRoot\collect-traces.ps1", "$repo\LICENSE"
)
foreach ($f in $files) { if (-not (Test-Path $f)) { throw "missing $f" }; Copy-Item $f $stage }
Copy-Item "$repo\docs\INSTALL.md" (Join-Path $stage 'README.md')
Set-Content (Join-Path $stage 'VERSION.txt') $Version -NoNewline

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content (Join-Path $Out 'SHA256SUMS.txt')
Write-Output "packaged $zip ($([int]((Get-Item $zip).Length / 1KB)) KB) sha256 $hash"
exit 0
