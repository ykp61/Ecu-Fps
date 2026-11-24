param([Parameter(Mandatory)][string]$Probe)
# SEC-03: kilit, oyuncu hive'i ve dosya hedef kacisi gercek Windows ile.
$ErrorActionPreference = 'Stop'

# --- 1) Eszamanli yazma kilidi (ayni oturumda Local\ ad; broker Global\ kullanir) ---
$name = "Local\EcuFps.Test." + [guid]::NewGuid().ToString('N')
$out = Join-Path ([IO.Path]::GetTempPath()) ("ef-lock-" + [guid]::NewGuid().ToString('N') + ".txt")
$a = Start-Process $Probe -ArgumentList 'lock', $name, 1500, "`"$out`"" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 400
$b = Start-Process $Probe -ArgumentList 'lock', $name, 0, "`"$out`"" -PassThru -WindowStyle Hidden -Wait
$a.WaitForExit(10000) | Out-Null
$lines = Get-Content $out
Remove-Item $out -ErrorAction SilentlyContinue
if ($b.ExitCode -ne 3 -or -not ($lines -contains 'busy')) { throw "second writer was not blocked: $($lines -join '|')" }
Write-Host "lock: second writer blocked while first holds it"
$c = Start-Process $Probe -ArgumentList 'lock', $name, 0, "`"$env:TEMP\ef-lock-after.txt`"" -PassThru -WindowStyle Hidden -Wait
Remove-Item "$env:TEMP\ef-lock-after.txt" -ErrorAction SilentlyContinue
if ($c.ExitCode -ne 0) { throw "lock not released after holder exit" }
Write-Host "lock: released after holder exit"

# --- 2) Oyuncunun hive'i HKU\SID uzerinden ---
$h = Start-Process $Probe -ArgumentList 'hive' -PassThru -WindowStyle Hidden -Wait
if ($h.ExitCode -ne 0) { throw "user hive check exit $($h.ExitCode)" }
Write-Host "hive: HKU\<session SID> matches HKCU; SYSTEM SID refused"

# --- 3) Dosya hedef kacisi: junction ve hardlink ---
$root = Join-Path ([IO.Path]::GetTempPath()) ("ef-scope-" + [guid]::NewGuid().ToString('N'))
$outside = Join-Path ([IO.Path]::GetTempPath()) ("ef-outside-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory "$root\cfg", $outside | Out-Null
Set-Content "$root\cfg\game.cfg" 'fps_max 0'
Set-Content "$outside\secret.cfg" 'x'
cmd /c mklink /J "$root\link" "$outside" | Out-Null
cmd /c mklink /H "$root\cfg\hard.cfg" "$outside\secret.cfg" | Out-Null
function Probe-File($rel, $expectOk) {
    $p = Start-Process $Probe -ArgumentList 'file', "`"$root`"", "`"$rel`"" -PassThru -WindowStyle Hidden -Wait
    if ($expectOk -and $p.ExitCode -ne 0) { throw "$rel should open (exit $($p.ExitCode))" }
    if (-not $expectOk -and $p.ExitCode -ne 9) { throw "$rel should be rejected (exit $($p.ExitCode))" }
    Write-Host ("file {0,-18} {1}" -f $rel, $(if ($expectOk) { 'ok' } else { 'rejected' }))
}
try {
    Probe-File 'cfg\game.cfg' $true
    Probe-File 'link\secret.cfg' $false     # junction ile kok disi
    Probe-File 'cfg\hard.cfg' $false        # hardlink
    Probe-File '..\x.cfg' $false
    Probe-File 'C:\Windows\win.ini' $false
} finally {
    cmd /c rmdir "$root\link" | Out-Null
    Remove-Item -Recurse -Force $root, $outside -ErrorAction SilentlyContinue
}
Write-Host 'SCOPE OK'
