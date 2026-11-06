#Requires -Version 7
# Hardware-free self-tests, run by CI. Each uses asiohost_test against the proxy DLL:
#   1. missing driver   -> init fails cleanly, exit 2
#   2. faulty driver    -> the SEH guard catches an access violation, writes a dump, host survives, exit 1
#   3. version resource -> the DLL reports the version it was built with
param(
    [string] $Bin = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\release'),
    [string] $Version = ''
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $Bin 'asiohost_test.exe'
$dll = Join-Path $Bin 'CrashBarrier_x64.dll'
$traceDir = Join-Path $env:LOCALAPPDATA 'CrashBarrier'
$failed = 0

function Check($name, $ok, $detail) {
    if ($ok) { Write-Output "PASS  $name" } else { Write-Output "FAIL  $name  ($detail)"; $script:failed++ }
}
function NewestTrace($since) {
    Get-ChildItem "$traceDir\trace_*.log" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $since } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

$t0 = Get-Date
& $exe --dll $dll --sysref null --driver 'C:\nonexistent\driver.dll' *> $null
Check 'missing driver returns ASIOFalse from init (exit 2)' ($LASTEXITCODE -eq 2) "exit $LASTEXITCODE"
$trace = NewestTrace $t0
Check 'missing driver is logged' ($trace -and (Select-String -Path $trace.FullName -Pattern 'LoadLibraryW.*failed' -Quiet)) 'no LoadLibraryW failure line'

$t1 = Get-Date
& $exe --dll $dll --sysref null --driver (Join-Path $Bin 'FaultyDriver_x64.dll') *> $null
Check 'faulty driver: host survives with exit 1' ($LASTEXITCODE -eq 1) "exit $LASTEXITCODE"
$trace = NewestTrace $t1
$caught = $trace -and (Select-String -Path $trace.FullName -Pattern 'FAULT in getChannels: access violation' -Quiet)
Check 'faulty driver: access violation caught in getChannels' $caught 'no FAULT line'
$dump = $null
if ($trace) {
    $m = Select-String -Path $trace.FullName -Pattern 'FAULT minidump: (.+\.dmp)' | Select-Object -First 1
    if ($m) { $dump = $m.Matches[0].Groups[1].Value }
}
Check 'faulty driver: minidump written' ($dump -and (Test-Path $dump) -and ((Get-Item $dump).Length -gt 0)) "dump path '$dump'"
$refused = $trace -and (Select-String -Path $trace.FullName -Pattern 'refused: proxy is faulted' -Quiet)
Check 'faulty driver: later calls short-circuit' $refused 'no refusal line'

$info = (Get-Item $dll).VersionInfo
if ($Version) { Check "DLL ProductVersion is $Version" ($info.ProductVersion -eq $Version) "got '$($info.ProductVersion)'" }
Check 'DLL has a FileVersion' ([bool]$info.FileVersion) 'empty'

Write-Output "$failed failure(s)"
exit ($failed -gt 0 ? 1 : 0)
