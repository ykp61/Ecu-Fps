param([Parameter(Mandatory)][string]$Exe)
# SEC-01: rol sozlesmesi gercek EXE ile. UAC penceresi ACILMAZ (runas cagrilmaz).
$ErrorActionPreference = 'Stop'
function Run([string]$a) {
    $p = Start-Process -FilePath $Exe -ArgumentList $a -PassThru -WindowStyle Hidden
    if (-not $p.WaitForExit(15000)) { $p.Kill(); throw "timeout: $a" }
    return $p.ExitCode
}
$me = [Security.Principal.WindowsIdentity]::GetCurrent()
$elevated = ([Security.Principal.WindowsPrincipal]$me).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$nonce = '0123456789abcdef0123456789abcdef'

$c = Run "--role=admin --parent=1 --nonce=$nonce"; if ($c -ne 2) { throw "unknown role exit $c" }
$c = Run "--role=broker --parent=1 --nonce=$nonce --exec=cmd.exe"; if ($c -ne 2) { throw "extra arg exit $c" }
$c = Run "--role=broker --parent=$PID --nonce=$nonce"
if ($elevated) { if ($c -ne 12) { throw "elevated broker with foreign parent must exit 12, got $c" } }
else { if ($c -ne 10) { throw "unelevated broker must refuse (10), got $c" } }
$c = Run "--role=bench --parent=$PID --nonce=$nonce"; if ($c -ne 12) { throw "bench with foreign parent must exit 12, got $c" }
$c = Run "--smoke"; if ($c -ne 0) { throw "smoke exit $c" }
Write-Host "roles OK (runner elevated=$elevated)"
