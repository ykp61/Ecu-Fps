<#
.SYNOPSIS
  EF-X18/X15/X20 oyun kabul turu yardimcisi (docs/19_GAME_ACCEPTANCE_ROUND.md). Yalniz kullanici calistirir.

.DESCRIPTION
  -Check      Salt okuma on kontrol: git/build_id, OS, GPU surucusu, koruma servisleri, guc kaynagi, ETW oturumlari.
  -Capture    ef_game_capture.exe ile tek olcum (yonetici). Koruma servisi calisiyorsa veya hedef korumali
              oyun listesindeyse REDDEDER. Oyun baslatmaz, ayar yazmaz; once/sonra ETW oturum farkini kaydeder.
  -Calibrate  ef_diag_calibrate.exe ile diagcal dosyasini dogrular (--emit-header ASLA kullanilmaz).
  -Plan       (B11) ABBA/BAAB kosu sirasini yazdirir ve bos sonuc sablonunu uretir. Hicbir sey olcmez/yazmaz.
  -Round      (B11) Gercek oyun ABBA turunu adim adim yurutur (docs/18 bolum 7). AYAR YAZMAZ: Performans Acik/Kapali
              gecisini siz urunun kendi Evet / Hayir akisiyla yaparsiniz; betik yalniz sirayi soyler, koruma
              servislerini her adimda denetler, oyunun kendi raporladigi FPS degerlerini sizden alip CSV'ye yazar
              ve sonunda -Analyze ile hesaplar. Oyuna dokunmaz: bellek okuma, enjeksiyon, overlay yok.
  -Analyze    (B11) Tur CSV'sinden blok farki ve %95 guven araligi hesaplar; hukum: veri yetersiz / Fark yok / yonlu.
  Ciktilar artifacts/evidence/game-round-<tarih>/ altina yazilir. Hicbir sistem ayari degistirilmez.

.EXAMPLE
  powershell -File scripts\game_round.ps1 -Check
  powershell -File scripts\game_round.ps1 -Capture MyOfflineGame.exe -Seconds 60
  powershell -File scripts\game_round.ps1 -Calibrate D:\calib\diagcal_v2.json
  powershell -File scripts\game_round.ps1 -Plan -Blocks 3
  powershell -File scripts\game_round.ps1 -Round MyOfflineGame.exe -Blocks 3
  powershell -File scripts\game_round.ps1 -Analyze artifacts\evidence\game-round-<tarih>\round-<oyun>-<saat>.csv
