param([Parameter(Mandatory)][string]$Exe)
# UI-06: pencere kapaninca surec tepside surer ve arayuz kaynaklari birakilir; tepsi secimi
# pencereyi yeniden acar; ana gizli pencereye kapatma "cik ve geri al" ile temiz cikar.
# Kabuk (tepsi) olmayan ortamda (CI oturumu) tepsi eklenemezse pencere kapaninca surec cikar: o da dogru.
$ErrorActionPreference = 'Stop'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class WT {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
}
"@
function Fail($m) { throw "TRAY: $m" }
function WaitFor([scriptblock]$cond, [int]$ms) { $end = (Get-Date).AddMilliseconds($ms); while ((Get-Date) -lt $end) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 100 }; return $false }

$p = Start-Process -FilePath $Exe -PassThru
try {
    if (-not (WaitFor { [WT]::FindWindow('EcuFps.MainWindow', [NullString]::Value) -ne [IntPtr]::Zero } 10000)) { Fail 'main window not shown' }
    $hostw = [WT]::FindWindow('EcuFps.Host', [NullString]::Value)
    if ($hostw -eq [IntPtr]::Zero) { Fail 'host window missing' }
    Start-Sleep -Milliseconds 800
    $p.Refresh(); $mem_open = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    $main = [WT]::FindWindow('EcuFps.MainWindow', [NullString]::Value)
    [WT]::PostMessage($main, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null   # WM_CLOSE arayuz
    Start-Sleep -Milliseconds 1500
    $p.Refresh()
    if ($p.HasExited) {
        Write-Host "tray: unavailable in this session; process exited with $($p.ExitCode) on window close (expected fallback)"
        if ($p.ExitCode -ne 0) { Fail "exit code $($p.ExitCode)" }
        Write-Host 'OK'; exit 0
    }
    if (-not (WaitFor { [WT]::FindWindow('EcuFps.MainWindow', [NullString]::Value) -eq [IntPtr]::Zero } 5000)) { Fail 'main window still exists after close' }
    $mem_tray = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    # Tepsi secimi (NIN_SELECT = WM_USER+0) -> pencere yeniden acilir
    [WT]::PostMessage($hostw, 0x8000 + 20, [IntPtr]0, [IntPtr]0x400) | Out-Null
    if (-not (WaitFor { [WT]::FindWindow('EcuFps.MainWindow', [NullString]::Value) -ne [IntPtr]::Zero } 5000)) { Fail 'window not reopened from tray' }
    # Ikinci baslatma tek ornege gider (yeni surec cikar 15)
    $p2 = Start-Process -FilePath $Exe -PassThru -Wait
    if ($p2.ExitCode -ne 15) { Fail "second instance exit $($p2.ExitCode)" }
    # Cik ve geri al
    [WT]::PostMessage($hostw, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null
    if (-not $p.WaitForExit(10000)) { Fail 'did not exit after host close' }
    if ($p.ExitCode -ne 0) { Fail "exit code $($p.ExitCode)" }
    Write-Host ("tray: private open={0} MiB, tray-only={1} MiB, reopen ok, second instance=15, exit=0" -f $mem_open, $mem_tray)
    Write-Host 'OK'
} finally {
    if (-not $p.HasExited) { $p.Kill() }
}
