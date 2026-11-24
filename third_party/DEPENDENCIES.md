# Bağımlılık kaydı

Son güncelleme: 2026-11-04 (ENV-02). Kayan sürüm ("latest") kullanılmaz.

## Derleme araçları (geliştirici makinesi, dağıtılmaz)

| Araç | Sabitlenen sürüm | Kaynak | Not |
|---|---|---|---|
| Visual Studio Build Tools 2022 | 17.14.37314.3 | Microsoft | C++ masaüstü iş yükü |
| MSVC toolset | 14.44.35207 (cl 19.44.35227) | Build Tools | C++20, /permissive- |
| Windows SDK | 10.0.26100.0 | Build Tools | Win32, Direct2D/DirectWrite, D3D11 başlıkları |
| CMake | 3.31.6-msvc6 (min 3.25) | Build Tools içinde | `scripts/dev.ps1` VsDevCmd ile PATH'e alır |
| Ninja | 1.12.1 | Build Tools içinde | CMake generator |

Kontrol: `.\scripts\dev.ps1 env` gerçek yolları ve SDK sürümünü yazdırır.

## Çalışma zamanı bağımlılıkları (EXE)

| Bileşen | Bağlantı | Disk extraction | Lisans | Karar |
|---|---|---|---|---|
| MSVC CRT/STL | Statik (`/MT`) | Yok | Microsoft VC++ (uygulamaya gömülü dağıtım izinli) | Kabul — VC++ redistributable gerekmez |
| Windows sistem DLL'leri (user32, kernel32, gdi32; sonra d2d1, dwrite, d3d11, dxgi) | Dinamik, System32 | Yok | Windows parçası | Kabul — her Windows 10/11'de var |
| Test çerçevesi `tests/unit/ef_test.h` | Yalnız test EXE'si | Yok | Proje kodu | Kabul — üçüncü taraf yok |

Gözlenen import tablosu (ENV-03, Release): `USER32.dll`, `KERNEL32.dll`, `GDI32.dll`. VCRUNTIME/MSVCP/UCRT DLL'i yok.

## Üretici GPU runtime'ları (GPU-01, 2026-11-04)

Runtime DLL'leri sürücüyle gelir ve **yalnız System32'den**, yüklemeden önce imza (gömülü Authenticode veya Windows sürücü kataloğu) doğrulanarak yüklenir. EXE yanına DLL konmaz; uygulama girişinde `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` ve `SetDllDirectory("")` uygulanır.

| Üretici | Runtime | Bu PC'de gözlenen | SDK (başlık) | Lisans | Durum |
|---|---|---|---|---|---|
| NVIDIA | nvapi64.dll | 32.0.16.2101, katalog imzalı, yüklendi | NVAPI SDK (github.com/NVIDIA/nvapi) | MIT | Başlıklar `third_party/nvapi` altında, sabit commit `70d337db9186e968eab622f7e786de7e437faf3d` (SOURCE.txt) |
| AMD | amdadlx64.dll | 32.0.21045.5002, katalog imzalı, yüklendi | ADLX SDK (github.com/GPUOpen-LibrariesAndSDKs/ADLX) | ADLX SDK License Agreement (MIT değil; tam metin 2026-11-24 okundu: başlıklar GİZLİ bilgi, tazminat yükümlülüğü, "yalnız AMD donanımı" amacı, kullanıcı EULA'sında AMD üçüncü taraf hak sahibi) | **Dışarıda (B-006):** sözleşme kabul edilmedi, başlık repoda yok, kod bağlanmıyor |
| Intel | ControlLib.dll | Yok (Intel GPU yok, B-004) | IGCL (github.com/intel/drivers.gpu.control-library) | Intel Software License Agreement 10.07.21 (MIT DEĞİL; yeniden dağıtım serbest ama "yalnız Intel platformları" ve tazminat maddesi var; 2026-11-24 okundu) | GPU-04; bu PC'de "missing" yolu doğrulandı |

## Gömülü yazı tipi

| Bileşen | Sürüm | Lisans | Bağlantı | Disk extraction | Kayıt |
|---|---|---|---|---|---|
| JetBrains Mono (Medium + SemiBold altkümesi) | 2.304 (resmî GitHub sürümü, zip sha256 6f6376c6…7bbf) | SIL OFL 1.1 | EXE içine bayt dizisi (cmake/embed.cmake), DirectWrite bellek içi koleksiyon | Yok | third_party/fonts/jetbrains-mono/SOURCE.txt, OFL.txt; Ayarlar > Lisanslar |

## Kaynak olarak gömülen üçüncü taraf kod

| Bileşen | Sürüm / commit | Lisans | Bağlantı | Disk extraction | Kayıt |
|---|---|---|---|---|---|
| Intel PresentMon PresentData (ETW present tüketicisi) | v2.6.0 / `e13fce6acdb55a808fd8318175a56863e532d95f` | MIT | Statik kütüphane `ef_presentdata` (+ sistem `tdh`, `advapi32`); servis/ek EXE yok | Yok | `third_party/presentmon/VERSION.md` (MEAS-01; upstream dosyaları değiştirilmedi, CommonUtilities yerine küçük shim). Henüz uygulama EXE'sine bağlanmadı. |

Her yeni runtime bağımlılığı için: kaynak URL/commit, lisans, boyut, statik/dinamik, extraction, güvenilir yükleme yolu, CVE kontrolü ve kabul kararı bu tabloya yazılır.
