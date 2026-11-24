param([Parameter(Mandatory)][string]$Tool)
# INV: gercek envanteri bagimsiz Windows kaynaklariyla karsilastir.
$ErrorActionPreference = 'Stop'
function Fail($m) { throw "INV: $m" }

$raw = & $Tool --raw | ConvertFrom-Json
$red = (& $Tool) -join "`n"

# --- INV-01: OS ---
$os = Get-CimInstance Win32_OperatingSystem
$cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
if ($raw.os.build.value -ne [int]$os.BuildNumber) { Fail "build $($raw.os.build.value) != $($os.BuildNumber)" }
if ($raw.os.ubr.value -ne $cv.UBR) { Fail "UBR" }
if ($raw.os.display_version.value -ne $cv.DisplayVersion) { Fail "DisplayVersion" }
if ($raw.os.native_arch.value -ne 'x64') { Fail "native arch $($raw.os.native_arch.value)" }
if ($os.Caption -notlike "*$($raw.os.product_name.value.Replace('Windows ', ''))*") { Fail "product name '$($raw.os.product_name.value)' vs '$($os.Caption)'" }

# --- INV-01: kullanici ---
$me = [Security.Principal.WindowsIdentity]::GetCurrent()
if ($raw.user.process_user_sid.value -ne $me.User.Value) { Fail "process SID" }
if ($raw.user.session_id.value -ne (Get-Process -Id $PID).SessionId) { Fail "session id" }
$isAdmin = ([Security.Principal.WindowsPrincipal]$me).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($raw.user.elevated.value -ne $isAdmin) { Fail "elevated" }
if ($raw.user.target_user_sid.value -ne $raw.user.session_user_sid.value) { Fail "target must be session user" }
if ($raw.user.different_admin.value -ne $false) { Fail "same-user run flagged as different admin" }

# --- INV-01: guc ---
Add-Type -AssemblyName System.Windows.Forms
$ps = [System.Windows.Forms.SystemInformation]::PowerStatus
$onAc = $ps.PowerLineStatus -eq 'Online'
if ($ps.PowerLineStatus -ne 'Unknown' -and $raw.power.on_ac.value -ne $onAc) { Fail "on_ac" }
$noBattery = ($ps.BatteryChargeStatus -band [System.Windows.Forms.BatteryChargeStatus]::NoSystemBattery) -ne 0
if ($raw.power.has_battery.value -ne (-not $noBattery)) { Fail "has_battery" }
if ($noBattery -and $raw.power.battery_percent.unknown -eq $null) { Fail "battery percent must be unknown without battery" }

# --- INV-01: politika (her biri absent/present/unknown) ---
foreach ($p in $raw.policy.policies) { if ($p.state -notin 'absent', 'present', 'unknown') { Fail "policy state $($p.state)" } }

# --- INV-02: CPU / RAM / anakart ---
$cpu = Get-CimInstance Win32_Processor
if ($raw.cpu.physical_cores.value -ne ($cpu | Measure-Object NumberOfCores -Sum).Sum) { Fail "cores $($raw.cpu.physical_cores.value)" }
if ($raw.cpu.logical_processors.value -ne ($cpu | Measure-Object NumberOfLogicalProcessors -Sum).Sum) { Fail "logical" }
if ($raw.cpu.brand.value -ne $cpu[0].Name.Trim()) { Fail "brand '$($raw.cpu.brand.value)' vs '$($cpu[0].Name.Trim())'" }
$dimms = @(Get-CimInstance Win32_PhysicalMemory)
if ($raw.memory.populated_slots.value -ne $dimms.Count) { Fail "dimm count $($raw.memory.populated_slots.value) vs $($dimms.Count)" }
$capMib = ($dimms | Measure-Object Capacity -Sum).Sum / 1MB
$modMib = ($raw.memory.modules | % { $_.size_mib.value } | Measure-Object -Sum).Sum
if ($modMib -ne $capMib) { Fail "module MiB $modMib vs $capMib" }
if ($raw.memory.channel_count.value -ne $null) { Fail "channel count must stay unknown" }
$bb = Get-CimInstance Win32_BaseBoard
if ($raw.board.board_product.value -and $raw.board.board_product.value -ne $bb.Product.Trim()) { Fail "board product" }
if ($red -match 'SERIAL' -or ($bb.SerialNumber -and $bb.SerialNumber.Trim().Length -gt 3 -and ($red + ($raw | ConvertTo-Json -Depth 8)) -match [regex]::Escape($bb.SerialNumber.Trim()))) { Fail "serial leaked" }

