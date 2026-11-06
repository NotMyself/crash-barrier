#Requires -Version 7
# Configure + build with the VS x64 toolchain, CMake and Ninja.
param([ValidateSet('release', 'debug')] [string] $Config = 'release')
$ErrorActionPreference = 'Stop'

$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$env:Path = "$installer;$env:Path"  # Launch-VsDevShell expects vswhere.exe on PATH
$vs = & "$installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the C++ x64 toolset found.' }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation | Out-Null

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    cmake --preset "ninja-$Config"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    cmake --build --preset "ninja-$Config"
    exit $LASTEXITCODE
}
finally { Pop-Location }
