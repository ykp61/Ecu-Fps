# Bağımlılık ve lisans kaydı

Son güncelleme: 2026-11-18 (B11: JetBrains Mono kaydı, import tablosu birim testiyle okundu, ADR-035 DNSAPI düzeltmesi); önceki 2026-11-12 (oyun ikonları, ADR-029: yeni import yok); önceki 2026-11-11 (WINHTTP importu, ADR-027/028), 2026-11-08 (REL-02/REL-03). Kayan sürüm
("latest") kullanılmaz; aşağıdaki sürümler ENV/GPU görevlerinde gözlenen
değerlerdir.

## Derleme araçları (geliştirici makinesi, dağıtılmaz)

| Araç | Sabitlenen sürüm | Kaynak | Not |
|---|---|---|---|
| Visual Studio Build Tools 2022 | 17.14.37314.3 | Microsoft | C++ masaüstü iş yükü |
| MSVC toolset | 14.44.35207 (cl 19.44.35227) | Build Tools | C++20, `/permissive-`, `/W4 /WX` |
| Windows SDK | 10.0.26100.0 | Build Tools | Win32, Direct2D/DirectWrite, D3D11 |
| CMake | 3.31.6-msvc6 (min 3.25) | Build Tools içinde | `scripts/dev.ps1` VsDevCmd ile PATH'e alır |
| Ninja | 1.12.1 | Build Tools içinde | CMake generator |
| CPack | CMake 3.31.6 ile birlikte | Build Tools içinde | ZIP generator (`scripts/dev.ps1 package`) |

Kontrol: `.\scripts\dev.ps1 env` gerçek yolları ve SDK sürümünü yazdırır.
Ayrıntılı makine kaydı: `third_party/DEPENDENCIES.md`, `artifacts/environment.md`.

## Çalışma zamanı bağımlılıkları (EcuFps.exe)

| Bileşen | Bağlantı | Disk extraction | Lisans | Karar |
|---|---|---|---|---|
| MSVC CRT/STL | Statik (`/MT`) | Yok | Microsoft VC++ (uygulamaya statik bağlama izinli) | Kabul — VC++ redistributable gerekmez |
| Windows sistem DLL'leri | Dinamik, yalnız System32 | Yok | Windows parçası | Kabul — her Windows 10/11'de var |
| Test çerçevesi `tests/unit/ef_test.h` | Yalnız test EXE'si | Yok | Proje kodu | Kabul — üçüncü taraf degil |

2026-11-08 `x64-dist` Release import tablosu (gerçek ölçüm, `ef_package_probe`):
`USER32.dll`, `d3d11.dll`, `d2d1.dll`, `DWrite.dll`, `dwmapi.dll`,
`UIAutomationCore.DLL`, `COMCTL32.dll`, `SHELL32.dll`, `ole32.dll`,
`ADVAPI32.dll`, `WTSAPI32.dll`, `NETAPI32.dll`, `dxgi.dll`, `POWRPROF.dll`,
`OLEAUT32.dll`, `SETUPAPI.dll`, `bcrypt.dll`, `WINTRUST.dll`, `VERSION.dll`,
`pdh.dll`, `WINSPOOL.DRV`, `IPHLPAPI.DLL`, `KERNEL32.dll`, `GDI32.dll`.
VCRUNTIME/MSVCP/UCRT DLL'i yoktur; her ad System32'de doğrulanır
(`ctest -R windows.package`).

