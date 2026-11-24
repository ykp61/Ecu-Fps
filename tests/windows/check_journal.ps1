param([Parameter(Mandatory)][string]$Probe)
# REC-01: gercek dizinde atomik journal, yazilamayan dizinde mutation engeli, makine dizini kapsami.
$ErrorActionPreference = 'Stop'
function Run($a) { (Start-Process $Probe -ArgumentList $a -PassThru -WindowStyle Hidden -Wait).ExitCode }

$dir = Join-Path ([IO.Path]::GetTempPath()) ("ef-journal-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $dir | Out-Null
try {
    $c = Run @('write', "`"$dir`""); if ($c -ne 0) { throw "journal write exit $c" }
    if (@(Get-ChildItem $dir -Filter *.tmp).Count) { throw 'temp file left behind' }
    if (@(Get-ChildItem $dir -Filter 'tx-*.json').Count -ne 1) { throw 'journal file missing' }
    Write-Host 'journal: atomic write + persist + scan on real disk'
} finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }

# Yazma izni kaldirilmis dizin: begin basarisiz -> mutation baslamaz
$ro = Join-Path ([IO.Path]::GetTempPath()) ("ef-journal-ro-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $ro | Out-Null
$me = [Security.Principal.WindowsIdentity]::GetCurrent().Name
icacls $ro /deny "${me}:(OI)(CI)(W,AD,WD)" | Out-Null
try {
    $c = Run @('blocked', "`"$ro`""); if ($c -ne 0) { throw "begin succeeded on unwritable dir (exit $c)" }
    Write-Host 'journal: unwritable dir -> begin refused, no mutation'
} finally {
    icacls $ro /remove:d $me | Out-Null
    Remove-Item -Recurse -Force $ro -ErrorAction SilentlyContinue
}

$c = Run @('machine'); if ($c -ne 0) { throw "machine journal scope exit $c" }
Write-Host 'journal: machine scope gives explicit result (Denied when unelevated)'
Write-Host 'JOURNAL OK'
