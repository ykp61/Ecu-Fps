# WIN-07 kabul: urun kaynaginda surucu/guncelleme KURAN veya INDIREN kod yolu yok.
# Yasakli API'ler kaynak agacinda (src/) gecmemeli. third_party ve testler taranmaz.
# ADR-027/028: WinHTTP (WinHttp* cagrilari) yalniz $netFolder altinda izinli (kullanici baslatmali hiz testi ve
# acilista zorunlu guncelleme: speedtest_http.cpp, update_http.cpp). Indirme/kurulum API'leri her yerde yasak kalir.
# ADR-031 (spec 2026-11-12 H): Windows Update Agent indirme/kurulum arabirimleri (IUpdateInstaller, IUpdateDownloader,
# CreateUpdateInstaller, CreateUpdateDownloader) YALNIZ yukseltilmis yardimcinin tek klasorunde (src\platform\windows\wua_install\) izinli;
# baska her yerde (salt okuma arama klasoru src\platform\windows\wua\ dahil) ihlaldir. Diger kurulum API'leri her yerde yasak.
# ADR-032: ekran karti ureticisinin RESMI paketi (kullanicinin kendi indirdigi; uygulama indirmez) yalniz dogrulanmis yolla
# calisir: paketi surec olarak baslatan ve imzalayan adini okuyan kod YALNIZ src\platform\windows\vendor_install\ 
# klasorundedir; o arabirimi yalniz yukseltilmis yardimcinin baglantisi (broker_channel.cpp) kurar; ureticinin sessiz
# kurulum anahtarlari yalniz src\core\drivers\vendor_install.* dosyalarinda durur. DiInstallDriver / pnputil / msiexec / BITS /
# kaynagi belirsiz indirme yasaklari degismez.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src = Join-Path $root 'src'
$forbidden = @(
    'UpdateDriverForPlugAndPlayDevices', 'DiInstallDriver', 'DiInstallDevice', 'DiUpdateDriver',
    'SetupCopyOEMInf', 'DriverPackageInstall', 'pnputil',
    'URLDownloadToFile', 'msiexec', 'InternetOpenUrl', 'BITS.*AddFile', 'AddFileSet'
)
# Yalniz ag klasorunde izinli desenler (klasor disinda gecerse ihlal).
$netFolder = 'src\platform\windows\net\'
# ADR-035: DnsQueryEx (kullanici baslatmali DNS karsilastirma olcumu) da yalniz ag klasorunde.
$netOnly = @('WinHttp', 'DnsQueryEx')
$netRoot = Join-Path $root $netFolder
# Yalniz yukseltilmis WUA kurulum yardimcisinin klasorunde izinli desenler.
$wuaInstallFolder = 'src\platform\windows\wua_install\'
$wuaInstallOnly = @('IUpdateInstaller', 'IUpdateDownloader', 'CreateUpdateInstaller', 'CreateUpdateDownloader')
$wuaInstallRoot = Join-Path $root $wuaInstallFolder
# ADR-032: desen -> izinli dosya / klasor onekleri (kok dizine gore).
$vendorOnly = @(
    @('make_vendor_install', @('src\platform\windows\vendor_install\', 'src\platform\windows\broker_channel.cpp')),
    @('WTHelperGetProvSignerFromChain', @('src\platform\windows\vendor_install\')),
    @('Display\.Driver', @('src\core\drivers\vendor_install.')),
    @('L"-INSTALL"', @('src\core\drivers\vendor_install.'))
)
# ADR-033 (setup 0.3.0): hizmet olusturma/silme/yapilandirma, kisayol, yeniden baslatmada silme ve ARP kaydi YALNIZ
# yukseltilmis kurulum iscisinin klasorunde (src\platform\windows\install\). Onceden var olan izin listesindeki broker
# adaptoru (win_tasks_services.cpp: hizmet baslatma turu/durumu, journal'li) ve salt okuma surucu/uygulama tespiti
# (graphics_reader.cpp: Uninstall altini KEY_READ ile okur) bu kuraldan once vardi; adlariyla izinlidir.
# StartServiceCtrlDispatcherW yalniz hizmet klasorunde (gorev 4).
$installFolder = 'src\platform\windows\install\'
$serviceFolder = 'src\platform\windows\service\'
$tasksServices = 'src\platform\windows\adapters\win_tasks_services.cpp'
# 0.8.2 (ADR-033 ek): hizmetin KENDI hata eylemleri politikasini (3xRESTART) dogrulayip onarmasi; yalniz bu tek dosya,
# yalniz SERVICE_CONFIG_FAILURE_ACTIONS. Hizmet olusturma/silme/baska yapilandirma burada yasak kalir.
$failureActions = 'src\platform\windows\service\failure_actions.cpp'
$setupOnly = @(
    @('CreateServiceW', @($installFolder)),
    @('\bDeleteService\s*\(', @($installFolder)),
    @('ChangeServiceConfigW', @($installFolder, $tasksServices)),
    @('ChangeServiceConfig2W', @($installFolder, $tasksServices, $failureActions)),
    @('CLSID_ShellLink', @($installFolder)),
    @('MOVEFILE_DELAY_UNTIL_REBOOT', @($installFolder)),
    # ADR-035: games\library_scan.cpp Battle.net oyun tespiti icin Uninstall altini yalniz KEY_READ ile okur.
    @('CurrentVersion\\+Uninstall', @($installFolder, 'src\core\setup\layout.h', 'src\platform\windows\readiness\graphics_reader.cpp', 'src\platform\windows\games\library_scan.cpp')),
    @('\bStartServiceW\s*\(', @($installFolder, $serviceFolder, $tasksServices)),
    @('\bControlService\s*\(', @($installFolder, $serviceFolder, $tasksServices)),
    @('StartServiceCtrlDispatcherW', @($serviceFolder))
)
$hits = @()
Get-ChildItem -Path $src -Recurse -Include *.h, *.cpp | ForEach-Object {
    $text = Get-Content -Raw -LiteralPath $_.FullName
    foreach ($f in $forbidden) {
        if ($text -match $f) { $hits += "$($_.FullName): $f" }
    }
    $inNet = $_.FullName.StartsWith($netRoot, [System.StringComparison]::OrdinalIgnoreCase)
    foreach ($f in $netOnly) {
        if (-not $inNet -and $text -match $f) { $hits += "$($_.FullName): $f (yalniz $netFolder)" }
    }
    foreach ($pair in $vendorOnly) {
        if ($text -notmatch $pair[0]) { continue }
        $ok = $false
        foreach ($allowed in $pair[1]) {
            if ($_.FullName.StartsWith((Join-Path $root $allowed), [System.StringComparison]::OrdinalIgnoreCase)) { $ok = $true }
        }
        if (-not $ok) { $hits += "$($_.FullName): $($pair[0]) (yalniz ADR-032 resmi paket yolu)" }
    }
    foreach ($pair in $setupOnly) {
        if ($text -notmatch $pair[0]) { continue }
        $ok = $false
        foreach ($allowed in $pair[1]) {
            if ($_.FullName.StartsWith((Join-Path $root $allowed), [System.StringComparison]::OrdinalIgnoreCase)) { $ok = $true }
        }
        if (-not $ok) { $hits += "$($_.FullName): $($pair[0]) (yalniz ADR-033 kurulum iscisi)" }
    }
    $inWuaInstall = $_.FullName.StartsWith($wuaInstallRoot, [System.StringComparison]::OrdinalIgnoreCase)
    foreach ($f in $wuaInstallOnly) {
        if (-not $inWuaInstall -and $text -match $f) { $hits += "$($_.FullName): $f (yalniz $wuaInstallFolder)" }
    }
}
if ($hits.Count -gt 0) {
    Write-Host "Yasakli kurulum/indirme API'si bulundu:"
    $hits | ForEach-Object { Write-Host "  $_" }
    exit 1
}
Write-Host "no_install: $($forbidden.Count) yasakli API + $($netOnly.Count) yalniz-$netFolder + $($wuaInstallOnly.Count) yalniz-$wuaInstallFolder + $($vendorOnly.Count) yalniz-ADR-032 + $($setupOnly.Count) yalniz-ADR-033 desen, src/ temiz"
exit 0
