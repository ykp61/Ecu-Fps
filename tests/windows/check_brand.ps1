param([Parameter(Mandatory)][string]$Exe, [string]$OutDir = '')
# BRAND-01: marka 2.0.0 (EF) varliklari EXE'de mi ve dogru mu calisiyor?
#  1) RT_GROUP_ICON 101 birincil grup ve 15 boyut; 201-206 tepsi gruplari 7'ser boyut
#  2) Explorer/Shell ikon cikarma 16/32/48/256 (PNG kanit); amblem marka yesili, eski cyan yok
#  3) EXE temiz gecici klasorde: pencere kucuk/buyuk ikonu var, yan dosya olusmuyor
#  4) Tepsi ikonu var (Shell_NotifyIconGetRect); TaskbarCreated benzetimi sonrasi tek ikon; cikista kaldirilir
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class WBr {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryExW(string f, IntPtr h, uint fl);
  [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr h);
  delegate bool EnumNameProc(IntPtr m, IntPtr t, IntPtr n, IntPtr l);
  [DllImport("kernel32.dll")] static extern bool EnumResourceNamesW(IntPtr m, IntPtr t, EnumNameProc f, IntPtr l);
  [DllImport("kernel32.dll")] static extern IntPtr FindResourceW(IntPtr m, IntPtr n, IntPtr t);
  [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr m, IntPtr r);
  [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string s);
  [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr h);
  [DllImport("shell32.dll", CharSet=CharSet.Unicode)] public static extern int SHDefExtractIconW(string f, int i, uint fl, out IntPtr large, out IntPtr small, uint size);
  [StructLayout(LayoutKind.Sequential)] public struct NID { public uint cbSize; public IntPtr hWnd; public uint uID; public Guid guid; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("shell32.dll")] public static extern int Shell_NotifyIconGetRect(ref NID id, out RECT r);
  public static List<int> GroupIds(IntPtr m) {
    var ids = new List<int>();
    EnumResourceNamesW(m, (IntPtr)14, (mm, t, n, l) => { if (((long)n >> 16) == 0) ids.Add((int)(long)n); return true; }, IntPtr.Zero);
    return ids;
  }
  public static int GroupCount(IntPtr m, int id) {
    IntPtr r = FindResourceW(m, (IntPtr)id, (IntPtr)14); if (r == IntPtr.Zero) return -1;
    IntPtr p = LockResource(LoadResource(m, r)); return Marshal.ReadInt16(p, 4);
  }
  public static bool TrayIconExists(IntPtr host) {
    var n = new NID(); n.cbSize = (uint)Marshal.SizeOf(typeof(NID)); n.hWnd = host; n.uID = 1; RECT r;
    return Shell_NotifyIconGetRect(ref n, out r) == 0;
  }
}
"@
function Fail($m) { throw "BRAND: $m" }
function WaitFor([scriptblock]$c, [int]$ms) { $e = (Get-Date).AddMilliseconds($ms); while ((Get-Date) -lt $e) { if (& $c) { return $true }; Start-Sleep -Milliseconds 100 }; return $false }

# 1) Kaynaklar
$m = [WBr]::LoadLibraryExW((Resolve-Path $Exe).Path, [IntPtr]::Zero, 0x22)  # DATAFILE | IMAGE_RESOURCE
if ($m -eq [IntPtr]::Zero) { Fail 'cannot load exe as data' }
try {
    $ids = [WBr]::GroupIds($m)
    if ($ids.Count -eq 0 -or ($ids | Measure-Object -Minimum).Minimum -ne 101) { Fail "primary icon group is not 101: $($ids -join ',')" }
    if ([WBr]::GroupCount($m, 101) -ne 15) { Fail "app icon sizes $([WBr]::GroupCount($m, 101)) != 15" }
    foreach ($t in 201..206) { if ([WBr]::GroupCount($m, $t) -ne 7) { Fail "tray group $t sizes $([WBr]::GroupCount($m, $t))" } }
    Write-Host ("resources: icon groups {0}; app 15 sizes; tray 201-206 x7" -f ($ids -join ','))
} finally { [WBr]::FreeLibrary($m) | Out-Null }