2026-11-11 `x64-dist` import tablosu (`ef_package_probe`, sürüm 0.2.0): yukarıdakilere
ek olarak `WINHTTP.dll`, `wevtapi.dll`, `tbs.dll`, `tdh.dll`. **WINHTTP.dll** (Windows
parçası) yalnız iki kullanıcı/açılış işi için: Cloudflare hız testi (ADR-027) ve açılış
güncelleme kontrolü (ADR-028). Çağrılar yalnız `src/platform/windows/net/` altında;
izinli hostlar `speed.cloudflare.com`, `github.com`, `objects.githubusercontent.com`,
`release-assets.githubusercontent.com`. WS2_32/WININET/URLMON hâlâ yasak
(`windows.offline`, `windows.no_install`); DNSAPI.dll 2026-11-17 tarihli ADR-035 ile yalnız kullanıcı başlatınca DNS çözücü karşılaştırması için izinli (aşağıdaki 2026-11-18 tablosu). Üçüncü taraf ağ kütüphanesi yoktur.
Yayın imzalama betiği (`scripts/release_sign.py`) geliştirici makinesinde Python
`cryptography` paketini kullanır; dağıtılmaz. İkon üretim betiği
(`scripts/build_brand_icons.py`) yalnız ICO'ları yeniden üretirken ve karşılaştırma
sayfasını çizerken Python Pillow kullanır (gömülü ICO'lar Pillow 12.1.1 ile üretildi);
dağıtılmaz, derleme ve `--check` (ctest `windows.brand_icons`) için gerekmez.

2026-11-18 import tablosu (B11; gerçek ölçüm: `tests/unit/core/test_third_party_inventory.cpp` ürün EXE'sinin
PE import ve gecikmeli yükleme tablosunu okur, her DLL'in `System32`'de bulunduğunu ve bu kayıtta geçtiğini denetler;
x64-debug derlemesi): `advapi32.dll`, `bcrypt.dll`, `cfgmgr32.dll`, `crypt32.dll`, `d2d1.dll`, `d3d11.dll`,
`dnsapi.dll`, `dwmapi.dll`, `dwrite.dll`, `dxgi.dll`, `gdi32.dll`, `iphlpapi.dll`, `kernel32.dll`, `netapi32.dll`,
`ole32.dll`, `oleaut32.dll`, `pdh.dll`, `powrprof.dll`, `setupapi.dll`, `shell32.dll`, `tbs.dll`, `tdh.dll`,
`uiautomationcore.dll`, `user32.dll`, `version.dll`, `wevtapi.dll`, `winhttp.dll`, `winspool.drv`, `wintrust.dll`,
`wtsapi32.dll`. Gecikmeli yükleme yok. 2026-11-11 listesine göre yeni: `cfgmgr32.dll` (aygıt yöneticisi okuma: sorunlu
aygıt ve sürücü sağlığı), `crypt32.dll` (imza/sertifika doğrulama), `dnsapi.dll` (ADR-035: `DnsQueryEx`, yalnız
`src/platform/windows/net/dns_query.cpp`, yalnız kullanıcı başlatınca). Hepsi Windows parçasıdır; üçüncü taraf DLL yoktur.
Dağıtım (`dist`) tablosunun doğrulaması `ctest -R windows.package` ve `windows.offline` ile ayrıca yapılır.

2026-11-12 oyun ikonları (ADR-029): import tablosuna **yeni DLL eklenmedi** (`ef_package_probe`
ile ölçüldü; liste yukarıdakiyle aynı). Yeni kullanılan sistem API'leri: `SHELL32.dll`
`SHDefExtractIconW` (EXE ikon kaynağı) ve COM üzerinden Windows Imaging Component
(`CLSID_WICImagingFactory`, `CLSID_WICJpegDecoder`; `ole32.dll` `CoCreateInstance`).
Yalnız Windows'un kendi JPEG kod çözücüsü kullanılır; üçüncü taraf codec, kütüphane
veya indirilen görsel yoktur. Test görselleri kodla üretilir (telifli logo depoda yok).

## Üretici GPU runtime'ları (GPU-01)

Runtime DLL'leri sürücüyle gelir ve **yalnız System32'den**, yüklemeden önce
imza (gömülü Authenticode veya Windows sürücü kataloğu) doğrulanarak yüklenir.
EXE yanına DLL konmaz; uygulama girişinde
`SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` ve
`SetDllDirectory("")` uygulanır. Runtime DLL'leri paketlenmez.

| Üretici | Runtime | Bu PC'de gözlenen | SDK (başlık) | Lisans | Durum |
|---|---|---|---|---|---|
| NVIDIA | nvapi64.dll | 32.0.16.2101, katalog imzalı, yüklendi | NVAPI SDK commit `70d337db9186e968eab622f7e786de7e437faf3d` | MIT | Başlıklar `third_party/nvapi`; statik `.lib` alınmadı |
| AMD | amdadlx64.dll | 32.0.21045.5002, katalog imzalı, yüklendi | ADLX SDK | ADLX SDK License Agreement (MIT degil) | **Dışarıda — B-006**: sözleşme kabul edilmedi, başlık repoda yok, kod bağlanmıyor; AMD yolu `unsupported` |
| Intel | ControlLib.dll | Yok (Intel GPU yok, B-004) | IGCL | Intel Software License Agreement (MIT değil; tazminat maddesi, "yalnız Intel platformları") | **Dışarıda — B-004**: başlık/runtime yok, `unsupported` |

## Kaynak olarak gömülen üçüncü taraf kod

| Bileşen | Sürüm / commit | Lisans | Bağlantı | Disk extraction | Kayıt |
|---|---|---|---|---|---|
| Intel PresentMon PresentData (ETW present tüketicisi) | v2.6.0 / `e13fce6acdb55a808fd8318175a56863e532d95f` | MIT | Statik `ef_presentdata`; yalnız ölçüm/test araçlarında (`ef_frame_probe`, `ef_game_capture`); `EcuFps.exe`'ye bağlı değil | Yok | `third_party/presentmon/VERSION.md` (upstream dosyalar değiştirilmedi; CommonUtilities yerine küçük shim) |
| NVIDIA NVAPI SDK başlıkları | `70d337db9186e968eab622f7e786de7e437faf3d` | MIT | Yalnız başlık; `ef_platform` derlemesine girer | Yok | `third_party/nvapi/SOURCE.txt` |
| JetBrains Mono (Medium ve SemiBold alt kümesi) | 2.304 | SIL Open Font License 1.1 | Bayt dizisi olarak EXE'ye gömülü; DirectWrite bellek koleksiyonu | Yok (diske yazılmaz) | `third_party/fonts/jetbrains-mono/SOURCE.txt`, `OFL.txt` |
| 02/BOOST marka varlıkları | v1.0.0 | Ürün sahibine ait (ADR-017) | Gömülü ICO/SVG | Yok | `docs/branding/PROVENANCE.md` |

PresentMon servisi, `PresentMonAPI2.dll`, cereal ve concurrentqueue
alınmadı/dağıtılmaz. AMD ADLX ve Intel IGCL başlıkları repoda yoktur.

## Lisans bildirimleri ve SBOM

- `third_party/NOTICES`: NVAPI ve PresentMon MIT metinlerinin tamamı, gömülü
  bileşen listesi ve ADLX/B-006, IGCL/B-004, PresentMon servis dışlama notları.
  Portable ZIP içinde `NOTICE.txt` olarak yer alır. Uygulama içinde
  Ayarlar > "Üçüncü taraf lisansları" kartı aynı bildirimi gösterir.
- `LICENSE.txt`: ürün lisansı (Ecu Fps, tüm hakları saklı; nihai EULA ürün
  sahibi kararıdır). ZIP içinde `LICENSE.txt` olarak yer alır.
- `artifacts/sbom.json`: CycloneDX 1.5 taslağı; bileşenler, bağlantı türü,
  paket içeriği ve imzasızlık durumu ile güncellendi.
- İmzalama: B-003 nedeniyle Authenticode sertifikası yok; dağıtım
  **imzasız alpha** olarak etiketlenir. Özel anahtar repoda/EXE'de yoktur.

## Paket doğrulama

`ctest -R windows.package` (her ctest koşusunda):

1. `cmake --install` ile gerçek paket yerleşimi geçici klasöre kurulur ve
   içerik tam olarak `EcuFps.exe`, `LICENSE.txt`, `NOTICE.txt`,
   `README.txt` olmalıdır; alt klasör veya loose DLL/config/asset kabul edilmez.
2. `ef_package_probe` PE import tablosunu okur; her import System32'de
   bulunmalı, CRT/üretici DLL'i reddedilir.
3. `scripts/dev.ps1 package -Config dist` aynı denetimden sonra EXE'yi
   `dist\EcuFps.exe` olarak kopyalar ve SHA-256'sını `dist\SHA256SUMS`'a
   yazar (ADR-033: taşınabilir ZIP teslim edilmez).

Her yeni runtime bağımlılığı için: kaynak URL/commit, lisans, boyut,
statik/dinamik, extraction, güvenilir yükleme yolu, CVE kontrolü ve kabul
kararı bu tabloya yazılır. Ağ olmadan temel ölçüm/rollback çalışabilmelidir.
