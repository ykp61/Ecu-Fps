<#
.SYNOPSIS
  Ecu Fps boşta ayak izi ölçümü (P9-B9): boşta CPU, çalışma kümesi, özel bayt, handle sayısı; pencere açık ve tepsi durumu.

.DESCRIPTION
  YALNIZ OKUMA. Betik kendi başlattığı bir uygulama kopyasını ölçer (EF_TEST_BACKGROUND=1, EF_UPDATE_CHECK=0, EF_SETUP_ROLE=0,
  EF_DATA_DIR = %TEMP% altında geçici klasör; gerçek veri klasörüne, çalışan uygulamaya veya hizmete dokunulmaz).
  Ayar uygulanmaz, performans anahtarı kullanılmaz. Süreç sayaçları yalnız bu betiğin başlattığı PID için okunur.
  Aynı oturumda başka bir EcuFps (kullanıcı arayüzü) çalışıyorsa tek örnek kuralı kopyayı ona devreder; bu durumda
  betik ölçmeden çıkar (çıkış 77, ctest SKIP). Oturum 0'daki yardımcı hizmet sayılmaz.

  Ölçüm adımları:
    1) Açık: pencere görünür (arka planda), ısınma süresi, sonra IdleSeconds boyunca boşta CPU (toplam makine yüzdesi).
    2) Tepsi: ana pencere kapatılır (CloseMainWindow), süreç tepside sürer, TraySeconds boyunca ölçülür.
  CPU yüzdesi = (TotalProcessorTime farkı) / (duvar saati * mantıksal çekirdek) * 100 (makinenin tamamı).
  Bellek: WorkingSetSize, PrivateWorkingSetSize (yalnız bu sürece ait fiziksel sayfalar) ve PrivateUsage (özel bayt = commit); GetProcessMemoryInfo ile okunur. Tepsi bellek ölçümünden önce kısa bekleme vardır
  (uygulama kapanışta kendi heap'ini ve çalışma kümesini kırpar).

  -Check ile eşik karşılaştırması yapılır (çıkış 1 = bütçe aşımı). Eşiksiz çalışma yalnız rapor üretir.

.EXAMPLE
  .\scripts\footprint.ps1 -Exe build\x64-dist\src\app\EcuFps.exe -IdleSeconds 60 -Out artifacts\evidence\P9-footprint-0.9.x.md
.EXAMPLE
  .\scripts\footprint.ps1 -Exe $exe -IdleSeconds 15 -TraySeconds 10 -Check -MaxOpenPrivateMiB 40 -MaxIdleCpuPercent 0.2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Exe,
    [int]$WarmupSeconds = 8,
    [int]$IdleSeconds = 30,
    [int]$TraySeconds = 20,
    [string]$Label = '',
    [string]$Out = '',
    [switch]$Check,
    [double]$MaxOpenPrivateMiB = 0,
    [double]$MaxOpenWorkingSetMiB = 0,
    [double]$MaxOpenPrivateWsMiB = 0,
    [double]$MaxTrayPrivateWsMiB = 0,
    [double]$MaxTrayPrivateMiB = 0,
    [double]$MaxTrayWorkingSetMiB = 0,
    [double]$MaxIdleCpuPercent = 0,   # tepsi (pencere kapali) boşta CPU, % makine
    [double]$MaxOpenCpuPercent = 0,   # pencere açıkken CPU, % makine (canlı örnekleme nedeniyle tepsiden yüksektir)
    [int]$MaxHandles = 0,
    [switch]$NoWelcomeDismiss
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "footprint: HATA: exe yok: $Exe" -ForegroundColor Red; exit 2 }
$Exe = (Resolve-Path -LiteralPath $Exe).Path

