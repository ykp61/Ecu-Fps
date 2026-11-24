param([string]$Exe, [string]$OutDir)
# Opt-in: gercek ayar degistirir (journal'li ve geri alinir). Yalniz EF_ALLOW_MUTATION=1 ile calisir.
if ($env:EF_ALLOW_MUTATION -ne '1') { Write-Host 'ui_flow: skipped (set EF_ALLOW_MUTATION=1)'; exit 77 }
# UI-02 / OPT-02 gercek cihaz dongusu (UIA ile; sekme sayisina bagli degil):
# Guc tasarrufu -> anahtar -> onay sirasinda degisiklik yok -> Evet -> Yuksek performans -> anahtar -> Evet -> Guc tasarrufu.
# Sonunda ozgun plan her kosulda geri yuklenir.
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
function Find($win, $id) { $win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id))) }
function Toggle($win) { (Find $win 'perf.toggle').GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle() }
function Yes($win) { (Find $win 'perf.yes').GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
function Scheme { ((powercfg /getactivescheme) -join '') -replace '.*GUID: ([0-9a-f-]+).*','$1' }
$saver = 'a1841308-3541-4fab-bc81-f71556f20b4a'
$high = '8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c'
$orig = Scheme
$log = @("orig=$orig")
$ok = $false
$p = $null
try {
  powercfg /setactive $saver
  $log += "start=$(Scheme)"
  $p = Start-Process $Exe -PassThru
  $win = $null
  for ($i = 0; $i -lt 40 -and -not $win; $i++) {
    Start-Sleep -Milliseconds 250; $p.Refresh()
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $w = $AE::FromHandle($p.MainWindowHandle); if (Find $w 'perf.toggle') { $win = $w } }
  }
  if (-not $win) { throw 'window/perf.toggle not found' }
  Toggle $win; Start-Sleep -Seconds 2
  $c = Scheme; $log += "during_confirm=$c"
  Yes $win; Start-Sleep -Seconds 4
  $a = Scheme; $log += "after_yes=$a"
  Toggle $win; Start-Sleep -Seconds 1
  Yes $win; Start-Sleep -Seconds 4
  $b = Scheme; $log += "after_restore=$b"
  $ok = ($c -eq $saver) -and ($a -eq $high) -and ($b -eq $saver)
} finally {
  if ($p -and -not $p.HasExited) { $p.Kill() }
  powercfg /setactive $orig
  $log += "final=$(Scheme)"
}
$log += "PASS=$ok"
$log -join "`n"
if (-not $ok) { exit 1 }
