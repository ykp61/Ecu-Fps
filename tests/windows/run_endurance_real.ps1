param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$OutDir,
    [int]$TimeoutSec = 900
)
# EF-X21 gercek uzun test kabul turu (opt-in, ctest'e bagli degil; ~10 dk sabit butce).
# Yalniz urunun kendi "Uzun test" dugmesi UIA ile cagrilir (measure.endurance.start). Sistem ayari degismez,
# Performans/Duzeltme/Temizlik akisi calistirilmaz. Urunun normal davranisi geregi sonuc
# %LOCALAPPDATA%\EcuFps\history.json'a bir "endurance" kaydi olarak eklenir; once yedeklenir.
# Oyun sureci calisiyorsa baslamaz (cikis 3). Cikis: 0 sonuc kaydedildi, 1 hata, 3 oyun calisiyor.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$null = New-Item -ItemType Directory -Force -Path $OutDir
$games = 'cs2','VALORANT-Win64-Shipping','r5apex','FortniteClient-Win64-Shipping','TslGame','RainbowSix'
function GameRunning { [bool](Get-Process $games -ErrorAction SilentlyContinue) }
if (GameRunning) { Write-Host 'game process running; not started'; exit 3 }

$hist = Join-Path $env:LOCALAPPDATA 'EcuFps\history.json'
$before = @()
if (Test-Path $hist) {
    Copy-Item $hist (Join-Path $OutDir 'history.before.json') -Force
    $before = @((Get-Content $hist -Raw | ConvertFrom-Json).entries | Where-Object { $_.source -eq 'endurance' } | ForEach-Object { $_.when_unix_ms })
}
$log = [ordered]@{ started_at = (Get-Date).ToString('o'); exe = (Resolve-Path $Exe).Path
    exe_sha256 = (Get-FileHash (Resolve-Path $Exe).Path -Algorithm SHA256).Hash; endurance_entries_before = $before.Count }

function Find($win, $id) {
    $win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, $id)))
}
$p = Start-Process -FilePath $Exe -PassThru
$rc = 1
try {
    $win = $null
    for ($i = 0; $i -lt 100 -and -not $win; $i++) {
        Start-Sleep -Milliseconds 250; $p.Refresh()
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $w = $AE::FromHandle($p.MainWindowHandle); if (Find $w 'nav.measurements') { $win = $w } }
    }
    if (-not $win) { throw 'window/nav.measurements not found' }
    (Find $win 'nav.measurements').GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 800
    # B10: uzun test karti her zaman cizilir ("Test ayarlari" acilir bolumu kaldirildi)
    $start = Find $win 'measure.endurance.start'
    if (-not $start) { throw 'measure.endurance.start not found' }
    $log.start_enabled = $start.Current.IsEnabled
    $t0 = Get-Date
    $start.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Seconds 3
    $log.stop_visible_after_start = [bool](Find $win 'measure.endurance.stop')
    $new = $null
    while (-not $new -and ((Get-Date) - $t0).TotalSeconds -lt $TimeoutSec) {
        Start-Sleep -Seconds 10
        if ($p.HasExited) { throw "app exited (code $($p.ExitCode))" }
        if (GameRunning) {
            $s = Find $win 'measure.endurance.stop'
            if ($s) { $s.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
            $log.cancelled_for_game = $true
        }
        if (Test-Path $hist) {
            $new = (Get-Content $hist -Raw | ConvertFrom-Json).entries |
                Where-Object { $_.source -eq 'endurance' -and $before -notcontains $_.when_unix_ms } | Select-Object -First 1
        }
    }
    $log.wall_seconds = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    if (-not $new) { throw "no new endurance history entry within $TimeoutSec s" }
    $log.entry = $new
    $rc = 0
} catch {
    $log.error = "$_"
} finally {
    if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2 }
    if (-not $p.HasExited) { $p.Kill() }
    $log.finished_at = (Get-Date).ToString('o')
    $log | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $OutDir 'endurance_result.json') -Encoding UTF8
    $log | ConvertTo-Json -Depth 8
}
exit $rc
