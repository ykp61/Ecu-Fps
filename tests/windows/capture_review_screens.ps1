param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$OutDir
)
# B4: gercek uygulama penceresinden sekiz sayfanin genis/dar goruntusu.
# Yalniz sekme secimi ve perf onay karti okunur; Evet'e BASILMAZ, ayar degismez, dosya silinmez.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class RW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$null = New-Item -ItemType Directory -Force -Path $OutDir

function Fail($m) { throw "capture: $m" }

$p = Start-Process -FilePath $Exe -PassThru
$captures = @()
try {
    $deadline = (Get-Date).AddSeconds(25)
    while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 150; $p.Refresh() }
    if ($p.MainWindowHandle -eq 0) { Fail "window not found" }
    $hwnd = [IntPtr]$p.MainWindowHandle
    [RW]::ShowWindow($hwnd, 9) | Out-Null  # SW_RESTORE
    Start-Sleep -Milliseconds 1200
    $win = $AE::FromHandle($p.MainWindowHandle)

    function Find($id) {
        $c = New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id)
        return $win.FindFirst($TS::Descendants, $c)
    }
    function SelectPage($id) {
        $e = Find $id
        if (-not $e) { Fail "missing $id" }
        $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Start-Sleep -Milliseconds 500
    }
    function Capture($name) {
        [RW]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 350
        $r = New-Object RW+RECT
        if (-not [RW]::GetWindowRect($hwnd, [ref]$r)) { Fail "GetWindowRect" }
        $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
        if ($w -le 0 -or $h -le 0) { Fail "empty window rect" }
        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        # Yalniz uygulama penceresi okunur: ekran kopyalama kullanicinin diger pencerelerini yakalardi.
        $hdc = $g.GetHdc()
        $ok = [RW]::PrintWindow($hwnd, $hdc, 2)  # PW_RENDERFULLCONTENT
        $g.ReleaseHdc($hdc)
        if (-not $ok) { Fail "PrintWindow" }
        $g.Dispose()
        $path = Join-Path $OutDir ($name + '.png')
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $script:captures += [pscustomobject]@{ file = ($name + '.png'); page = $name; x = $r.Left; y = $r.Top; w = $w; h = $h }
        Write-Host ("captured {0} ({1}x{2})" -f $name, $w, $h)
    }
    function Resize($w, $h) {
        [RW]::SetWindowPos($hwnd, [IntPtr](-1), 120, 60, $w, $h, 0x0040) | Out-Null  # HWND_TOPMOST, SWP_SHOWWINDOW
        [RW]::SetWindowPos($hwnd, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null     # HWND_NOTOPMOST, SWP_NOSIZE|SWP_NOMOVE
        [RW]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 700
    }

    $pages = @(
        @('overview',    'nav.overview'),
        @('readiness',   'nav.readiness'),
        @('games',       'nav.games'),
        @('measurements','nav.measurements'),
        @('updates',     'nav.updates'),
        @('cleanup',     'nav.cleanup'),
        @('history',     'nav.history'),
        @('settings',    'nav.settings')
    )

    # Genis pencere
    Resize 1800 1100
    foreach ($pg in $pages) {
        SelectPage $pg[1]
        if ($pg[0] -eq 'cleanup' -or $pg[0] -eq 'startup' -or $pg[0] -eq 'readiness') { Start-Sleep -Milliseconds 1800 }
        Capture ("real_tr_" + $pg[0] + "_wide_1800x1100")
    }

    # Onay durumu: perf.toggle yalniz Planlama/Onaylama karti acar; Evet'e basilmaz.
    SelectPage 'nav.overview'
    $tog = Find 'perf.toggle'
    if (-not $tog) { Fail "missing perf.toggle" }
    $tp = $tog.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
    $stateBefore = $tp.Current.ToggleState
    $tp.Toggle()
    $yes = $null
    $deadline = (Get-Date).AddSeconds(12)
    while (-not $yes -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $yes = Find 'perf.yes'
    }
    if ($yes) {
        Start-Sleep -Milliseconds 500
        Capture 'real_tr_perf_confirm_wide_1800x1100'
        $no = Find 'perf.no'
        if (-not $no) { Fail "missing perf.no" }
        $no.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        Start-Sleep -Milliseconds 800
        $stateAfter = $tp.Current.ToggleState
        if ($stateAfter -ne $stateBefore) { Fail "perf.no did not cancel confirmation" }
        Write-Host ("perf confirm captured; toggle {0} -> {1} (cancel)" -f $stateBefore, $stateAfter)
    } else {
        Write-Host 'perf confirm not shown (plan bos veya degisiklik yok); capture atlandi'
    }

    # Dar pencere
    Resize 1000 720
    foreach ($pg in $pages) {
        SelectPage $pg[1]
        if ($pg[0] -eq 'cleanup' -or $pg[0] -eq 'startup' -or $pg[0] -eq 'readiness') { Start-Sleep -Milliseconds 1800 }
        Capture ("real_tr_" + $pg[0] + "_narrow_1000x720")
    }

    $r = New-Object RW+RECT
    [RW]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $meta = [ordered]@{
        captured_at = (Get-Date).ToString('o')
        exe = (Resolve-Path $Exe).Path
        exe_sha256 = (Get-FileHash (Resolve-Path $Exe).Path -Algorithm SHA256).Hash
        git_commit = (& git -C (Split-Path (Split-Path $PSScriptRoot)) rev-parse HEAD 2>$null)
        culture = (Get-Culture).Name
        ui_culture = (Get-UICulture).Name
        screen = ([System.Windows.Forms.Screen]::PrimaryScreen.Bounds | Out-String).Trim()
        window_final = [ordered]@{ x = $r.Left; y = $r.Top; w = ($r.Right - $r.Left); h = ($r.Bottom - $r.Top); dpi = [RW]::GetDpiForWindow($hwnd) }
        data_source = '%LOCALAPPDATA%\EcuFps (gercek kullanici profili: history.json 3 eski kayit, preferences.json dark, journal islemleri restored)'
        captures = $script:captures
    }
    $meta | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $OutDir 'capture_meta.json') -Encoding UTF8
    Write-Host 'OK'
} finally {
    if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2 }
    if (-not $p.HasExited) { $p.Kill() }
}
