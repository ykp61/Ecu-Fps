<#
.SYNOPSIS
  GitHub Actions self-hosted runner kurulum yardimcisi (sahip icin). Kendiliginden hicbir sey kaydetmez.

.DESCRIPTION
  Bu betik YALNIZ sahibin elinde, sahibin onayiyla calisir:
    - Kayit belirteci (token) ve GitHub'daki kayit islemi SAHIBINDIR. Betik belirteci diske yazmaz, gunluge basmaz,
      parametre olarak kabul etmez; yalniz kayit adiminda gizli giris ister.
    - Her yan etkili adimdan once Evet/Hayir sorar: (1) runner indirme, (2) Windows hizmeti (yalniz -Mode Service), (3) GitHub'a kayit.
      Evet disindaki her yanit ve etkilesimsiz calisma "Hayir" sayilir. -Yes gibi bir bypass YOKTUR.
    - UAC'ye tiklamaz, sistem ayari yazmaz, repo degiskenini degistirmez. Hizmet kurulumu yonetici ister; yonetici
      degilseniz betik komutu yazdirir, siz yukseltilmis PowerShell'de calistirirsiniz.
    - -PlanOnly: ag ve disk kullanmadan ne yapacagini yazar (provalar bunu kullanir).

  Varsayilan mod Interactive: hizmet kurulmaz; runner yalniz siz run.cmd'yi actiginizda calisir. Bu, ayni PC'de oyun
  oynarken runner'in arka planda CPU/disk harcamamasi icin onerilen moddur. Service modu her acilista calisir.

  Guvenlik: self-hosted runner depo kodunu bu PC'de calistirir. Yalniz OZEL depoda ve guvendiginiz katilimcilarla
  kullanin; public depo veya yabanci fork PR'i ile ASLA (Settings > Actions > General > Fork pull request workflows).

  Kayittan sonra workflow'un bu runner'i secmesi icin depoda bir degisken tanimlamaniz gerekir (betik tanimlamaz):
    Settings > Secrets and variables > Actions > Variables > EF_RUNS_ON = ["self-hosted","windows","ecufps-local"]
  Degiskeni silmek workflow'u GitHub-hosted makineye (windows-2025) geri dondurur.

.EXAMPLE
  .\scripts\runner_setup.ps1 -PlanOnly
  .\scripts\runner_setup.ps1                      # soru-cevap, Interactive
  .\scripts\runner_setup.ps1 -Mode Service        # hizmet icin ayrica sorar; yukseltilmis PowerShell gerekir
#>
[CmdletBinding()]
param(
    [string]$RepoUrl = 'https://github.com/ykp61/Ecu-Fps',
    [string]$RunnerDir = '',
    [string]$RunnerName = '',
    [string[]]$Labels = @('ecufps-local'),
    [ValidateSet('Interactive', 'Service')][string]$Mode = 'Interactive',
    [switch]$PlanOnly
)

$ErrorActionPreference = 'Stop'
if (-not $RunnerDir) { $RunnerDir = Join-Path $env:SystemDrive 'ecufps-runner' }
if (-not $RunnerName) { $RunnerName = "$env:COMPUTERNAME-ecufps" }