# Başka bir kullanıcı arayüzü örneği (bu oturum) varsa kopya ona devredilir: ölçme.
$session = (Get-Process -Id $PID).SessionId
$other = @(Get-Process -Name 'EcuFps' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session })
if ($other.Count -gt 0) {
    Write-Host 'footprint: atlandi (bu oturumda baska bir EcuFps calisiyor; tek ornek kurali kopyayi ona devrederdi)'
    exit 77
}

$dataDir = Join-Path ([IO.Path]::GetTempPath()) ('ef-footprint-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$env:EF_TEST_BACKGROUND = '1'
$env:EF_UPDATE_CHECK = '0'
$env:EF_SETUP_ROLE = '0'   # derleme klasorunden bos acilis kurulum penceresi degil kullanici arayuzu olsun (ctest ile ayni)
$env:EF_DATA_DIR = $dataDir

$cores = [Environment]::ProcessorCount
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class EfMem {
  [StructLayout(LayoutKind.Sequential)] public struct EX2 {
    public uint cb; public uint PageFaultCount; public UIntPtr PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage,
      QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage, PrivateWorkingSetSize;
    public ulong SharedCommitUsage; }
  [DllImport("psapi.dll", SetLastError = true)] static extern bool GetProcessMemoryInfo(IntPtr h, ref EX2 c, uint cb);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inh, int pid);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  // [WorkingSet, PrivateUsage, PrivateWorkingSet] bayt; okunamazsa null.
  public static ulong[] Read(int pid) {
    IntPtr h = OpenProcess(0x1000 | 0x0010, false, pid); // QUERY_LIMITED_INFORMATION | VM_READ
    if (h == IntPtr.Zero) return null;
    try { EX2 c = new EX2(); c.cb = (uint)Marshal.SizeOf(typeof(EX2));
      if (!GetProcessMemoryInfo(h, ref c, c.cb)) return null;
      return new ulong[] { (ulong)c.WorkingSetSize, (ulong)c.PrivateUsage, (ulong)c.PrivateWorkingSetSize }; }
    finally { CloseHandle(h); }
  }
}
'@
function MiB([double]$b) { [math]::Round($b / 1MB, 1) }

function Get-Sample($p) {
    $p.Refresh()
    $m = [EfMem]::Read($p.Id)
    [pscustomobject]@{
        Cpu     = $p.TotalProcessorTime.TotalSeconds
        Ws      = $(if ($m) { $m[0] } else { $p.WorkingSet64 })
        Private = $(if ($m) { $m[1] } else { $p.PrivateMemorySize64 })
        PrivWs  = $(if ($m) { $m[2] } else { 0 })
        Handles = $p.HandleCount
        Threads = $p.Threads.Count
        At      = [Diagnostics.Stopwatch]::GetTimestamp()
    }
}

# Çocuk süreçler (varsa; "aynı EXE yardımcı süreç" olabilir): yalnız okuma, PID ağacı.
function Get-Children([int]$ParentPid) {
    $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, Name -ErrorAction SilentlyContinue)
    $ids = New-Object System.Collections.Generic.List[int]
    $queue = New-Object System.Collections.Generic.Queue[int]; $queue.Enqueue($ParentPid)
    while ($queue.Count) {
        $cur = $queue.Dequeue()
        foreach ($c in ($all | Where-Object { $_.ParentProcessId -eq $cur -and $_.ProcessId -ne $cur })) { $ids.Add([int]$c.ProcessId); $queue.Enqueue([int]$c.ProcessId) }
    }
    $ids
}

function Measure-Phase($p, [int]$Seconds) {
    # 5 sn'lik dilimlerle ornekler: toplam ortalama + dilim dizisi (CPU ms) rapora girer; bellek/handle son ornekten.
    $a = Get-Sample $p
    $prev = $a; $slices = New-Object System.Collections.Generic.List[int]
    $remaining = $Seconds
    while ($remaining -gt 0) {
        $step = [math]::Min(5, $remaining)
        Start-Sleep -Seconds $step
        $cur = Get-Sample $p
        $slices.Add([int][math]::Round(($cur.Cpu - $prev.Cpu) * 1000, 0))
        $prev = $cur; $remaining -= $step
    }
    $b = $prev
    $wall = ($b.At - $a.At) / [Diagnostics.Stopwatch]::Frequency
    $cpuPct = if ($wall -gt 0) { [math]::Round((($b.Cpu - $a.Cpu) / ($wall * $cores)) * 100, 3) } else { 0 }
    [pscustomobject]@{
        Seconds = [math]::Round($wall, 1); CpuMs = [math]::Round(($b.Cpu - $a.Cpu) * 1000, 0); CpuPct = $cpuPct
        WsMiB = MiB $b.Ws; PrivateMiB = MiB $b.Private; PrivWsMiB = MiB $b.PrivWs; Handles = $b.Handles; Threads = $b.Threads
        Slices = ($slices -join ',')
    }
}