# --- INV-03: GPU / depolama ---
$cs = Get-CimInstance Win32_ComputerSystem
$isVm = $cs.Model -match 'Virtual|VMware|KVM|VirtualBox|HVM domU' -or $cs.Manufacturer -match 'Xen|QEMU|innotek'
# Yalniz goruntu adaptorleri (Hyper-V Video, Basic Display) DXGI render adaptoru degildir.
$vc = @(Get-CimInstance Win32_VideoController | ? { $_.Name -notmatch 'Hyper-V Video|Basic Display' })
foreach ($v in $vc) {
    $m = @($raw.gpus | ? { $_.name.value -eq $v.Name })
    if ($m.Count -lt 1) { Fail "GPU '$($v.Name)' not enumerated by DXGI" }
    if ($m[0].driver_version.value -ne $v.DriverVersion) { Fail "driver $($m[0].driver_version.value) vs $($v.DriverVersion)" }
}
$dgpu = @($raw.gpus | ? { $_.vendor -eq 'NVIDIA' })
if ($dgpu.Count -and $dgpu[0].kind -ne 'discrete') { Fail "NVIDIA must be discrete" }
$igpu = @($raw.gpus | ? { $_.vendor -eq 'AMD' -and $_.name.value -like '*Radeon(TM) Graphics*' })
if ($igpu.Count -and $igpu[0].kind -ne 'integrated') { Fail "AMD iGPU must be integrated (got $($igpu[0].kind))" }
$warp = @($raw.gpus | ? { $_.kind -eq 'software' })
$pd = @(Get-PhysicalDisk)
foreach ($p in $pd) {
    $d = @($raw.disks | ? { $_.disk_number -eq [int]$p.DeviceId })
    if ($d.Count -ne 1) { Fail "disk $($p.DeviceId) missing" }
    $bus = @{ 'NVMe' = 'nvme'; 'SATA' = 'sata'; 'USB' = 'usb'; 'RAID' = 'raid'; 'SAS' = 'sas' }[[string]$p.BusType]
    if ($bus -and $d[0].bus -ne $bus) { Fail "disk $($p.DeviceId) bus $($d[0].bus) vs $($p.BusType)" }
    $media = @{ 'SSD' = 'ssd'; 'HDD' = 'hdd' }[[string]$p.MediaType]
    if ($media -and $d[0].media -ne $media) { Fail "disk $($p.DeviceId) media $($d[0].media) vs $($p.MediaType)" }
}
$sysVol = @($raw.volumes | ? { $_.root -eq "$env:SystemDrive\" })
$sysDisk = (Get-Partition -DriveLetter $env:SystemDrive[0]).DiskNumber
if ($sysVol[0].disk_number.value -ne $sysDisk) { Fail "system volume disk $($sysVol[0].disk_number.value) vs $sysDisk" }
if ($raw.virtual_machine -ne $isVm) { Fail "virtual_machine=$($raw.virtual_machine) but model '$($cs.Model)'" }

# --- INV-05: guc / guvenlik ---
$ps5 = $raw.power_security
$pc = (powercfg /getactivescheme) -join ' '
if ($pc -notmatch [regex]::Escape($ps5.active_plan_guid.value)) { Fail "active plan $($ps5.active_plan_guid.value) vs '$pc'" }
try {
    $dg = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction Stop
    $vbsMap = @{ 0 = 'off'; 1 = 'enabled_not_running'; 2 = 'running' }
    if ($ps5.vbs.value -ne $vbsMap[[int]$dg.VirtualizationBasedSecurityStatus]) { Fail "vbs $($ps5.vbs.value)" }
    $hv = @($dg.SecurityServicesRunning) -contains 2
    if ($ps5.hvci_running.value -ne $hv) { Fail "hvci" }
} catch { Write-Host "DeviceGuard CIM unavailable: $($_.Exception.Message)" }
if ($ps5.oem_mode.value -ne $null) { Fail "oem mode must stay unknown" }

# --- INV-06: saglik ---
$h = $raw.health
if (-not $isVm -and $dgpu.Count) {
    if ($h.dgpu_pcie -eq $null -or $h.dgpu_pcie.max_width -eq $null) { Fail "dGPU PCIe link not read" }
    $pnp = Get-PnpDevice -Class Display | ? { $_.InstanceId -match 'VEN_10DE' } | Select-Object -First 1
    $w = (Get-PnpDeviceProperty -InstanceId $pnp.InstanceId -KeyName DEVPKEY_PciDevice_CurrentLinkWidth).Data
    $mw = (Get-PnpDeviceProperty -InstanceId $pnp.InstanceId -KeyName DEVPKEY_PciDevice_MaxLinkWidth).Data
    if ($h.dgpu_pcie.current_width -ne $w -or $h.dgpu_pcie.max_width -ne $mw) { Fail "PCIe width $($h.dgpu_pcie.current_width)/$($h.dgpu_pcie.max_width) vs $w/$mw" }
}
foreach ($f in $h.findings) { if (-not $f.text_key.StartsWith('health.')) { Fail "finding text key" } }

# --- redaksiyon: varsayilan cikti SID icermemeli ---
if ($red -match [regex]::Escape($me.User.Value)) { Fail "SID leaked in redacted output" }

Write-Host ("OS: {0} {1} build {2}.{3} {4}" -f $raw.os.product_name.value, $raw.os.display_version.value, $raw.os.build.value, $raw.os.ubr.value, $raw.os.native_arch.value)
Write-Host ("User: elevated={0} session={1} remote={2} different_admin={3}" -f $raw.user.elevated.value, $raw.user.session_id.value, $raw.user.remote_session.value, $raw.user.different_admin.value)
Write-Host ("Power: on_ac={0} has_battery={1} battery%={2} saver={3}" -f $raw.power.on_ac.value, $raw.power.has_battery.value, ($raw.power.battery_percent.value, $raw.power.battery_percent.unknown -ne $null)[0], $raw.power.battery_saver.value)
Write-Host ("Policy: domain={0} mdm={1} {2}" -f $raw.policy.domain_joined.value, $raw.policy.mdm_enrolled.value, (($raw.policy.policies | % { "$($_.id)=$($_.state)" }) -join ' '))
Write-Host ("CPU: {0} cores={1} logical={2} hybrid={3} smt={4} L3={5}KiB x{6}" -f $raw.cpu.brand.value, $raw.cpu.physical_cores.value, $raw.cpu.logical_processors.value, $raw.cpu.hybrid.value, $raw.cpu.smt.value, $raw.cpu.l3_total_kib.value, $raw.cpu.l3_slices.value)
Write-Host ("RAM: usable={0}MiB installed={1}MiB slots={2}/{3} channels={4}" -f $raw.memory.usable_mib.value, $raw.memory.installed_mib.value, $raw.memory.populated_slots.value, $raw.memory.total_slots.value, $raw.memory.channel_count.unknown)
foreach ($m in $raw.memory.modules) { Write-Host ("  {0} {1}MiB {2} {3}/{4} MT/s {5}" -f $m.locator.value, $m.size_mib.value, $m.type.value, $m.speed_mts.value, $m.configured_mts.value, $m.manufacturer.value) }
Write-Host ("Board: {0} {1} BIOS {2} {3}" -f $raw.board.board_vendor.value, $raw.board.board_product.value, $raw.board.bios_version.value, $raw.board.bios_date.value)
foreach ($g in $raw.gpus) { Write-Host ("GPU{0}: {1} [{2}/{3}] VRAM={4}MiB drv={5} uma={6}" -f $g.index, $g.name.value, $g.kind, $g.kind_source, $g.dedicated_vram_mib.value, $g.driver_version.value, $g.uma.value) }
foreach ($d in $raw.disks) { Write-Host ("Disk{0}: {1} {2}/{3} {4}GiB removable={5}" -f $d.disk_number, $d.model.value, $d.bus, $d.media, $d.size_gib.value, $d.removable.value) }
foreach ($v in $raw.volumes) { Write-Host ("Vol {0} disk={1} {2} {3}/{4}GiB free" -f $v.root, $v.disk_number.value, $v.filesystem.value, $v.free_gib.value, $v.total_gib.value) }
Write-Host ("VM: {0}" -f $raw.virtual_machine)
Write-Host ("PowerSec: plan={0} ({1}) overlay={2} vbs={3} hvci={4} credguard={5} notes={6}" -f $ps5.active_plan, $ps5.active_plan_name.value, $ps5.overlay, $ps5.vbs.value, $ps5.hvci_running.value, $ps5.credential_guard_running.value, ($ps5.notes -join ','))
Write-Host ("Health: pcie=x{0}/x{1} gen{2}/gen{3} rebar={4} findings={5}" -f $h.dgpu_pcie.current_width, $h.dgpu_pcie.max_width, $h.dgpu_pcie.current_gen, $h.dgpu_pcie.max_gen, $h.rebar_enabled, (($h.findings | % { "$($_.code)[$($_.confidence)]: $($_.evidence)" }) -join '; '))
# --- WIN-07: surucu surumu/tarihi WMI ile ayni ---
$uc = $raw.update_check
if ($uc.installs_anything -ne $false) { Fail "installs_anything" }
foreach ($vc in (Get-CimInstance Win32_VideoController | Where-Object { $_.PNPDeviceID -like 'PCI\VEN_*' })) {
    $m = @($uc.drivers | Where-Object { $_.version.value -eq $vc.DriverVersion })
    if ($m.Count -lt 1) { Fail "driver $($vc.Name) $($vc.DriverVersion) missing" }
    if ($vc.DriverDate) {
        $days = [int][Math]::Floor(($vc.DriverDate.ToUniversalTime() - [datetime]'1970-01-01').TotalDays)
        if ([Math]::Abs($m[0].date_unix_days.value - $days) -gt 1) { Fail "driver date $($vc.Name): $($m[0].date_unix_days.value) vs $days" }
    }
}
foreach ($d in $uc.drivers) { Write-Host ("Driver: {0} {1} ({2}) days={3}" -f $d.device, $d.version.value, $d.display_version, $d.date_unix_days.value) }
Write-Host ("UpdateCheck findings={0}" -f (@($uc.findings | ForEach-Object { $_.code }) -join ','))
Write-Host 'OK'
