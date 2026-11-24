param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Probe)
# SEC-05: ikinci ornek mevcut pencereyi one getirir; otomatik baslatma test anahtarinda ac/kapat.
$ErrorActionPreference = 'Stop'
# Bu test one getirmeyi dogrular: arka plan test modu (EF_TEST_BACKGROUND) burada kapatilir.
Remove-Item Env:EF_TEST_BACKGROUND -ErrorAction SilentlyContinue
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
'@

if ([W32]::FindWindow('EcuFps.MainWindow', [NullString]::Value) -ne [IntPtr]::Zero) { throw 'an Ecu Fps window is already open; close it first' }
$first = Start-Process $Exe -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 50; $h = [W32]::FindWindow('EcuFps.MainWindow', [NullString]::Value) }
    if ($h -eq [IntPtr]::Zero) { throw 'first instance window not found' }
    [W32]::ShowWindow($h, 6) | Out-Null   # SW_MINIMIZE
    Start-Sleep -Milliseconds 300
    if (-not [W32]::IsIconic($h)) { throw 'could not minimize first window' }

    $second = Start-Process $Exe -PassThru -Wait
    if ($second.ExitCode -ne 15) { throw "second instance exit $($second.ExitCode), expected 15 (already running)" }
    Start-Sleep -Milliseconds 500
    if ([W32]::IsIconic($h) -or -not [W32]::IsWindowVisible($h)) { throw 'first window was not restored' }
    Write-Host 'single instance: second launch exited 15 and restored the first window'
} finally {
    if (-not $first.HasExited) { $first.CloseMainWindow() | Out-Null; if (-not $first.WaitForExit(5000)) { $first.Kill() } }
}

$a = Start-Process $Probe -ArgumentList 'autostart' -PassThru -WindowStyle Hidden -Wait
if ($a.ExitCode -ne 0) { throw "autostart probe exit $($a.ExitCode)" }
if (Test-Path 'HKCU:\Software\EcuFpsTest') { throw 'autostart test key not cleaned up' }
Write-Host 'autostart: enable/disable on test key, real Run key untouched'
Write-Host 'SINGLE INSTANCE OK'
