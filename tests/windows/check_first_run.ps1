param([Parameter(Mandatory)][string]$Exe)
# Uyumluluk B9 (bosluk 23): ilk calistirma / bos profil. Uygulama, %TEMP% altinda HIC OLMAYAN ve Turkce karakterli bir
# veri klasoruyle (EF_DATA_DIR; yalniz EF_TEST_BACKGROUND=1 ile kabul edilir) acilir:
#  1) --smoke: bos profille acilip kapanir (cikis 0).
#  2) Ui: ilk acilis karsilamasi (welcome.start) gorunur -> gercek veri klasorundeki tercihler OKUNMADI.
#     Karsilama kapatilinca preferences.json yonlendirilen klasore yazilir -> yazmalar da yonlendirildi.
#  3) Ui tekrar: karsilama yok (tercih ayni klasorden okundu).
#  4) Ayarlar > Dil: canli dil degisimi (ayni surec, ayni pencere yeri), tercih yonlendirilen klasore (Uyumluluk B1).
# Sistem ayari degistiren hicbir oge cagrilmaz; yalniz karsilama ve uygulama dili (gecici klasordeki tercih). Gecici
# klasor sonunda silinir.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
function Fail($m) { throw "FIRST_RUN: $m" }
$AE = [System.Windows.Automation.AutomationElement]; $TS = [System.Windows.Automation.TreeScope]

# Yalniz bu oturumdaki ornek tek ornek kuralina takilir (oturum 0'daki yardimci hizmet sayilmaz).
$session = (Get-Process -Id $PID).SessionId
if (Get-Process -Name 'EcuFps' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session }) {
    Write-Host 'first_run: skipped (another EcuFps instance is running; single-instance would hand off to it)'; exit 77
}
# "ilk-calistirma" Turkce karakterlerle (betik ASCII kalsin diye kod noktasi ile): c-cedilla, dotless i, s-cedilla.
$tr = 'ilk-' + [char]0x00E7 + 'al' + [char]0x0131 + [char]0x015F + [char]0x0131 + 'rma'
$data = Join-Path ([IO.Path]::GetTempPath()) ("ef-$tr-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
if (Test-Path -LiteralPath $data) { Fail "data dir already exists: $data" }
$env:EF_TEST_BACKGROUND = '1'
$env:EF_DATA_DIR = $data

function Start-Ui {
    $p = Start-Process -FilePath $Exe -PassThru
    $end = (Get-Date).AddSeconds(20)
    while ($p.MainWindowHandle -eq 0 -and -not $p.HasExited -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200; $p.Refresh() }
    if ($p.HasExited) { Fail "process exited with $($p.ExitCode) before a window appeared (15 = another EcuFps instance holds the single-instance lock)" }
    if ($p.MainWindowHandle -eq 0) { $p.Kill(); Fail 'window not found' }
    Start-Sleep -Seconds 2
    return $p
}
function Find($p, $id) {
    $win = $AE::FromHandle($p.MainWindowHandle)
    $win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id)))
}
function Stop-Ui($p) {
    if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; $p.WaitForExit(3000) | Out-Null }
    if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit(5000) | Out-Null }
}

try {
    # 1) smoke
    $s = Start-Process -FilePath $Exe -ArgumentList '--smoke' -PassThru -WindowStyle Hidden
    if (-not $s.WaitForExit(20000)) { $s.Kill(); Fail 'smoke timeout' }
    if ($s.ExitCode -ne 0) { Fail "smoke exit $($s.ExitCode)" }
    # Yonlendirme kabul edildi mi? Uygulama baslangicta <veri>\crash klasorunu olusturur. Yoksa (reddedildi; uygulama
    # test kipinde kaliciligi kapatir) UI adimlarina GECILMEZ.
    if (-not (Test-Path -LiteralPath $data -PathType Container)) { Fail "override not honoured after smoke (data dir not created: $data); UI steps skipped" }
    Write-Host 'first_run: smoke on empty profile exit 0'

    # 2) ilk acilis
    $p = Start-Ui
    try {
        $w = Find $p 'welcome.start'
        if (-not $w) { Fail 'welcome not shown on empty data dir (override ignored or real preferences read)' }
        $w.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        Start-Sleep -Milliseconds 1200
    } finally { Stop-Ui $p }
    $prefs = Join-Path $data 'preferences.json'
    if (-not (Test-Path -LiteralPath $prefs)) { Fail 'preferences.json not written to the overridden data dir' }
    Write-Host 'first_run: welcome shown, dismissal saved in overridden data dir'

    # 3) ikinci acilis
    $p = Start-Ui
    try {
        if (Find $p 'welcome.start') { Fail 'welcome shown again (preferences not read from overridden data dir)' }
    } finally { Stop-Ui $p }
    Write-Host 'first_run: second start reads overridden preferences (no welcome)'

    # 4) Uyumluluk B1 (bosluk 1): dil canli degisir (surec yeniden baslamaz), pencere yeri korunur, tercih yonlendirilen
    #    klasore yazilir. Gercek preferences.json'a dokunulmaz (EF_DATA_DIR). Tepsi menusu UIA ile okunmaz (denetlenmedi).
    $p = Start-Ui
    try {
        $n0 = (Find $p 'nav.overview').Current.Name
        if ($n0 -eq 'Panel') { $choice = 2; $want = 'Dashboard'; $lang = 'en-US' }
        elseif ($n0 -eq 'Dashboard') { $choice = 1; $want = 'Panel'; $lang = 'tr-TR' }
        else { Fail "unexpected overview name '$n0'" }
        $r0 = $AE::FromHandle($p.MainWindowHandle).Current.BoundingRectangle
        (Find $p 'nav.settings').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        # Sabit bekleme yuk altinda kararsizdi: kontrol gorunene dek (en cok 10 sn) yokla.
        $btn = $null; $bend = (Get-Date).AddSeconds(10)
        while (-not $btn -and (Get-Date) -lt $bend) { Start-Sleep -Milliseconds 200; $btn = Find $p 'settings.language' }
        if (-not $btn) { Fail 'settings.language not found' }
        $btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        $end = (Get-Date).AddSeconds(5); $opt = $null
        while (-not $opt -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200; $opt = Find $p "modal.choice.$choice" }
        if (-not $opt) {
            # P9-B9 flake tanisi (debug 1/150): Invoke etkisiz kaldiginda hangi durumda oldugumuzu yaz (sonraki olusumda neden gorunsun).
            $ids = @()
            try { $ids = @($AE::FromHandle($p.MainWindowHandle).FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition) |
                    ForEach-Object { $_.Current.AutomationId } | Where-Object { $_ -match '^(modal|settings\.language|nav\.settings)' }) } catch { $ids = @("uia: $($_.Exception.Message)") }
            Fail "language choice modal.choice.$choice not found (settings.language invoked; visible ids: $($ids -join ','); exited=$($p.HasExited))"
        }
        $opt.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 400
        (Find $p 'modal.yes').GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        $end = (Get-Date).AddSeconds(10); $n1 = $null
        while ((Get-Date) -lt $end) {
            Start-Sleep -Milliseconds 300
            if ($p.HasExited) { Fail 'process exited during the language switch' }
            $p.Refresh()
            if ($p.MainWindowHandle -eq 0) { continue }
            try { $e = Find $p 'nav.overview'; if ($e) { $n1 = $e.Current.Name } } catch { $n1 = $null }
            if ($n1 -eq $want) { break }
        }
        if ($n1 -ne $want) { Fail "window language did not switch live: '$n0' -> '$n1' (want '$want')" }
        $r1 = $AE::FromHandle($p.MainWindowHandle).Current.BoundingRectangle
        if ([math]::Abs($r1.X - $r0.X) -gt 2 -or [math]::Abs($r1.Y - $r0.Y) -gt 2 -or [math]::Abs($r1.Width - $r0.Width) -gt 2 -or
            [math]::Abs($r1.Height - $r0.Height) -gt 2) { Fail "window placement changed: $r0 -> $r1" }
    } finally { Stop-Ui $p }
    $txt = Get-Content -LiteralPath $prefs -Raw -Encoding UTF8
    if ($txt -notmatch [regex]::Escape("`"$lang`"")) { Fail "preferences.json does not hold ui_language $lang" }
    Write-Host "first_run: language switched live '$n0' -> '$n1' in the same process, placement kept, $lang saved"
    Write-Host 'FIRST_RUN OK'
} finally {
    if (Test-Path -LiteralPath $data) { Remove-Item -LiteralPath $data -Recurse -Force -ErrorAction SilentlyContinue }
}