# Ölçüm sırasında BAŞKA bir EcuFps arayüzü (örn. paralel çalışan başka bir test/ajan) açılırsa tek örnek kuralı onu
# bizim kopyamıza devreder (pencereyi yeniden gösterir): ölçüm geçersizdir, hata değil atlama (77) sayılır.
function Get-OtherUi([int]$OurPid) {
    @(Get-Process -Name 'EcuFps' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session -and $_.Id -ne $OurPid })
}
$skipReason = ''
$result = [ordered]@{}
$p = $null
$notes = New-Object System.Collections.Generic.List[string]
try {
    $p = Start-Process -FilePath $Exe -PassThru
    $end = (Get-Date).AddSeconds(30)
    while ($p.MainWindowHandle -eq 0 -and -not $p.HasExited -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 200; $p.Refresh() }
    if ($p.HasExited) { throw "uygulama beklenmedik sekilde cikti (kod $($p.ExitCode))" }
    if ($p.MainWindowHandle -eq 0) { throw 'ana pencere 30 sn icinde acilmadi' }

    # Karşılama (boş profil): kapatılır ki boşta durum genel bakış sayfasında ölçülsün. Başarısız olursa not düşülür.
    if (-not $NoWelcomeDismiss) {
        try {
            Add-Type -AssemblyName UIAutomationClient; Add-Type -AssemblyName UIAutomationTypes
            $AE = [System.Windows.Automation.AutomationElement]; $TS = [System.Windows.Automation.TreeScope]
            Start-Sleep -Seconds 2
            $win = $AE::FromHandle($p.MainWindowHandle)
            $w = $win.FindFirst($TS::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::AutomationIdProperty, 'welcome.start')))
            if ($w) { $w.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); $notes.Add('Karşılama kapatıldı (geçici veri klasörü; tercih orada).') }
            else { $notes.Add('Karşılama bulunamadı (zaten kapalı).') }
        } catch { $notes.Add("Karşılama kapatılamadı: $($_.Exception.Message)") }
    }
    Start-Sleep -Seconds $WarmupSeconds

    $kids = @(Get-Children $p.Id)
    $result.Children = $kids.Count
    $result.Open = Measure-Phase $p $IdleSeconds
    $result.Open | Add-Member -NotePropertyName WindowVisible -NotePropertyValue ($p.MainWindowHandle -ne 0)
    if ((Get-OtherUi $p.Id).Count -gt 0) { throw 'SKIP: ölçüm sırasında bu oturumda başka bir EcuFps açıldı (tek örnek devri); ölçüm geçersiz' }

    # Tepsi: ana pencereyi kapat; süreç tepside sürmeli.
    $closeSw = [Diagnostics.Stopwatch]::StartNew()
    $sent = $p.CloseMainWindow()
    $tend = (Get-Date).AddSeconds(30)
    do { Start-Sleep -Milliseconds 300; $p.Refresh() } while (-not $p.HasExited -and $p.MainWindowHandle -ne 0 -and (Get-Date) -lt $tend)
    $notes.Add("Pencere kapatma: CloseMainWindow=$sent, tepsiye geçiş $([math]::Round($closeSw.Elapsed.TotalSeconds, 1)) sn, pencere hâlâ görünür=$($p.MainWindowHandle -ne 0).")
    if (-not $p.HasExited -and $p.MainWindowHandle -ne 0) {
        $result.Tray = $null
        $notes.Add('Pencere 30 sn içinde kapanmadı; tepsi durumu ÖLÇÜLEMEDİ (açık durumu tepsi diye raporlamamak için).')
    } elseif ($p.HasExited) {
        $result.Tray = $null
        $notes.Add("Pencere kapanınca süreç çıktı (kod $($p.ExitCode)); tepsi durumu ÖLÇÜLEMEDİ (tepsi simgesi eklenememiş olabilir).")
    } else {
        Start-Sleep -Seconds 4   # uygulama kendi heap/çalışma kümesini kırpar; sonra ölç
        $result.Tray = Measure-Phase $p $TraySeconds
        $result.Tray | Add-Member -NotePropertyName WindowVisible -NotePropertyValue ($p.MainWindowHandle -ne 0)
        if ((Get-OtherUi $p.Id).Count -gt 0) { throw 'SKIP: tepsi ölçümü sırasında bu oturumda başka bir EcuFps açıldı (tek örnek devri); ölçüm geçersiz' }
    }
} catch {
    if ($_.Exception.Message.StartsWith('SKIP:')) { $skipReason = $_.Exception.Message }
    else { Write-Host "footprint: HATA: $($_.Exception.Message)" -ForegroundColor Red; $result.Error = $_.Exception.Message }
} finally {
    if ($p -and -not $p.HasExited) {
        # Yalnız bu betiğin başlattığı PID: önce nazik kapanış, sonra sonlandırma.
        try { [void]$p.CloseMainWindow(); [void]$p.WaitForExit(3000) } catch {}
        if (-not $p.HasExited) { try { $p.Kill(); [void]$p.WaitForExit(5000) } catch {} }
    }
    Remove-Item -LiteralPath $dataDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($skipReason) { Write-Host "footprint: atlandi ($skipReason)"; exit 77 }
if ($result.Error -or -not $result.Open) { exit 2 }

# ---- rapor ----
$head = (& git -C $Root rev-parse --short=12 HEAD 2>$null)
$exeInfo = Get-Item -LiteralPath $Exe
$rel = $Exe; if ($Exe.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)) { $rel = $Exe.Substring($Root.Length).TrimStart('\') }
$os = (Get-CimInstance Win32_OperatingSystem)
$cpuName = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$lines = New-Object System.Collections.Generic.List[string]
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm'
$lines.Add("Ölçüm: $stamp, HEAD $head, $($os.Caption) $($os.Version), $cpuName ($cores mantıksal çekirdek).")
$lines.Add("Uygulama: ``$rel`` ($([math]::Round($exeInfo.Length / 1MB, 2)) MiB)$(if ($Label) { ", $Label" }).")
$lines.Add("Yöntem: kendi başlattığı kopya (EF_TEST_BACKGROUND=1, EF_UPDATE_CHECK=0, EF_SETUP_ROLE=0, EF_DATA_DIR=geçici klasör); yalnız okuma. Isınma $WarmupSeconds sn, açık $IdleSeconds sn, tepsi $TraySeconds sn. CPU = makinenin tamamına oran.")
$lines.Add('')
$lines.Add('| Durum | Süre (sn) | CPU (ms) | CPU (% makine) | Çalışma kümesi (MiB) | Özel çalışma kümesi (MiB) | Özel bayt = commit (MiB) | Handle | İş parçacığı |')
$lines.Add('|---|---|---|---|---|---|---|---|---|')
function Row([string]$name, $m) { "| $name | $($m.Seconds) | $($m.CpuMs) | $($m.CpuPct) | $($m.WsMiB) | $($m.PrivWsMiB) | $($m.PrivateMiB) | $($m.Handles) | $($m.Threads) |" }
$lines.Add((Row 'Açık (pencere görünür, arka planda)' $result.Open))
if ($result.Tray) { $lines.Add((Row 'Tepsi (pencere kapalı)' $result.Tray)) } else { $lines.Add('| Tepsi | ölçülemedi | | | | | | | |') }
$lines.Add('')
$trayVis = ''; if ($result.Tray) { $trayVis = "; tepside pencere görünür: $($result.Tray.WindowVisible)" }
$lines.Add("CPU dilimleri (5 sn başına ms): açık [$($result.Open.Slices)]$(if ($result.Tray) { "; tepsi [$($result.Tray.Slices)]" }).")
$lines.Add("Çocuk süreç sayısı: $($result.Children). Açıkken pencere görünür: $($result.Open.WindowVisible)$trayVis.")
foreach ($n in $notes) { $lines.Add("- $n") }

# ---- eşik denetimi ----
$fails = New-Object System.Collections.Generic.List[string]
function Gate([string]$what, [double]$val, [double]$max) { if ($max -gt 0 -and $val -gt $max) { $fails.Add("$what $val > $max") } }
if ($Check) {
    Gate 'acik ozel bayt MiB' $result.Open.PrivateMiB $MaxOpenPrivateMiB
    Gate 'acik calisma kumesi MiB' $result.Open.WsMiB $MaxOpenWorkingSetMiB
    Gate 'acik ozel calisma kumesi MiB' $result.Open.PrivWsMiB $MaxOpenPrivateWsMiB
    Gate 'acik CPU %' $result.Open.CpuPct $MaxOpenCpuPercent
    Gate 'acik handle' $result.Open.Handles $MaxHandles
    if ($result.Tray) {
        Gate 'tepsi ozel bayt MiB' $result.Tray.PrivateMiB $MaxTrayPrivateMiB
        Gate 'tepsi calisma kumesi MiB' $result.Tray.WsMiB $MaxTrayWorkingSetMiB
        Gate 'tepsi ozel calisma kumesi MiB' $result.Tray.PrivWsMiB $MaxTrayPrivateWsMiB
        Gate 'tepsi bosta CPU %' $result.Tray.CpuPct $MaxIdleCpuPercent
        Gate 'tepsi handle' $result.Tray.Handles $MaxHandles
    } else {
        $fails.Add('tepsi durumu olculemedi')
    }
    $verdict = 'bütçe içinde'; if ($fails.Count) { $verdict = 'AŞIM: ' + ($fails -join '; ') }
    $lines.Add('')
    $lines.Add("Eşikler: açık özel bayt ≤ $MaxOpenPrivateMiB MiB, açık WS ≤ $MaxOpenWorkingSetMiB MiB, açık özel WS ≤ $MaxOpenPrivateWsMiB MiB, tepsi özel bayt ≤ $MaxTrayPrivateMiB MiB, tepsi WS ≤ $MaxTrayWorkingSetMiB MiB, tepsi özel WS ≤ $MaxTrayPrivateWsMiB MiB, açık CPU ≤ %$MaxOpenCpuPercent, tepsi boşta CPU ≤ %$MaxIdleCpuPercent, handle ≤ $MaxHandles (0 = denetlenmez). Sonuç: $verdict.")
}

$lines | ForEach-Object { Write-Host $_ }
if ($Out) {
    $dir = Split-Path -Parent $Out
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
    $lines | Set-Content -LiteralPath $Out -Encoding UTF8
    Write-Host "footprint: rapor -> $Out"
}
if ($fails.Count) { Write-Host "footprint: BUTCE ASIMI: $($fails -join '; ')" -ForegroundColor Red; exit 1 }
Write-Host 'footprint: OK'
exit 0
