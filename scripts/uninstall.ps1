#Requires -Version 7
#Requires -RunAsAdministrator
# Unregisters the proxy (regsvr32 /u -> DllUnregisterServer) and verifies by read-back.
# Always restores the Stadium CLSID to the Line 6 DLL in case install.ps1 -Stealth was used.
# Also the recovery step for a poisoned saved connection in Rocksmith+ (or edit
# Documents\My Games\Rocksmith+\state.cfg directly: asiodevicename / inputdevicename / outputdevicename).
param([string] $DllPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\release\CrashBarrier_x64.dll'))
$ErrorActionPreference = 'Stop'
$DllPath = (Resolve-Path $DllPath).Path

$inproc = 'HKLM:\SOFTWARE\Classes\CLSID\{bf6ad246-4cb9-426c-b41d-520e50f0d983}\InprocServer32'
$line6 = 'C:\WINDOWS\system32\StadiumAsio_OnInterposer3_64.dll'
if ((Test-Path $inproc) -and (Get-ItemProperty $inproc).'(default)' -ne $line6) {
    Set-ItemProperty $inproc -Name '(default)' -Value $line6
    if ((Get-ItemProperty $inproc).'(default)' -ne $line6) { throw 'stealth: failed to restore the Line 6 InprocServer32' }
    Write-Output 'Stealth: "ASIO Stadium" restored to the Line 6 DLL'
}

$p = Start-Process regsvr32.exe -ArgumentList '/s', '/u', "`"$DllPath`"" -Wait -PassThru -NoNewWindow
if ($p.ExitCode) { throw "regsvr32 /u failed with exit code $($p.ExitCode) (0x$('{0:X8}' -f $p.ExitCode))" }
Write-Output "Unregistered $DllPath"
& "$PSScriptRoot\verify-asio-registry.ps1"
exit $LASTEXITCODE
