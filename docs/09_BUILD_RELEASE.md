# Derleme ve yayın

## Gözlemlenen ortam (ENV-02, 2026-11-04; paket turu 2026-11-08)

Windows native x64 geliştirme ortamı: Visual Studio Build Tools 2022
17.14.37314.3, MSVC 14.44.35207 (cl 19.44.35227), Windows SDK 10.0.26100.0,
CMake 3.31.6-msvc6 (min 3.25), Ninja 1.12.1. Test PC'si: Windows 11 Pro 26H2
x64, NVIDIA RTX 5080 + AMD Radeon iGPU (Intel GPU yok: B-004). Ayrıntılı
kayıt: `third_party/DEPENDENCIES.md`, `artifacts/environment.md`.

Linux'ta saf çekirdek geliştirme/unit test mümkün olabilir; Windows UAC,
DirectX, üretici API veya gerçek GPU testinin yerine geçmez. Sanal GPU da
fiziksel ekran kartı desteğini kanıtlamaz.

## Derleme hedefi

Release kullanıcı dağıtımı tek `EcuFps.exe`. Native CRT statik (`/MT`);
bağımlılıklar statik/gömülü. Windows ve kurulu resmi sürücü DLL'leri güvenilir
konumdan (System32, imza doğrulamalı) dinamik kullanılır. Geliştirici kaynak
deposunun çok dosyalı olması normaldir; tek EXE şartı son kullanıcı
dağıtımını tanımlar. PDB, test programları, SBOM ve build logları
geliştirici/yayın kanıtıdır; kullanıcı EXE'sinin yanına zorunlu çalışma
dosyası olarak konmaz. Lisans bildirimleri EXE içine gömülür.

## Gerçek build komutları

Önkoşul: Visual Studio Build Tools 2022 (C++ masaüstü iş yükü, Windows SDK).
CMake ve Ninja onun içinde gelir; ayrı kurulum gerekmez. `scripts/dev.ps1`
VsDevCmd ortamını kendisi yükler.

