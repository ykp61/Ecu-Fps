<#
.SYNOPSIS
  Flake avı (P9-B9): bir ctest testini N kez art arda koşar, her koşunun sonucunu ve süresini yazar, başarısızlık
  çıktısını saklar. ctest --repeat until-fail ilk hatada durur; bu betik hata sonrası da sürer ve oran verir.

.DESCRIPTION
  Pencere açan testler (windows.ui_lifecycle, windows.first_run, ui_snapshot) için varsayılan test seti.
  OYUN ÇALIŞIRKEN pencere açan test koşulmaz: bilinen oyun kitaplığı klasörlerinden (steamapps\common, Epic Games,
  Riot Games, XboxGames, ...) çalışan bir süreç ya da -GameProcess adı varsa betik 77 ile çıkar, hiçbir şey koşmaz.
  Test koşusu paralel değildir (tek ctest, tek test). Sistem ayarı değiştirmez.

.EXAMPLE
  .\scripts\flake_hunt.ps1 -Repeat 50 -Log artifacts\evidence\P9-flake-50.log
.EXAMPLE
  .\scripts\flake_hunt.ps1 -Tests 'windows.ui_lifecycle' -Repeat 20 -BuildDir build\x64-debug
#>
[CmdletBinding()]
param(
    [string[]]$Tests = @('windows.ui_lifecycle', 'windows.first_run'),
    [int]$Repeat = 50,
    [string]$BuildDir = 'build\x64-debug',
    [string]$Log = '',
    [string[]]$GameProcess = @(),
    [switch]$AllowGame,
    [int]$StartRun = 1,       # kesilen kosuyu surdurmek icin: bu numaradan basla
    [switch]$Append           # mevcut -Log dosyasina ekle (surdurme)
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$bd = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $Root $BuildDir }
if (-not (Test-Path -LiteralPath $bd)) { Write-Host "flake_hunt: HATA: $bd yok; once derleyin." -ForegroundColor Red; exit 2 }

# ctest PATH'te degilse (VS gelistirici kabugu disinda) Build Tools'un CMake'ini bul.
$ctest = (Get-Command ctest.exe -ErrorAction SilentlyContinue).Source
if (-not $ctest) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        $cand = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
        if ($vs -and (Test-Path -LiteralPath $cand)) { $ctest = $cand }
    }
}
if (-not $ctest) { Write-Host 'flake_hunt: HATA: ctest bulunamadi (VS gelistirici kabugunda calistirin).' -ForegroundColor Red; exit 2 }

# --- oyun denetimi (pencere açan test koşmadan önce) ---
$libPattern = '[\\/](steamapps[\\/]common|Epic Games|Riot Games|XboxGames|GOG Games|GOG Galaxy[\\/]Games|Ubisoft Game Launcher[\\/]games)[\\/]'
$games = @()
foreach ($pr in Get-Process -ErrorAction SilentlyContinue) {
    $path = $null; try { $path = $pr.Path } catch {}
    if (($path -and $path -match $libPattern -and $path -notmatch 'Launcher|Setup|Redist|Crash|EasyAntiCheat|BEService') -or ($GameProcess -contains $pr.ProcessName)) {
        $games += "$($pr.ProcessName) ($($pr.Id))"
    }
}
if ($games.Count -gt 0 -and -not $AllowGame) {
    Write-Host "flake_hunt: ATLANDI: oyun calisiyor ($($games -join ', ')); pencere acan testler kosulmaz." -ForegroundColor Yellow
    exit 77
}

if (-not $env:EF_TEST_BACKGROUND) { $env:EF_TEST_BACKGROUND = '1' }
if (-not $env:EF_UPDATE_CHECK) { $env:EF_UPDATE_CHECK = '0' }

# Baska bir EcuFps arayuzu (bu oturum; paralel ajan/test) tek ornek kurali yuzunden pencere testlerini bozabilir
# (kopya ona devredilir ya da bizim pencereyi yeniden gosterir). Her kosuda baslangic/bitis sayisi yazilir; sifirdan
# farkli ise basarisizlik "dis kaynakli" diye siniflanir.
$mySession = (Get-Process -Id $PID).SessionId
function Get-UiCount { (Get-Process -Name 'EcuFps' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $mySession } | Measure-Object).Count }
$out = New-Object System.Collections.Generic.List[string]
if ($Append -and $Log -and (Test-Path -LiteralPath $Log)) { foreach ($l in (Get-Content -LiteralPath $Log -Encoding UTF8)) { $out.Add($l) } }
function Emit([string]$s) { Write-Host $s; $out.Add($s) }
if ($Log) { $logDir = Split-Path -Parent $Log; if ($logDir -and -not (Test-Path -LiteralPath $logDir)) { New-Item -ItemType Directory -Force $logDir | Out-Null } }
# Log dosyasini baska bir surec okuyorsa (ornegin ilerleme izleyen grep) yazma IOException verir; kosuyu oldurmesin: yeniden dene.
function Flush {
    if (-not $Log) { return }
    for ($try = 0; $try -lt 20; $try++) {
        try { $out | Set-Content -LiteralPath $Log -Encoding UTF8 -ErrorAction Stop; return } catch { Start-Sleep -Milliseconds 250 }
    }
    Write-Host "flake_hunt: UYARI: log yazilamadi ($Log); kosu surer, sonraki Flush'ta tekrar denenecek" -ForegroundColor Yellow
}

