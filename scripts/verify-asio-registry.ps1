#Requires -Version 7
# Prints the full HKLM\SOFTWARE\ASIO state with each CLSID resolved to its InprocServer32 DLL.
# Exit 1 if any ASIO entry points at a missing CLSID or a missing DLL. Run after every
# install/uninstall: registry mutations are verified by read-back, never assumed.
$ErrorActionPreference = 'Stop'
$bad = 0
$rows = foreach ($key in Get-ChildItem 'HKLM:\SOFTWARE\ASIO') {
    $p = Get-ItemProperty $key.PSPath
    $inproc = "HKLM:\SOFTWARE\Classes\CLSID\$($p.CLSID)\InprocServer32"
    $dll = $null; $model = $null; $status = 'ok'
    if (-not $p.CLSID) { $status = 'NO CLSID VALUE'; $bad++ }
    elseif (-not (Test-Path $inproc)) { $status = 'CLSID NOT REGISTERED'; $bad++ }
    else {
        $s = Get-ItemProperty $inproc
        $dll = $s.'(default)'; $model = $s.ThreadingModel
        if (-not (Test-Path $dll)) { $status = 'DLL MISSING'; $bad++ }
    }
    [pscustomobject]@{ Name = $key.PSChildName; CLSID = $p.CLSID; Threading = $model; Dll = $dll; Status = $status }
}
$rows | Format-Table -AutoSize | Out-String -Width 300 | Write-Output
if ($bad) { Write-Error "$bad dangling ASIO registration(s)"; exit 1 }
exit 0
