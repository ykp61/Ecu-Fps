param([Parameter(Mandatory)][string]$Probe, [Parameter(Mandatory)][string]$Out)
# Uyumluluk B9 (bosluk 24): opt-in gercek GPU denge testi. ctest etiketi "real_gpu"; varsayilan kosuda ve
# scripts/ctest_evidence.ps1'de KOSMAZ. Calistirmak: EF_REAL_GPU=1 ve  ctest -L real_gpu
# Her donanim bagdastiricisinda (yuksek performans sirasi) ef_arena_probe --balance --real --quick --adapter N kosar.
# Pencere acmaz, ayar yazmaz; GPU'yu ~1 dk yukler, bu yuzden bir oyun acikken KOSULMAZ (bilinen oyun surecleri denetlenir).
# Sayilar bu test sahnesinin olcumudur; oyun FPS tahmini degildir.
if ($env:EF_REAL_GPU -ne '1') { Write-Host 'balance_real_gpu: skipped (opt-in: set EF_REAL_GPU=1, run ctest -L real_gpu)'; exit 77 }
$ErrorActionPreference = 'Stop'
$games = 'cs2', 'csgo', 'valorant', 'VALORANT-Win64-Shipping', 'FortniteClient-Win64-Shipping', 'r5apex', 'dota2', 'LeagueClient',
    'League of Legends', 'RainbowSix', 'eldenring', 'GTA5', 'RDR2', 'Overwatch', 'cod', 'TslGame', 'RocketLeague', 'destiny2'
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $games -contains $_.ProcessName }
if ($running) { Write-Host ("balance_real_gpu: skipped (game running: {0})" -f (($running | ForEach-Object ProcessName | Sort-Object -Unique) -join ', ')); exit 77 }

$list = & $Probe --list-adapters
$list | ForEach-Object { Write-Host $_ }
$m = $list | Select-String -Pattern '^adapters=(\d+)$' | Select-Object -First 1
if (-not $m) { throw 'balance_real_gpu: --list-adapters output not understood' }
$count = [int]$m.Matches[0].Groups[1].Value
if ($count -eq 0) { Write-Host 'balance_real_gpu: skipped (no hardware adapter)'; exit 77 }

$failed = @(); $ran = 0
for ($i = 0; $i -lt $count; $i++) {
    $dir = Join-Path $Out "balance_real_gpu_$i"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Write-Host "=== adapter $i ==="
    & $Probe --balance --real --quick --adapter $i --out $dir | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    Write-Host "adapter ${i}: exit $code"
    # 77 = bagdastirici puanlanamaz (yazilim / acilamadi): gecti sayilmaz, basarisiz da degil; ozet satirinda gorunur.
    if ($code -eq 0) { $ran++ } elseif ($code -ne 77) { $failed += $i }
}
if ($failed.Count) { throw ("balance_real_gpu: failed on adapter(s) {0}" -f ($failed -join ', ')) }
if ($ran -eq 0) { Write-Host 'balance_real_gpu: skipped (no adapter could be measured)'; exit 77 }
Write-Host "BALANCE_REAL_GPU OK ($ran of $count adapter(s) measured)"
