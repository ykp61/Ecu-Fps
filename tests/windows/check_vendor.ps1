param([Parameter(Mandatory)][string]$Probe)
# GPU-01: EXE/calisma klasorune konan taklit nvapi64/amdadlx64/ControlLib DLL'leri yuklenmez;
# eksik runtime (ornek: Intel) kontrollu "missing" olur ve surec cokmez.
$ErrorActionPreference = 'Stop'
$dir = Join-Path ([IO.Path]::GetTempPath()) ("ef-vendor-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $dir | Out-Null
try {
    Copy-Item $Probe "$dir\probe.exe"
    # Taklitler: gecerli ama ilgisiz bir DLL'i uretici adlariyla kopyala
    $native = Join-Path $env:WINDIR $(if ([Environment]::Is64BitProcess) { 'System32' } else { 'Sysnative' })
    $decoy = Join-Path $native 'version.dll'  # 64 bit, yuklenebilir gercek bir DLL
    foreach ($n in 'nvapi64.dll', 'amdadlx64.dll', 'ControlLib.dll') { Copy-Item $decoy "$dir\$n" }
    $p = Start-Process "$dir\probe.exe" -ArgumentList 'harden' -WorkingDirectory $dir -PassThru -Wait -NoNewWindow -RedirectStandardOutput "$dir\out.txt"
    $out = Get-Content "$dir\out.txt"
    $out | % { Write-Host $_ }
    if ($p.ExitCode -ne 0) { throw "a decoy DLL from the EXE folder was loaded (exit $($p.ExitCode))" }
    foreach ($line in $out) {
        if ($line -match 'path=(.+?) detail' -and $Matches[1] -and $Matches[1] -notmatch '(?i)\\System32\\') { throw "runtime loaded outside System32: $line" }
    }
    # 32 bit PowerShell'de System32 -> SysWOW64 yonlendirilir; gercek 64 bit dizin icin Sysnative.
    $sys = Join-Path $env:WINDIR $(if ([Environment]::Is64BitProcess) { 'System32' } else { 'Sysnative' })
    foreach ($pair in @(@('nvidia', 'nvapi64.dll'), @('amd', 'amdadlx64.dll'), @('intel', 'ControlLib.dll'))) {
        $present = Test-Path (Join-Path $sys $pair[1])
        $line = $out | ? { $_ -like "$($pair[0]) *" }
        if (-not $present -and $line -notmatch 'state=missing') { throw "$($pair[0]) runtime absent but not reported missing: $line" }
        if ($present -and $line -notmatch 'state=(loaded|untrusted)') { throw "$($pair[0]) runtime present but: $line" }
    }
    Write-Host 'VENDOR OK'
} finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
