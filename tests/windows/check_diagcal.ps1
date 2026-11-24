# DIAG-06/BM-02: kalibrasyon araci sozlesmesi.
#  - Sentetik fixture gecerli veri olarak okunur ama yayin kapisi ASLA acilmaz.
#  - Bos ornek listesi bicim hatasi olarak reddedilir (sessiz kabul yok).
param(
    [Parameter(Mandatory = $true)][string]$Tool,
    [Parameter(Mandatory = $true)][string]$Dataset,
    [string]$DatasetV2,
    [string]$DatasetLeak,
    [string]$DatasetV1Real
)
$ErrorActionPreference = 'Stop'

$out = & $Tool $Dataset 2>&1 | Out-String
$code = $LASTEXITCODE
Write-Host $out
if ($code -ne 3) { throw "synthetic dataset exit code $code, expected 3 (valid but gate closed)" }
if ($out -notmatch 'gate ready=0 reason=diag\.gate\.no_real_holdout') {
    throw 'expected gate closed with no_real_holdout'
}
if ($out -notmatch 'real_holdout=0') { throw 'synthetic dataset must report real_holdout=0' }
if ($out -notmatch 'dataset samples=5') { throw 'expected 5 parsed samples' }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("ef-diagcal-" + [guid]::NewGuid().ToString('N') + ".json")
try {
    Set-Content -Path $tmp -Encoding Ascii -Value '{"schema_version":1,"format":"diagcal_v1","samples":[]}'
    # Beklenen stderr, PowerShell 5.1'de ErrorRecord'a donusur; EAP=Stop bunu istisnaya cevirmesin.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $bad = & $Tool $tmp 2>&1 | Out-String
        $badCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    Write-Host $bad
    if ($badCode -ne 2) { throw "empty dataset exit code $badCode, expected 2" }
} finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $tmp
}
if ($DatasetV2) {
    $o2 = & $Tool $DatasetV2 2>&1 | Out-String
    $c2 = $LASTEXITCODE
    Write-Host $o2
    if ($c2 -ne 3) { throw "v2 dataset exit code $c2, expected 3" }
    foreach ($p in @('dataset samples=4 format=diagcal_v2', 'integrity ok=1', 'gate ready=0 reason=diag\.gate\.no_real_holdout', 'model memory_storage scored=0')) {
        if ($o2 -notmatch $p) { throw "v2 dataset missing: $p" }
    }
}
if ($DatasetLeak) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $o3 = & $Tool $DatasetLeak 2>&1 | Out-String
        $c3 = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    Write-Host $o3
    if ($c3 -ne 5) { throw "leak dataset exit code $c3, expected 5" }
    if ($o3 -notmatch 'integrity issue=run_group_multi_machine') { throw 'expected run_group_multi_machine' }
}
if ($DatasetV1Real) {
    # EF-X20 dogrulama bulgusu 4: grupsuz v1 gercek ornek sizinti degildir (exit 3, not; exit 5 degil).
    $o4 = & $Tool $DatasetV1Real 2>&1 | Out-String
    $c4 = $LASTEXITCODE
    Write-Host $o4
    if ($c4 -ne 3) { throw "v1 real dataset exit code $c4, expected 3" }
    foreach ($p in @('format=diagcal_v1', 'integrity ok=1 issues=0', 'integrity note=ungrouped_real_sample', 'gate ready=0 reason=diag\.gate\.no_real_holdout')) {
        if ($o4 -notmatch $p) { throw "v1 real dataset missing: $p" }
    }
    if ($o4 -match 'split_leakage') { throw 'v1 real dataset must not report split_leakage' }
}
exit 0
