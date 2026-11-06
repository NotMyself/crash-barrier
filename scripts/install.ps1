#Requires -Version 7
#Requires -RunAsAdministrator
# Registers the built proxy DLL (regsvr32 -> DllRegisterServer) and verifies by read-back.
#
# -Stealth additionally points the Stadium's own CLSID at the proxy DLL and removes the separate
# "ASIO Stadium (Proxy)" entry, so hosts that activate "ASIO Stadium" get the proxy without knowing.
# uninstall.ps1 -Stealth restores the Line 6 registration.
param(
    [string] $DllPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\release\CrashBarrier_x64.dll'),
    [switch] $Stealth
)
$ErrorActionPreference = 'Stop'
$DllPath = (Resolve-Path $DllPath).Path
$p = Start-Process regsvr32.exe -ArgumentList '/s', "`"$DllPath`"" -Wait -PassThru -NoNewWindow
if ($p.ExitCode) { throw "regsvr32 failed with exit code $($p.ExitCode) (0x$('{0:X8}' -f $p.ExitCode))" }
Write-Output "Registered $DllPath"

if ($Stealth) {
    $inproc = 'HKLM:\SOFTWARE\Classes\CLSID\{bf6ad246-4cb9-426c-b41d-520e50f0d983}\InprocServer32'
    Set-ItemProperty $inproc -Name '(default)' -Value $DllPath
    if ((Get-ItemProperty $inproc).'(default)' -ne $DllPath) { throw 'stealth: InprocServer32 read-back mismatch' }
    Remove-Item 'HKLM:\SOFTWARE\ASIO\ASIO Stadium (Proxy)' -Recurse -ErrorAction SilentlyContinue
    if (Test-Path 'HKLM:\SOFTWARE\ASIO\ASIO Stadium (Proxy)') { throw 'stealth: proxy ASIO entry still present' }
    Write-Output 'Stealth: "ASIO Stadium" now resolves to the proxy DLL'
}
& "$PSScriptRoot\verify-asio-registry.ps1"
exit $LASTEXITCODE
