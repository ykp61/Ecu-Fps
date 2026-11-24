param([Parameter(Mandatory)][string]$Probe)
# FOUND-05: 5 kez coktur, max 3 dump kaldigini ve boyut sinirini dogrula.
$ErrorActionPreference = 'Stop'
$dir = Join-Path ([IO.Path]::GetTempPath()) ("ef-crash-" + [guid]::NewGuid().ToString('N'))
try {
    for ($i = 1; $i -le 5; $i++) {
        $p = Start-Process -FilePath $Probe -ArgumentList "`"$dir`"", '3' -PassThru -Wait -NoNewWindow
        if ($p.ExitCode -eq 0) { throw "probe cokmedi (exit 0)" }
        Start-Sleep -Milliseconds 1100   # dosya adlari saniye cozunurluklu
    }
    $dumps = @(Get-ChildItem $dir -Filter *.dmp)
    Write-Host "dump sayisi: $($dumps.Count)"
    foreach ($d in $dumps) { Write-Host "  $($d.Name) $($d.Length) bayt" }
    if ($dumps.Count -ne 3) { throw "beklenen 3 dump, bulunan $($dumps.Count)" }
    foreach ($d in $dumps) {
        if ($d.Length -le 0 -or $d.Length -gt 2MB) { throw "dump boyutu sinir disi: $($d.Length)" }
        $sig = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($d.FullName), 0, 4)
        if ($sig -ne 'MDMP') { throw "gecersiz minidump imzasi" }
    }
    Write-Host 'OK'
}
finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }
