param([Parameter(Mandatory)][string]$Exe, [string]$OutDir = '', [int]$CaptureStep = 1, [switch]$Foreground, [switch]$Complete)
# Spec 2026-11-12 A4 "canli test ekrani" UIA turu (GERCEK GPU; pencere acar): Olcumler > Denge testi > Testi baslat >
# icerik alani canli gorunume gecer (measure.live.step / measure.live.fps / measure.live.load + measure.stop; sayfa
# ogeleri kaybolur) > canli sayilar gelir (FPS metni) > Esc > test durur (iptal bildirimi; sonuc KAYDEDILMEZ) >
# Olcumler sayfasi geri gelir.
# Hicbir ayar degismez: yalniz oturumluk kapsam secimi yapilir; "Canli test ekrani" tercihi okunur, degistirilmez
# (kapaliysa tur atlanir). Test Esc ile iptal edildigi icin gecmise kayit yazilmaz. Ag kullanilmaz (EF_UPDATE_CHECK=0).
# Opt-in: EF_UIA_TOUR=1 (aksi halde cikis 77 = atlandi). EF_TEST_BACKGROUND=1 varsayilan (-Foreground kapatir):
# pencere etkinlestirilmez. Oyun calisiyorsa uygulama testi baslatmaz -> tur atlanir (77).
# -OutDir verilirse canli gorunumun ve bitis sayfasinin pencere goruntusu (PrintWindow; yalniz uygulama penceresi)
# PNG olarak yazilir. -CaptureStep N: goruntu / Esc icin N. adim beklenir (1..9; varsayilan 1).
if ($env:EF_UIA_TOUR -ne '1') {
    Write-Host 'live_view_tour: skipped (set EF_UIA_TOUR=1; opens the app window and runs the balance test on the real GPU for a few seconds)'
    exit 77
}
if ($Foreground) { Remove-Item Env:EF_TEST_BACKGROUND -ErrorAction SilentlyContinue } else { $env:EF_TEST_BACKGROUND = '1' }
$env:EF_UPDATE_CHECK = '0'
$ErrorActionPreference = 'Stop'
$procName = [IO.Path]::GetFileNameWithoutExtension($Exe)
if (Get-Process -Name $procName -ErrorAction SilentlyContinue) {
    Write-Host "live_view_tour: skipped ($procName already running; close it first)"; exit 77
}
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class LV {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
function Fail($m) { throw "LIVE: $m" }
$AE = [System.Windows.Automation.AutomationElement]; $TS = [System.Windows.Automation.TreeScope]
# Iptal bildirimi metni (iki dil): katalogdan okunur, betikte metin kopyasi tutulmaz.
$cancelled = @()
foreach ($loc in 'tr-TR', 'en-US') {
    $json = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "..\..\design\strings\$loc.json") | ConvertFrom-Json
    $cancelled += $json.strings.'measure.cancelled'
}
$p = Start-Process -FilePath $Exe -PassThru
try {
    $deadline = (Get-Date).AddSeconds(25)
    while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 150; $p.Refresh() }
    if ($p.MainWindowHandle -eq 0) { Fail 'window not found' }
    $hwnd = [IntPtr]$p.MainWindowHandle
    Start-Sleep -Milliseconds 1500
    $win = $AE::FromHandle($hwnd)
    function Find($id) { $win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id))) }
    function Name($id) { $e = Find $id; if ($e) { $e.Current.Name } else { $null } }
    function WaitFor([scriptblock]$cond, [int]$seconds, [string]$what) {
        $end = (Get-Date).AddSeconds($seconds)
        while ((Get-Date) -lt $end) { if (& $cond) { return }; Start-Sleep -Milliseconds 200 }
        Fail "timeout waiting for $what"
    }
    function Capture($name) {
        if (-not $OutDir) { return }
        $null = New-Item -ItemType Directory -Force -Path $OutDir
        $r = New-Object LV+RECT
        if (-not [LV]::GetWindowRect($hwnd, [ref]$r)) { Fail 'GetWindowRect' }
        $bmp = New-Object System.Drawing.Bitmap(($r.Right - $r.Left), ($r.Bottom - $r.Top))
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        $ok = [LV]::PrintWindow($hwnd, $hdc, 2)  # PW_RENDERFULLCONTENT: yalniz uygulama penceresi
        $g.ReleaseHdc($hdc); $g.Dispose()
        if ($ok) { $bmp.Save((Join-Path $OutDir ($name + '.png')), [System.Drawing.Imaging.ImageFormat]::Png); Write-Host "captured $name" }
        $bmp.Dispose()
    }

    (Find 'nav.measurements').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    WaitFor { Find 'measure.scope.balance' } 10 'measurements page'
    (Find 'measure.scope.balance').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 500
    if (-not (Find 'measure.scope.balance').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected) { Fail 'balance scope not selected' }

    # "Canli test ekrani" tercihi (B10: Test kartinin icinde): yalniz okunur. Kapaliysa kullanicinin tercihidir; tur atlanir.
    WaitFor { Find 'measure.live_view' } 10 'live view setting'
    $setting = (Find 'measure.live_view').GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState
    if ($setting -ne 'On') { Write-Host 'live_view_tour: skipped (the "live test view" setting is off on this PC; not changed)'; exit 77 }

    $start = Find 'measure.start'
    if (-not $start -or -not $start.Current.IsEnabled) { Write-Host 'live_view_tour: skipped (test cannot start now: a game is running or detection is not reliable)'; exit 77 }
    $start.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()

    # Icerik alani canli gorunume gecti: canli ogeler var, sayfa ogeleri yok, ust menu duruyor.
    WaitFor { Find 'measure.live.fps' } 20 'live view'
    foreach ($id in 'measure.live.step', 'measure.live.load', 'measure.stop', 'nav.measurements', 'nav.overview') { if (-not (Find $id)) { Fail "missing $id in the live view" } }
    foreach ($id in 'measure.source.builtin', 'measure.start', 'measure.mode.quick') { if (Find $id) { Fail "$id still exposed under the live view" } }
    if ((Find 'measure.live.fps').Current.ControlType -ne [System.Windows.Automation.ControlType]::Text) { Fail 'live fps is not a Text element' }
    Write-Host ("live view shown: step='{0}'" -f (Name 'measure.live.step'))

    # Canli sayilar: FPS metni bir sayi tasir; adim metni "N/9" icerir; ilerleme cubugu okunur.
    $failedEarly = $false
    WaitFor { $n = Name 'measure.live.fps'; if (-not $n) { $script:failedEarly = $true; return $true }; $n -match '\d' -and $n -match 'FPS' } 40 'live fps numbers'
    if ($failedEarly) {
        $why = Name 'status.1'
        Write-Host "live_view_tour: skipped (the test ended before the first measurement: $why)"; exit 77
    }
    WaitFor { $n = Name 'measure.live.step'; (-not $n) -or ($n -match "\b$CaptureStep/9\b") } 120 "step $CaptureStep"
    if (-not (Find 'measure.live.step')) { Fail ("test ended before step ${CaptureStep}: " + (Name 'status.1')) }
    Start-Sleep -Milliseconds 1500
    $fps1 = Name 'measure.live.fps'
    Write-Host ("live numbers: step='{0}' | fps='{1}' | load='{2}'" -f (Name 'measure.live.step'), $fps1, (Name 'measure.live.load'))
    $prog = Find 'progress.0'
    if (-not $prog) { Fail 'progress bar missing in the live view' }
    $rv = $prog.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).Current.Value
    $want = [math]::Round(($CaptureStep - 1) * 100.0 / 9, 1)
    if ([math]::Abs($rv - $want) -gt 0.6) { Fail "progress value $rv (expected about $want)" }
    Capture 'live_view'

    # -Complete (elle kabul turu; ctest'e kayitli degil): Esc yerine testin bitmesi beklenir. Sonuc bu bilgisayarin
    # gercek gecmisine YAZILIR (gercek olcum). Sonuc sayfasi ve Panel goruntulenir.
    if ($Complete) {
        WaitFor { -not (Find 'measure.live.fps') -and (Find 'measure.start') } 240 'balance test to finish'
        Start-Sleep -Milliseconds 1200
        Write-Host ("finished: status='{0}'" -f (Name 'status.1'))
        Capture 'after_complete'
        (Find 'nav.overview').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 1500
        Capture 'panel_after_complete'
        Write-Host 'OK'
        return
    }

    # Esc: ayni iptal yolu. Canli gorunum kapanir, sayfa geri gelir, iptal bildirimi gorunur.
    [LV]::PostMessage($hwnd, 0x0100, [IntPtr]0x1B, [IntPtr]0) | Out-Null   # WM_KEYDOWN VK_ESCAPE
    $t0 = Get-Date
    WaitFor { -not (Find 'measure.live.fps') -and (Find 'measure.start') } 40 'test to stop after Esc'
    $took = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    $note = Name 'status.1'
    if ($cancelled -notcontains $note) { Fail "expected the cancelled notice, got '$note'" }
    foreach ($id in 'measure.source.builtin', 'measure.mode.quick') { if (-not (Find $id)) { Fail "$id missing after the test stopped" } }
    Write-Host ("Esc stopped the test in {0} s: '{1}'" -f $took, $note)
    Start-Sleep -Milliseconds 600
    Capture 'after_esc'
    Write-Host 'OK'
} finally {
    if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2 }
    if (-not $p.HasExited) { $p.Kill() }
}