function Confirm-Step([string]$Question) {
    # Etkilesimsiz konak (Read-Host hata verir) veya Evet disi yanit = Hayir.
    try { $a = Read-Host "$Question [E/H]" } catch { return $false }
    return ($a -match '^\s*(e|evet|y|yes)\s*$')
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Plan {
    Write-Host ''
    Write-Host 'Plan (sirayla; her adim ayri onay ister):' -ForegroundColor Cyan
    Write-Host "  1. Runner indirme: github.com'dan actions/runner son surumu (zip + SHA-256 dogrulamasi) -> $RunnerDir"
    Write-Host "  2. Kayit: $RepoUrl icin, ad '$RunnerName', etiketler: self-hosted, windows, $($Labels -join ', ')"
    Write-Host '     (kayit belirtecini SIZ verirsiniz: GitHub > Settings > Actions > Runners > New self-hosted runner)'
    if ($Mode -eq 'Service') {
        Write-Host '  3. Windows hizmeti olarak kur (otomatik baslar; yonetici gerekir) - ayrica sorulur'
    } else {
        Write-Host "  3. Hizmet KURULMAZ (Interactive). Runner'i istediginizde: $RunnerDir\run.cmd"
    }
    Write-Host '  4. Betik repo degiskenini tanimlamaz; siz tanimlarsiniz: EF_RUNS_ON = ["self-hosted","windows","ecufps-local"]'
    Write-Host '  Geri alma: config.cmd remove --token <kaldirma belirteci>; RunnerDir silinir; EF_RUNS_ON degiskeni silinir.'
}

# ---- Onkosullar (yalniz yerel okuma) ----
Write-Host 'Ecu Fps self-hosted runner kurulumu' -ForegroundColor Cyan
Write-Host "Depo: $RepoUrl | Mod: $Mode | Dizin: $RunnerDir"
$notes = @()
if ($env:OS -ne 'Windows_NT') { $notes += 'Windows degil.' }
if (-not [Environment]::Is64BitOperatingSystem) { $notes += 'x64 Windows gerekli.' }
foreach ($t in 'git', 'python') { if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { $notes += "$t PATH'te yok." } }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { $notes += 'Visual Studio Build Tools (vswhere) bulunamadi; CI derlemesi calismaz.' }
if (Test-Path -LiteralPath $RunnerDir) { $notes += "$RunnerDir zaten var; betik uzerine yazmaz." }
if ($Mode -eq 'Service' -and -not (Test-IsAdmin)) { $notes += 'Service modu yonetici ister; bu PowerShell yonetici degil (betik UAC istemez).' }
if ($notes.Count) { $notes | ForEach-Object { Write-Host "  uyari: $_" -ForegroundColor Yellow } } else { Write-Host '  onkosullar tamam.' -ForegroundColor Green }
Write-Plan

if ($PlanOnly) {
    Write-Host ''
    Write-Host 'PlanOnly: hicbir sey indirilmedi, yazilmadi, kaydedilmedi.' -ForegroundColor Green
    exit 0
}
if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) { Write-Host 'HATA: Windows x64 gerekli.' -ForegroundColor Red; exit 2 }
if (Test-Path -LiteralPath $RunnerDir) { Write-Host "HATA: $RunnerDir zaten var; baska -RunnerDir verin veya once elle silin." -ForegroundColor Red; exit 2 }

# ---- 1. Indirme ----
if (-not (Confirm-Step "1/3 actions/runner son surumu github.com'dan indirilip $RunnerDir icine acilsin mi?")) {
    Write-Host 'Indirme onaylanmadi; hicbir sey yapilmadi.' -ForegroundColor Yellow
    exit 0
}
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$rel = Invoke-RestMethod -Uri 'https://api.github.com/repos/actions/runner/releases/latest' -Headers @{ 'User-Agent' = 'ef-runner-setup' }
$ver = $rel.tag_name.TrimStart('v')
$asset = $rel.assets | Where-Object { $_.name -eq "actions-runner-win-x64-$ver.zip" } | Select-Object -First 1
if (-not $asset) { Write-Host "HATA: win-x64 paketi bulunamadi (surum $ver)." -ForegroundColor Red; exit 2 }
$expected = ''
if ($rel.body -match '(?s)<!--\s*BEGIN SHA win-x64\s*-->\s*([0-9a-fA-F]{64})\s*<!--\s*END SHA win-x64\s*-->') { $expected = $Matches[1].ToLowerInvariant() }
if (-not $expected) { Write-Host 'HATA: surum notunda SHA-256 bulunamadi; dogrulanamayan paket kurulmaz.' -ForegroundColor Red; exit 2 }
Write-Host "Surum $ver, $([math]::Round($asset.size / 1MB, 1)) MB, SHA-256 (surum notundan) $expected"
$zip = Join-Path ([IO.Path]::GetTempPath()) $asset.name
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -Headers @{ 'User-Agent' = 'ef-runner-setup' }
$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
if ($actual -ne $expected) { Remove-Item -LiteralPath $zip -Force; Write-Host "HATA: SHA-256 uyusmuyor ($actual); indirilen dosya silindi." -ForegroundColor Red; exit 2 }
New-Item -ItemType Directory -Path $RunnerDir | Out-Null
Expand-Archive -LiteralPath $zip -DestinationPath $RunnerDir
Remove-Item -LiteralPath $zip -Force
Write-Host "Runner $ver acildi: $RunnerDir (SHA-256 dogrulandi). Henuz KAYITLI DEGIL." -ForegroundColor Green

