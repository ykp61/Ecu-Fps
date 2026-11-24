param([Parameter(Mandatory)][string]$Exe)
# UI-10 gercek cihaz: ayar bazli Evet/Hayir. Opt-in (gercek kullanici ayari degisir ve geri alinir): EF_ALLOW_MUTATION=1.
#  A) "Windows animasyonlari" grubu Hayir -> uygula: hicbir sey degismez (NoneSelected / yalniz secili)
#  B) grup Evet -> uygula: menu animasyonu/golge kapanir; Kapali -> eski degerler geri gelir
if ($env:EF_ALLOW_MUTATION -ne '1') { Write-Host 'ui_selection: skipped (set EF_ALLOW_MUTATION=1)'; exit 77 }
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SPI { [DllImport("user32.dll")] static extern bool SystemParametersInfoW(uint a, uint b, out int v, uint f);
  public static int Get(uint a) { int v; SystemParametersInfoW(a, 0, out v, 0); return v; } }
"@
function Fail($m) { throw "SEL: $m" }
function Vfx { "{0}{1}{2}" -f [SPI]::Get(0x1002), [SPI]::Get(0x1024), [SPI]::Get(0x1042) }  # menu anim, drop shadow, client anim
$AE = [System.Windows.Automation.AutomationElement]; $TS = [System.Windows.Automation.TreeScope]
$before = Vfx
$p = Start-Process $Exe -PassThru
try {
    $end = (Get-Date).AddSeconds(15); while ($p.MainWindowHandle -eq 0 -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200; $p.Refresh() }
    Start-Sleep -Seconds 4
    $win = $AE::FromHandle($p.MainWindowHandle)
    function Find($id) { $win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id))) }
    function Invoke($id) { $e = Find $id; if (-not $e) { Fail "missing $id" }; $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Start-Sleep -Milliseconds 700 }
    function Toggle($id) { $e = Find $id; if (-not $e) { Fail "missing $id" }; $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle(); Start-Sleep -Milliseconds 900 }
    function State($id) { (Find $id).GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Current.ToggleState }
    if (Find 'welcome.start') { Invoke 'welcome.start' }   # ilk acilis karsilamasi paneli asagi itmesin
    Invoke 'perf.details'
    $vfx = Find 'perf.group.2'
    if (-not $vfx) { Write-Host "ui_selection: Windows effects already off on this PC (group not changeable) - skipped"; exit 77 }
    if ((State 'perf.group.2') -ne 'On') { Toggle 'perf.group.2' }
    # A) Hayir
    Toggle 'perf.group.2'
    if ((State 'perf.group.2') -ne 'Off') { Fail 'group did not switch off' }
    Toggle 'perf.toggle'; Start-Sleep -Seconds 2
    if (Find 'perf.yes') { Invoke 'perf.yes'; Start-Sleep -Seconds 3 }
    $a = Vfx
    if ($a -ne $before) { Fail "unselected group changed settings: $before -> $a" }
    if ((State 'perf.toggle') -ne 'Off') { Fail 'switch turned on with nothing selected' }
    Write-Host "A) vfx=No: nothing changed ($a)"
    # B) Evet
    Toggle 'perf.group.2'
    Toggle 'perf.toggle'; Start-Sleep -Seconds 2
    Invoke 'perf.yes'; Start-Sleep -Seconds 4
    $b = Vfx
    if ((State 'perf.toggle') -ne 'On') { Fail 'switch not on after apply' }
    if ($b -eq $before) { Fail "effects unchanged after apply ($b)" }
    Write-Host "B) vfx=Yes applied: $before -> $b"
    Toggle 'perf.toggle'; Start-Sleep -Seconds 1
    Invoke 'perf.yes'; Start-Sleep -Seconds 4
    $c = Vfx
    if ($c -ne $before) { Fail "restore mismatch: $before vs $c" }
    if ((State 'perf.toggle') -ne 'Off') { Fail 'switch not off after restore' }
    Write-Host "restore verified: $c"
    Write-Host 'OK'
} finally {
    $after = Vfx
    if (-not $p.HasExited) { $p.Kill() }
    Write-Host ("final effects={0} (start {1})" -f $after, $before)
}
