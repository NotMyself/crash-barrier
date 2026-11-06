#Requires -Version 7
# Copies the newest proxy trace log(s) and any minidumps into traces/<tag>_<timestamp>.*
# Example: .\collect-traces.ps1 -Tag ableton
param(
    [Parameter(Mandatory)] [string] $Tag,
    [int] $Count = 1
)
$ErrorActionPreference = 'Stop'
$src = Join-Path $env:LOCALAPPDATA 'CrashBarrier'
$dst = Join-Path (Split-Path $PSScriptRoot -Parent) 'traces'
$logs = Get-ChildItem $src -Filter 'trace_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First $Count
if (-not $logs) { throw "no trace logs in $src" }
foreach ($log in $logs) {
    $stamp = $log.LastWriteTime.ToString('yyyyMMdd-HHmmss')
    $pid_ = ($log.BaseName -split '_')[1]
    Copy-Item $log.FullName (Join-Path $dst "${Tag}_${stamp}.log")
    Get-ChildItem $src -Filter "fault_${pid_}_*.dmp" | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $dst "${Tag}_${stamp}_$($_.Name)")
    }
    Write-Output "collected $($log.Name) -> ${Tag}_${stamp}.log"
}
