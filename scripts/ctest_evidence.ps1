<#
.SYNOPSIS
  Tam dist ctest kosusu + HEAD'e bagli kanit dosyasi (yayin kapisi icin).

.DESCRIPTION
  build\x64-dist\Testing\Temporary eski sonuclari silinir (eski "LastTestsFailed" yeni kosuyla celismesin), sonra
  ctest uia_tour etiketi disinda (pencere acan turlar) calisir. Cikti build\x64-dist\ctest-evidence-<HEAD12>.log
  dosyasina yazilir. publish_release.ps1 -Publish -SkipBuild bu dosyada "100% tests passed" gormeden yayinlamaz.
  Calisma agaci kirliyse kanit yazilmaz (kanit belirli bir commit'e aittir).
#>
[CmdletBinding()]
param([string]$Preset = 'x64-dist')

$ErrorActionPreference = 'Stop'
# Arac yoksa kanit URETILMEZ: 'ctest' bulunamayinca LASTEXITCODE bos kalir ve 'exit $null' sifir (sahte gecis) verirdi.
# ctest ve dumpbin (windows.offline testi) PATH'te olmali; yoksa cikis 3, gunluk dosyasi yazilmaz.
foreach ($tool in 'ctest', 'dumpbin') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Host "HATA: $tool PATH'te yok (VS gelistirici ortami gerekir); kanit uretilmedi." -ForegroundColor Red
        exit 3
    }
}
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build\$Preset"
if (-not (Test-Path -LiteralPath $BuildDir)) { Write-Host "HATA: $BuildDir yok; once derleyin." -ForegroundColor Red; exit 2 }
if (& git -C $Root status --porcelain) { Write-Host 'HATA: calisma agaci kirli; kanit bir commit icin uretilir.' -ForegroundColor Red; exit 2 }
$head = (& git -C $Root rev-parse --short=12 HEAD).Trim()

$tmp = Join-Path $BuildDir 'Testing\Temporary'
if (Test-Path -LiteralPath $tmp) { Get-ChildItem -LiteralPath $tmp -File | Remove-Item -Force }

if (-not $env:EF_TEST_BACKGROUND) { $env:EF_TEST_BACKGROUND = '1' }
if (-not $env:EF_UPDATE_CHECK) { $env:EF_UPDATE_CHECK = '0' }
$log = Join-Path $BuildDir "ctest-evidence-$head.log"
$started = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
# real_gpu (Uyumluluk B9): opt-in gercek GPU denge testi; kanit kosusu GPU'yu yuklemez ve donanima bagli kalmaz.
# ctest stderr satirlari (ornek: basarisiz test ozeti) 'Stop' altinda kosuyu sonlandirip kaniti yazdirmiyordu:
# yalniz bu cagri icin Continue; sonuc cikis koduyla degerlendirilir.
$ErrorActionPreference = 'Continue'
$out = & ctest --test-dir $BuildDir -LE 'uia_tour|real_gpu' --output-on-failure 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($null -eq $code) { Write-Host 'HATA: ctest cikis kodu alinamadi; kanit yazilmadi.' -ForegroundColor Red; exit 3 }
@("HEAD $head", "preset $Preset", "started $started", "exit $code", '') + $out | Set-Content -LiteralPath $log -Encoding UTF8
$summary = $out | Where-Object { $_ -match 'tests passed' } | Select-Object -Last 1
# Sifir cikis ama ctest ozet satiri yoksa (ctest kosmamis) gecis sayilmaz.
if ($code -eq 0 -and -not $summary) { Write-Host 'HATA: cikis 0 ama ctest ozet satiri yok; gecis sayilmaz.' -ForegroundColor Red; exit 3 }
Write-Host "$summary  ->  $log"
exit $code
