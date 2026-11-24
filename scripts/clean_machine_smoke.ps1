<#
.SYNOPSIS
  P9/B4 sahip VM turu: temiz Windows VM'de kurulum -> Performans Acik -> Kapali -> kaldirma turunun her durak noktasinda
  SALT OKUMA durum fotografi ve gunluk toplar; duraklari karsilastirir. YALNIZ sanal makinede (veya Windows Sandbox'ta) calisir.

.DESCRIPTION
  Betik HICBIR ayar, kayit defteri, hizmet, paket veya dosya DEGISTIRMEZ (yalniz -OutDir altina yazar). Kurulum EXE'sini
  baslatmaz, UAC istemez ve UAC'ye tiklamaz; her adimi sahip kendisi yapar (Evet/Hayir ve UAC sahibindir). Ana PC'de
  calistirilirsa reddeder (cikis 3). Yonetici olmadan calisirsa yalniz yonetici isteyen okumalar "okunamadi" yazilir.

  Durak noktalari (sira): before -> installed -> on -> off -> uninstalled.
  Kabul: before ile off arasinda adaptor hedeflerinde fark YOK; before ile uninstalled arasinda adaptor hedeflerinde fark
  YOK ve urun izi (ARP, hizmet, kurulum klasoru, kisayollar) kalmamis.

  Toplanan: ARP kaydi, EcuFpsHelper yapilandirmasi + hata eylemleri, kurulum/ProgramData/LocalAppData agaclari ve
  SHA-256, gunluk ve journal dosyalarinin KOPYASI (restore.log dahil), 0.9.0 adaptor hedefleri (ayar katalogu anahtarlari,
  UserGpuPreferences, AppCompat Layers, IFEO PerfOptions, ozellik hizmetleri baslangic tipi, Store paketleri, Defender
  dislamalari), SCM olay gunlugundeki son hizmet olaylari.

.EXAMPLE
  .\clean_machine_smoke.ps1 -Checkpoint before -OutDir C:\ef-tour -SetupExe C:\Users\u\Downloads\EcuFps-Setup-0.9.2.exe -ExpectedSha256 <sha>
  .\clean_machine_smoke.ps1 -Checkpoint installed -OutDir C:\ef-tour
  .\clean_machine_smoke.ps1 -Compare before,off -OutDir C:\ef-tour
  .\clean_machine_smoke.ps1 -Compare before,uninstalled -OutDir C:\ef-tour

.NOTES
  Cikis kodlari: 0 tamam, 1 hata, 2 karsilastirmada kabul disi fark, 3 sanal makine degil (calismadi).
  Denetim listesi: docs/TEST_SAFETY.md "Ek: P9 temiz VM turu".
#>
param(
    [ValidateSet('before', 'installed', 'on', 'off', 'uninstalled')]
    [string]$Checkpoint,
    [string]$Compare,
    [Parameter(Mandatory)][string]$OutDir,
    [string]$SetupExe,
    [string]$ExpectedSha256
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

# ---------------------------------------------------------------- yalniz VM

function Get-VmEvidence {
    $cs = Get-CimInstance -ClassName Win32_ComputerSystem
    $bios = Get-CimInstance -ClassName Win32_BIOS
    $text = "$($cs.Manufacturer) | $($cs.Model) | $($bios.SerialNumber) | $($bios.Manufacturer)"
    $isVm = $false
    foreach ($p in 'Virtual Machine', 'VMware', 'VirtualBox', 'innotek', 'QEMU', 'KVM', 'Xen', 'Parallels', 'Bochs') {
        if ($text -match [regex]::Escape($p)) { $isVm = $true }
    }
    if ($env:USERNAME -eq 'WDAGUtilityAccount') { $isVm = $true; $text += ' | Windows Sandbox' }
    [pscustomobject]@{ IsVm = $isVm; Text = $text }
}

# ---------------------------------------------------------------- salt okuma yardimcilari

function Read-RegKey([Microsoft.Win32.RegistryHive]$Hive, [string]$Subkey) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $k = $base.OpenSubKey($Subkey, $false)
        if ($null -eq $k) { return [ordered]@{ exists = $false } }
        try {
            $vals = [ordered]@{}
            foreach ($n in ($k.GetValueNames() | Sort-Object)) {
                $kind = $k.GetValueKind($n)
                $v = $k.GetValue($n, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                if ($v -is [byte[]]) { $v = ($v | ForEach-Object { $_.ToString('x2') }) -join '' }
                elseif ($v -is [string[]]) { $v = $v -join '|' }
                $vals[$n] = "$kind=$v"
            }
            return [ordered]@{ exists = $true; values = $vals; subkeys = @($k.GetSubKeyNames() | Sort-Object) }
        } finally { $k.Close() }
    } catch {
        return [ordered]@{ exists = $null; error = $_.Exception.Message }
    } finally { $base.Close() }
}

function Read-Tree([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return [ordered]@{ exists = $false } }
    $items = @()
    try {
        foreach ($f in Get-ChildItem -LiteralPath $Path -Recurse -Force -File -ErrorAction Stop) {
            $h = $null
            try { if ($f.Length -le 64MB) { $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $f.FullName).Hash.ToLowerInvariant() } } catch { $h = 'okunamadi' }
            $items += [ordered]@{ path = $f.FullName.Substring($Path.Length).TrimStart('\'); bytes = $f.Length; sha256 = $h }
        }
        return [ordered]@{ exists = $true; files = $items }
    } catch {
        return [ordered]@{ exists = $true; files = $items; error = $_.Exception.Message }
    }
}

function Copy-Logs([string]$From, [string]$To) {
    if (-not (Test-Path -LiteralPath $From)) { return }
    foreach ($f in Get-ChildItem -LiteralPath $From -Recurse -Force -File -ErrorAction SilentlyContinue) {
        if ($f.Extension -notin '.log', '.json', '.journal', '.txt') { continue }
        $rel = $f.FullName.Substring($From.Length).TrimStart('\')
        $dst = Join-Path $To $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        try { Copy-Item -LiteralPath $f.FullName -Destination $dst -Force } catch { Add-Content -LiteralPath (Join-Path $To 'copy-errors.txt') -Value "$rel : $($_.Exception.Message)" }
    }
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# 0.9.0 yazma adaptorlerinin hedefleri (kaynak: src/core/adapters/windows/*). Liste degisirse burada da guncellenir.
$TweakKeys = @(
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'),
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\Windows\DeliveryOptimization'),
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\Windows\DataCollection'),
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\WindowsStore'),
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\Windows\System'),
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\Edge'),
    @('LocalMachine', 'SOFTWARE\Policies\Microsoft\Windows\OneDrive'),
    @('CurrentUser', 'Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo'),
    @('CurrentUser', 'Software\Microsoft\Windows\CurrentVersion\Privacy'),
    @('CurrentUser', 'Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager'),
    @('CurrentUser', 'Software\Policies\Microsoft\Windows\Explorer'),
    @('CurrentUser', 'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'),
    @('CurrentUser', 'Control Panel\Mouse'),
    @('CurrentUser', 'Software\Microsoft\DirectX\UserGpuPreferences'),
    @('CurrentUser', 'Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers')
)
$IfeoExes = @('cs2.exe', 'dota2.exe', 'Minecraft.Windows.exe')
$FeatureServices = @('BTAGService', 'bthserv', 'PrintNotify', 'Spooler', 'XblGameSave', 'XboxNetApiSvc', 'XboxGipSvc', 'XblAuthManager', 'WSearch')
$AppxFamilies = @('Clipchamp.Clipchamp_yxz26nhyzhsrt', 'Microsoft.ZuneMusic_8wekyb3d8bbwe', 'Microsoft.ZuneVideo_8wekyb3d8bbwe',
    'Microsoft.MicrosoftSolitaireCollection_8wekyb3d8bbwe', 'Microsoft.BingNews_8wekyb3d8bbwe', 'Microsoft.BingWeather_8wekyb3d8bbwe')

function Get-Targets {
    $t = [ordered]@{}
    foreach ($e in $TweakKeys) { $t["reg:$($e[0]):$($e[1])"] = Read-RegKey ([Microsoft.Win32.RegistryHive]$e[0]) $e[1] }
    foreach ($x in $IfeoExes) {
        $sk = "SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\$x\PerfOptions"
        $t["ifeo:$x"] = Read-RegKey ([Microsoft.Win32.RegistryHive]::LocalMachine) $sk
    }
    foreach ($s in $FeatureServices) {
        $svc = Get-CimInstance -ClassName Win32_Service -Filter "Name='$s'" -ErrorAction SilentlyContinue
        $delayed = $null
        if ($svc -and $svc.PSObject.Properties['DelayedAutoStart']) { $delayed = $svc.DelayedAutoStart }
        $t["svc:$s"] = if ($svc) { [ordered]@{ exists = $true; start = $svc.StartMode; delayed = $delayed; state = $svc.State } } else { [ordered]@{ exists = $false } }
    }
    foreach ($f in $AppxFamilies) {
        $name = $f.Substring(0, $f.LastIndexOf('_'))
        $p = @(Get-AppxPackage -Name $name -ErrorAction SilentlyContinue | Where-Object { $_.PackageFamilyName -eq $f })
        $t["appx:$f"] = [ordered]@{ installed = ($p.Count -gt 0) }  # tam ad (surum) Kapali'da provisioned surume donebilir
    }
    if (Test-Admin) {
        try { $t['defender:exclusions'] = [ordered]@{ paths = @((Get-MpPreference).ExclusionPath | Where-Object { $_ } | Sort-Object) } }
        catch { $t['defender:exclusions'] = [ordered]@{ error = $_.Exception.Message } }
    } else {
        $t['defender:exclusions'] = [ordered]@{ error = 'okunamadi: yonetici degil (Defender listeyi gizler)' }
    }
    $t
}

function Get-Product {
    $pf = [Environment]::GetFolderPath('ProgramFiles')
    $pd = [Environment]::GetFolderPath('CommonApplicationData')
    $la = [Environment]::GetFolderPath('LocalApplicationData')
    $svc = Get-CimInstance -ClassName Win32_Service -Filter "Name='EcuFpsHelper'" -ErrorAction SilentlyContinue
    $failure = $null
    if ($svc) { $failure = (& sc.exe qfailure EcuFpsHelper 2>&1 | Out-String).Trim() }
    $events = @()
    try {
        $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Service Control Manager'; StartTime = (Get-Date).AddDays(-2) } -MaxEvents 400 -ErrorAction Stop |
            Where-Object { $_.Message -match 'EcuFps' } | Select-Object -First 40 |
            ForEach-Object { [ordered]@{ time = $_.TimeCreated.ToString('s'); id = $_.Id; msg = ($_.Message -replace '\s+', ' ') } })
    } catch { }
    [ordered]@{
        arp = Read-RegKey ([Microsoft.Win32.RegistryHive]::LocalMachine) 'Software\Microsoft\Windows\CurrentVersion\Uninstall\EcuFps'
        service = if ($svc) { [ordered]@{ exists = $true; start = $svc.StartMode; state = $svc.State; path = $svc.PathName; account = $svc.StartName; exit = $svc.ExitCode; failure_actions = $failure } } else { [ordered]@{ exists = $false } }
        install_dir = Read-Tree (Join-Path $pf 'Ecu Fps')
        machine_dir = Read-Tree (Join-Path $pd 'EcuFps')
        user_dir = Read-Tree (Join-Path $la 'EcuFps')
        start_menu_link = Test-Path -LiteralPath (Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Ecu Fps.lnk')
        desktop_link = Test-Path -LiteralPath (Join-Path ([Environment]::GetFolderPath('CommonDesktopDirectory')) 'Ecu Fps.lnk')
        scm_events = $events
    }
}

function Save-Checkpoint([string]$Name) {
    $dir = Join-Path $OutDir $Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $os = Get-CimInstance -ClassName Win32_OperatingSystem
    $snap = [ordered]@{
        checkpoint = $Name
        time = (Get-Date).ToString('s')
        os = "$($os.Caption) $($os.Version) build $($os.BuildNumber)"
        vm = (Get-VmEvidence).Text
        admin = (Test-Admin)
        setup_exe = $null
        product = Get-Product
        targets = Get-Targets
    }
    if ($SetupExe) {
        $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $SetupExe).Hash.ToLowerInvariant()
        $snap.setup_exe = [ordered]@{ path = $SetupExe; sha256 = $h; expected = $ExpectedSha256; match = ($ExpectedSha256 -and $h -eq $ExpectedSha256.ToLowerInvariant()) }
    }
    $snap | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $dir 'state.json') -Encoding UTF8
    $pd = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'EcuFps'
    $la = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'EcuFps'
    Copy-Logs $pd (Join-Path $dir 'files\ProgramData')
    Copy-Logs $la (Join-Path $dir 'files\LocalAppData')
    Write-Host "Durak '$Name' kaydedildi: $dir"
    if ($snap.setup_exe -and -not $snap.setup_exe.match) { Write-Warning 'Kurulum EXE SHA-256 beklenenle ESLESMIYOR: kuruluma gecmeyin.' }
    $next = @{
        before      = "Simdi: kurulum EXE'sini cift tiklayin, UAC'yi SIZ onaylayin, kurulum bitince: -Checkpoint installed"
        installed   = "Simdi: uygulamada Performans Acik, plandaki 0.9.0 gruplarina Evet; uygulama bitince: -Checkpoint on"
        on          = "Simdi: Performans Kapali (geri yukleme) bitince: -Checkpoint off ; sonra -Compare before,off"
        off         = "Simdi: Ayarlar > Kaldir (UAC sizde); bitince: -Checkpoint uninstalled ; sonra -Compare before,uninstalled"
        uninstalled = 'Tur bitti: -Compare before,uninstalled sonucunu ve OutDir klasorunu kanit olarak saklayin.'
    }
    Write-Host $next[$Name]
}

# ---------------------------------------------------------------- karsilastirma

function Get-Flat($obj, [string]$prefix) {
    $out = [ordered]@{}
    if ($null -eq $obj) { $out[$prefix] = '<null>'; return $out }
    if ($obj -is [pscustomobject]) {
        foreach ($p in $obj.PSObject.Properties) { foreach ($kv in (Get-Flat $p.Value "$prefix/$($p.Name)").GetEnumerator()) { $out[$kv.Key] = $kv.Value } }
    } elseif ($obj -is [array]) {
        $out[$prefix] = ($obj | ForEach-Object { ($_ | ConvertTo-Json -Compress -Depth 6) }) -join ','
    } else { $out[$prefix] = "$obj" }
    $out
}

# Not: PowerShell degisken adlari buyuk/kucuk harf duyarsizdir; parametre ve nesne adlari ayri tutulur.
function Compare-Checkpoints([string]$From, [string]$To) {
    $sa = Get-Content -LiteralPath (Join-Path $OutDir "$From\state.json") -Raw | ConvertFrom-Json
    $sb = Get-Content -LiteralPath (Join-Path $OutDir "$To\state.json") -Raw | ConvertFrom-Json
    $fa = Get-Flat $sa.targets 'targets'
    $fb = Get-Flat $sb.targets 'targets'
    $diff = @()
    foreach ($k in (@($fa.Keys) + @($fb.Keys) | Sort-Object -Unique)) {
        $va = if ($fa.Contains($k)) { $fa[$k] } else { '<yok>' }
        $vb = if ($fb.Contains($k)) { $fb[$k] } else { '<yok>' }
        if ($va -ne $vb) { $diff += "| $k | $va | $vb |" }
    }
    $trace = @()
    if ($To -eq 'uninstalled') {
        if ($sb.product.arp.exists) { $trace += 'ARP kaydi duruyor' }
        if ($sb.product.service.exists) { $trace += 'EcuFpsHelper hizmeti duruyor' }
        if ($sb.product.install_dir.exists) { $trace += 'Program Files\Ecu Fps duruyor (yeniden baslatma bekleniyor olabilir)' }
        if ($sb.product.start_menu_link -or $sb.product.desktop_link) { $trace += 'kisayol duruyor' }
    }
    $md = @("# VM turu karsilastirma: $From -> $To", '', "- A: $($sa.time) $($sa.os) [$($sa.vm)]", "- B: $($sb.time) [$($sb.vm)]", '')
    $md += if ($diff.Count) { @('## Adaptor hedef farklari (KABUL DISI)', '', '| hedef | A | B |', '|---|---|---|') + $diff } else { '## Adaptor hedefleri: fark yok' }
    if ($To -eq 'uninstalled') { $md += @('', '## Urun izi') + $(if ($trace.Count) { $trace | ForEach-Object { "- $_" } } else { '- iz yok (ARP, hizmet, klasor, kisayol)' }) }
    if (-not $sa.admin -or -not $sb.admin) { $md += @('', '- Not: yonetici olmayan durak var; Defender dislamalari okunamadi (fark olarak sayilmaz ama dogrulanmadi).') }
    $file = Join-Path $OutDir "compare-$From-$To.md"
    $md | Set-Content -LiteralPath $file -Encoding UTF8
    Write-Host ($md -join [Environment]::NewLine)
    if ($diff.Count -or $trace.Count) { return 2 }
    0
}

# ---------------------------------------------------------------- giris

try {
    $vm = Get-VmEvidence
    if (-not $vm.IsVm) {
        Write-Error "Bu makine sanal makine olarak tanimlanmadi ($($vm.Text)). Betik yalniz izole VM'de calisir; hicbir sey yapilmadi." -ErrorAction Continue
        exit 3
    }
    if ($Compare) {
        $p = $Compare.Split(',')
        if ($p.Count -ne 2) { throw '-Compare iki durak ister: ornek before,off' }
        exit (Compare-Checkpoints $p[0].Trim() $p[1].Trim())
    }
    if (-not $Checkpoint) { throw '-Checkpoint veya -Compare gerekli.' }
    Save-Checkpoint $Checkpoint
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
