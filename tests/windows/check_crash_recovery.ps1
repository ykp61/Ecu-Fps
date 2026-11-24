param([Parameter(Mandatory)][string]$Probe)
# REC-03: her journal yaziminda gercek surec olumu (TerminateProcess) + yeni surecte uzlastirma.
$ErrorActionPreference = 'Stop'
function Run([string[]]$a) { (Start-Process $Probe -ArgumentList $a -PassThru -WindowStyle Hidden -Wait).ExitCode }
$deaths = 0
for ($n = 0; $n -lt 12; $n++) {
    $dir = Join-Path ([IO.Path]::GetTempPath()) ("ef-crashrec-" + [guid]::NewGuid().ToString('N'))
    try {
        if ((Run @('setup', "`"$dir`"")) -ne 0) { throw 'setup failed' }
        $c = Run @('apply', "`"$dir`"", $n)
        if ($c -eq 0) { Write-Host "crash_at=${n}: completed without crash (all journal writes passed)"; break }
        if ($c -ne 77) { throw "apply crash_at=$n exit $c" }
        $deaths++
        $r = Run @('reconcile', "`"$dir`"")
        if ($r -ne 0) { throw "crash_at=${n}: reconcile exit $r (targets not restored)" }
    } finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
}
if ($deaths -lt 8) { throw "expected >= 8 crash points, got $deaths" }
Write-Host "real process deaths: $deaths, every one reconciled to the original targets"
Write-Host 'CRASH RECOVERY OK'
