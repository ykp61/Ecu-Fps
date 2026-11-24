<#
.SYNOPSIS
  Gerçek ayar testinden önce Windows Sistem Geri Yükleme noktası alır veya listeler (ENV-06).

.DESCRIPTION
  Yönetici yetkisi gerekir; yükseltilmemişse UAC ile kendini yeniden başlatır (kullanıcı onaylar).
  Sistem Koruması kapalıysa AÇMAZ; durumu bildirip çıkar (sistem ayarı değiştirilmez).
  Windows varsayılan olarak 24 saatte bir nokta oluşturur; bu sınır değiştirilmez,
  oluşturma atlanırsa açıkça raporlanır.

.EXAMPLE
  .\scripts\restore-point.ps1 -Status
  .\scripts\restore-point.ps1 -List
  .\scripts\restore-point.ps1 -Create "Ecu Fps WIN-01 testi öncesi"
#>
[CmdletBinding(DefaultParameterSetName = 'Status')]
param(
    [Parameter(ParameterSetName = 'Status')][switch]$Status,
    [Parameter(ParameterSetName = 'List')][switch]$List,
    [Parameter(ParameterSetName = 'Create', Mandatory)][string]$Create,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'

function Test-Admin {
    ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    if (-not $LogFile) { $LogFile = Join-Path $env:TEMP "ef-restore-point-$PID.log" }
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-LogFile', "`"$LogFile`"")
    switch ($PSCmdlet.ParameterSetName) {
        'List' { $argList += '-List' }
        'Create' { $argList += @('-Create', "`"$Create`"") }
        default { $argList += '-Status' }
    }
    Write-Host 'Yönetici izni gerekiyor; UAC penceresi açılacak.'
    try { $p = Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait -PassThru }
    catch { Write-Host 'UAC onayı verilmedi. Hiçbir değişiklik yapılmadı.'; exit 3 }
    if (Test-Path $LogFile) { Get-Content $LogFile -Encoding UTF8; Remove-Item $LogFile -ErrorAction SilentlyContinue }
    exit $p.ExitCode
}

$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s); Write-Host $s }

function Get-SystemDriveProtection {
    # SystemRestore WMI: C: için koruma açıksa RPSessionInterval/Config okunabilir; en güvenilir yol
    # mevcut noktaları sorgulamak ve vssadmin ShadowStorage çıktısına bakmaktır.
    $cfg = Get-CimInstance -Namespace root/default -ClassName SystemRestoreConfig -ErrorAction SilentlyContinue
    $shadow = (& vssadmin list shadowstorage /for=$env:SystemDrive 2>$null) -join "`n"
    [pscustomobject]@{
        Config         = $cfg
        HasShadowStore = $shadow -match [regex]::Escape("$env:SystemDrive")
    }
}

$code = 0
try {
    $prot = Get-SystemDriveProtection
    Say "Sistem sürücüsü: $env:SystemDrive"
    Say "Sistem Koruması (gölge depolama tanımlı): $(if ($prot.HasShadowStore) { 'Evet' } else { 'Hayır / bilinmiyor' })"

    switch ($PSCmdlet.ParameterSetName) {
        'List' {
            $pts = @(Get-ComputerRestorePoint -ErrorAction SilentlyContinue)
            Say "Geri yükleme noktası sayısı: $($pts.Count)"
            foreach ($rp in $pts | Sort-Object SequenceNumber) {
                Say ("  #{0}  {1}  {2}" -f $rp.SequenceNumber, $rp.ConvertToDateTime($rp.CreationTime), $rp.Description)
            }
        }
        'Create' {
            if (-not $prot.HasShadowStore) {
                Say 'Sistem Koruması kapalı görünüyor. Bu betik açmaz. Ayarlar > Sistem > Hakkında > Sistem koruması üzerinden açın.'
                $code = 2
                break
            }
            $beforeMax = (@(Get-ComputerRestorePoint -ErrorAction SilentlyContinue) | Measure-Object SequenceNumber -Maximum).Maximum
            Checkpoint-Computer -Description $Create -RestorePointType MODIFY_SETTINGS -WarningVariable w -WarningAction SilentlyContinue
            $after = @(Get-ComputerRestorePoint -ErrorAction SilentlyContinue) | Sort-Object SequenceNumber | Select-Object -Last 1
            if ($after -and $after.SequenceNumber -gt $beforeMax) {
                Say ("Oluşturuldu: #{0} {1} '{2}'" -f $after.SequenceNumber, $after.ConvertToDateTime($after.CreationTime), $after.Description)
            }
            else {
                Say 'Yeni nokta oluşmadı. Windows 24 saatte bir sınırı uyguluyor olabilir; son nokta:'
                if ($after) { Say ("  #{0} {1} '{2}'" -f $after.SequenceNumber, $after.ConvertToDateTime($after.CreationTime), $after.Description) }
                if ($w) { Say "  Uyarı: $w" }
                $code = 4
            }
        }
        default {
            $last = @(Get-ComputerRestorePoint -ErrorAction SilentlyContinue) | Sort-Object SequenceNumber | Select-Object -Last 1
            if ($last) { Say ("Son nokta: #{0} {1} '{2}'" -f $last.SequenceNumber, $last.ConvertToDateTime($last.CreationTime), $last.Description) }
            else { Say 'Kayıtlı geri yükleme noktası yok.' }
        }
    }
}
catch { Say "HATA: $($_.Exception.Message)"; $code = 1 }
finally { if ($LogFile) { $out | Set-Content -Path $LogFile -Encoding UTF8 } }
exit $code
