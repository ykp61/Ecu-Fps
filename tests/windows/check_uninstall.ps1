param([Parameter(Mandatory)][string]$Probe)
# REC-06: geri al + sil gercek dosya sisteminde; kismi basarisizlikta veri korunur.
$ErrorActionPreference = 'Stop'
function Run([string[]]$a) { (Start-Process $Probe -ArgumentList $a -PassThru -WindowStyle Hidden -Wait).ExitCode }
$key = 'HKCU:\Software\EcuFpsTest\Run'

# 1) Basarili akis
$root = Join-Path ([IO.Path]::GetTempPath()) ("ef-uninst-" + [guid]::NewGuid().ToString('N'))
try {
    if ((Run @('prepare', "`"$root`"")) -ne 0) { throw 'prepare failed' }
    if ((Get-Content "$root\targets\a.val" -Raw) -ne 'new-a') { throw 'change not applied' }
    if (-not (Get-ItemProperty $key -Name 'EcuFps' -ErrorAction SilentlyContinue)) { throw 'test autostart not set' }
    $c = Run @('uninstall', "`"$root`""); if ($c -ne 0) { throw "uninstall exit $c" }
    if ((Get-Content "$root\targets\a.val" -Raw) -ne 'old-a') { throw 'target not restored' }
    if (Test-Path "$root\EcuFps") { throw 'user data folder still present' }
    if (Get-ItemProperty $key -Name 'EcuFps' -ErrorAction SilentlyContinue) { throw 'autostart value still present' }
    Write-Host 'uninstall: restored, data folder and autostart removed, no trace left'
} finally {
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force 'HKCU:\Software\EcuFpsTest' -ErrorAction SilentlyContinue
}

# 2) Kismi basarisizlik: hedef salt okunur -> geri alma dogrulanamaz -> veri silinmez
$root = Join-Path ([IO.Path]::GetTempPath()) ("ef-uninst-" + [guid]::NewGuid().ToString('N'))
try {
    if ((Run @('prepare', "`"$root`"")) -ne 0) { throw 'prepare failed' }
    Set-ItemProperty "$root\targets\a.val" -Name IsReadOnly -Value $true
    $c = Run @('uninstall', "`"$root`""); if ($c -ne 5) { throw "partial failure should exit 5, got $c" }
    if (-not (Test-Path "$root\EcuFps\journal")) { throw 'journal deleted despite failed restore' }
    if (-not (Get-ItemProperty $key -Name 'EcuFps' -ErrorAction SilentlyContinue)) { throw 'autostart removed despite failed restore' }
    Write-Host 'uninstall: failed restore -> journal, data and autostart kept'
} finally {
    if (Test-Path "$root\targets\a.val") { Set-ItemProperty "$root\targets\a.val" -Name IsReadOnly -Value $false }
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force 'HKCU:\Software\EcuFpsTest' -ErrorAction SilentlyContinue
}
Write-Host 'UNINSTALL OK'
