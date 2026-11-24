param([Parameter(Mandatory)][string]$Exe)
# UI-04: gercek pencerede UI Automation agaci. Ekran okuyucunun gordugu ad/rol/durum ve desenler.
# Ayar degistiren hicbir oge cagrilmaz (yalniz sekme secimi ve okuma).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
function Fail($m) { throw "UIA: $m" }
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]

# P9-B6: pencere testi arka plan kipinde (one alinmaz, odak calinmaz), bos bir gecici veri klasoruyle (gercek tercihler
# okunmaz / yazilmaz) ve kurulum rolu kapali (derleme klasorunden acilis Ui'dir) calisir. ctest ayni degiskenleri verir;
# betik tek basina calistirilinca da ayni olsun diye burada atanir.
$env:EF_TEST_BACKGROUND = '1'
$env:EF_UPDATE_CHECK = '0'
$env:EF_SETUP_ROLE = '0'
$env:EF_AUTOSTART_READONLY = '1'
$dataDir = Join-Path ([IO.Path]::GetTempPath()) ('ef-uia-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$env:EF_DATA_DIR = $dataDir
$p = Start-Process -FilePath $Exe -PassThru
try {
    $deadline = (Get-Date).AddSeconds(15)
    # Kok neden (2026-11-22): baska bir EcuFps ornegi aciksa (ornegin eszamanli baska test) tek ornek kurali bu sureci
    # hemen cikis 15 ile bitirir. Cikmis surecte PowerShell MainWindowHandle'i $null verir ($null -eq 0 yanlis), dongu
    # aninda biter ve FromHandle(null) anlasilmaz hatayla patlardi. Cikis acik bir mesajla bildirilir.
    while (-not $p.HasExited -and $p.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100; $p.Refresh() }
    if ($p.HasExited) { Fail "process exited with $($p.ExitCode) before a window appeared (15 = another EcuFps instance holds the single-instance lock)" }
    if ($p.MainWindowHandle -eq 0) { Fail "window not found" }
    Start-Sleep -Milliseconds 800
    if ($p.HasExited) { Fail "process exited with $($p.ExitCode) right after the window appeared (15 = another EcuFps instance)" }
    $win = $AE::FromHandle($p.MainWindowHandle)
    function Find($id) {
        $c = New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id)
        return $win.FindFirst($TS::Descendants, $c)
    }
    $navIds = 'nav.overview', 'nav.games', 'nav.measurements', 'nav.history', 'nav.settings'
    foreach ($id in $navIds) {
        $e = Find $id
        if (-not $e) { Fail "missing $id" }
        if ($e.Current.ControlType -ne [System.Windows.Automation.ControlType]::TabItem) { Fail "$id not TabItem" }
        if ([string]::IsNullOrWhiteSpace($e.Current.Name)) { Fail "$id has no name" }
        if (-not $e.Current.IsKeyboardFocusable) { Fail "$id not focusable" }
        $r = $e.Current.BoundingRectangle
        if ($r.Width -le 0 -or $r.Height -le 0) { Fail "$id empty bounds" }
    }
    $sel = (Find 'nav.overview').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    if (-not $sel.Current.IsSelected) { Fail "overview not selected" }

    # Gerileme (UI-07): eslesmemis WM_LBUTTONUP (basma olmadan birakma) ogeyi etkinlestirmemeli.
    Add-Type @"
using System; using System.Runtime.InteropServices;
public static class WU {
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public struct POINT { public int X, Y; }
}
"@
    $r = (Find 'nav.settings').Current.BoundingRectangle
    $pt = New-Object WU+POINT
    $pt.X = [int]($r.X + $r.Width / 2); $pt.Y = [int]($r.Y + $r.Height / 2)
    [WU]::ScreenToClient([IntPtr]$p.MainWindowHandle, [ref]$pt) | Out-Null
    [WU]::PostMessage([IntPtr]$p.MainWindowHandle, 0x0202, [IntPtr]0, [IntPtr](($pt.Y -shl 16) -bor ($pt.X -band 0xFFFF))) | Out-Null
    Start-Sleep -Milliseconds 500
    $sel = (Find 'nav.overview').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    if (-not $sel.Current.IsSelected) { Fail "stray WM_LBUTTONUP changed the page" }

    $tog = Find 'perf.toggle'
    if (-not $tog) { Fail "missing perf.toggle" }
    $tp = $tog.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
    Write-Host ("toggle: name='{0}' state={1}" -f $tog.Current.Name, $tp.Current.ToggleState)

    # Denetim 5.4 (B10 sonrasi): Olcumler'deki acilir bolum kaldirildi; ExpandCollapse Genel bakis'taki "neleri
    # degistirir" baglantisinda (perf.details, Expander) denetlenir. Yalniz gorunum durumu degisir (ayar yazilmaz).
    $det = $null
    $deadline = (Get-Date).AddSeconds(5)
    while (-not $det -and (Get-Date) -lt $deadline) { $det = Find 'perf.details'; if (-not $det) { Start-Sleep -Milliseconds 200 } }
    if (-not $det) { Fail "perf.details missing" }
    $ec = $det.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    $was = $ec.Current.ExpandCollapseState
    if ($was -eq 'Expanded') { $ec.Collapse() } else { $ec.Expand() }
    Start-Sleep -Milliseconds 700
    $ec = (Find 'perf.details').GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    if ($ec.Current.ExpandCollapseState -eq $was) { Fail "perf.details did not change via ExpandCollapse" }
    if ($was -eq 'Expanded') { $ec.Expand() } else { $ec.Collapse() }  # baslangic durumuna don
    Start-Sleep -Milliseconds 700
    $ec = (Find 'perf.details').GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
    if ($ec.Current.ExpandCollapseState -ne $was) { Fail "perf.details did not return via ExpandCollapse" }

    # Sekmeyi UIA ile sec -> Olcumler sayfasi ogeleri gorunmeli
    (Find 'nav.measurements').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 700
    $m = (Find 'nav.measurements').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    if (-not $m.Current.IsSelected) { Fail "measurements not selected after Select()" }
    $b = Find 'measure.source.builtin'
    $g = Find 'measure.source.game'
    if (-not $b -or -not $g) { Fail "measurement source items missing" }
    if ($b.Current.ControlType -ne [System.Windows.Automation.ControlType]::RadioButton) { Fail "source not RadioButton" }
    if (Find 'perf.toggle') { Fail "overview toggle still exposed on measurements page" }
    # B10: "Test ayarlari" acilir bolumu, hedef FPS ve canli FPS paneli kaldirildi; test suresi Test kartinin icinde
    # (RadioButton). Yalniz okunur: secim degistirilmez.
    foreach ($gone in 'measure.options', 'measure.target.custom', 'measure.target.edit', 'measure.live_panel') {
        if (Find $gone) { Fail "$gone still exposed (feature removed)" }
    }
    if ($b.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected) {
        $mq = Find 'measure.mode.quick'
        if (-not $mq) { Fail "measure.mode.quick missing in the test card" }
        if ($mq.Current.ControlType -ne [System.Windows.Automation.ControlType]::RadioButton) { Fail "duration choice not RadioButton" }
    }
    $all = $win.FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    Write-Host ("elements={0} builtin='{1}' game='{2}'" -f $all.Count, $b.Current.Name, $g.Current.Name)

    # ---- P9-B6: sekiz sayfa UIA agaci + GERCEK klavye turu (yalniz okuma; ayar degistiren oge cagrilmaz) ----
    # Her sekme UIA SelectionItem ile secilir; sayfanin tum ogeleri gercek UIA saglayicisindan okunur:
    #  - her ogenin AutomationId'si var ve sayfada tekil; Name bos / bosluklu degil;
    #  - rol beklenen kumeden (TabItem, Button, RadioButton, CheckBox, Text, ProgressBar, Edit); rol ile desen uyumlu
    #    (TabItem/RadioButton -> SelectionItem, Button -> Invoke veya Toggle, Text -> etkilesim deseni yok);
    #  - etkin etkilesimli oge klavye odaklanabilir, devre disi / salt okunur degil; ekrandaki oge bos sinir kutusu tasimaz;
    #  - sekiz sekme TabItem, yalniz secili sayfa secili.
    # Klavye turu: ana pencereye gercek WM_KEYDOWN(VK_TAB) gonderilir (pencere etkin olmak zorunda degil; EF_TEST_BACKGROUND=1
    # pencereyi one almaz). Uygulama odagi her degistirdiginde UIA saglayicisi FocusChanged olayi yollar; olaylar bir
    # Automation istemcisiyle toplanir. n odaklanabilir oge icin n Tab = her biri bir kez (n + 1. Tab ilk ogeye doner);
    # sekiz ust sekme ust sekme sirasiyla gelir. Evet/Hayir onaylarinin varsayilan odagi birim testlerde (test_a11y_tree)
    # sinanir; burada onay acilmaz (Performans anahtari cagrilmaz).
    Add-Type -AssemblyName WindowsBase
    Add-Type -ReferencedAssemblies ($AE.Assembly.Location), ([System.Windows.Automation.ControlType].Assembly.Location), ([System.Windows.Rect].Assembly.Location) -TypeDefinition @"
using System; using System.Collections.Concurrent; using System.Windows.Automation;
public class FocusLog {
  public ConcurrentQueue<string> Ids = new ConcurrentQueue<string>();
  public int Pid;
  AutomationFocusChangedEventHandler h;
  public void Start(int pid) { Pid = pid; h = new AutomationFocusChangedEventHandler(OnF); Automation.AddAutomationFocusChangedEventHandler(h); }
  public void Stop() { Automation.RemoveAutomationFocusChangedEventHandler(h); }
  void OnF(object s, AutomationFocusChangedEventArgs e) {
    try { var el = s as AutomationElement; if (el == null) return; if (el.Current.ProcessId != Pid) return; Ids.Enqueue(el.Current.AutomationId); } catch { Ids.Enqueue("?"); }
  }
}
public static class KB {
  [System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
    $CT = [System.Windows.Automation.ControlType]
    $allowed = @($CT::TabItem, $CT::Button, $CT::RadioButton, $CT::CheckBox, $CT::Text, $CT::ProgressBar, $CT::Edit)
    $interactive = @($CT::TabItem, $CT::Button, $CT::RadioButton, $CT::CheckBox)
    $tabs = 'nav.overview', 'nav.games', 'nav.readiness', 'nav.measurements', 'nav.updates', 'nav.cleanup', 'nav.history', 'nav.settings'
    $cap = 40

    # Bir sayfanin agacini denetler. Donus: @{ Err = <ilk bulgu ya da $null>; Focusable = <Tab duraklari>; Count = <oge sayisi> }.
    # Sayfa verisi (baslangic / gecmis / oyun listesi) arka planda yuklenirken satirlar yer degistirebilir; bu yuzden cagiran
    # bulgu halinde bir kez daha, sakinlesmis agacta dener (ayni bulgu iki kez cikarsa gercek hatadir).
    function Audit-Page($tabId) {
        $els = @($win.FindAll($TS::Descendants, [System.Windows.Automation.Condition]::TrueCondition))
        $r = @{ Err = $null; Focusable = @(); Count = $els.Count }
        if ($els.Count -lt 10) { $r.Err = "page tree too small ($($els.Count))"; return $r }
        $seen = @{}
        $tabItems = 0; $selected = 0
        foreach ($e in $els) {
            try { $c = $e.Current; $pats = @($e.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName }) }
            catch { $r.Err = "element vanished while reading ($($_.Exception.Message))"; return $r }
            $id = $c.AutomationId
            if ([string]::IsNullOrWhiteSpace($id)) { $r.Err = "element without AutomationId (name='$($c.Name)')"; return $r }
            if ($seen.ContainsKey($id)) { $r.Err = "duplicate AutomationId $id"; return $r }
            $seen[$id] = $true
            if ([string]::IsNullOrWhiteSpace($c.Name)) { $r.Err = "$id has empty name"; return $r }
            if ($c.Name -ne $c.Name.Trim()) { $r.Err = "$id name has outer whitespace"; return $r }
            if ($allowed -notcontains $c.ControlType) { $r.Err = "$id unexpected control type $($c.ControlType.ProgrammaticName)"; return $r }
            $isInter = $interactive -contains $c.ControlType
            if ($c.ControlType -eq $CT::TabItem -or $c.ControlType -eq $CT::RadioButton) {
                if ($pats -notcontains 'SelectionItemPatternIdentifiers.Pattern') { $r.Err = "$id ($($c.ControlType.ProgrammaticName)) has no SelectionItem pattern"; return $r }
            }
            if ($c.ControlType -eq $CT::Button -and $c.IsEnabled) {
                if (($pats -notcontains 'InvokePatternIdentifiers.Pattern') -and ($pats -notcontains 'TogglePatternIdentifiers.Pattern')) { $r.Err = "$id Button has neither Invoke nor Toggle"; return $r }
            }
            if (-not $isInter) {
                if ($c.IsKeyboardFocusable) { $r.Err = "read-only $id is keyboard focusable"; return $r }
                foreach ($bad in 'InvokePatternIdentifiers.Pattern', 'TogglePatternIdentifiers.Pattern', 'SelectionItemPatternIdentifiers.Pattern') {
                    if ($pats -contains $bad) { $r.Err = "read-only $id exposes $bad"; return $r }
                }
            }
            if (-not $c.IsEnabled -and $c.IsKeyboardFocusable) { $r.Err = "disabled $id is keyboard focusable"; return $r }
            if ($isInter -and $c.IsEnabled -and $c.ControlType -ne $CT::RadioButton -and -not $c.IsKeyboardFocusable) { $r.Err = "enabled $id is not keyboard focusable"; return $r }
            if (-not $c.IsOffscreen) {
                $b = $c.BoundingRectangle
                if ($b.IsEmpty -or $b.Width -le 0 -or $b.Height -le 0) { $r.Err = "on-screen $id has empty bounds"; return $r }
            }
            if ($c.ControlType -eq $CT::TabItem) {
                $tabItems++
                if ($e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected) {
                    $selected++
                    if ($id -ne $tabId) { $r.Err = "wrong tab selected ($id)"; return $r }
                }
            }
            if ($c.IsKeyboardFocusable -and $c.IsEnabled) { $r.Focusable += $id }
        }
        if ($tabItems -ne 8) { $r.Err = "expected 8 TabItems, found $tabItems"; return $r }
        if ($selected -ne 1) { $r.Err = "expected exactly one selected tab, found $selected"; return $r }
        return $r
    }

    $fl = New-Object FocusLog
    $fl.Start($p.Id)
    $tourLog = @()
    try {
        foreach ($tabId in $tabs) {
            $t = Find $tabId
            if (-not $t) { Fail "page tab $tabId missing" }
            $t.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
            Start-Sleep -Milliseconds 900
            if ($tabId -eq 'nav.cleanup') {
                # Temizlik sayfasi acilinca disk taramasi baslar: tarama bitene kadar (scan_cancel kaybolur) odaklanabilir
                # kume degisir. Sabit bekleme yuklu makinede yetmiyordu; taramanin bitmesini bekle (en cok 40 sn).
                $scanDeadline = (Get-Date).AddSeconds(40)
                while ((Find 'cleanup.scan_cancel') -and (Get-Date) -lt $scanDeadline) { Start-Sleep -Milliseconds 300 }
                Start-Sleep -Milliseconds 500
            }
            $res = Audit-Page $tabId
            if ($res.Err) {
                Start-Sleep -Milliseconds 1500  # sayfa verisi yuklenirken satirlar kayabilir: sakinlesince bir kez daha
                $res2 = Audit-Page $tabId
                if ($res2.Err) { Fail "${tabId}: $($res2.Err) (first read: $($res.Err))" }
                $res = $res2
            }
            $focusable = @($res.Focusable)

            # Klavye turu: n Tab -> n farkli oge, n + 1. Tab ilkine doner.
            $n = $focusable.Count
            if ($n -lt 8) { Fail "${tabId}: fewer than 8 tab stops ($n)" }
            # Uzun sanal listeler (Gecmis: yuzlerce satir Tab duragi) ilk $cap durakla turlanir; tam tur kisa sayfalarda.
            $m = [Math]::Min($n, $cap)
            $x = $null; while ($fl.Ids.TryDequeue([ref]$x)) { }
            for ($i = 0; $i -lt $m + 1; $i++) {
                [KB]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]9, [IntPtr]0x000F0001) | Out-Null
                [KB]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]9, [IntPtr]0xC00F0001) | Out-Null
                Start-Sleep -Milliseconds 100
            }
            Start-Sleep -Milliseconds 500
            $seq = @(); while ($fl.Ids.TryDequeue([ref]$x)) { if ($x -ne 'ecufps.main') { $seq += $x } }  # kok (pencere) olaylari tur disi
            if ($seq.Count -ne $m + 1) { Fail "${tabId}: $($m + 1) Tab presses produced $($seq.Count) focus events: $($seq -join ',')" }
            $firstN = @($seq[0..($m - 1)])
            if (($firstN | Sort-Object -Unique).Count -ne $m) { Fail "${tabId}: Tab tour repeats an element before the full tour: $($firstN -join ',')" }
            if ($n -le $cap) {
                $extra = @($firstN | Where-Object { $focusable -notcontains $_ })
                $missing = @($focusable | Where-Object { $firstN -notcontains $_ })
                if ($extra.Count -or $missing.Count) { Fail "${tabId}: Tab tour != focusable set (missing: $($missing -join ',') extra: $($extra -join ','))" }
                if ($seq[$n] -ne $seq[0]) { Fail "${tabId}: Tab does not wrap to the first stop ($($seq[$n]) vs $($seq[0]))" }
            }
            # Ust menu sekmeleri tur icinde ust menu sirasiyla gelir.
            $navSeq = @($firstN | Where-Object { $tabs -contains $_ })
            if ($navSeq.Count -gt 1) {
                $start = [Array]::IndexOf($tabs, $navSeq[0])
                $rot = @(); for ($k = 0; $k -lt $navSeq.Count; $k++) { $rot += $tabs[($start + $k) % $tabs.Count] }
                if (($navSeq -join ',') -ne ($rot -join ',')) { Fail "${tabId}: nav tabs out of order in the tour: $($navSeq -join ',')" }
            }
            $tourLog += ("{0}: elements={1} tab_stops={2} toured={3} order={4}" -f $tabId, $res.Count, $n, $m, ($firstN -join ' > '))
        }
    } finally { $fl.Stop() }
    $tourLog | ForEach-Object { Write-Host $_ }
    Write-Host ("a11y tree + keyboard tour: {0} pages OK" -f $tabs.Count)
    Write-Host 'OK'
} finally {
    if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2 }
    if (-not $p.HasExited) { $p.Kill() }
    try { if (Test-Path -LiteralPath $dataDir) { Remove-Item -LiteralPath $dataDir -Recurse -Force } } catch { }
}