#>
[CmdletBinding(DefaultParameterSetName = 'Check')]
param(
    [Parameter(ParameterSetName = 'Check')][switch]$Check,
    [Parameter(ParameterSetName = 'Capture', Mandatory)][string]$Capture,
    [Parameter(ParameterSetName = 'Capture')][ValidateRange(5, 120)][int]$Seconds = 60,
    [Parameter(ParameterSetName = 'Calibrate', Mandatory)][string]$Calibrate,
    [Parameter(ParameterSetName = 'Plan', Mandatory)][switch]$Plan,
    [Parameter(ParameterSetName = 'Plan')][Parameter(ParameterSetName = 'Round')][ValidateRange(3, 8)][int]$Blocks = 3,
    [Parameter(ParameterSetName = 'Round', Mandatory)][string]$Round,
    [Parameter(ParameterSetName = 'Analyze', Mandatory)][string]$Analyze,
    [string]$Preset = 'x64-dist'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root "build\$Preset\tests"
$day = Get-Date -Format 'yyyy-MM-dd'
$ev = Join-Path $root "artifacts\evidence\game-round-$day"
New-Item -ItemType Directory -Force $ev | Out-Null
$stamp = Get-Date -Format 'HHmmss'

# Korumali oyunlar (src/core/games/protection_gate.cpp builtin_matrix) ve koruma servisleri. Liste bilgi amaclidir;
# burada olmayan bir oyunun korumasiz oldugu anlamina gelmez: kullanici oyunun cevrimdisi ve korumasiz oldugunu dogrular.
$protectedExe = @('cs2.exe', 'r5apex.exe', 'r5apex_dx12.exe', 'TslGame.exe', 'RainbowSix.exe', 'RainbowSix_BE.exe',
    'FortniteClient-Win64-Shipping.exe', 'VALORANT-Win64-Shipping.exe', 'FACEIT.exe', 'faceitclient.exe')
$acServices = @('EasyAntiCheat', 'EasyAntiCheat_EOS', 'BEService', 'vgc', 'FACEIT', 'FACEITService')

function Get-Etw { (& logman query -ets 2>$null) -join "`n" }
function Get-Protection {
    foreach ($n in $acServices) {
        $s = Get-Service -Name $n -ErrorAction SilentlyContinue
        if ($s) { [pscustomobject]@{ name = $n; status = "$($s.Status)" } }
    }
}
function Stop-Round($code, $msg) { [Console]::Error.WriteLine($msg); exit $code }
function Write-Section($file, $title, $body) { Add-Content -Encoding utf8 $file "## $title`n$body`n" }

# ---- B11: gercek oyun ABBA turu (docs/18 bolum 7). Bu islevler ayar YAZMAZ; yalniz hesap ve kayit. ----
# A = Performans Kapali (uygulamanin onceki degerleri), B = Performans Acik. Blok tek sayida 'ABBA', cift sayida 'BAAB'.
function Get-BlockOrder([int]$i) { if ($i % 2 -eq 1) { 'ABBA' } else { 'BAAB' } }
function Get-Schedule([int]$n) {
    $rows = @()
    for ($b = 1; $b -le $n; $b++) {
        $o = Get-BlockOrder $b
        for ($k = 0; $k -lt 4; $k++) { $rows += [pscustomobject]@{ block = $b; pos = $k + 1; arm = $o.Substring($k, 1) } }
    }
    $rows
}
function Get-ArmName([string]$arm) { if ($arm -eq 'A') { 'Performans Kapali' } else { 'Performans Acik' } }
# Iki yonlu %95 Student t kritik degeri (df 1..30; ustu icin normal yaklasim 1.96). Standart tablo degerleri.
function Get-TQuantile95([int]$df) {
    $t = @(12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120,
        2.110, 2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042)
    if ($df -lt 1) { return [double]::NaN }
    if ($df -gt 30) { return 1.96 }
    $t[$df - 1]
}
$inv = [Globalization.CultureInfo]::InvariantCulture
function ConvertTo-InvariantDouble([string]$text) {
    $v = 0.0
    $t = ($text + '').Trim().Replace(',', '.')
    if ([double]::TryParse($t, [Globalization.NumberStyles]::Float, $inv, [ref]$v) -and -not [double]::IsNaN($v) -and -not [double]::IsInfinity($v) -and $v -gt 0) { return $v }
    $null
}
# Blok farki: B ortalamasi - A ortalamasi (yalniz gecerli kosular; bir kolda gecerli kosu yoksa blok atilir).
function Get-AbbaStats($rows, [string]$field) {
    $diffs = New-Object System.Collections.Generic.List[double]
    $skipped = 0
    foreach ($g in ($rows | Group-Object block)) {
        $a = @($g.Group | Where-Object { $_.arm -eq 'A' -and $_.valid -eq 'evet' } | ForEach-Object { ConvertTo-InvariantDouble $_.$field } | Where-Object { $null -ne $_ })
        $b = @($g.Group | Where-Object { $_.arm -eq 'B' -and $_.valid -eq 'evet' } | ForEach-Object { ConvertTo-InvariantDouble $_.$field } | Where-Object { $null -ne $_ })
        if ($a.Count -lt 1 -or $b.Count -lt 1) { $skipped++; continue }
        $diffs.Add((($b | Measure-Object -Average).Average) - (($a | Measure-Object -Average).Average))
    }
    $n = $diffs.Count
    $r = [ordered]@{ field = $field; n = $n; skipped_blocks = $skipped; mean = $null; sd = $null; ci_lo = $null; ci_hi = $null; verdict = '' }
    if ($n -lt 3) { $r.verdict = "veri yetersiz (gecerli blok $n < 3; atlanan blok $skipped)"; return [pscustomobject]$r }
    $mean = ($diffs | Measure-Object -Average).Average
    $ss = 0.0
    foreach ($d in $diffs) { $ss += ($d - $mean) * ($d - $mean) }
    $sd = [math]::Sqrt($ss / ($n - 1))
    $half = (Get-TQuantile95 ($n - 1)) * $sd / [math]::Sqrt($n)
    $r.mean = [math]::Round($mean, 3); $r.sd = [math]::Round($sd, 3)
    $r.ci_lo = [math]::Round($mean - $half, 3); $r.ci_hi = [math]::Round($mean + $half, 3)
    if ($mean - $half -le 0 -and $mean + $half -ge 0) {
        $r.verdict = 'Fark yok (%95 guven araligi sifiri iciriyor; bu olcumde ayirt edilemedi, "fark olmadigi" kaniti degildir)'
    } elseif ($mean - $half -gt 0) {
        $r.verdict = 'Performans Acik daha yuksek olculdu (yalniz bu oyun, bu sahne, bu bilgisayar; baska sistem icin vaat degildir)'
    } else {
        $r.verdict = 'Performans Acik daha dusuk olculdu (yalniz bu oyun, bu sahne, bu bilgisayar)'
    }
    [pscustomobject]$r
}
function Format-AbbaStats($st) {
    if ($null -eq $st.mean) { return "$($st.field): $($st.verdict)" }
    "$($st.field): n=$($st.n) blok, ortalama blok farki (B-A) = $($st.mean), sd = $($st.sd), %95 GA [$($st.ci_lo) .. $($st.ci_hi)] -> $($st.verdict)"
}
function New-ResultTemplate {
@'
# Gercek oyun ABBA turu sonucu (SABLON - doldurulmadan sonuc sayilmaz)

| Alan | Deger |
|---|---|
| Tarih | |
| commit | |
| build_id | |
| Ecu Fps surumu | |
| Isletim sistemi (build.UBR) | |
| Ekran karti ve surucu | |
| Guc kaynagi | |
| Oyun adi | |
| Oyun surumu | |
| Oyun korumasiz mi (cevrimdisi, tek oyunculu) | |
| Sahne / benchmark | |
| Cozunurluk | |
| FPS siniri | |
| VSync / VRR | |
| Performans gruplari (onay ekranindaki Evet/Hayir secimleri) | |

| Blok | Sira | Kol | Ortalama FPS | %1 dusuk | Gecerli mi | Not |
|---|---|---|---|---|---|---|
| | | | | | | |

| Olcu | Deger |
|---|---|
| Gecerli blok sayisi | |
| Blok farki (B-A) ortalama | |
| %95 guven araligi | |
| Atlanan kosular ve nedeni | |
| Hukum (veri yetersiz / Fark yok / yonlu) | |
| Kapsam siniri | Yalniz bu oyun, bu sahne, bu bilgisayar. FPS vaadi degildir. |
'@
}

if ($PSCmdlet.ParameterSetName -eq 'Plan') {
    $f = Join-Path $ev "plan-$stamp.md"
    $sched = Get-Schedule $Blocks
    Set-Content -Encoding utf8 $f "# ABBA kosu plani ($Blocks blok = $($Blocks * 2) A + $($Blocks * 2) B kosu)`n"
    Add-Content -Encoding utf8 $f "A = Performans Kapali (onceki degerler), B = Performans Acik. Bu plan ayar yazmaz; gecisleri siz urunun Evet / Hayir akisiyla yaparsiniz.`n"
    Add-Content -Encoding utf8 $f "| Adim | Blok | Sira | Kol |`n|---|---|---|---|"
    $i = 0
    foreach ($r in $sched) { $i++; Add-Content -Encoding utf8 $f "| $i | $($r.block) | $(Get-BlockOrder $r.block) #$($r.pos) | $($r.arm) = $(Get-ArmName $r.arm) |" }
    Get-Content $f
    $t = Join-Path $ev "result-template-$stamp.md"
    Set-Content -Encoding utf8 $t (New-ResultTemplate)
    Write-Host "Plan: $f"
    Write-Host "Sablon: $t"
    exit 0
}

if ($PSCmdlet.ParameterSetName -eq 'Analyze') {
    if (-not (Test-Path $Analyze)) { throw "Dosya yok: $Analyze" }
    $rows = @(Import-Csv $Analyze)
    if ($rows.Count -eq 0) { throw 'CSV bos.' }
    foreach ($col in 'block', 'pos', 'arm', 'avg_fps', 'valid') { if (-not ($rows[0].PSObject.Properties.Name -contains $col)) { throw "CSV sutunu eksik: $col" } }
    $f = Join-Path $ev "analyze-$stamp.md"
    $stAvg = Get-AbbaStats $rows 'avg_fps'
    Set-Content -Encoding utf8 $f "# ABBA analizi ($Analyze)`n"
    Add-Content -Encoding utf8 $f ("- " + (Format-AbbaStats $stAvg))
    if ($rows[0].PSObject.Properties.Name -contains 'low1_fps') { Add-Content -Encoding utf8 $f ("- (ikincil, hukmu etkilemez) " + (Format-AbbaStats (Get-AbbaStats $rows 'low1_fps'))) }
    Add-Content -Encoding utf8 $f "- Kapsam siniri: yalniz bu oyun, bu sahne, bu bilgisayar. FPS vaadi degildir."
    Get-Content $f
    Write-Host "Kayit: $f"
    exit 0
}

if ($PSCmdlet.ParameterSetName -eq 'Round') {
    $target = [IO.Path]::GetFileName($Round)
    if ($protectedExe -contains $target) { Stop-Round 5 "Reddedildi: $target korumali oyun listesinde. Bu turda korumali oyun olculmez." }
    $running = @(Get-Protection | Where-Object status -eq 'Running')
    if ($running.Count) { Stop-Round 4 ("Reddedildi: koruma servisi calisiyor: " + (($running | ForEach-Object name) -join ', ')) }
    Write-Host 'Bu betik AYAR YAZMAZ ve oyuna dokunmaz. Performans Acik/Kapali gecisini siz urunde yaparsiniz (Evet / Hayir onayi sizindir; UAC cikarsa yalniz siz basin).'
    $answer = Read-Host "$target cevrimdisi, tek oyunculu ve korumasiz mi? Tur $($Blocks * 4) kosu surer. Devam icin EVET yazin"
    if ($answer -cne 'EVET') { Write-Host 'Iptal edildi (atlandi olarak kaydedin).'; exit 1 }
    $game = Read-Host 'Oyun adi ve surumu'
    $scene = Read-Host 'Sahne / benchmark adi, cozunurluk, FPS siniri, VSync (tek satir)'
    $csv = Join-Path $ev "round-$($target)-$stamp.csv"
    $md = Join-Path $ev "round-$($target)-$stamp.md"
    $out = @()
    $sched = Get-Schedule $Blocks
    $prev = ''
    $i = 0
    $stopped = $false
    foreach ($r in $sched) {
        $i++
        $running = @(Get-Protection | Where-Object status -eq 'Running')
        if ($running.Count) { Write-Warning ("Koruma servisi calisiyor: " + (($running | ForEach-Object name) -join ', ') + ' -> tur durduruldu.'); $stopped = $true; break }
        Write-Host ""
        Write-Host "=== Adim $i/$($sched.Count): blok $($r.block) ($(Get-BlockOrder $r.block)), kol $($r.arm) = $(Get-ArmName $r.arm) ==="
        if ($r.arm -ne $prev) {
            Write-Host "1) Oyunu KAPATIN (oyun aciksa urun ayar yazmaz)."
            Write-Host "2) Ecu Fps'te '$(Get-ArmName $r.arm)' yapin. Onay ekraninda Evet / Hayir secimlerini tum kollarda ayni verin; 'yeniden baslatma gerekir' isaretli grup varsa Hayir deyin. Geri okuma 'dogrulandi' diyene kadar bekleyin."
            Write-Host "3) Oyunu acin, ayni sahneyi/benchmark'i yukleyin."
        } else {
            Write-Host "Kol ayni: ayari DEGISTIRMEYIN. Oyunu kapatip yeniden acin ve ayni sahneyi yukleyin."
        }
        $ok = Read-Host 'Hazir olunca Enter (turu bitirmek icin DUR yazin)'
        if ($ok -ceq 'DUR') { $stopped = $true; break }
        Write-Host "Oyunun KENDI raporladigi sonucu girin (ilk gecis isinmadir; ikinci gecisi okuyun). Bos birakirsaniz kosu gecersiz sayilir."
        $avg = $null
        while ($true) {
            $txt = Read-Host 'Ortalama FPS'
            if ([string]::IsNullOrWhiteSpace($txt)) { break }
            $avg = ConvertTo-InvariantDouble $txt
            if ($null -ne $avg) { break }
            Write-Host 'Gecerli bir pozitif sayi girin (ornek 142.5) ya da bos birakin.'
        }
        $low = ConvertTo-InvariantDouble (Read-Host '%1 dusuk FPS (yoksa bos)')
        $bad = Read-Host 'Isil kisma, kare dusmesi, arka plan gorevi vb. sorun oldu mu? (yok ise Enter, varsa kisa not)'
        $valid = if ($null -ne $avg -and [string]::IsNullOrWhiteSpace($bad)) { 'evet' } else { 'hayir' }
        $out += [pscustomobject]@{
            block = $r.block; pos = $r.pos; arm = $r.arm; performans = (Get-ArmName $r.arm)
            avg_fps = $(if ($null -ne $avg) { $avg.ToString($inv) } else { '' }); low1_fps = $(if ($null -ne $low) { $low.ToString($inv) } else { '' })
            valid = $valid; note = ($bad -replace '[,\r\n]', ' '); time = (Get-Date -Format 'HH:mm:ss')
        }
        $out | Export-Csv -NoTypeInformation -Encoding ASCII $csv   # her adimda yazilir: yarim tur kaybolmaz
        $prev = $r.arm
    }
    $git = 'bilinmiyor'
    try { $g = & git -C $root rev-parse HEAD 2>$null; if ($g) { $git = "$g" } } catch { }
    $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $gpu = Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) / $($_.DriverVersion)" }
    Set-Content -Encoding utf8 $md "# Gercek oyun ABBA turu: $target ($day $stamp)`n"
    Add-Content -Encoding utf8 $md "- commit=$git`n- os=$($cv.CurrentBuild).$($cv.UBR)`n- gpu=$($gpu -join '; ')`n- oyun=$game`n- sahne/ayar=$scene`n- tur_durduruldu=$stopped`n- CSV: $csv"
    if ($out.Count) {
        Add-Content -Encoding utf8 $md "`n## Kosular"
        foreach ($o in $out) { Add-Content -Encoding utf8 $md "- blok $($o.block) #$($o.pos) $($o.arm) ($($o.performans)): ort=$($o.avg_fps) %1dusuk=$($o.low1_fps) gecerli=$($o.valid) $($o.note)" }
        Add-Content -Encoding utf8 $md "`n## Hesap (docs/18 bolum 7.4)"
        Add-Content -Encoding utf8 $md ("- " + (Format-AbbaStats (Get-AbbaStats $out 'avg_fps')))
        Add-Content -Encoding utf8 $md ("- (ikincil) " + (Format-AbbaStats (Get-AbbaStats $out 'low1_fps')))
    }
    Add-Content -Encoding utf8 $md "`n- Kapsam siniri: yalniz bu oyun, bu sahne, bu bilgisayar. FPS vaadi degildir. Atlanan/yarim adim: bu dosyada gorunmeyen adimlar 'not_run' sayilir."
    Get-Content $md
    Write-Host "Kayit: $md"
    Write-Host "CSV: $csv"
    exit $(if ($stopped) { 1 } else { 0 })
}

