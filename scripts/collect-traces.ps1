#Requires -Version 7
# Copies the newest proxy trace log(s) and any minidumps into traces\<tag>_<timestamp>.*
# (the repo's traces\ folder in a source checkout, or a traces\ folder next to this script otherwise).
# Example: .\collect-traces.ps1 -Tag ableton
param(
    [Parameter(Mandatory)] [string] $Tag,
    [int] $Count = 1
)
$ErrorActionPreference = 'Stop'
$src = Join-Path $env:LOCALAPPDATA 'CrashBarrier'
$repoTraces = Join-Path (Split-Path $PSScriptRoot -Parent) 'traces'
$dst = if (Test-Path $repoTraces) { $repoTraces } else { Join-Path $PSScriptRoot 'traces' }
New-Item $dst -ItemType Directory -Force | Out-Null
$logs = Get-ChildItem $src -Filter 'trace_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First $Count
if (-not $logs) { throw "no trace logs in $src" }
foreach ($log in $logs) {
    $stamp = $log.LastWriteTime.ToString('yyyyMMdd-HHmmss')
    $pid_ = ($log.BaseName -split '_')[1]
    Copy-Item $log.FullName (Join-Path $dst "${Tag}_${stamp}.log")
    Get-ChildItem $src -Filter "fault_${pid_}_*.dmp" | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $dst "${Tag}_${stamp}_$($_.Name)")
    }
    Write-Output "collected $($log.Name) -> $dst\${Tag}_${stamp}.log"
}
