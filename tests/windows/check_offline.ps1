param([Parameter(Mandatory)][string]$Exe)
# REC-05: kurtarma ag/lisans gerektirmez -> EXE HTTP/soket kutuphanesi import etmez; --recovery rolu calisir.
# Plan v3.5 (kullanici karari 2026-11-05, sinirli internet): IPHLPAPI izinli — bagdastirici bilgisi ve yalniz
# kullanici dugmesiyle ICMP yanki (Oyun Hazirligi ag testi).
# ADR-027 (2026-11-11, sahibin karari): WINHTTP.dll izinli — yalniz kullanici baslatinca Cloudflare hiz testi
# (src/platform/windows/net/). Soket/WinINet/URLMon/WebServices hala yasak; --recovery rolu ag kodu kullanmaz.
# ADR-035 (2026-11-17, sahibin karari): DNSAPI.dll izinli — yalniz kullanici baslatinca DNS cozucu karsilastirma
# olcumu (DnsQueryEx yalniz src/platform/windows/net/dns_query.cpp; check_no_install.ps1 klasor disini yasaklar).
$ErrorActionPreference = 'Stop'
$deps = (& dumpbin /nologo /dependents $Exe) -join "`n"
foreach ($bad in 'WS2_32', 'WININET', 'URLMON', 'WEBSERVICES') {
    if ($deps -match "(?i)$bad\.dll") { throw "network library imported: $bad.dll" }
}
$list = ($deps -split "`n" | ? { $_ -match '\.dll' } | % { $_.Trim() }) -join ', '
Write-Host "imports: $list"
$p = Start-Process $Exe -ArgumentList '--recovery' -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(20000)) { $p.Kill(); throw 'recovery role timeout' }
if ($p.ExitCode -notin 0, 20) { throw "recovery role exit $($p.ExitCode)" }
Write-Host "recovery role exit $($p.ExitCode) (0 clean / 20 needs user)"
Write-Host 'OFFLINE OK'