if ($PSCmdlet.ParameterSetName -eq 'Check') {
    $f = Join-Path $ev "check-$stamp.md"
    $commit = (& git -C $root rev-parse HEAD 2>$null)
    $dirty = (& git -C $root status --porcelain 2>$null)
    $exe = Join-Path $root "build\$Preset\src\app\EcuFps.exe"
    $special = if (Test-Path $exe) { (Get-Item $exe).VersionInfo.SpecialBuild } else { 'exe yok' }
    $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $gpu = Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) / $($_.DriverVersion)" }
    $bat = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue
    $power = if (-not $bat) { 'masaustu (pil yok)' } elseif ($bat.BatteryStatus -eq 2) { 'sebeke' } else { "pil (durum $($bat.BatteryStatus))" }
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    $prot = Get-Protection
    $running = @($prot | Where-Object status -eq 'Running')
    Set-Content -Encoding utf8 $f "# On kontrol $day $stamp`n"
    Write-Section $f 'Derleme' ("commit=$commit`nbuild_id(SpecialBuild)=$special`ncalisma agaci temiz=" + [string]([string]::IsNullOrWhiteSpace(($dirty -join ''))))
    Write-Section $f 'Sistem' ("os=$($cv.CurrentBuild).$($cv.UBR)`ngpu=" + ($gpu -join '; ') + "`nguc=$power`nyonetici=$admin")
    Write-Section $f 'Koruma servisleri' (($prot | ForEach-Object { "$($_.name)=$($_.status)" }) -join "`n")
    Write-Section $f 'ETW oturumlari' ('```' + "`n" + (Get-Etw) + "`n" + '```')
    Get-Content $f
    if ($special -match '-dirty' -or -not [string]::IsNullOrWhiteSpace(($dirty -join ''))) { Write-Warning 'Kirli derleme/agac: kabul kaniti icin temiz agactan derleyin.' }
    if ($running.Count) { Stop-Round 4 ("Koruma servisi calisiyor: " + (($running | ForEach-Object name) -join ', ') + ' -> tur yapilmaz (servisi kapatmak kullanicinin karari; uygulama kapatmaz).') }
    exit 0
}

if ($PSCmdlet.ParameterSetName -eq 'Capture') {
    $tool = Join-Path $tools 'ef_game_capture.exe'
    if (-not (Test-Path $tool)) { throw "Arac yok: $tool (once derleyin)" }
    $target = [IO.Path]::GetFileName($Capture)
    if ($protectedExe -contains $target) { Stop-Round 5 "Reddedildi: $target korumali oyun listesinde. Bu turda korumali oyun olculmez." }
    $running = @(Get-Protection | Where-Object status -eq 'Running')
    if ($running.Count) { Stop-Round 4 ("Reddedildi: koruma servisi calisiyor: " + (($running | ForEach-Object name) -join ', ')) }
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) { Stop-Round 6 'ETW kare olcumu yonetici PowerShell ister (UAC kullanicinin karari).' }
    $answer = Read-Host "$target cevrimdisi, tek oyunculu ve korumasiz mi? Olcum $Seconds sn surer. Devam icin EVET yazin"
    if ($answer -cne 'EVET') { Write-Host 'Iptal edildi (atlandi olarak kaydedin).'; exit 1 }
    $f = Join-Path $ev "capture-$($target)-$stamp.log"
    $before = Get-Etw
    $t0 = Get-Date
    & $tool $Capture $Seconds 2>&1 | Tee-Object -FilePath $f
    $code = $LASTEXITCODE
    $dur = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    $after = Get-Etw
    $leftover = Compare-Object ($before -split "`n") ($after -split "`n") | Where-Object SideIndicator -eq '=>' | ForEach-Object InputObject
    Add-Content -Encoding utf8 $f "`n--- tool_exit=$code wall_s=$dur"
    Add-Content -Encoding utf8 $f ("--- yeni ETW oturumu: " + $(if ($leftover) { $leftover -join ' | ' } else { 'yok' }))
    Write-Host "Kayit: $f"
    exit $code
}

if ($PSCmdlet.ParameterSetName -eq 'Calibrate') {
    $tool = Join-Path $tools 'ef_diag_calibrate.exe'
    if (-not (Test-Path $tool)) { throw "Arac yok: $tool" }
    if (-not (Test-Path $Calibrate)) { throw "Dosya yok: $Calibrate" }
    $f = Join-Path $ev "calibrate-$stamp.log"
    "dataset_sha256=$((Get-FileHash $Calibrate -Algorithm SHA256).Hash.ToLower())" | Tee-Object -FilePath $f
    & $tool $Calibrate 2>&1 | Tee-Object -FilePath $f -Append   # --emit-header bilerek yok
    $code = $LASTEXITCODE
    Add-Content -Encoding utf8 $f "--- tool_exit=$code (3 = kapi kapali beklenir; 5 = sizinti, ornekleri duzenlemeyin)"
    Write-Host "Kayit: $f"
    exit $code
}