| Amaç | Komut |
|---|---|
| Çift tıkla derle + test | `build.cmd` |
| Release derle + test | `.\scripts\dev.ps1 build` |
| Debug derle + test | `.\scripts\dev.ps1 build -Config debug` |
| Dağıtım derlemesi + test (release güven ilkesi) | `.\scripts\dev.ps1 build -Config dist` |
| Dağıtım EXE'si üret + doğrula | `.\scripts\package.ps1` |
| (eşdeğer) Dağıtım EXE'si üret + doğrula | `.\scripts\dev.ps1 package -Config dist` |
| Yalnız test | `.\scripts\dev.ps1 test` |
| Uygulamayı aç | `.\scripts\dev.ps1 run` |
| Araç yollarını göster | `.\scripts\dev.ps1 env` |
| Yerel CI (temiz klon, debug + dist, HEAD'e bağlı günlük) | `.\scripts\local_ci.ps1` (ayrıntı: "Yerel CI ve yayın kapısı") |
| Yerel CI, pencere açmayan alt küme (oyun sırasında) | `.\scripts\local_ci.ps1 -Scope Quiet` |

Çıktı: `build\x64-<config>\src\app\EcuFps.exe`. Paket: `dist\EcuFps.exe`
+ `dist\SHA256SUMS` (0.3.0+; yayın dosyası `EcuFps-Setup-<sürüm>.exe`).

## Test envanteri (2026-11-08, x64-dist)

- `ef_unit_tests`: 719 birim testi tek EXE'de; `unit` ctest
  kaydı. Test çerçevesi `tests/unit/ef_test.h` (üçüncü taraf yok).
- ctest toplamı: 48 test (`unit`, `smoke.app_open_close` ve
  `windows.*` entegrasyon testleri). Pencere açan testler
  `EF_TEST_BACKGROUND=1` ile en altta/odak çalmadan koşar; UIA turları
  varsayılan koşuda atlanır (çıkış 77, `-L uia_tour` ile açılır).
- `EF_DATA_DIR` (Uyumluluk B9, yalnız test): uygulama veri klasörünü
  yönlendirir; yalnız `EF_TEST_BACKGROUND=1` iken ve değer `%TEMP%` altında
  sürücü harfli bir alt klasörse kabul edilir (UNC, göreli yol, `..` yok).
  Gerekçe: veri klasörü kullanıcı journal'ını ve "Verileri sil" hedefini
  taşır; başıboş bir değişken üretimde geri alma kayıtlarını gizleyemez ve
  geçici klasör dışında bir ağacı hedefleyemez. Geçersiz değer yok sayılır
  (gerçek klasör). Destek paketi yalnız `set` / karar yazar, yolu yazmaz.
  `windows.first_run` bu yönlendirmeyle boş, Türkçe karakterli bir profilde
  açılışı denetler.
- `real_gpu` etiketi (`windows.balance_real_gpu`): opt-in gerçek GPU denge
  testi, her donanım bağdaştırıcısında `--balance --real --quick --adapter N`.
  Varsayılan koşuda atlanır (77), `scripts/ctest_evidence.ps1` dışarıda
  bırakır. Çalıştırma: `EF_REAL_GPU=1` ve `ctest -L real_gpu` (oyun açıkken
  koşulmaz).
- Paket testi `windows.package`: `cmake --install` ile gerçek paket yerleşimi
  (yalnız 4 dosya) ve PE import tablosu (yalnız System32 DLL'leri). ZIP
  denetimi 0.3.0'da kaldırıldı (ADR-033).
- Mock/fixture testi gerçek Windows/GPU testi sayılmaz. Donanım gerektiren
  gerçek turlar `state/PENDING_USER.md` ve ADR-023'teki gibi kullanıcı
  onayına bağlıdır.

## Dağıtım dosyası: kurulum = aynı EXE (ADR-033, 0.3.0)

- Preset: `x64-dist` (Release + `EF_RELEASE_TRUST=ON` + `EF_UNSIGNED_ALPHA=ON`).
  Release güven ilkesinde dev imzasız broker muafiyeti ikiliye derlenmez;
  `windows.trust` testi bunu doğrular. **İmzasız alpha: EF_UNSIGNED_ALPHA
  açık** (ADR-034): yükseltilmiş roller hiç imzası olmayan EXE'yi etiketli
  olarak kabul eder; kurcalanmış/güvenilmez/doğrulanamayan imza yine reddedilir.
  Kod imzalama geldiği gün preset'te OFF yapılır. Sürüm notu bu durumu
  "imzasız alpha: EF_UNSIGNED_ALPHA açık" diye belirtir.
- **Tek teslim dosyası `EcuFps-Setup-<sürüm>.exe`**: derlenen
  `EcuFps.exe`'nin birebir bayt kopyasıdır (ayrı kurulum çerçevesi yok).
  Kurulum klasörü dışından boş komut satırıyla veya `--setup` ile açılınca
  kurulum penceresi gösterilir. **Taşınabilir ZIP 0.3.0'dan itibaren
  yayımlanmaz**; CMake'teki CPack ZIP hedefi yalnız yerel ara çıktıdır,
  teslim dosyası değildir. `scripts\package.ps1` (`dev.ps1 package`) derler,
  ctest çalıştırır, kurulum içeriğini denetler ve `dist\EcuFps.exe` +
  `dist\SHA256SUMS` yazar.
- `cmake --install` yerleşimi hâlâ yalnız `EcuFps.exe`, `LICENSE.txt`,
  `NOTICE.txt`, `README.txt` (loose DLL/config/asset yok). Bildirimler:
  `third_party/NOTICES` (NVAPI + PresentMon MIT, ADLX/B-006 ve IGCL/B-004
  dışlama notları); uygulama içinde Kurulum → "Lisans ve gizlilik · Göster"
  ve Ayarlar → Lisanslar aynı metni gösterir. SBOM: `artifacts/sbom.json`.
- Doğrulama: `ctest -R windows.package` kurulum yerleşimini ve import
  tablosunu (yalnız System32 DLL'leri) denetler; ZIP denetimi kaldırıldı.
- **İmzalama yoktur (B-003).** Authenticode sertifikası/sağlayıcı erişimi
  ürün sahibinden gelmeden dosya **imzasız alpha** olarak etiketlenir.
  SmartScreen uyarısı gösterebilir; bu bir hata değildir.

### Kurulum yerleşimi (özet; ayrıntı spec 2026-11-13 §2)

| Öğe | Yer |
|---|---|
| Program | `C:\Program Files\Ecu Fps\EcuFps.exe` (yazma yalnız Administrators/SYSTEM) |
| Başlat menüsü | `ProgramData\Microsoft\Windows\Start Menu\Programs\Ecu Fps.lnk` |
| Masaüstü | Ortak masaüstü `C:\Users\Public\Desktop\Ecu Fps.lnk` (`FOLDERID_PublicDesktop`; hedef kurulu EXE, ikon EXE,0) |
| Uygulamalar listesi | HKLM `…\Uninstall\EcuFps` (`UninstallString` = `"<exe>" --uninstall`) |
| Windows ile başlat | HKCU `Run\EcuFps` = `"<kurulu exe>" --tray` (kurulumda isteğe bağlı, varsayılan açık) |
| Yardımcı hizmet | `EcuFpsHelper` (LocalSystem, otomatik) = aynı EXE `--service` |
| Makine verisi | `ProgramData\EcuFps` (makine journal'ları, `updates\` hazırlık, kurulum journal'ı; admin ACL) |
| Kullanıcı verisi | `%LOCALAPPDATA%\EcuFps` (değişmedi; taşınabilirden geçişte korunur) |

Kurulum: uygulama içi **Evet/Hayır** → **tek UAC** → yükseltilmiş aynı EXE
(`--role=setup`) işlemsel kurar; her adım kurulum journal'ında, hata olursa
yapılan adımlar ters sırada geri alınır.

### Yardımcı hizmet

Adlandırılmış kanal katı DACL'lidir; yalnız etkileşimli oturumdaki **kurulu**
EXE'den (istemci görüntü yolu = kurulum yolu; o yolu yalnız yöneticiler
yazabilir) gelen istekleri kabul eder ve yalnız `src/core/broker/admin.cpp`
izin listesindeki işlemleri yürütür (keyfi komut yok). Her istek yine uygulama
içi Evet/Hayır'dan geçer, aynı makine journal'ı kullanılır. Hizmet yok/bozuksa
uygulama bugünkü UAC broker'ına düşer. Kalan risk (aynı kullanıcıda kurulu
uygulamaya kod enjeksiyonu UAC'siz izinli işlem isteyebilir; etki sabit,
journal'lı, geri alınabilir değerlerle sınırlı) ADR-033'te kabul edildi.

### Kaldırma

Ayarlar → **Kaldır** veya Uygulamalar listesi (`--uninstall` → Ayarlar sayfası
ve aynı Kaldır onayı; sessiz kaldırma yok). Sıra (`run_full_uninstall`):

1. Değişen ayarlar geri yüklenir (makine grupları hizmet yoluyla, yoksa UAC).
   Biri bile geri yüklenemezse **hiçbir şey silinmez**.
2. Kurulu kopyada program kaldırma: tek UAC → `SetupKind::Uninstall`; hizmet
   durdur → ARP → Başlat menüsü kısayolu → masaüstü kısayolu → hizmeti sil (hepsi geri alınabilir) →
   `ProgramData\EcuFps` (yalnız makine journal'larında geri alınmamış kayıt
   yoksa) → dosyalar (çalışan EXE ve klasör yeniden başlatmada silinir).
   UAC reddi: "Yönetici izni verilmedi; hiçbir şey silinmedi". Başarısız:
   "Program kaldırılamadı; verileriniz korundu" — Run kaydı ve kullanıcı verisi
   **silinmez**.
3. Yalnız program kaldırıldıktan sonra: Run kaydı, `%LOCALAPPDATA%\EcuFps`.
   Sonuç: "Kaldırıldı; kalan dosya yeniden başlatmada silinir".

Taşınabilir (kurulu olmayan) kopyada eski davranış aynen: Run kaydı → makine
verisi (yönetici gerekirse yerinde bırakılır ve bildirilir) → kullanıcı verisi.

### Eski adlı 0.9.5 kurulumundan yükseltme (ADR-043, R-F2)

Eski kurulu istemci Setup EXE'si başlatmaz: eski hizmet yeni EXE'yi eski yolda
yerinde değiştirir, eski UI onu `--updated-from=<sürüm>` ile başlatır, eski
hizmet eski pipe'tan sağlık bekler (60 sn) ve SCM onu `--service` ile yeniden
başlatır (artık yeni ikili, eski hizmet adı, SYSTEM). Yeni sürüm bu çağrıları
tanır (`src/core/legacy/legacy_install.*`) ve `run_legacy_upgrade` ile yeni
klasöre yan yana kurar, makine journal'ını korumalı ACL ile taşır, yeni
hizmeti sağlıklı görmeden eski hiçbir şeyi silmez (`healthy` kaydı); eski
temizliği yeni hizmet ilk açılışta yapar. Çıkış: 0 devraldı, 5 geri alındı
(eski EXE), 6 yeniden dene. **Durum:** saf mantık + birim testleri ve Win32
bağlama (`src/platform/windows/legacy_upgrade_system.*`,
`legacy_health_signal.*`; hizmet girişi, yeni hizmet temizliği, UI sinyali)
yazıldı ve derlenir; **gerçek yükseltme turu DENENMEDİ** (sahip onaylı ayrı
tur; doğrulanacak varsayımlar ADR-043 ekinde).

### Geçiş yayını — legacy bridge (ADR-044, R-F3)

Eski 0.9.5 istemcileri yalnız eski depoyu (`ykp61/iTunex-Pro-releases`,
`releases/latest/download/manifest.json`) okur ve manifesti eski imza önekiyle,
eski yayıncı adıyla ve eski depo URL önekiyle doğrular. Bu yüzden eski depoya
**bir kez** yeni markalı EXE'yi taşıyan bir geçiş yayını konur; sonraki bütün
yayınlar yeni depoya normal (yeni önekli) manifestle gider. Geçiş manifesti
`release_sign.py --legacy-bridge` ile üretilir: eski önek, eski yayıncı, eski
URL öneki + `v<sürüm>/iTunexPro-Setup-<sürüm>.exe`, `manifest_version` 20
(eski depodaki son 19), kanal/mimari/`min_os_build` aynı, aynı yayın anahtarı
(`release_key.h` 059b3ee3'ten beri değişmedi, `rel-7e5df9066e6c47ad`).

Operatör adımları (sıra önemlidir):

0. Ön koşul: `ykp61/iTunex-Pro-releases` GitHub'da yeniden adlandırılmamış,
   `ykp61/Ecu-Fps-releases` ayrı bir depo olmalı (ad değişikliğinde GitHub
   eski URL'yi yönlendirir ve iki istemci aynı manifesti okur).
1. Sürümü yükselt (0.9.5'ten büyük, ör. 0.9.6) ve temiz ağaçta
   `.\scripts\local_ci.ps1` (Full) yeşil olsun — kapı normal yayınla aynıdır.
2. Çevrimdışı kanıt: `powershell -NoProfile -File scripts\legacy_bridge_proof.ps1`
   → `KANIT OK` (eski 059b3ee3 doğrulayıcısı geçiş manifestini kabul eder,
   normal manifesti reddeder; HEAD tersini yapar). Gerçek anahtar kullanılmaz.
3. Prova: `.\scripts\publish_release.ps1 -Version 0.9.6 -LegacyBridge`
   (DryRun; önceki `manifest_version` 19 varsayılır). Çıktıdaki
   `dist\publish-v0.9.6\manifest.json` için
   `python -I scripts\release_sign.py --legacy-bridge verify dist\publish-v0.9.6\manifest.json`
   → `OK version=0.9.6 manifest_version=20 (legacy bridge)`; varlık adı
   `iTunexPro-Setup-0.9.6.exe`, sürüm notu geçiş metnidir.
4. Sahip sohbette onaylar; `-LegacyBridge -Publish` aynı komutla eski depoya
   yayınlar (`--latest`; önceki `manifest_version` eski depodan okunur, 19'dan
   küçükse durur).
5. **Sonra** yeni depoya ilk normal yayın: `.\scripts\publish_release.ps1 -Version <daha yeni>`.
   Normal kip önceki `manifest_version`'ı en az 20 sayar (yeni depoda ilk = 21),
   çünkü geçişten gelen istemci tercihlerinde tekrar oynatma tabanı 20'dir.
   Yeni depodaki ilk yayının sürümü geçiş sürümünden büyük olmalıdır (aksi
   halde geçişle gelen istemci için "daha yeni değil").
6. Doğrulama: eski depoda `latest` = geçiş etiketi; indirilen `manifest.json`
   `--legacy-bridge verify` ile, yeni depodaki `manifest.json` bayraksız
   `verify` ile geçer. Gerçek eski kurulumda tur ayrıca yapılır (DENENMEDİ).

Geri dönüş: geçiş yayını sorunluysa eski depoya `-LegacyBridge -Halt -Version <geçiş sürümü>`
ile imzalı acil fren manifesti yüklenir (0.9.5 `halt`'ı tanır; `manifest_version` 21).

Anahtar: marka değişikliğinden önce `%APPDATA%\iTunexProRelease` altında eski
DPAPI entropisiyle mühürlenen gerçek yayın anahtarı `release_sign.py`
tarafından bulunur ve okunur (yeni klasörde anahtar yoksa); eski klasörde
anahtar varken `keygen` yeni anahtar üretmez.

## GitHub Releases dağıtım hattı (2026-11-08 hazırlık)

Üçüncü barındırma seçeneği (GitHub Releases) için yerel hat hazır; **yayın yapılmadı**
(kullanıcı açık talimatı beklenir). Araştırma: `research/RELEASE_HOSTING.md`.

| Amaç | Komut |
|---|---|
| Hash/manifest/git kontrolü (ağ yok) | `.\scripts\release.ps1 -Mode check` |
| Paket + `SHA256SUMS` + `RELEASE_BODY.md` | `.\scripts\release.ps1 -Mode prepare` |
| Draft + asset yükleme provası (ağ yok) | `.\scripts\release.ps1 -Mode draft -AllowUnsigned -DryRun` |
| Gerçek draft + asset yükleme | `$env:GITHUB_TOKEN=...; .\scripts\release.ps1 -Mode draft -AllowUnsigned -Repo <owner>/<repo>` |
| Draft'ı yayınlama (açık karar) | `.\scripts\release.ps1 -Mode publish -ReleaseId <id> -PublishPublic` |

- `dist/SHA256SUMS` ASCII `<sha256>  <dosya>` biçimindedir; `dev.ps1 package` da üretir.
- `dist/RELEASE_BODY.md` imzasız alpha uyarısı + asset tablosu + `docs/RELEASE_NOTES.md` içerir.
- Draft akışı immutable release kuralıyla uyumludur: önce draft + tüm asset'ler, sonra publish.
  Draft aşamasında tag oluşmaz; tag publish anında oluşur.
- **[Önemli]** `origin` deposu özeldir; özel deponun release asset'leri halka açık indirilemez.
  Public indirme için depo görünürlüğü kararı veya ayrı public release deposu gerekir (`-Repo`).
- Token fine-grained PAT (`Contents: Read and write`) olarak yalnız ortam değişkeninde tutulur;
  script token'ı dosyaya yazmaz. `gh` CLI gerekmez (REST API kullanılır).
- İmza yok (B-003): `-AllowUnsigned` olmadan draft/publish reddedilir; SmartScreen uyarısı
  gizlenmez. Immutable release attestation GitHub Actions gerektirir (B-008 açık).

## Zorunlu güncelleme yayını (ADR-028, 2026-11-11)

Uygulama açılışta yalnız açık `ykp61/Ecu-Fps-releases` deposundaki imzalı `manifest.json`'u
okur. Yayın akışı `scripts/publish_release.ps1 -Version <x.y.z>`:

1. Temiz ağaç; CMake sürümü `-Version` ile aynı olmalı (`-Bump` yükseltir ve yerel commit eder;
   bump commit'i HEAD'i değiştirir, yerel CI sonra çalıştırılır).
2. **Yayın kapısı** (P9-B10): `build\local-ci\local-ci-<HEAD12>.log` (Full kapsam, yeşil, aynı HEAD, temiz
   ağaç) ve günlüğe kayıtlı SHA-256 ile aynı EXE gerekir. `-Publish` kapı kapalıyken durur. Kapı açıksa
   derleme atlanır ve yayınlanan dosya yerel CI'nin test ettiği EXE'dir; kapı kapalıyken prova eski yola
   (`tools\final.ps1`, yoksa `scripts\package.ps1`) düşer ve uyarı yazar.
3. `dist\publish-v<sürüm>\` altına `EcuFps-Setup-<sürüm>.exe` (derlenen EXE'nin birebir
   kopyası; derleme kimliği HEAD ile eşleşmeli; kapı açıksa SHA-256 yeniden denetlenir) ve imzalı `manifest.json` (url
   `…/releases/download/v<sürüm>/EcuFps-Setup-<sürüm>.exe`, SHA-256 bu dosya için);
   `manifest_version` = önceki + 1 (`-PreviousManifestVersion`, `-Publish`'te son yayından).
   ZIP üretilmez ve `gh release` komutuna eklenmez (ADR-033). İstemci url dosya adını
   sınırlamaz (yalnız depo öneki + güvenli karakterler); 0.2.x kopyalar bu manifesti kabul eder.
4. Zarf gömülü genel anahtarla (`src/core/update/release_key.h`) geri doğrulanır.
5. `gh release create v<sürüm> … --repo ykp61/Ecu-Fps-releases` komutu yazılır. Varsayılan
   **prova**dır (ağ yok, gönderim yok); gerçek yayın yalnız `-Publish` ile ve ürün sahibi
   sohbette sürüm ve dosyaları onayladıktan sonra.

Anahtar: `python -I scripts\release_sign.py keygen` (bir kez; var olanın üzerine yazmaz).
Özel anahtar DPAPI ile `%APPDATA%\EcuFpsRelease\` içinde (+ yedek); depoya, EXE'ye veya
çıktıya girmez. Kaybı = mevcut kurulumlar otomatik güncellenemez. Sınır: 0.1.0 kendini
güncelleyemez, 0.2.0 bir kez elle kurulur. Açık depo henüz oluşturulmadı ve yayın yapılmadı.
Authenticode yoktur (B-003); sürüm imzasız alpha kalır.

## Yerel CI ve yayın kapısı (P9-B10, 2026-11-18)

GitHub Actions şu an faturalandırma kilidi yüzünden iş başlatmıyor (B-008; kod hatası değil). Sahip kararı:
workflow tetikleyicileri **açık kalır** (push/PR/manuel); fatura düzelmedikçe kırmızı görünür. Gerçek kapı yereldir.

**`scripts\local_ci.ps1`**

| Konu | Davranış |
|---|---|
| Temiz klon | HEAD `%TEMP%\ef-local-ci-<HEAD12>` altına klonlanır (kirli dosya, eski `build\` sızmaz); çalışma ağacı kirliyse durur (`-AllowDirty` günlüğe `dirty yes` yazar ve kapıyı geçmez) |
| Adımlar | `x64-debug` ve `x64-dist` için configure + build + ctest (`debug-build`, `debug-ctest`, `dist-build`, `dist-ctest`) |
| `-Scope Full` (varsayılan) | `uia_tour` ve `real_gpu` dışındaki tüm testler; pencere testleri `EF_TEST_BACKGROUND=1` ile odak çalmadan koşar. Yayın kapısı yalnız Full kabul eder |
| `-Scope Quiet` | Yalnız pencere açmayan `unit` ve `services` etiketleri (oyun sırasında); kapıyı **geçmez** |
| Çıktı | `build\local-ci\local-ci-<HEAD12>.log` (başlıkta HEAD, FULLHEAD, dirty, scope, adım çıkışları, `dist_exe_sha256`, `result`) ve `build\local-ci\EcuFps-<HEAD12>.exe`; `-EvidenceCopy` günlüğü ayrıca kopyalar |
| Kaynak kullanımı | Normal öncelik (`-Priority BelowNormal` seçilebilir); ctest `-j 4` (RUN_SERIAL testler tek başına); UAC, ağ ve ayar yazma yok; klon bitince silinir (`-Keep` bırakır) |
| `-Verify -Exe <dosya>` | Yayın kapısı: günlük HEAD/FULLHEAD eşleşmesi, temiz ağaç, Full kapsam, dört adım yeşil + `100% tests passed`, `result PASS`, EXE SHA-256 = günlük. Aksi hâlde çıkış 2 |
| `-SelfTest`, `-DryRun` | Kapı mantığı sentetik günlüklerle sınanır (`scripts\test_release_sign.py` bunu da koşar); `-DryRun` planı yazar |

Kapı yerel tutarlılık denetimidir, güvenlik sınırı değildir (günlük elle yazılabilir); amacı eski veya başka
commit'ten EXE yayınlamayı engellemektir. Pencere açan Full kapsam sahibin ana PC'sinde oyun sırasında koşulmaz;
oyun bittikten sonra çalıştırılır.

**GitHub Actions (`.github/workflows/ci.yml`)**

- Matris 3 işten 2'ye indi: `debug` ve `dist` (`release` dist'ten ek kanıt üretmediği için dakika/ücret adına kaldırıldı).
- `runs-on` repo değişkeniyle seçilir: `EF_RUNS_ON` (JSON). Boşsa `windows-2025`; self-hosted için
  `["self-hosted","windows","ecufps-local"]`. Değişkeni sahip tanımlar/siler (Settings > Secrets and variables > Actions > Variables).
- `dist` işi imzasız EXE'yi 7 gün saklanan artifact olarak yükler ve SHA-256'yı iş özetine yazar. Bu EXE yayın dosyası
  değildir; yayın dosyası yerel CI'nin test ettiği EXE'dir.
- `sign` işi **devre dışı iskelettir** (`if: ${{ false }}`); etkinleştirme koşulları ADR-038'dedir.

**Self-hosted runner (`scripts\runner_setup.ps1`)**: yalnız sahibin elinde çalışır. Kayıt belirteci ve GitHub'daki
kayıt sahibindir; betik belirteci diske yazmaz, parametre olarak almaz, onaysız hiçbir şey indirmez/kaydetmez/hizmet
kurmaz (her yan etkili adımdan önce Evet/Hayır; etkileşimsiz çalışma Hayır sayılır, `-Yes` yoktur). Varsayılan
Interactive modda hizmet kurulmaz; runner yalnız `run.cmd` açıldığında çalışır (aynı PC'de oyun için önerilen mod).
`-PlanOnly` ağ/disk kullanmadan planı yazar. Runner yalnız **özel depoda** kullanılmalıdır; public depo veya yabancı
fork PR'ı ile kullanılmaz. Geri alma: `config.cmd remove`, runner klasörünü silmek, `EF_RUNS_ON` değişkenini silmek.

## Build kayıtları

Derleyici/SDK/CMake ve bağımlılık sürümleri; kaynak commit; seçenekler;
asset/rule/benchmark sürümü; artifact SHA-256; statik analiz/test sonuçları;
release notları. Tekrarlanabilir derleme hedeflenir fakat zaman damgalı imza
çıktısının byte düzeyi farklılığı ayrı ele alınır. Bu turun kanıtı:
`artifacts/evidence/REL-PACKAGING-2026-11-08.md`.

## İmzalama

Authenticode sertifikası/uzak imzalama erişimi yayın sahibinden gelir; özel
anahtar repo/EXE'ye girmez. İmzasız alpha açıkça etiketlenir (B-003). İmzalı
release için sertifika zinciri/zaman damgası/WinVerifyTrust ve dev trust
bypass yokluğu test edilir. İmza SmartScreen uyarısızlık garantisi değildir.

**Durum (2026-11-18): sertifika yok; sürüm imzasız alpha kalır.** Yol kararı sahibindedir ve ADR-038'de
karşılaştırılır: A) kaynağı açıp SignPath Foundation, B) Microsoft Store/MSIX PoC (tek EXE değişmeziyle çelişir,
PoC gerekir), C) imzasız kalmak. Self-signed SmartScreen için imzasızla aynıdır; Azure Artifact Signing ücretli ve
Türkiye listede değildir; Certum ücretlidir. `ci.yml` içindeki `sign` işi devre dışıdır; karar verilene kadar açılmaz.

## Güncelleme ve veri

İlk sürümde çalışan EXE'yi kendine yazan gizli updater yerine kapalı
uygulamada doğrulanmış dosya değişimi seçilebilir (REL-04). 2026-11-11:
ADR-028 açılışta görünür, zorunlu ve imza doğrulamalı değişimi seçti
(`.new` → atomik değişim → 20 sn sağlık beklemesi, başarısızsa eski EXE).
Geri alma güncellemeye bağlı değildir. Journal şema
göçü/kurtarma önce yapılır. Bekleyen rollback'i çözmeden geri dönüş verisi
silinmez. Geri alma lisansa ve ağa bağlı değildir. Geçici disk benchmark
verisi süresi/kotasıyla temizlenir; hiçbir kullanıcı dosyası temizleme
kapsamına otomatik alınmaz.

## Yayın kapısı

v1.0 etiketi için docs/11_V1_ACCEPTANCE.md tamamlanır. UI demo/placeholder,
mock GPU veya yalnız compile başarısı yayın kanıtı değildir. Destek listesi
test edilmiş kombinasyonları söyler; her PC'de tüm ayarları/çok yüksek FPS'yi
garanti eden ifade kullanılmaz.

- Testlerde ağ yok: `EF_UPDATE_CHECK=0` ortam değişkeni açılış güncelleme kontrolünü atlar. ctest bunu tüm testlere verir; `final.ps1` de ayarlar. Kullanıcı için güvenlik farkı yoktur: kontrol edilemeyen durumda uygulama zaten uyarıyla açılır (ADR-028).

## Sahibin gerçek turu 0.3.0 (spec 2026-11-13 §11; cihazda doğrulanmadı)

Ajan oturumlarında yalnız derleme, `ef_unit_tests` (sahteler) ve ekran dışı `ef_ui_snapshot` çalıştı. Aşağıdakiler
ürün sahibi kendi turunu yapana kadar **cihazda doğrulanmadı** diye anılır. Her satır: yap → gözle → kanıta yaz.

- [ ] `EcuFps-Setup-0.3.0.exe` indir, SHA-256'yı manifestle karşılaştır; SmartScreen davranışını (imzasız alpha) not et.
- [ ] Kurulum penceresi: "En güncel sürüm kontrol ediliyor…" → "Sürüm 0.3.0 · en güncel sürüm"; ağ kapalıyken sarı
      "Güncellik denetlenemedi" + Yeniden dene / Vazgeç / "0.3.0'ı kur".
- [ ] Kur → uygulama içi Evet/Hayır → **tek** gerçek UAC → adım listesi → "Kurulum tamamlandı".
- [ ] `C:\Program Files\Ecu Fps` ACL'i geri okunur (yazma yalnız Administrators/SYSTEM).
- [ ] Başlat menüsü kısayolu açılır; Ayarlar → Uygulamalar listesinde "Ecu Fps" görünür.
- [ ] Masaüstünde "Ecu Fps" simgesi görünür (ortak masaüstü, Ecu Fps ikonu) ve kurulu EXE'yi açar; adım listesinde
      satır "Kısayollar". Önceden aynı adlı masaüstü kısayolu varsa başarısız kurulumda eski hedefiyle geri gelir.
- [ ] HKCU `Run\EcuFps` = kurulu yol `--tray` ("Windows ile başlat" açıkken); yeniden oturumda tepside açılır.
      Değer yoksa `%LOCALAPPDATA%\EcuFps\logs\autostart.log` satırlarını kaydet ("setup done" yazma/geri okuma
      sonucu ve her açılışın `seen=`/`branch=` satırı; 2026-11-13 sahip kurulumunda değer kayboldu, neden kanıtlanamadı).
- [ ] Hizmet `EcuFpsHelper` otomatik başlar; hata sonrası yeniden başlatma eylemi çalışır.
- [ ] Performans Açık/Kapalı ve yönetici gerektiren düzeltmeler **UAC'siz** (hizmet yolu) uygulanır ve geri alınır.
- [ ] Hizmet durdurulunca uygulama UAC broker'ına geri düşer (aynı Evet/Hayır, ayrı UAC).
- [ ] Gerçek GitHub'dan 0.3.0 → 0.3.1: indirme, hizmetin yeniden doğrulaması, UAC'siz değişim, sağlık sinyali; bozuk
      sürümde eski EXE'ye geri dönüş.
- [ ] 0.2.1 taşınabilir kopya: zorunlu güncellemeyle 0.3.0 → "Kurulum gerekli · Kur" → kurulum; kullanıcı verisi korunur.
- [ ] Ayarlar → Kaldır: geri yükleme → tek UAC → "Kaldırıldı; kalan dosya yeniden başlatmada silinir"; UAC reddinde
      "Yönetici izni verilmedi; hiçbir şey silinmedi" ve uygulama çalışır kalır.
- [ ] Uygulamalar listesinden Kaldır (`--uninstall`): Ayarlar sayfası + aynı Kaldır onayı açılır (sessiz kaldırma yok).
- [ ] Yeniden başlatmadan sonra `Program Files\Ecu Fps` ve kalan EXE silinmiş; hizmet, Başlat menüsü ve masaüstü kısayolu, ARP, Run kaydı,
      `ProgramData\EcuFps`, `%LOCALAPPDATA%\EcuFps` kalmamış.

## Dahili ad önekleri (R-F4, ADR-045)

Kod, test ve betikler `ef`/`EF` önekini kullanır: ad alanı `ef::`, makrolar `EF_TEST`/`EF_CHECK`, CMake hedefleri `ef_*`
(`ef_unit_tests`, `ef_ui_snapshot` ...), ortam değişkenleri `EF_*`. Eski `build\<önayar>` klasörleri yeniden
yapılandırılmalıdır (`cmake --preset x64-debug`). Eski `ITX_*` ortam değişkenleri artık okunmaz.

- **Operatör adımı:** GitHub depo değişkeni `ITX_RUNS_ON` tanımlıysa `EF_RUNS_ON` adıyla yeniden oluşturun (Settings → Secrets
  and variables → Actions → Variables); iş akışı yalnız yeni adı okur.
- Bilerek değişmeyenler (kalıcı/tel/sunucu): `itx-bench.marker`, `itx-machine:` özet öneki, IPC sihirli sayısı `ITXP`,
  sunucu ortam adları `ITX_KEY_PEPPER`, `ITX_KEK_HEX`, `ITX_SITE_VERSION_FILE`. Ayrıntı ve gerekçe: ADR-045.
- Geçiş yayını kanıtı `scripts/legacy_bridge_proof.ps1` eski depo kaynağını `/DLEGACY_NS=itx` ile derler (eski ad alanı).
