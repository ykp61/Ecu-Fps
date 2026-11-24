<#
.SYNOPSIS
  Ecu Fps dağıtım EXE'sini üretir: dist\EcuFps.exe (ADR-033: taşınabilir ZIP yok)

.DESCRIPTION
  x64-dist preset ile derler (EF_RELEASE_TRUST=ON), ctest çalıştırır, kurulum
  içeriğini denetler (yalnız EcuFps.exe + LICENSE + NOTICE + README; import
  tablosu yalnız System32 DLL'leri) ve EXE'yi dist\ altına kopyalar. Kurulum
  dosyası aynı EXE'dir (publish_release.ps1: EcuFps-Setup-<sürüm>.exe).
  İmzalama yapmaz (B-003).

.EXAMPLE
  .\scripts\package.ps1
#>
param(
    [ValidateSet('dist')]
    [string]$Config = 'dist'
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'dev.ps1') package -Config $Config
if ($LASTEXITCODE) { exit $LASTEXITCODE }
