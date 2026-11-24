param([Parameter(Mandatory)][string]$Probe)
# GPU-02: gercek NVIDIA kartinda NVAPI okuma; sicaklik/kullanim nvidia-smi ile karsilastirilir.
# NVIDIA yoksa (CI) kontrollu "unavailable" beklenir (cokme yok).
$ErrorActionPreference = 'Stop'
$out = & $Probe
$code = $LASTEXITCODE
Write-Host $out
$hasNvidia = @(Get-CimInstance Win32_VideoController | ? { $_.Name -match 'NVIDIA' }).Count -gt 0
if (-not $hasNvidia) {
    if ($code -ne 3) { throw "no NVIDIA GPU: expected controlled 'unavailable' (3), got $code" }
    Write-Host 'NVIDIA OK (not present, handled)'
    exit 0
}
if ($code -ne 0) { throw "nvapi probe exit $code" }
$smi = Join-Path $env:WINDIR 'System32\nvidia-smi.exe'
if (-not (Test-Path $smi)) { $smi = (Get-Command nvidia-smi -ErrorAction SilentlyContinue).Source }
if ($smi) {
    $q = (& $smi --query-gpu=temperature.gpu,driver_version --format=csv,noheader,nounits) -split ',' | % { $_.Trim() }
    $temp = [int]($out -replace '.*temp=(-?\d+).*', '$1')
    if ([math]::Abs($temp - [int]$q[0]) -gt 6) { throw "NVAPI temp $temp vs nvidia-smi $($q[0])" }
    Write-Host "nvidia-smi: temp=$($q[0]) driver=$($q[1]) (NVAPI temp=$temp)"
}
if ($out -notmatch 'G01=[0-9A-F]{8}@\w+') { throw 'G01 power mode not read' }
Write-Host 'NVIDIA OK'