$head = (& git -C $Root rev-parse --short=12 HEAD 2>$null)
Emit "flake_hunt: HEAD $head, build $BuildDir, $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), tekrar $Repeat, testler $($Tests -join ', ')"
Emit "flake_hunt: yuk: $((Get-Process -Name cl,link,ninja,cmake,ctest -ErrorAction SilentlyContinue | Measure-Object).Count) derleme/test sureci (ctest, cl, link, ninja, cmake) kosu basinda"
$anyFail = $false
$summary = @()
foreach ($t in $Tests) {
    $pass = 0; $fail = 0; $skip = 0; $skipStreak = 0; $durs = @(); $failRuns = @()
    Emit ''
    Emit "== $t =="
    for ($i = $StartRun; $i -le $Repeat; $i++) {
        $load0 = (Get-Process -Name cl,link,ninja,msbuild -ErrorAction SilentlyContinue | Measure-Object).Count
        $ui0 = Get-UiCount
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $ErrorActionPreference = 'Continue'
        $o = & $ctest --test-dir $bd -R "^$([regex]::Escape($t))$" --output-on-failure 2>&1 | ForEach-Object { "$_" }
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $sw.Stop()
        $sec = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        $skipped = ($o | Where-Object { $_ -match 'Skipped|\*\*\*Skipped|Not Run' }).Count -gt 0
        if ($code -eq 0 -and $skipped) { $skip++; $res = 'SKIP' }
        elseif ($code -eq 0) { $pass++; $res = 'PASS'; $durs += $sec }
        else { $fail++; $res = 'FAIL'; $failRuns += $i; $durs += $sec }
        $load1 = (Get-Process -Name cl,link,ninja,msbuild -ErrorAction SilentlyContinue | Measure-Object).Count
        $ui1 = Get-UiCount
        Emit ("run {0,3}/{1}: {2}  {3,6} sn  (derleme sureci: basta {4}, sonda {5}; baska EcuFps arayuzu: basta {6}, sonda {7})" -f $i, $Repeat, $res, $sec, $load0, $load1, $ui0, $ui1)
        if ($res -eq 'FAIL') {
            Emit "---- $t run $i basarisiz; ctest ciktisi ----"
            foreach ($l in $o) { Emit "    $l" }
            Emit '---- son ----'
            $anyFail = $true
        }
        Flush
        if ($res -eq 'SKIP') {
            # Genellikle bu oturumda baska bir EcuFps arayuzu calisiyor (paralel ajan). Sayilmaz: 15 sn bekle, ayni kosuyu tekrarla.
            $skipStreak++
            if ($skipStreak -gt 40) { Emit "flake_hunt: $t 10 dakikadir atlaniyor (baska EcuFps arayuzu?); birakiliyor."; break }
            Start-Sleep -Seconds 15
            $i--
            continue
        }
        $skipStreak = 0
    }
    $avg = if ($durs.Count) { [math]::Round(($durs | Measure-Object -Average).Average, 1) } else { 0 }
    $max = if ($durs.Count) { [math]::Round(($durs | Measure-Object -Maximum).Maximum, 1) } else { 0 }
    Emit "ozet $t : gecen $pass, kalan $fail, atlanan $skip; ort $avg sn, en uzun $max sn$(if ($failRuns.Count) { '; basarisiz kosular: ' + ($failRuns -join ',') })"
    $summary += [pscustomobject]@{ Test = $t; Pass = $pass; Fail = $fail; Skip = $skip }
}
Emit ''
Emit 'flake_hunt ozet:'
foreach ($s in $summary) { Emit ("  {0}: {1} gecti / {2} kaldi / {3} atlandi" -f $s.Test, $s.Pass, $s.Fail, $s.Skip) }
Flush
if ($anyFail) { exit 1 }
exit 0