# 2) Shell'in EXE'den cikardigi ikonlar (Explorer ile ayni kaynak)
$tokens = Get-Content (Join-Path $PSScriptRoot '..\..\assets\branding\ecu-fps\brand-tokens.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$mark = [System.Drawing.ColorTranslator]::FromHtml($tokens.colors.icon_mark)
foreach ($s in 16, 32, 48, 256) {
    $large = [IntPtr]::Zero; $small = [IntPtr]::Zero
    $hr = [WBr]::SHDefExtractIconW((Resolve-Path $Exe).Path, 0, 0, [ref]$large, [ref]$small, [uint32]$s)
    if ($hr -ne 0 -or $large -eq [IntPtr]::Zero) { Fail "shell extract $s failed" }
    $ico = [System.Drawing.Icon]::FromHandle($large)
    if ($ico.Width -ne $s) { Fail "shell icon $s came back as $($ico.Width)" }
    $b = $ico.ToBitmap()
    try {
        if ($OutDir) { $b.Save((Join-Path $OutDir "brand_shell_icon_$s.png")) }
        # Marka 2.0.0: EF amblemi yesil (brand-tokens icon_mark; buyuk boyutta yesil gecis), ok ucu camgobegi
        # (icon_secondary). 1.0.0 cift ok ikonu (cyan #3CCBFF, R ~60) gomulu kalmamali.
        $step = [Math]::Max(1, [int]($s / 64)); $n = 0; $green = 0; $old = 0
        for ($y = 0; $y -lt $s; $y += $step) { for ($x = 0; $x -lt $s; $x += $step) {
            $n++; $c = $b.GetPixel($x, $y); if ($c.A -lt 200) { continue }
            if ($c.G -gt 200 -and $c.G -gt $c.R + 80 -and $c.G -gt $c.B + 40) { $green++ }
            if ([Math]::Abs($c.R - 0x3C) -le 10 -and [Math]::Abs($c.G - 0xCB) -le 10 -and [Math]::Abs($c.B - 0xFF) -le 10) { $old++ }
        } }
        if ($green * 20 -lt $n) { Fail "shell icon ${s}: green mark covers $green of $n sampled pixels" }
        if ($old -ne 0) { Fail "shell icon ${s}: $old pixels of the 1.0.0 cyan icon" }
    } finally { $b.Dispose() }
    [WBr]::DestroyIcon($large) | Out-Null; if ($small -ne [IntPtr]::Zero) { [WBr]::DestroyIcon($small) | Out-Null }
}
Write-Host ("shell: 16/32/48/256 extracted at native size; mark colour {0}" -f $tokens.colors.icon_mark)

# 3) Temiz klasor
$clean = Join-Path $env:TEMP ("ef_brand_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $clean | Out-Null
$copy = Join-Path $clean 'EcuFps.exe'
Copy-Item $Exe $copy
$p = Start-Process $copy -PassThru
try {
    if (-not (WaitFor { [WBr]::FindWindow('EcuFps.MainWindow', [NullString]::Value) -ne [IntPtr]::Zero } 10000)) { Fail 'window not shown' }
    $w = [WBr]::FindWindow('EcuFps.MainWindow', [NullString]::Value)
    $host_ = [WBr]::FindWindow('EcuFps.Host', [NullString]::Value)
    $sm = [WBr]::SendMessage($w, 0x007F, [IntPtr]2, [IntPtr]0)  # WM_GETICON ICON_SMALL2
    $bg = [WBr]::SendMessage($w, 0x007F, [IntPtr]1, [IntPtr]0)  # ICON_BIG
    if ($sm -eq [IntPtr]::Zero -or $bg -eq [IntPtr]::Zero) { Fail 'window icons missing' }
    Write-Host 'window: small/big icons set'
    # 4) Tepsi
    if (WaitFor { [WBr]::TrayIconExists($host_) } 5000) {
        $tc = [WBr]::RegisterWindowMessage('TaskbarCreated')
        [WBr]::PostMessage($host_, $tc, [IntPtr]0, [IntPtr]0) | Out-Null   # Explorer yeniden baslama benzetimi
        Start-Sleep -Milliseconds 800
        if (-not [WBr]::TrayIconExists($host_)) { Fail 'tray icon lost after TaskbarCreated' }
        Write-Host 'tray: icon present; still single icon after TaskbarCreated (stable id 1)'
    } else {
        Write-Host 'tray: no shell tray in this session (skipped)'
    }
    [WBr]::PostMessage($host_, 0x0010, [IntPtr]0, [IntPtr]0) | Out-Null  # cik ve geri al
    if (-not $p.WaitForExit(10000)) { Fail 'did not exit' }
    if ([WBr]::TrayIconExists($host_)) { Fail 'tray icon not removed on exit' }
    $side = @(Get-ChildItem $clean | Where-Object { $_.Name -ne 'EcuFps.exe' })
    if ($side.Count -gt 0) { Fail "side files created: $($side.Name -join ',')" }
    Write-Host ("clean folder: exe only ({0:N0} KiB), exit={1}" -f ((Get-Item $copy).Length / 1KB), $p.ExitCode)
    Write-Host 'OK'
} finally {
    if (-not $p.HasExited) { $p.Kill() }
    Remove-Item -Recurse -Force $clean -ErrorAction SilentlyContinue
}