# ---- 2. Kayit ----
$allLabels = @($Labels) -join ','
if (-not (Confirm-Step "2/3 Bu PC $RepoUrl icin runner '$RunnerName' (etiket: $allLabels) olarak KAYDEDILSIN mi? (belirteci siz verirsiniz)")) {
    Write-Host "Kayit onaylanmadi. Runner dosyalari $RunnerDir icinde duruyor; sonra .\config.cmd ile kendiniz kaydedebilirsiniz." -ForegroundColor Yellow
    exit 0
}
$asService = $false
if ($Mode -eq 'Service') {
    if (-not (Test-IsAdmin)) {
        Write-Host 'Hizmet icin yukseltilmis PowerShell gerekir (betik UAC istemez); bu kayit hizmetsiz (Interactive) yapilacak. Hizmet icin yukseltilmis PowerShell icinde betigi -Mode Service ile yeniden calistirin.' -ForegroundColor Yellow
    } elseif (Confirm-Step '3/3 Runner Windows HIZMETI olarak kurulsun ve her acilista otomatik baslasin mi? (oyun sirasinda CPU/disk harcayabilir)') {
        $asService = $true
    } else {
        Write-Host 'Hizmet kurulmayacak (Interactive kayit).' -ForegroundColor Yellow
    }
}
$secure = Read-Host 'Kayit belirteci (GitHub > Settings > Actions > Runners > New self-hosted runner; ekranda gorunmez)' -AsSecureString
$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
try {
    $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    if (-not $token) { Write-Host 'Belirtec bos; kayit yapilmadi.' -ForegroundColor Yellow; exit 0 }
    Push-Location $RunnerDir
    try {
        # Belirtec komut satirinda kisa sure gorunur (config.cmd'nin tek yolu); tek kullanimliktir ve kisa omurludur.
        $cfgArgs = @('--unattended', '--url', $RepoUrl, '--token', $token, '--name', $RunnerName, '--labels', $allLabels, '--work', '_work')
        if ($asService) { $cfgArgs += '--runasservice' }
        & .\config.cmd @cfgArgs
        $code = $LASTEXITCODE
    } finally { Pop-Location }
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    $token = $null
}
if ($code) { Write-Host "HATA: config.cmd cikis $code. Kayit tamamlanmadi." -ForegroundColor Red; exit 2 }
Write-Host 'Runner kaydedildi.' -ForegroundColor Green

# ---- Sonuc ----
if ($asService) { Write-Host 'Runner Windows hizmeti olarak kuruldu (config.cmd --runasservice).' -ForegroundColor Green }
else { Write-Host "Hizmet kurulmadi. Runner'i baslatmak icin: cd '$RunnerDir'; .un.cmd" -ForegroundColor Cyan }
Write-Host ''
Write-Host 'Sirada (sahip): Settings > Secrets and variables > Actions > Variables:' -ForegroundColor Cyan
Write-Host '  EF_RUNS_ON = ["self-hosted","windows","ecufps-local"]'
Write-Host 'Fork PR calistirmayi kapali tutun; runner yalniz ozel depoda kullanilmalidir.'
exit 0
