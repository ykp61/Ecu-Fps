# Karar kaydı

| ID | Karar | Durum ve gerekçe |
|---|---|---|
| ADR-001 | C++20 / Win32 / Direct2D-DirectWrite, statik CRT | **Kabul (2026-11-04, ENV-04).** Ölçüm: tek EXE 108 KiB, import yalnız sistem DLL'leri, EXE yanına dosya yazılmıyor, pencere ≤106 ms, private ~1.7 MiB. UI-02 sonrası tekrar ölçüldü: release EXE 711 KiB, pencere 83 ms, boşta CPU 0 ms/10 sn, özel bellek 103 MiB → UI-05'te 15 MiB (ADR-015). Ayrıntı: artifacts/portable-feasibility.md |
| ADR-002 | Aynı EXE içinde UI/broker/benchmark/recovery rolleri | Tek indirme dosyası, ihtiyaç bazlı yetki; kalıcı servis yok |
| ADR-003 | Korumalı kalıcı journal | Güç kesintisi sonrası geri dönüş için gerekli; EXE yanında loose dosya bırakmıyor |
| ADR-004 | Tek performans kontrolü | Kullanıcı kararı: Açık/Kapalı; Evet/Hayır uygulama |
| ADR-005 | İki test kaynağı | Kullanıcı kararı: Uygulama Testi / Oyunda Test |
| ADR-006 | Sabit test içeriği ve sürümlü manifest | Önce/sonra tekrar edilebilirliği |
| ADR-007 | Kanıtla teşhis ve abstain | Model adı/tek kullanım yüzdesi ile yanlış kesinliği önleme |
| ADR-008 | Tüm önerilen modüller v1 kapsamı | Kullanıcı kararı; bağımlılıklı geliştirme, çalışmayan özellik iddiası yok |
| ADR-009 | Yeni görsel üretimi beklemede | Kullanıcı istemedikçe yok; önceki yön ve kompakt ölçüler korunuyor |
| ADR-010 | Claude Code / Codex ortak dosya tabanlı devir | Kısa kök kuralları; kalıcı backlog ve oturum durumu |
| ADR-011 | Ajan paket hazırlarken uygulama kodu yok | Bu teslimat geliştirme hazırlığı; başlatma mesajı ayrı |
| ADR-012 | Benzer sistem kıyasının çevrimdışı veri sözleşmesi önce | Yerel motor temel; gerçek veri ve isteğe bağlı hosting olmadan canlı sıralama iddiası yok |
| ADR-013 | Görünür tepsi + oyun başına tek onaylı otomatik mod | Kullanıcı kararı (2026-11-04). Tek örnek kilidi; açılışta başlama yalnız kullanıcı açarsa görünür HKCU Run ile; gizli servis/görev yok. Uygulama: SEC-05, UI-06, OPT-06. *("yalnız kullanıcı açarsa" kısmı ADR-030 ile superseded, 2026-11-13: varsayılan açık.)* |
| ADR-014 | Yönetici gerektirmeyen adımlar UI sürecinde, kullanıcı journal'ıyla | **Kabul (2026-11-04, UI-02).** Güç modu/planı gibi yönetici hakkı istemeyen (etkisi makine genelinde olsa da) adımlar, broker beklemeden UI sürecinde işlem motoru + kullanıcı journal'ı ile uygulanır; çökme sonrası aynı adaptör setiyle (app/adapter_set.h) kurtarma rolü geri alır. Yönetici gerektiren adım içeren plan, broker yürütmesi (OPT-02) gelene kadar "izin yok" sonucu verir ve hiçbir şey değiştirmez. Gerekçe: gereksiz UAC istemi yok; geri dönüş garantisi journal ile aynı. |
| ADR-015 | UI çizimi yazılım (CPU) Direct2D hedefiyle | **Kabul (2026-11-04, UI-05).** Ölçüm: yazılım hedefi 15 MiB özel bellek ve süreçte GPU sürücüsü yok; donanım hedefi 66–68 MiB ve NVIDIA kullanıcı modu sürücüsü yüklü. Kare ~2 ms, çizim yalnız değişiklik/kısa geçişte. Envanter GPU türünü DXCore ile cihaz oluşturmadan okur. Tanı için EF_UI_RENDERER=hardware. Ayrıntı: artifacts/ui-performance.md |
| ADR-016 | Disk temizliği açık ve geri alınamaz bir işlem | **Kabul (2026-11-04, WIN-08).** Journal/rollback modeline girmez, Kapalı ile geri yüklenmez. Yalnız yeniden oluşturulan geçici/önbellek kategorileri (Geri Dönüşüm Kutusu tek istisna, hiç varsayılan seçili değil); kullanıcı belgeleri yok. Önce salt okunur önizleme, kategori bazlı Evet/Hayır, yalnız önizlemedeki dosyalar silinir; izin listesi kökü, yeniden ayrıştırma noktası reddi, kilitli dosya atlanır; kazanılan alan ölçülür. Yönetici kategorileri broker ile. Ayrıntı: artifacts/evidence/WIN-08-engine.md |
| ADR-017 | Onaylı 02/BOOST marka varlıkları gömülü | **Kabul (2026-11-04, BRAND-01).** Kullanıcının verdiği paket kullanıldı; yeni görsel üretilmedi (ADR-009 ile çelişmez). EXE ikon grubu 101 (15 boyut) ve tepsi 201–206 (7 boyut) .rc kaynağı; logo SVG yolları gömülü ve küçük ayrıştırıcı ile Direct2D geometrisine çevrilir (yeni SVG motoru/WebView/font yok). Tepside aktif ikon yalnız doğrulanmış Açık durumda; belirsiz/geçişte nötr + durum metni. Görev çubuğu açık/koyu `SystemUsesLightTheme` ile okunur — belgelenmiş API değildir; okunamazsa koyu varsayılır. Yüksek kontrastta amblem sistem metin rengiyle çizilip HICON yapılır; olmazsa siyah/beyaz ICO. Kimlikler (journal yolu, mutex, IPC, sınıf adları) değişmedi. *(2026-11-11, sıkı yerleşim §6, ürün sahibi onaylı spec: EXE ICO'su ve dört koyu/açık tepsi ICO'su aynı amblem geometrisinden `scripts/build_brand_icons.py` ile yeniden üretildi — amblem #3DFF7A, nötr tepsi durumu içi boş kontur. "Yeni görsel üretilmedi" ibaresi yalnız 2026-11-04 paketi içindir. Kaynak kimlikleri, boyut kümeleri, `choose_tray`, ipuçları ve yüksek kontrast ICO'ları değişmedi; ctest `windows.brand_icons`.)* |
| ADR-018 | Performans anahtarı yuvarlak halka + ayar bazlı Evet/Hayır | **Kabul (2026-11-04, UI-10, kullanıcı isteği).** docs/07'deki 48×28 sürgü yerine 44 DIP (dokunma hedefi) yuvarlak halka: Kapalı kırmızı (error), Açık mavi (accent), geçişte mavi yay dolar (120 ms; azaltılmış harekette 0). Tek anahtar kuralı korunur (mod/profil yok); altında "Bu ayar neleri değiştirir" paneli her grubu durumuyla (Değişecek/Zaten uygun/Uygulanmayacak) gösterir, grup başına Evet/Hayır kalıcıdır; seçilmeyen grup uygulanmaz ve journal'a yazılmaz. |
| ADR-019 | Başlangıç girdisi değişiklikleri ayrı journal kapsamı | **Kabul (2026-11-04, WIN-09).** Başlangıç sayfasındaki değişiklikler oturumluk değil kalıcı kullanıcı tercihidir; bu yüzden Performans Kapalı onları geri almaz. Kullanıcı journal dizininde ayrı `startup` kapsamı (aynı işlem motoru, kalıcı kayıt → yaz → geri oku). Girdi başına tek işlem; aynı anahtarla tekrar açmak o işlemi geri alır (StartupApproved ham baytı birebir, önceden yoksa silinir). Yalnız kullanıcı kapsamı (HKCU Run, kullanıcı Startup klasörü); HKLM/ortak klasör broker (OPT-02) bekler. Güvenlik, hile önleme ve Windows bileşeni girdileri bu sayfadan değiştirilemez. Kaldırma/tam geri yükleme bu kapsamı da kapsar. Hizmet durdurma (B07) bu sayfada yalnız bilgidir. |
| ADR-020 | OPT zinciri GPU-03/04'ü beklemez | **Kabul (2026-11-04).** GPU-03 (AMD ADLX lisans kararı, kullanıcıda) ve GPU-04 (Intel cihaz yok, B-004) bloklu. OPT-01 kural seçimi bunlara sert bağlı kalırsa tüm optimizasyon akışı durur. Karar: OPT-01 bağımlılığı GPU-02'ye çekildi; AMD/Intel ayarları capability kaydında `unsupported` kalır ve plan dışıdır (kural motoru belirsiz/geri alınamaz ayarı zaten dışlar). GPU-03/04 tamamlanınca yalnız kural eklenir; plan sözleşmesi değişmez. |
| ADR-021 | Korunan oyuna müdahalesizlik kapısı; oyun başlarken otomatik uygulama kaldırıldı | **Kabul (2026-11-05, handoff v1 EXT-AC-02, kullanıcı şartı).** ADR-013'ün "oyun başlarken otomatik Açık" kısmını **supersede eder** (görünür tepsi ve tek örnek aynen geçerli). Algılanan herhangi bir kurulu oyun çalışırken (koruma tespiti kusursuz varsayılmaz) ayar mutasyonu (Performans Evet/geri yükleme, Başlangıç uygula, Sistem kontrolü Evet), disk temizliği ve ağır iç benchmark başlatılmaz; düğme gösterilmez, neden kartı görünür, App katmanı ayrıca reddeder. Oyun başlarken soru penceresi açılmaz (oyunu alta atıyordu). Çıkışta geri yükleme gerekiyorsa oyun kapanınca yapılır. Canlı oyun içi ölçüm: gömülü uyumluluk matrisi (CS2+VAC, CS2+FACEIT `unverified`), bilinmeyen kombinasyon kapalı, kullanıcı override yok; "Oyunda Test" kapalı ve nedeni yazılı. Geliştirici araçları (ef_game_capture/ef_gpu_now) korumalı oyun üzerinde bir daha çalıştırılmaz. Kod: src/core/games/protection_gate.*, integration/ecufps-handoff-v1/decisions.md |
| ADR-022 | Windows oyun ayarları için Ayarlar uygulamasının kayıt değerleri | **Kabul (2026-11-05, plan v3.5, OPT-09/CHK).** Game Mode (HKCU GameBar AutoGameModeEnabled), "Olanları kaydet" arka plan kaydı (GameDVR HistoricalCaptureEnabled), pencereli oyun iyileştirmesi ve Windows VRR (DirectX UserGpuPreferences DirectXUserGlobalSettings öğeleri) için resmî API yok; değerler Windows Ayarlar uygulamasının kendi sakladığı yerlerdir (gözlem + topluluk kaynağı, Microsoft belgesi değil). Bu yüzden: sabit izin listesi (serbest yol yok), yalnız HKCU, journal + geri okuma, ham bayt birebir geri yükleme, önceden yoksa silme; ortak DirectX dizesinde yalnız ilgili öğe değişir. Ekran yenileme hızı düzeltmesi CDS_UPDATEREGISTRY ile kalıcıdır, 15 sn "Koru/Geri al" onayıyla ve ayrı "fixes" journal kapsamındadır (Performans Kapalı geri almaz). HAGS/NVIDIA sürücü profili gibi yönetici ayarları broker (OPT-02) bitene kadar Windows ayar sayfasına yönlendirilir. VBS/HVCI yalnız bilgi. |
| ADR-023 | Yönetici yürütme yolu: HAGS + NVIDIA DRS global (broker) | **Kabul (2026-11-05, plan v3.5, OPT-02/OPT-10; ADR-022'nin "broker bitene kadar ayar sayfası" kısmını günceller).** Tek Evet → tek UAC (ShellExecuteEx runas, aynı EXE `--role=broker`) → nonce + PID el sıkışmalı pipe (SEC-02; isteğe bağlı `action` = apply/restore/fix) → SEC-03 dağıtıcı (Machine kapsamı, yönetici, makine kilidi `Global\EcuFps.MachineOps`) → makine journal'ı (ProgramData, korumalı ACL): başlangıç kaydı → yaz → geri oku; hata → aynı adımda geri al. İzin listesi derlenmiştir: `hags:hwschmode` = 1/2 (HKLM GraphicsDrivers HwSchMode, Ayarlar > Grafik'in yazdığı değer; gözlem + topluluk kaynağı, resmî API yok), `drs:global:` PREFERRED_PSTATE=1 (PREFER_MAX), PRERENDERLIMIT=1 ("Düşük Gecikme Açık"), PS_SHADERDISKCACHE_MAX_SIZE=10240 (yalnız mevcut değer küçükse; 0 ve daha büyük/sınırsız değere dokunulmaz; MB birimi NVCP turunda doğrulanacak). Oyun profili, VSync/FPS sınırı ve serbest komut yok. HAGS yalnız D3DKMT desteği doğrulanınca önerilir; sonuç `pending_reboot`. UAC reddi → yönetici grupları atlanır, kullanıcı kapsamı uygulanır, "kısmi" açıkça bildirilir; Kapalı'da UAC reddi → Açık kalır. Otomatik (oyun başladı) uygulama yönetici gruplarını atlar (oyun açılırken UAC yok). Oyun Hazırlığı HAGS düzeltmesi ayrı makine "fixes" journal'ındadır (Kapalı geri almaz). Gerçek UAC turu kullanıcı varken (PENDING_USER #12). |
| ADR-024 | Tek tema: koyu; açık tema ve tema seçimi kaldırıldı | **Kabul (2026-11-09, UI-14, kullanıcı isteği).** UI-07'nin açık palet ve Windows uygulama modu takibi kısmını ve R25'in "açık/koyu tema takibi" ibaresini **supersede eder**. Palet seçimi tek noktada: yüksek kontrast etkinse sistem renkleri, değilse `colors` (koyu). `colors_light`, `Theme::light`, tema radyoları (`Hit::ThemeSystem/Light/Dark`), `Preferences::theme` ve ilgili string'ler kaldırıldı; eski `preferences.json` içindeki `theme` alanı bilinmeyen alan olarak yok sayılır (şema 1 kalır). DWM başlık çubuğu her zaman koyu; HC'de varsayılan renk. Yüksek kontrast erişilebilirlik değişmezi olarak kalır; tepsi ikonu görev çubuğu temasını izlemeye devam eder (ADR-017). Gerekçe: kullanıcı tek görünüm istedi; iki palet bakım/test yükü ve Windows moduna bağlı beklenmeyen görünüm değişimi kalktı. |
| ADR-025 | Güç planı AC alt ayarları: boost, çekirdek parkı, PCIe ASPM (broker) | **Kabul (2026-11-09, F-32/F-33/F-42, windows_write dilim 3).** ADR-023'ün broker izin listesini genişletir; yeni bir yürütme yolu açmaz. Üç sabit ayar, Windows belgeli güç ayarı GUID'leriyle: PERFBOOSTMODE (`be337238-…`, hedef 1 = Enabled), CPMINCORES (`0cc5b647-…`, hedef 100 = park kapalı), PCIe Link State (`ee12f906-…`, hedef 0 = Off). Yalnız **ACSettingIndex** yazılır; DC (pil) değerine dokunulmaz. Boost yalnız "Efficient" modlardan (3/4) "Enabled"a çekilir; kullanıcı/OEM tarafından kapatılmış (0), zaten performans odaklı (1/2/5/6) veya okunamayan değer ezilmez. Anlık görüntü, değerin okunduğu **plan GUID'ini** taşır; geri alma o plana yazar (kullanıcı sonradan plan değiştirse bile). Aynı işlemde `PowerSchemeSet` varsa alt ayarlar plana girmez: anlık görüntü eski plana, yazma yeni plana giderdi. Değer yoksa/okunamazsa plana girmez. Broker izin listesi yalnız `power:boost:ac=1`, `power:parking:ac=100`, `power:aspm:ac=0` kabul eder; yeniden başlatma gerekmez. Kanıt: artifacts/evidence/PERF-12-POWER-SUBS-2026-11-09.md. |
| ADR-026 | v1 kapsam kararı: güvenli varsayılanlar (X03/X04-X07/X10/X13/X26-X30) | **Kabul (2026-11-10, ürün sahibinin "bu işleri sen hallet" devriyle; ürün sahibi geri alabilir).** Para, hesap, lisans kabulü veya hukuki onay gerektirmeyen kararlar muhafazakâr seçildi: sürücü yalnız teşhis + resmî bağlantı; CPU undervolt yalnız uygunluk/rehber/değerlendirme; MPO/ULPS yazma kapalı; üretici katalogları boş ve "veri yok"; ağ yük testi v1 dışı; v1 çevrimdışı imzasız alpha, lisans/mağaza/güncelleme/destek/duyuru hizmeti yalnız yerel sözleşme testi. Hiçbir özelliği done yapmaz. Ürün sahibinde kalanlar: B-006, B-003, D03-D05, B-004, K1-K15. ADR-008 ile gerilim aşağıda kayıtlı. Ayrıntı: aşağıdaki ADR-026 bölümü. **X13 satırı ve "ağ kütüphanesi eklenmez" ibaresi ADR-027, X26 "ürün içi güncelleme yok" kısmı ADR-028 ile superseded (2026-11-11). X03 satırı ve X04–X06'daki "güncelleme var denmez" cümlesi ADR-031 ile superseded (2026-11-13).** |
| ADR-027 | Zehir yeşili yeniden tasarım + uygulama içi Cloudflare hız testi | **Kabul (2026-11-11, ürün sahibi onayı).** Koyu siyah zemin, tek vurgu #3DFF7A, üst menü, 3 DIP hareketli RGB şerit, beş canlı kutucuk. Marka kılavuzundaki parlama/RGB yasağı yalnız spec kurallarıyla delinir. Kullanıcı başlatmalı WinHTTP hız testi (`speed.cloudflare.com`) ADR-026 X13 satırının ve "ağ kütüphanesi eklenmez" ibaresinin yerini alır. Açık risk: Cloudflare yazılı izni yok. Ayrıntı aşağıda. |
| ADR-028 | Açılışta zorunlu güncelleme, projenin kendi ECDSA P-256 yayın anahtarıyla | **Kabul (2026-11-11, ürün sahibi isteği).** İmzalı manifest açık `ykp61/Ecu-Fps-releases` deposundan okunur; ADR-026 X26 "ürün içi güncelleme yok" ve EF-X26 "Authenticode yokken güncelleme kapalı" kuralını supersede eder. Çevrimdışı → uygulama uyarıyla çalışır. Ayrıntı aşağıda. |
| ADR-030 | Windows ile başlatma varsayılan açık (tepside) | **Kabul (2026-11-13, ürün sahibi isteği; spec 2026-11-12 G).** ADR-013'ün "açılışta başlama yalnız kullanıcı açarsa" kısmını supersede eder; Evet/Hayır kuralının bu tek kayıt için bilinçli istisnası. İlk çalıştırmada HKCU Run `EcuFps` = `"<exe>" --tray` (yönetici yok), ilk açılışta bir kez engellemeyen bildirim + "Kapat", Ayarlar anahtarı. Ayrıntı aşağıda. |
| ADR-031 | Güncellemeler sayfası: Windows Update sürücü tespiti + resmî üretici bağlantıları | **Kabul (2026-11-13, ürün sahibi onayı; spec 2026-11-12 H, "A" seçeneği).** Kaynak yalnız Windows Update (yükseltilmemiş, salt okuma `IsInstalled=0 and Type='Driver' and IsHidden=0` araması; arka plan, iptal, zaman aşımı); üretici sitelerine otomatik sorgu yok, yalnız kullanıcı tıklamalı resmî sayfa. Firmware "Elle denetlenir"; uygulama BIOS/firmware yazmaz. ADR-026 X03 ve X04–X06 "güncelleme var denmez" cümlesini supersede eder; WUA kurulum arabirimleri yalnız H2 yardımcısının klasöründe. Ayrıntı aşağıda. |
| ADR-033 | Kurulum (aynı EXE setup) + LocalSystem yardımcı hizmet; 0.3.0 | **Kabul (2026-11-13, ürün sahibi onayı, canlı maket).** `EcuFps-Setup-<v>.exe` = aynı EXE; Program Files'a tek UAC'li işlemsel kurulum (journal + geri alma), Başlat menüsü, ARP, HKCU Run. Hizmet (`--service`) yalnız kurulu EXE'den gelen etkileşimli oturum isteklerini ve yalnız broker izin listesini yürütür; zorunlu güncellemeyi UAC'siz değiştirir. ADR-001/002'deki "yalnız taşınabilir tek EXE, kalıcı servis yok" dağıtım kısmını ve işlem başına UAC broker'ın varsayılan yol olmasını supersede eder (broker geri düşme yolu olarak kalır). Ayrıntı aşağıda. |
| ADR-036 | Windows Update duraklatma / etkin saat: yalnız okuma + öneri (WIN-11) | **Kabul (2026-11-18, kanıta dayalı).** Kullanıcı uygulamasına açık belgeli duraklatma API'si yok (`IAutomaticUpdates::Pause` Windows 10'dan beri işlemsiz); belgeli yol yalnız BT yöneticisinin aygıt ilkesi (Policy CSP / Grup İlkesi). Ürün duraklatma/etkin saat yazmaz. ADR-035 T03 grupları ayrıdır. Ayrıntı: ADR-036 bölümü. |

Yeni kararlar templates/ADR.md biçimiyle eklenir. Tarihsel bir kararı silmek yerine superseded ve yeni karar bağlantısı yazılır. Teknik adaylar ölçüm sonucu diye sunulmaz.

## ADD-ADR-02 — DNS ve ayrı ağ erişim modülü (2026-11-10)

Karar: DNS değişikliği yerel Windows API'si (Get/SetInterfaceDnsSettings) ile, yalnız yükseltilmiş broker'da, arayüz GUID'i hedefiyle ve derlenmiş hazır sunucu listesiyle yapılır; fixes journal'ında kaydedilir, geri alma otomatik/DHCP durumunu aynen geri getirir. VPN, etki alanı, NRPT veya DoH ilkesi varsa ya da okunamıyorsa değişiklik önerilmez.
Paket işleme gerektiren erişim modülü (GoodbyeDPI/WinDivert sınıfı) tek EXE, sürücüsüz çekirdek ve anti-cheat önceliğiyle çelişir. Ürün sahibi açık politika kararı verene kadar kapalıdır: indirilmez, servis veya sürücü kurulmaz, oyun açıkken/algılama belirsizken başlatılmaz. Karar D02 olarak tek kullanıcı listesindedir.

## ADR-026 — v1 kapsam kararı: ürün sahibinin devrettiği kararlar için güvenli varsayılanlar

Durum: accepted (devredilmiş yetkiyle; ürün sahibi istediği an geri alabilir veya genişletebilir)

Tarih: 2026-11-10

İlgili görevler: EF-X03 (WIN-07, PERF-09), EF-X04/X05/X06 (INV-02, INV-06), EF-X07 (GPU-03), EF-X10 (OPT-10, REC-02), EF-X13 (CHK-06, NET-01), EF-X26 (REL-03/04/07/08), EF-X27, EF-X28, EF-X29 (FOUND-05), EF-X30. Dış kararlar: D01, D03, D04, D05, D06 (`integration/ecufps-expansion-v1/planning/external_dependencies.json`).

Sorun ve mevcut kanıt: Ürün sahibi (programcı değil) kalan ürün kararlarını ajana devretti: "bu işleri sen hallet". Genişleme ekinde birkaç özellik ürün kararı beklediği için yarım kalıyordu. Bu kayıt yalnız **para, hesap, lisans sözleşmesi kabulü veya hukuki onay gerektirmeyen** kararları verir; o tür kararlar ajan tarafından verilemez ve ürün sahibinde kalır (aşağıda). 2026-11-10 gözlemi: `src` altında HTTP/soket istemcisi yok (WinHTTP/WinINet/WS2_32/URLMon çağrısı bulunmadı); tek ağ işlemi kullanıcı tetikli ICMP yankı testi (`network_reader.cpp`, `IcmpSendEcho2`). `tests/windows/check_offline.ps1` bu kütüphanelerin import edilmediğini, `tests/windows/check_no_install.ps1` sürücü kurulum/indirme API'lerinin (pnputil, DiInstallDriver, UpdateDriverForPlugAndPlayDevices, SetupCopyOEMInf, URLDownloadToFile, WinHttpOpen, msiexec) kullanılmadığını denetler. İmzalı katalog motoru `src/core/catalog/` içinde var; doğrulanmış katalog yoksa `CatalogStatus::Missing` ile iddia üretmez. Yerel lisans/mağaza/destek hizmeti `services/ecufps-api/` altında yalnız sözleşme testleriyle (`test_contracts.py`) vardır; ürün EXE'si ona bağlanmaz.

Değerlendirilen seçenekler: (a) Kararları ürün sahibine geri sormak — sahip açıkça devretti, iş durur. (b) Özellikleri tam açmak — resmî API, sertifika, sunucu veya lisans olmadan belgesiz yazma ya da çalışmayan düğme demektir; AGENTS.md ürün değişmezleriyle çelişir. (c) **Seçilen:** her başlıkta en az riskli, geri alınabilir, ağsız ve yazmasız yolu v1 için sabitlemek; engelli parçayı açık görev olarak bırakmak.

Seçim ve gerekçe:

| Başlık | v1 kararı | Gerekçe |
|---|---|---|
| X03 sürücü kurulum/temizleme | Yalnız teşhis (sürüm, yaş, aygıt durumu, DriverStore sayıları) + resmî üretici bağlantısı rehberi. Ürün sürücü indirmez, kurmaz, kaldırmaz; DDU/AMD Cleanup gömülmez veya çalıştırılmaz; INF dışa aktarma/geri yükleme yok. | R28 ("resmî bağlantı, sessiz kurulum yok") ve WIN-07 ile aynı çizgi. Geri alınamayan sürücü değişimi için doğrulanabilir geri alma sözü verilemez; yayıncı/imza doğrulamalı indirme ağ istemcisi ve sertifika kararı ister. |
| X07 CPU undervolt | Nihai v1 kapsamı: uygunluk (model/kilit/üretici aracı desteği), mevcut durumun kaydı, Ryzen Master/XTU/BIOS rehberi ve önce/sonra kararlılık-performans değerlendirmesi. **Otomatik uygulama yok**; voltaj/PBO/Curve Optimizer yazımı yok. | Genel resmî CPU voltaj yazma API'si doğrulanmadı (EF-X07); izleme SDK'sı yazma SDK'sı değildir; AMD tarafı B-006 lisansına bağlı. Özel sürücü (SMU/MSR) AGENTS.md ile yasak. Rehberin tamamlanması otomatik undervolt tamamlandı sayılmaz. |
| X10 MPO/ULPS | v1'de **yazma kapalı** (MPO dahil). Yalnız salt okuma bulgusu ve rehber metni olabilir. | ULPS için güncel üretici yazma sözleşmesi yok. MPO için NVIDIA destek tarifi var ama hedef anahtar/değer bağımsız doğrulanmadı, yeniden başlatma doğrulaması ve gerçek UAC turu yapılmadı; tek kullanıcı PC'sinde sessiz deneme yasak. `kShaderCacheItemEnabled=false` emsaliyle derlemede kapalı tutulur. |
| X04/X05/X06 üretici katalogları | Katalog motoru gömülü, **katalog içeriği boş** gönderilir; kullanıcıya "veri yok" gösterilir. "Güncel", "güncelleme var" veya "etkilenmiyor" denmez. v1'de katalog indiren ağ istemcisi yok; kazıyıcı (scraper) yok. | Uydurma veri gösterme yasağı (AGENTS.md). Gerçek katalog verisi toplama, imzalama anahtarı yönetimi ve dağıtım kanalı v1'de yok; ağ istemcisi `check_offline` sözleşmesini değiştirir. BIOS yaşı bulgusu (BiosOld) ayrı ve salt okuma olarak kalır. |
| X13 ağ yük testi (D06) | Dış yüklü gecikme/bufferbloat testi **v1 dışı**. Mevcut kullanıcı tetikli ICMP ping testi ve gateway/internet ayrımı kalır; yerel test düzeneği (harness) kod olarak kalabilir. Üçüncü taraf speedtest sunucusu izinsiz yüklenmez. | İşletilen veya izinli bir test sunucusu yok; trafik maliyeti ve kullanım izni ürün sahibine ait dış bağımlılık. |
| X26-X30 güncelleme/lisans/mağaza/destek/duyuru (D03/D04/D05) | v1 **çevrimdışı, imzasız alpha**. Ürün içinde güncelleme indirme yok, lisans etkinleştirme/çevrimiçi doğrulama yok (alpha lisans kapısı olmadan çalışır), mağaza/Shopier bağlantısı yok, destek talebi gönderimi yok (yalnız mevcut yerel destek paketi dışa aktarımı; kullanıcı kendisi iletir), çevrimiçi duyuru akışı yok. `services/ecufps-api/` yerel sözleşme testleri olarak kalır; istemci ağ bağlantısı ertelenir. | İmza sertifikası (B-003), alan adı, e-posta sağlayıcısı, satış hesabı ve fiyat kararı yok; bunlar para/hesap/hukuk gerektirir. İmzasız güncelleyici güvenli değildir (EF-X26: sertifika yokken release auto-update kapalı). Ağsız ürün `check_offline` ile doğrulanabilir kalır. |

Bu karar hiçbir özelliği **done yapmaz** ve hiçbir kabul kanıtı yerine geçmez. Kapsam dışı bırakılan parçalar silinmez; ilgili görevlerde blocked kalır ve neden `v1 kapsam dışı (ADR-026)` olarak yazılır (ör. X03 kurulum/temizleme, X07 otomatik uygulama, X10 MPO/ULPS yazma, X04/X05/X06 katalog içeriği ve ağdan güncelleme, X13 dış yük testi, X26 ağ güncellemesi, X27/X28 canlı lisans/satış, X29 çevrimiçi gönderim, X30 çevrimiçi akış). Çalışılabilir salt okuma/rehber parçaları kendi kabul kanıtıyla ayrıca tamamlanır. `status.json` ve `tasks/backlog.json` bu kayıtla değiştirilmedi; o güncelleme ayrı yapılır.

**Ajanın karar vermediği, ürün sahibinde kalanlar:**

- AMD ADLX SDK lisans sözleşmesinin kabulü (B-006, GPU-03, PENDING_USER #4 / K7).
- Kod imzalama sertifikası alımı/başvurusu (B-003; araştırma research/B003_SIGNING.md).
- Shopier hesabı, alan adı, e-posta sağlayıcısı, ürün/SKU, fiyat ve lisans politikası (D03, D04, D05).
- Intel GPU donanımı temini/testi (B-004).
- `state/PENDING_USER.md` içindeki tüm gerçek UAC/yönetici ve fiziksel kabul turları K1-K15 (ve numaralı eski satırlar). Ajan UAC'ye tıklamaz, ayar yazan onaylarda Evet'e basmaz.
- Ayrıca: D02 (sürücülü ağ erişim bileşeni, ADD-ADR-02) ve izinli yük testi sunucusunun açılması (D06) için para/hesap gerektiren her adım.

Ürün sahibi bunlardan birini sağladığında yalnız ilgili satır yeniden açılır; bu ADR o satır için superseded yazılır.

**Gereksinim belgeleriyle çelişki (gereksinimler düzenlenmedi, burada kayıtlı):**

- ADR-008 ("Tüm önerilen modüller v1 kapsamı", kullanıcı kararı) ve `docs/17_FUTURE_FEATURES_IN_V1.md` ("modül yokmuş gibi gelecek sürüme itilmez") ile gerilim var. ADR-026, ADR-008'i supersede **etmez**: modüller kod ve görev olarak kalır, yalnız para/hesap/lisans/sunucu gerektiren parçanın v1 yayınında kapalı olduğu açıkça yazılır. Ürün sahibinin devri bu daraltmanın dayanağıdır; sahip aksini söylerse ADR-008 geçerli kalır.
- `docs/01_REQUIREMENTS.md` "R01-R38 v1 hedefidir" ve "özelliği kapsamdan sessizce çıkarma" der. Doğrudan etkilenenler: **R22** (güvenli güncelleme kontrolü; v1'de ürün içi güncelleme yok — docs/11 zaten kısmi diyor), **R17** (imzalama; B-003 nedeniyle imzasız alpha), **R03** (AMD/Intel gerçek destek matrisi; B-006/B-004). Bunlar sessiz çıkarma değildir; bu kayıtla açık ve gerekçeli kapalıdır, v1 tamamlandı sayılmadan önce ürün sahibi kararı gerekir.
- Çelişmeyenler: R28 ve R30 (sürücü/BIOS "rehberli", sessiz kurulum yok) X03/X04 kararıyla aynı; R21 (yerel destek paketi) X29 kararıyla aynı; R15 (veri yoksa "veri yetersiz") X04-X06 "veri yok" yaklaşımıyla aynı. X13 yük testi, X07 undervolt ve X26-X30 hizmetleri R01-R38 içinde ayrı madde olarak geçmiyor; genişleme ekiyle (`integration/ecufps-expansion-v1/02_SCOPE_30_FEATURES.md`, "Sınırlı özelliklerin kapsam dışı sanılmaması") gerilim yukarıdaki ADR-008 notuyla aynı şekilde çözülür: engelli parça açık görev kalır.

Tek EXE / hafiflik / geri dönüş / lisans etkisi: Olumlu. Yeni bağımlılık, ağ kütüphanesi, sürücü veya üçüncü taraf lisans eklenmez. *(2026-11-11: ağ kütüphanesi kısmı ADR-027/ADR-028 ile superseded; yalnız WINHTTP.dll, yalnız `src/platform/windows/net/`.)* Yazma yolu açılmadığı için yeni journal/geri alma yükü yok. Boş katalog EXE boyutunu anlamlı değiştirmez. Hiçbir lisans sözleşmesi kabul edilmez.

Gerekli testler: `tests/windows/check_offline.ps1` (`OFFLINE OK`: HTTP/soket/DNS kütüphanesi import edilmez); `tests/windows/check_no_install.ps1` (sürücü kurulum/indirme API'si yok); katalog birim testleri: katalog yok/imzasız/süresi dolmuş/eski sürümde sonuç "veri yok/Unknown", asla "güncel" veya "güncelleme var" değil; X07/X10 için yazma yolunun derlemede kapalı olduğunu ve capability kaydında `unsupported`/`release_enabled=false` kaldığını doğrulayan test; `services/ecufps-api/test_contracts.py` yalnız yerel sözleşme testi olarak. Bu ADR yazılırken bu testler yeniden çalıştırılmadı; kanıt ilgili görev checkpoint'inde kaydedilir.

Geçersiz kılınan karar: Yok. ADR-008 supersede edilmez (yukarıdaki not). ADR-020, ADR-021, ADD-ADR-02 aynen geçerli.

Kaynaklar: `integration/ecufps-expansion-v1/features/EF-X03.md`, `EF-X05.md`, `EF-X07.md`, `EF-X10.md`, `EF-X13.md`, `EF-X26.md`; `integration/ecufps-expansion-v1/planning/external_dependencies.json` (D01-D07); `state/BLOCKERS.md` (B-003, B-004, B-006); `state/PENDING_USER.md` (K1-K15); `docs/01_REQUIREMENTS.md`; `docs/11_V1_ACCEPTANCE.md` (R22 notu); `src/core/catalog/catalog.h`.

## ADR-027 — Zehir yeşili yeniden tasarım ve uygulama içi internet hız testi

Durum: accepted (ürün sahibi sohbette tasarımı ve hız testini onayladı)

Tarih: 2026-11-11

İlgili: EF-UI-RD, EF-X13 (CHK-06, NET-01). Spec `docs/superpowers/specs/2026-11-11-zehir-yesili-redesign-design.md`, plan `docs/superpowers/plans/2026-11-11-zehir-yesili-redesign.md`.

Karar:

- **Görünüm:** koyu siyah zemin (#050505, kart #0E0E0E), tek vurgu zehir yeşili #3DFF7A; sol menü yerine 48 DIP üst menü; en üstte 3 DIP RGB şerit; Panel'de beş canlı kutucuk (işlemci, bellek, ekran kartı, disk, ağ). Ürün değişmezleri (tek Performans Açık/Kapalı, Evet/Hayır, geri alma) aynen geçerli. Tokenlar `design/tokens.json`.
- **Marka kılavuzu istisnası:** `docs/branding/BRAND_GUIDE_TR.md` içindeki "glow, RGB çevrim yok" kuralı yalnız şu kadar delinir: parlama yalnız ana düğme, açık anahtar ve kadran yayı/büyük sayıda (3 halkalı dolgu taklidi, kartlarda yok); RGB şerit 8 sn'lik tur, ~20 kare/sn, yalnız şerit alanı yeniden çizilir; pencere görünmez/odakta değil, oyun açık, ölçüm/hız testi sürüyor, pilde, "animasyonları azalt", uygulama animasyonları kapalı, "RGB ışık şeridi" kapalı veya yüksek kontrastta durur. HC ve azaltılmış harekette parlama çizilmez. Başka efekt (parçacık, parlayan metin, blur) eklenmez.
- **Hız testi:** Ölçümler sayfasında, yalnız kullanıcı başlatır, Evet/Hayır onayıyla; oyun açıkken veya algı belirsizken başlamaz; her an durdurulur; ≤60 sn, ≤256 MiB; tek host derleme sabiti `speed.cloudflare.com`, yol yalnız `/__down`, `/__up`; TLS doğrulaması açık, sistem proxy'si; sonuç diske yazılmaz, karşıya gönderilmez; bağlantı yoksa "Ölçülemedi", sahte değer yok. Kod: `src/core/net/speed_test.*` (saf), `src/platform/windows/net/speedtest_http.*` (WinHTTP).

Geçersiz kılınan: ADR-026 tablosundaki **X13 satırı** ("dış yük testi v1 dışı") ve "ağ kütüphanesi eklenmez" ibaresi. Koruma daraltılır, kaldırılmaz: `check_offline.ps1` yalnız WINHTTP.dll'e izin verir (WS2_32, WININET, URLMON, WEBSERVICES, DNSAPI yasağı sürer); `check_no_install.ps1` WinHttp* çağrısını yalnız `src/platform/windows/net/` altında kabul eder; `--recovery` ve broker rolleri ağ koduna girmez; birim testi izinli host kümesini sabitler.

Açık risk (ürün sahibinde): Cloudflare uç noktalarının üçüncü taraf uygulamada kullanımı için **yazılı izin yok** (`state/PENDING_USER.md`). Sürüm "imzasız alpha" kalır. Gerçek ağla hız testi koşusu (K16) yapılmadı; şerit animasyonunun boştaki ek işlemci yükü henüz ölçülmedi — bütçeyi aşarsa varsayılan "kapalı" yapılır.

Tek EXE / lisans etkisi: yeni sistem DLL importu WINHTTP.dll (Windows parçası); üçüncü taraf kod veya lisans eklenmez.

## ADR-028 — Açılışta zorunlu güncelleme (kendi yayın anahtarı, açık yayın deposu)

Durum: accepted (ürün sahibi istedi: güncel değilse güncellemeden çalışmasın, yalnız ilk açılışta kontrol; internet yoksa açılsın, uyarı versin; sunucu ayrı açık GitHub deposu)

Tarih: 2026-11-11

İlgili: EF-UPD, EF-X26 (REL-03/04/07/08), R22. Spec `docs/superpowers/specs/2026-11-11-zorunlu-guncelleme-design.md`.

Karar:

- Yalnız UI rolü, süreç başına bir kez, açılışta kontrol eder (≤8 sn, bağlanma ≤4 sn); periyodik kontrol, görev veya hizmet yok. Broker, bench, capture, cleanup ve `--recovery` ağa çıkmaz. Kontrol bitene kadar sistem yazan eylemler bekler.
- Manifest: `https://github.com/ykp61/Ecu-Fps-releases/releases/latest/download/manifest.json`; yönlendirmeler elle izlenir, hostlar yalnız `github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`; `url` öneki `…/Ecu-Fps-releases/releases/download/`.
- Güven zinciri: EXE'ye gömülü ECDSA P-256 genel anahtarı (`src/core/update/release_key.h`, key_id `rel-7e5df9066e6c47ad`) → imzalı manifest zarfı (domain `release`, `manifest_version` ile tekrar oynatma koruması) → boyut + SHA-256 → indirilen EXE. TLS ek katmandır. Gömülü anahtar yoksa güncelleme yok (`UnsignedBuild`).
- Yeni sürüm varsa tam ekran zorunlu güncelleme ekranı (Şimdi güncelle / Çık; Performans açık veya etkin kayıt varsa "Önceki ayarlara dön" — geri alma güncellemeye bağlı değildir). EXE `.new` → atomik değişim → yeni sürüm 20 sn içinde sağlıklı açılmazsa eski EXE geri gelir. Journal/ayar/test verisine dokunulmaz.
- **Çevrimdışı / sunucu hatası / imza geçersiz:** uygulama normal çalışır, sarı uyarı şeridi gösterir; geçersiz imzada indirme yapılmaz. Klasör yazılamıyorsa yönetici izni istenmez, indirme sayfası önerilir.
- Özel anahtar: ürün sahibinin bilgisayarında, depo dışında, DPAPI ile şifreli `%APPDATA%\EcuFpsRelease\release-key.dpapi` (+ aynı klasörde şifreli yedek). **Anahtar kaybolursa mevcut kurulumlar otomatik güncellenemez**; kullanıcılar yeni sürümü elle kurar. Anahtar `scripts/release_sign.py` ile üretilir ve imzalar; yayın `scripts/publish_release.ps1` (varsayılan prova, `-Publish` yalnız ürün sahibi onayıyla).

Geçersiz kılınan: ADR-026 X26-X30 satırındaki "ürün içinde güncelleme indirme yok" kısmı ve EF-X26 "imza sertifikası yokken release auto-update kapalı" kuralı. Lisans/mağaza/destek/duyuru kısmı ADR-026'daki gibi kapalı kalır. Authenticode (B-003) yerine geçmez: SmartScreen uyarısı sürer, sürüm "imzasız alpha" diye anılır.

Sınırlar ve açık işler: 0.1.0 kendini güncelleyemez; 0.2.0 bir kez elle kurulur. Açık deponun oluşturulması ve her yayın dışa açık eylemdir, her seferinde ürün sahibi onayı ister. Bu kayıt yazılırken depo oluşturulmadı, yayın yapılmadı, gerçek ağla uçtan uca güncelleme denenmedi.

Tek EXE / lisans etkisi: WINHTTP.dll (ADR-027 ile ortak) ve mevcut bcrypt.dll; yeni üçüncü taraf kod yok. İmzalama betiği geliştirici makinesinde Python `cryptography` paketini kullanır (dağıtılmaz).

## ADR-029 — Oyun ikonları: yeni salt okunur dosya yüzeyi (Steam kitaplık önbelleği, oyun EXE'sinin ikon kaynağı)

Durum: accepted (ürün sahibi istedi: "oyunlar kısmında oyun ikonları gözüksün, örnek Fortnite FO yazıyor, logosu yok")

Tarih: 2026-11-12

İlgili: spec `docs/superpowers/specs/2026-11-12-simetri-ve-oyun-ikonlari-design.md` §4; EXT-AC-01/02 (Oyunlar sayfası), EF-X11 (elle eklenen oyun).

Karar:

- Oyun kartındaki 32 DIP kutuda oyunun **gerçek** ikonu gösterilir. İkon bulunamazsa mevcut iki harfli kutu kalır. Genel/kabuk ikonu, yer tutucu veya indirilen görsel **asla** gösterilmez (`SHGetFileInfo`, `IShellItemImageFactory` kullanılmaz; ağ yok).
- Uygulamanın okuduğu dosya kümesi iki kaynakla genişler; ikisi de yalnız dosya okumadır:
  1. **Steam kitaplık önbelleği:** `<SteamPath>\appcache\librarycache\<appid>\` klasörü yalnız listelenir; adı tam `^[0-9a-f]{40}\.jpg$` olan **tek** dosya varsa o okunur. 0 veya birden çok dosya, yarım/reddedilen liste, rakam dışı appid → atlanır (kural kapalı tarafa düşer; biçim Valve tarafından belgelenmemiştir). Okuma sınırı 4 MiB; akış EOI'ye kadar tam olmalı (`jpeg_stream_complete`); yalnız Windows'un kendi JPEG kod çözücüsü (`CLSID_WICJpegDecoder`); kaynak kenarı ≤ 4096 px.
  2. **Çözülmüş oyun EXE'sinin ikon kaynağı:** `SHDefExtractIconW(yol, 0, 0, &hicon, nullptr, 64)`; dosya veri olarak okunur, çalıştırılmaz. `S_FALSE` (ikon kaynağı yok) → ikon yok; `E_FAIL` (erişilemedi) → ikon yok ve sonuç önbelleğe "yok" diye yazılmaz. EXE, launcher'ın bildirdiği kurulum klasörünün altında olmalıdır (elle eklenen oyunda kullanıcının seçtiği EXE). Epic (LaunchExecutable), elle eklenen, GOG/Ubisoft ve EXE'si çözülmüş Steam/Xbox satırları.
- Xbox/UWP paket logosu bu sürümde okunmaz (sağlayıcı doğrulanmadı) → harf kutusu.
- Yükleme izleme iş parçacığında, `collect_library_facts` içinde ve mevcut **derin kapı** altında yapılır: bir oyun çalışırken hiçbir ikon dosyasına (Steam önbelleği dahil) dokunulmaz; satır son bilinen ikonunu bellekten korur. Önbellek: anahtar = küçük harf yol + son yazma zamanı + boyut; kitaplıkta olmayan satır/dosya baştan sona derin çalışan her turda düşer (oyun çalışırken gelen eksik liste hiçbir girdiyi düşürmez). Çıktı 64 × 64 ön-çarpılmış BGRA (ikon başına 16 KiB; en çok 200 satır).
- Çizim: `Graphics::image`; hedefe bağlı `ID2D1Bitmap` önbelleği `release_target()` içinde bırakılır. Yüksek kontrastta ikon çizilmez (harf kutusu).

Değişmezlerle uyum: oyun sürecine erişim yok, süreç tutamacı açılmaz/yetkisi genişlemez, oyun belleği okunmaz, enjeksiyon/hook/overlay yok, hiçbir dosyaya yazılmaz, ağdan görsel indirilmez. Diske ikon kopyası yazılmaz (yalnız bellek). `HICON` her yüklemede `DestroyIcon` ile bırakılır (1000 yüklemede GDI/USER sayacı sabit: `ctest -R windows.game_icons`).

Kanıt ve sınırlar: saf kurallar birim testte (`tests/unit/games/test_game_icon.cpp`); yükleyici `ef_game_icon_probe` ile (uygulamanın kendi EXE'si → 64 × 64 görünür alfa; ikonsuz ikili → boş; kesik/büyük/JPEG olmayan dosya → boş; önbellek davranışı); çizim `ui_snapshot` "icons" durumunda kodla üretilmiş piksellerle (gerçek kurulu oyuna bağlı değil). Bu bilgisayarda yalnız Steam önbellek klasörünün biçimi gözlendi (appid 730: tek 40 hex `.jpg`). **Cihazda doğrulanmadı:** Fortnite (Epic) satırı, ikincil Steam kitaplığındaki oyun, korumalı oyun çalışırken derin kapı, %100/%150/%200 ölçek — uygulama ürün sahibinin bilgisayarında açılınca gözle doğrulanacak. Bilinen sınırlar: Steam ikonu 32 × 32 JPEG'dir (%100 üstünde yumuşak görünür); bazı oyunların EXE ikonu motorun varsayılan ikonudur (gerçek ama ayırt edici değil).

Tek EXE / lisans etkisi: yeni DLL importu yok (`SHELL32.dll`, `ole32.dll` zaten içe aktarılıyor; WIC yalnız COM ile kullanılır). Üçüncü taraf kod veya codec eklenmez.

## ADR-030 — Windows ile başlatma varsayılan açık (tepside)

Durum: accepted (ürün sahibi isteği; spec `docs/superpowers/specs/2026-11-12-canli-arayuz-ve-guncellemeler-design.md` G ve B7)

Tarih: 2026-11-13

İlgili görevler: SEC-05 (tek örnek, `--tray`), UI-06 (tepsi), R20 (kaldırma), spec B7 (Ayarlar listesi).

Sorun ve mevcut kanıt: ADR-013 açılışta başlamayı yalnız kullanıcı açarsa izin veriyordu (değer `Ecu Fps`). Ürün sahibi uygulamanın oturum açılışında tepside hazır beklemesini varsayılan olarak istedi.

Değerlendirilen seçenekler: (1) eskisi gibi kapalı varsayılan; (2) ilk açılışta Evet/Hayır sorusu; (3) varsayılan açık + ilk açılışta bir kez engellemeyen bildirim + tek dokunuşla kapatma. Gizli servis, zamanlanmış görev, HKLM veya Startup klasörü değerlendirme dışıdır.

Seçim ve gerekçe: (3). Evet/Hayır kuralının bu tek kayıt için bilinçli istisnasıdır: değişiklik yalnız uygulamanın kendi görünür başlangıç kaydıdır (Windows başlangıç listesinde ve Görev Yöneticisi'nde görünür), sistem ayarı değildir, yönetici gerektirmez ve tek dokunuşla geri alınır.

- Kayıt: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, değer `EcuFps` (REG_SZ) = `"<exe yolu>" --tray`. Eski ad `Ecu Fps` açılışta ve kaldırmada silinir.
- İlk çalıştırma (`preferences.json` içinde `autostart_initialized` yok — yeni kullanıcı ya da bu sürümden önceki dosya): tercih açık, kayıt yazılır ve geri okunur; başarılıysa ilk açılan pencerede bir kez, üst menünün altında tek satır "Windows açılışında tepside başlar" + "Kapat" (anahtarı kapatır). `--tray` ile başlandıysa bildirim pencere ilk açıldığında gösterilir.
- Sonraki açılışlar: tercih açık ve kayıt bu EXE'yi gösteriyorsa dokunulmaz; başka yolu gösteriyorsa (EXE taşınmış) yeni yola yeniden yazılır; kayıt uygulama dışında silinmişse kullanıcının kararı sayılır, tercih kapanır ve yeniden yazılmaz. Tercih kapalıyken kayıt varsa silinir ve bir daha yazılmaz.
- Ayarlar → "Windows ile başlat" anahtarı: aç = yaz + geri oku; kapat = sil + geri oku. Yazma/doğrulama başarısızsa anahtar kapalı görünür ve neden satırı yazılır (kayıt yarım bırakılmaz); silme başarısızsa kayıt durduğu için açık görünür ve neden yazılır. Kayıt okunamazsa hiçbir şey yazılmaz, anahtar devre dışıdır (TH-01).
- Kaldırma akışı (R20) yeni ve eski adlı kayıtları siler ve yokluğunu geri okur.
- `--tray` ile başlayan örnek pencere açmaz; zorunlu güncelleme denetimini yine bir kez yapar (ADR-028).
- Smoke, arka plan test koşuları (`EF_TEST_BACKGROUND=1`) ve ctest altındaki her test (`EF_AUTOSTART_READONLY=1`; ön plan pencere testleri `EF_TEST_BACKGROUND`'u kaldırır) açılışta kayda yazmaz, yalnız okur. Kayıt okunamazsa ilk çalıştırma işaretlenmez (sonraki açılışta varsayılan açık yine uygulanır). Kural `core/settings/autostart.*` içinde saf koddur; kayıt erişimi `RunKeyStore` arabirimindedir (`platform/windows/run_key_store.*`); birim testleri sahte depo kullanır ve gerçek kayıt defterine dokunmaz (`tests/unit/core/test_autostart.cpp`: ilk çalıştırma, kapat/aç, taşınmış EXE, dış silme, yazma ve doğrulama hatası, okuma hatası, kaldırma).

Tek EXE / hafiflik / geri dönüş / lisans etkisi: yeni DLL yok (`ADVAPI32` zaten içe aktarılıyor). Tepside bekleme bütçesi docs/02 hafiflik hedeflerine tabidir (değişmedi). Geri dönüş: anahtarı kapatmak veya kaldırma akışı kaydı siler. Lisans etkisi yok.

Gerekli testler: birim (yukarıdaki); Windows'ta gerçek oturum açılışında tepside başlama ve Görev Yöneticisi başlangıç listesinde görünme ürün sahibinin bilgisayarında gözle doğrulanacak (**cihazda doğrulanmadı**).

Geçersiz kılınan karar: ADR-013'ün "açılışta başlama yalnız kullanıcı açarsa" kısmı (görünür tepsi ve tek örnek aynen geçerli).

Kaynaklar: Microsoft Learn, "Run and RunOnce Registry Keys".

## ADR-031 — Güncellemeler sayfası: Windows Update sürücü tespiti ve resmî üretici bağlantıları

Durum: accepted (ürün sahibi onayı; spec `docs/superpowers/specs/2026-11-12-canli-arayuz-ve-guncellemeler-design.md` H, "A" seçeneği)

Tarih: 2026-11-13

İlgili görevler: EF-X03 (WIN-07), EF-X04/X05/X06 (INV-02, INV-06), spec H (tespit) ve H2 (kurulum, ayrı bölüm).

Sorun ve mevcut kanıt: ADR-026 X03 sürücüyü "yalnız teşhis + resmî bağlantı" ile sınırlıyor, X04–X06 "güncel / güncelleme var denmez" diyordu; çünkü güvenilir bir sürüm kaynağı yoktu. Ürün sahibi sürücü güncellemelerinin uygulamada görünmesini istedi. Windows Update Agent (WUA) yükseltilmemiş bir kullanıcı sürecinde `IsInstalled=0 and Type='Driver' and IsHidden=0` aramasını yapabiliyor: bu bilgisayarda 2026-11-12'de ~8 sn'de 2 öneri, 2026-11-13'te `ef_wua_search_probe` ile 6,5 sn'de aynı 2 öneri döndü (Apple USBDevice 552.0.0.0, Microsoft AudioProcessingObject 1.0.4.55794; sınıf `OtherHardware`). Kaynak Microsoft'un aygıt kimliğiyle eşleştirip imzaladığı sürücülerdir.

Değerlendirilen seçenekler: (A) yalnız Windows Update + üreticinin resmî sayfasına kullanıcı tıklamalı bağlantı; (B) NVIDIA / AMD / Intel / anakart sitelerinden otomatik sürüm sorgusu (site koşulları, kazıyıcı, imza/kaynak belirsizliği); (C) üçüncü taraf sürücü havuzu (kaynağı belirsiz). Ürün sahibi A'yı seçti.

Seçim ve gerekçe: (A).

- Tespit `core/drivers/driver_updates.*` (saf; WUA çağrısı yok) + `platform/windows/wua/wua_search.*` (WUA `IUpdateSession` → `IUpdateSearcher::BeginSearch`, MTA, `RequestAbort`). Arama arka plan iş parçacığındadır (`DriverScanner`), iptal edilebilir, 120 sn zaman aşımı vardır. Yalnız kullanıcı "Denetle"ye bastığında ya da sayfa ilk açıldığında çalışır; oyun açıkken başlamaz, oyun açılınca durur.
- Durumlar: Denetlenmedi, Denetleniyor, Güncel (= "Windows Update'e göre güncel"; "en yeni" denmez), "N güncelleme bulundu", Denetlenemedi (+ neden: çevrimdışı, hizmet kapalı, ilke/WSUS, zaman aşımı, durduruldu, hata kodu) + "Yeniden dene". Başarısız / zaman aşımına uğramış / süresi dolduktan sonra gelen sonuç hiçbir zaman boş bir "Güncel" ya da yarım liste olarak gösterilmez.
- Firmware sınıfı (`{f2e7dd72-6468-4e36-b6f1-6488f42c1b52}`, sınıf adı `Firmware`, `UEFI\RES_*`) listede "Elle denetlenir" olarak durur; kurulabilir sayılmaz. Uygulama BIOS / firmware yazmaz.
- Ekran kartı, yonga seti ve BIOS satırları envanterden (salt okuma) gelir; "Elle denetlenir" + "<Üretici> sayfası" düğmesi. Adres yalnız sabit resmî sayfalardır (NVIDIA / AMD / Intel indirme merkezi; ASUS / MSI / GIGABYTE / ASRock destek), `action_uri_allowed` izin listesinden geçer ve yalnız kullanıcı tıklayınca varsayılan tarayıcıda açılır. Üretici sitelerine otomatik sorgu yoktur; tanınmayan üreticiye bağlantı uydurulmaz.
- Ağ ilkesi: bağlantıyı Windows Update hizmeti kurar; uygulamanın kendi açtığı bağlantı listesi (ADR-027/028 `kNetHosts`) değişmez. Arama klasörü WinHTTP ve adres içermez (`tests/unit/core/test_net_policy.cpp`).
- Kurulum (H2) bu kararla açılan ama bu sürümde **yapılmayan** yoldur: tek Evet/Hayır → UAC → yükseltilmiş yardımcı (UpdateID + RevisionNumber ile yeniden arar, oyun kilidi ve Firmware reddi, geri yükleme noktası, geri okuma, Geçmiş kaydı). `tests/windows/check_no_install.ps1` WUA indirme/kurulum arabirimlerine (`IUpdateInstaller`, `IUpdateDownloader`, `CreateUpdateInstaller`, `CreateUpdateDownloader`) yalnız yardımcının tek klasöründe (`src/platform/windows/wua_install/`) izin verir; arama klasörü dahil başka her yerde yasaktır. Diğer kurulum API'leri (SetupAPI/DIFx/pnputil/msiexec/BITS) her yerde yasak kalır.

Tek EXE / hafiflik / geri dönüş / lisans etkisi: yeni DLL yok (`wuapi` COM sunucusu System32'de; `ole32` / `oleaut32` zaten içe aktarılıyor). Arama yalnız istek üzerine ve arka planda; boşta yük yok. Tespit hiçbir şey yazmaz (geri dönüş gerekmez). Lisans etkisi yok (Windows API'si).

Gerekli testler: `tests/unit/core/test_driver_updates.cpp` (bütün durumlar, zaman aşımı, iptal, geç başarı, firmware kuralı, sıralama, sürüm/ad ayrıştırma, HRESULT sınıflandırma, resmî sayfalar), `test_net_policy` (arama klasörü), `test_hit_ids` (bağlantı dağıtımı), `ef_ui_snapshot` (Güncellemeler durumları). Gerçek salt okuma arama bu bilgisayarda koşuldu (yukarıda). Kurulum yolu **cihazda doğrulanmadı** (H2; ürün sahibinin Evet/Hayır + UAC ile yapacağı kabul adımı).

Geçersiz kılınan karar: ADR-026 tablosundaki **X03** satırı ("sürücü yalnız teşhis + resmî bağlantı"; kurulum yasağı H2 yardımcısı dışında sürer) ve **X04–X06** satırındaki "'Güncel', 'güncelleme var' ... denmez" cümlesi (yalnız Windows Update sonucu için; üretici katalogları boş kalır, kazıyıcı yok).

Kaynaklar: Microsoft Learn, "Windows Update Agent API" (`IUpdateSearcher::BeginSearch`, `ISearchJob::RequestAbort`, `IWindowsDriverUpdate`), "System-Defined Device Setup Classes" (Firmware sınıfı GUID'i), "Windows Update error codes".

## ADR-032 — Güncellemeler sayfası: üretici sürücüsü uygulama içinden (yalnız resmî, imzalı paket; dışarı yönlendirme yok); BIOS yalnız bilgi

Durum: accepted (ürün sahibi kararı 2026-11-13: "Elle denetlenir + üretici sayfası" yerine uygulama içinde bul, indir, kur; BIOS yazılmaz)

Tarih: 2026-11-13

İlgili: spec `docs/superpowers/specs/2026-11-12-canli-arayuz-ve-guncellemeler-design.md` H ve H2, ADR-031.

Sorun ve kanıt: Ürün sahibi ekran kartı / yonga seti satırlarının kullanıcıyı üretici sayfasına göndermesini istemiyor; uygulama en yeni resmî sürücüyü bulmalı, indirmeli, doğrulamalı ve kurmalı. 2026-11-13 araştırması (yalnız resmî belgeler; hiçbir paket indirilmedi): **hiçbir üretici** üçüncü tarafın kullanabileceği belgeli bir "en yeni sürücü" API'si yayımlamıyor. NVIDIA site koşulları 3.2(f) robot / kazıyıcı / izleme aracını, Intel kullanım koşulları 4.1.14/4.1.16/4.1.17 bot, metin madenciliği ve "sistematik indirme"yi yazılı izin olmadan yasaklıyor; AMD indirme sunucusu amd.com yönlendirmesi olmayan isteği reddediyor ve indirme bağlantısına tıklamayı kullanıcının EULA kabulü sayıyor. Üçünde de sonuç **FALLBACK**.

Seçim (sapma kaydıyla):

- **Bulma = Windows Update** (araştırmanın ilk önerisi; ADR-031 araması). Ekran kartı satırı, aynı PCI üreticisinin (`VEN_xxxx`) Windows Update ekran kartı önerisiyle, yonga seti satırı yonga seti önerisiyle eşlenir. Eşleşme varsa satır "Güncelleme var <sürüm>" + **İndir ve kur** gösterir ve ADR-031 H2 yolunu kullanır (uygulama içi Evet/Hayır → UAC → yardımcı UpdateID + RevisionNumber ile yeniden arar, indirir, kurar, sonucu ve yeniden başlatma gereğini geri okur, journal). Eşleşen öneri ayrı satır olmaz. Eşleşme yoksa çip "Güncel" (= Windows Update'e göre; HelpText "üreticinin daha yeni sürümü olabilir"), "Denetlenmedi", "Denetleniyor…" ya da "Denetlenemedi".
- **Üreticinin resmî paketi (yalnız ekran kartı; NVIDIA / AMD / Intel):** **Dosyadan kur**. Kullanıcı paketi kendisi indirir (koşullara uygun tek yol); uygulama paketi `%LOCALAPPDATA%\EcuFps\drivers\<16 hex>.part`'a kopyalar (ilerleme, Durdur, 1 MiB–3 GiB sınırı, yarım kopya hiçbir zaman tamam sayılmaz ve açılışta / yeni seçimde silinir), `.exe` yapar, imza + yayıncıyı ön denetler, uygulama içi Evet/Hayır (dosya · boyut · "İmza: <yayıncı> (doğrulandı)" · "Yönetici izni istenir… yeniden başlatma gerekebilir") → UAC → yardımcıda **tek yeni izinli işlem** `OperationId::VendorDriverInstall`, `RequestAction::VendorInstall`, hedef `vdrv:<nvidia|amd|intel>:<16 hex>`, değer `install`. Hedef yol taşıyamaz (dosya adı kimlikten kurulur). Yardımcı arayüze güvenmez: dosyayı yalnız-okuma paylaşımıyla açar (bağlantı noktası / dizin reddedilir), makine journal kökünün `vendor_pkg` alt klasörüne (yönetici ACL'i, `verify_machine_journal_dir`) kopyalar, boyutu yeniden denetler, **kopya üzerinde** WinVerifyTrust (çevrimdışı) + imzalayan sertifika konu adı **tam eşitlik** ("NVIDIA Corporation", "Advanced Micro Devices, Inc.", "Intel Corporation") ister, oyun kapısı, ayrı `vendor_drivers` makine journal'ında kalıcı başlangıç kaydı, geri yükleme noktası, oyun kapısı, kurulu sürüm (DXGI UMD) okunur, paket resmî sessiz anahtarlarla çalışır (NVIDIA `-s -n Display.Driver`; AMD `-INSTALL`, `-boot` yok; Intel `-s`, `--overwrite` yok), çıkış kodu eşlenir (NVIDIA 0/1=yeniden başlat; Intel 0, 3010/14 yeniden başlat, 2/5/7/9 ayrı neden; AMD 0, 3010), sürüm geri okunur, journal kapanır, kopya silinir. Başlamış kurulum kesilmez (45 dk üst sınır → "zaman aşımı", kayıt RecoveryRequired).
- **Yonga seti:** AMD yonga seti paketinin resmî sessiz anahtarı yok, Intel yonga seti araştırılmadı → yalnız Windows Update yolu; düğme yok, HelpText bunu söyler.
- **BIOS:** yalnız bilgi (SMBIOS sürüm + tarih, "Yalnız bilgi" çipi, satırda soluk "BIOS güncellemesi bu uygulamadan yapılmaz"). Dış sayfa düğmesi yok; eski "Elle denetlenir" ve resmî sayfa notu UIA HelpText'te kalır. Uygulama BIOS / firmware yazmaz (değişmez).
- **Dışarı yönlendirme kalktı:** `UpdatesLink` kimliği ve `gpu/chipset/board_vendor_page` korunur ama cizilmez.

Sapmalar (görev metnine göre): (1) Uygulama üretici sitesinden **sürüm sorgulamaz ve paket indirmez**; "en yeni resmî sürücü" Windows Update'in önerisidir. HTTPS indirme / host izin listesi kodu eklenmedi (kullanılacak uyumlu kaynak yok; ölü ağ kodu ve yeni ağ yüzeyi açılmadı, `kNetHosts` değişmedi). (2) Araştırma ürün başına bir salt-okuma sorgusuna izin verse de koşullar otomatik sorguyu yasakladığı için **hiçbir prob çalıştırılmadı**. (3) "İndirme" testleri (boyut sınırı, iptal) kopya aşamasında karşılanır.

Doğrulanmayanlar: yayıncı adları gerçek bir paket üzerinde okunmadı (state/PENDING_USER.md K25, adım 2); NVIDIA tam paketinin `-s -n` anahtarını kendi açıcısına geçirdiği, AMD ve yeni Intel paketlerinin çıkış kodları cihazda doğrulanmadı. Kurulum yolu bu bilgisayarda **çalıştırılmadı**.

Tek EXE / hafiflik / geri dönüş / lisans: yeni DLL yok (`crypt32` System32; `wintrust`, `bcrypt`, `dxgi` zaten içe aktarılıyor). Boşta yük yok (yalnız kullanıcı tıklayınca). Geri dönüş: geri yükleme noktası + journal kaydı; üretici paketi kendi kaldırıcısıyla kaldırılır (uygulama sürücü kaldırmaz). Lisans: paket kullanıcının kendi indirdiği resmî pakettir; uygulama dağıtmaz / yeniden barındırmaz.

Testler: `tests/unit/core/test_vendor_install.cpp` (hedef biçimi / klasör dışı yol reddi, imza + yanlış yayıncı reddi, anahtarlar, çıkış kodları + yeniden başlatma, boyut sınırı, iptal, okuma/yazma hatası, oyun kapısı, journal, zaman aşımı, ayrıntı kodlaması, broker izin listesi / eylem eşleşmesi / kapı, istemci bildirimi), `test_hit_ids` (dış bağlantı yok, `updates.local.N`, Windows Update eşlemesi), `ef_ui_snapshot` (güncelleme var, güncel, kopyalanıyor, imza denetleniyor, kuruluyor, kuruldu + yeniden başlat, yanlış imza, başarısız kod, yonga seti / BIOS satırları; HC, 960x540, dar), `tests/windows/check_no_install.ps1` (paket başlatma ve imzalayan okuma yalnız `src\platform\windows\vendor_install\`; arabirimi yalnız `broker_channel.cpp`; sessiz anahtarlar yalnız `src\core\drivers\vendor_install.*`; DiInstallDriver / pnputil / msiexec / BITS yasağı sürer).

Geçersiz kılınan karar: ADR-031'in "Ekran kartı, yonga seti ve BIOS satırları … 'Elle denetlenir' + '<Üretici> sayfası' düğmesi" maddesi ve spec H'nin BIOS "üretici destek sayfası düğmesi" cümlesi. ADR-031'in Windows Update tespiti / kurulumu ve "uygulama BIOS / firmware yazmaz" kuralı aynen geçerli.

Kaynaklar: NVIDIA Terms of Service; NVIDIA driver license; NVIDIA data-center driver installation guide (Windows, sessiz kurulum / çıkış kodları); AMD "Download incomplete" SSS; AMD komut satırı kurulum PDF'i; AMD site koşulları ve EULA; Intel Terms of Use; Intel 000006773 (komut satırı kurulum), 000054972 (çıkış kodları), 000056649 (DSA); Microsoft Learn WUA arama / indirme / kurulum akışı.


## ADR-033 — Kurulum (aynı EXE setup) ve LocalSystem yardımcı hizmet; taşınabilir dağıtımın sonu (0.3.0)

Durum: accepted (ürün sahibi onayı, 2026-11-13, canlı maketten)

Tarih: 2026-11-13

İlgili: ADR-001, ADR-002, ADR-023 (broker), ADR-028 (zorunlu güncelleme), ADR-030 (Run kaydı), REC-06/R20 (kaldırma), SEC-01..05. Spec `docs/superpowers/specs/2026-11-13-setup-ve-yardimci-hizmet-design.md`, plan `docs/superpowers/plans/2026-11-13-setup-0.3.md`.

Sorun ve mevcut kanıt: Taşınabilir EXE her yönetici ayarında (Performans Açık/Kapalı, Oyun Hazırlığı düzeltmeleri, sürücü kurulumu) ayrı UAC istiyor; ürün sahibi bunu kullanıcı deneyimi açısından kabul etmedi. Taşınabilir klasör kullanıcıya yazılabilir olduğundan yükseltilmiş bir yardımcıya kalıcı güven verilemez; güvenli kalıcı yardımcı için yalnız yöneticinin yazabildiği bir kurulum yeri gerekir.

Değerlendirilen seçenekler: (1) bugünkü gibi işlem başına UAC; (2) zamanlanmış görev ile yükseltilmiş başlatma (görev tanımı da yönetici kurulumu ister, istemci doğrulaması zayıf); (3) MSI/üçüncü taraf kurulum çatısı (tek EXE ve bağımlılık kuralına aykırı); (4) aynı EXE'nin setup rolü + Program Files + LocalSystem hizmet, pipe istemcisi kurulu EXE yoluyla doğrulanır.

Seçim ve gerekçe: (4).

- Dağıtım tek dosyadır: `EcuFps-Setup-<v>.exe`, ürün EXE'sinin aynısı. Kurulum yeri dışında argümansız açılınca veya `--setup` ile kurulum penceresi açar; Kur → Evet/Hayır → tek UAC → yükseltilmiş aynı EXE (`--role=setup`) journal'lı, adım adım geri alınabilir kurulum yapar: `Program Files\Ecu Fps\EcuFps.exe` (yalnız yönetici yazar), Başlat menüsü kısayolu, ARP kaydı (kaldırma komutu `--uninstall`), hizmet, `ProgramData\EcuFps\updates` (yalnız SY/BA). HKCU Run yükseltilmemiş süreçte yazılır. Kullanıcı verisi `%LOCALAPPDATA%\EcuFps`'te kalır.
- Hizmet `EcuFpsHelper` (`--service`, LocalSystem, otomatik-gecikmeli). Pipe `\.\pipe\EcuFps.Helper.v1` (ilk örnek, uzak istemci reddi, etkileşimli kullanıcı DACL'i). Her bağlantıda istemci süreci: görüntü yolu kurulu EXE, görüntü dosyası o yoldaki dosyanın kendisi (birim+dosya kimliği), oturum ≠ 0, INTERACTIVE grubu, hesap BUILTIN\Administrators (S-1-5-32-544) üyesi (K27); kurulum klasörü ACL'i güvenli değilse hizmet her isteği reddeder. Yalnız `admin_operation_allowed()` izin listesi (mevcut `AdminExecutor`, aynı makine journal'ları ve oyun kapısı) ve sabit güncelleme çerçeveleri yürütülür; serbest komut/yol yok, hizmet ağa çıkmaz. Her işlem yine uygulama içi Evet/Hayır'dan geçer. Hizmet yok/bozuksa istemci, istek göndermeden önce bugünkü UAC broker'a geri düşer.
- Zorunlu güncelleme kurulu kopyada: Ui indirir ve doğrular, hizmete akıtır; hizmet imzalı manifesti, tekrar oynatma tabanını, boyutu ve SHA-256'yı yeniden doğrular, mevcut `run_swap`/`recover_swap` ile değiştirir; yeni Ui sağlık sinyali verene kadar `.old` saklanır, vermezse eski EXE geri gelir.
- Kaldırma: önce değişen ayarlar geri yüklenir (biri bile başarısızsa hiçbir şey silinmez); sonra tek UAC ile hizmet, ARP, kısayol, `ProgramData\EcuFps`, dosyalar (çalışan EXE yeniden başlatmada); sonra Run kaydı ve kullanıcı verisi.
- Taşınabilir ZIP yayımlanmaz. 0.2.x taşınabilir kopya zorunlu güncellemeyle 0.3.0'a gelir ve kurulum önerir.

**2026-11-13 owner decision K27: service clients limited to Administrators group members; standard accounts use UAC broker.** Hizmet yalnız belirtecinde BUILTIN\Administrators (S-1-5-32-544) grubu bulunan istemciye hizmet verir (UAC ile süzülmüş belirteçte deny-only giriş de sayılır; `CheckTokenMembership` kullanılmaz). Standart hesap Hello'dan önce reddedilir (`not_admin`); UI de standart hesapta hizmete hiç bağlanmaz ve bugünkü UAC broker'a düşer (yönetici kimlik sorusu). Zorunlu güncelleme standart hesapta 0.3 öncesi gibi `NotWritable` (indirme sayfası; kurulum UAC ile yönetici kimliği ister). Hizmet böylece hiçbir standart hesaba yeni yetki vermez.

Bilinçli kabul edilen kalan risk (K27 ile daraltıldı): yönetici grubundaki bir hesapta çalışan kötü amaçlı bir süreç kurulu uygulamaya kod enjekte ederse izin listesindeki işlemleri UAC penceresi olmadan isteyebilir. O hesap zaten UAC onayıyla yükselebilir; standart hesaplar bu yoldan yararlanamaz. Etki izin listesindeki sabit, journal'lı ve geri alınabilir değerlerle ve imzası doğrulanan sürücü paketleriyle sınırlıdır; keyfi kod çalıştırılamaz.

Tek EXE / hafiflik / geri dönüş / lisans etkisi: yeni DLL yok (SCM `advapi32`, kısayol `ole32`/`shell32` sistem DLL'leri). Hizmet boşta pipe bekler; boşta CPU ve bellek bütçesi docs/02 hafiflik hedeflerine eklenir ve ölçülür (ölçülmeden hedef karşılandı denmez). Geri dönüş: kurulum journal'ı + adım başına geri alma; kaldırma her şeyi siler. Üçüncü taraf kurulum çatısı yok, lisans etkisi yok.

Gerekli testler: sahte `SetupOps`/journal ile kurulum-kaldırma işlem motoru (her adımda hata ve güç kesintisi), istemci doğrulama kararı, hizmet oturumu, güncelleme alıcısı ve denetçisi, kanal geri düşmesi, kurulum penceresi ekran dışı görüntüleri. Gerçek UAC, SCM, HKLM, Program Files, ARP, kısayol, hizmetle güncelleme ve kaldırma ürün sahibinin gerçek turunda (**cihazda doğrulanmadı**).

Geçersiz kılınan karar: ADR-001/ADR-002'deki "tek taşınabilir EXE, kalıcı servis yok" dağıtım kısmı (aynı EXE/rol modeli, statik CRT ve loose dosya yasağı geçerli); ADR-023'ün işlem başına UAC broker'ın varsayılan yol olması (broker geri düşme yolu olarak kalır); ADR-028'in "periyodik kontrol, görev veya hizmet yok" cümlesindeki "hizmet" kısmı yalnız EXE değişimi için (kontrol yine yalnız Ui'de, açılışta bir kez).

Kaynaklar: Microsoft Learn — "CreateServiceW", "SERVICE_FAILURE_ACTIONS_FLAG", "Named Pipe Security and Access Rights", "GetNamedPipeClientProcessId", "ImpersonateNamedPipeClient", "Uninstall Registry Key", "IShellLinkW", "MoveFileExW (MOVEFILE_DELAY_UNTIL_REBOOT)".

## ADR-034 — İmzasız alpha istisnası (EF_UNSIGNED_ALPHA)

Durum: accepted (ürün sahibi kararı "İmzasız alpha istisnası", 2026-11-13)

Tarih: 2026-11-13

İlgili: SEC-04 (`src/core/security/trust.{h,cpp}`), ADR-033 (kurulum işçisi), B-003 (kod imzalama ertelendi), K26 (gerçek kurulum turu).

Bağlam: Kod imzalama (Authenticode) ertelendi (B-003); dağıtılan x64-dist ikilisi imzasız. SEC-04 yayın ilkesi (`EF_RELEASE_TRUST=ON`) yükseltilmiş rollerde (Broker, CaptureWorker, CleanupWorker, SetupWorker) yalnız geçerli imzayı kabul ettiği için her yönetici adımı UAC'den hemen sonra `kExitUntrusted` (14) ile çıkıyordu. Ürün sahibinin gerçek turunda (2026-11-13) `EcuFps-Setup-0.3.0.exe` UAC onayından sonra "Kurulum tamamlanamadı / Kurulum sonucu doğrulanamadı" gösterdi, tüm adımlar "Bekliyor" kaldı ve hiçbir şey kurulmadı (pencere gerçek nedeni de göstermedi).

Karar: Yeni CMake seçeneği `EF_UNSIGNED_ALPHA` (CMakeLists.txt varsayılanı OFF; `x64-dist` preset'inde ON; `EF_RELEASE_TRUST` gibi derleme tanımı). Açıkken `broker_trust(Release, Unsigned)` izin verir ve kararı `unsigned_alpha=true`, neden "release unsigned alpha: no signature present" olarak etiketler; yükseltilmiş rol `EF-UNSIGNED-ALPHA` hata ayıklama işaretini yazar (dev işareti değil). Geçerli imza izinli kalır; **Invalid (kurcalanmış imza), Untrusted ve Unknown her derlemede reddedilir.** Dev ilkesi değişmedi; `dev_bypass_marker` yayın ikilisinde yine yok. Parent-aynı-EXE, yükseltme ve nonce denetimleri aynen sürer. Kurulum işçisinin hazırlık kopyası denetimi (`win_setup_ops.cpp`) aynı kararı kullanır. Kurulum penceresi, istek çerçevesi gönderilmeden biten işçiyi (erken çıkış, pipe/hello hatası) artık "başlatılamadı; hiçbir şey değişmedi" (NotStarted) olarak, `kExitUntrusted` için ayrı "imzayı kabul etmedi" metniyle ve teknik ayrıntı satırıyla gösterir; istekten sonra yanıt yoksa yine "sonuç doğrulanamadı".

Risk (bilinçli kabul): (1) İleride imzalı bir derlemenin imzasını söküp dağıtan bir saldırgan bu istisnadan geçer; bu yüzden **kod imzalama mevcut olduğu gün `x64-dist` preset'inde `EF_UNSIGNED_ALPHA` OFF yapılmalıdır**. (2) İmza denetimi zaten EXE'nin kendi içinde çalışır; kurcalanmış bir EXE denetimi yamalayarak kaldırabilir. Bu alpha döneminde gerçek kapılar UAC onayı (yönetici kullanıcının bilinçli izni) ve yükseltilmiş sürecin aynı EXE'den başlatıldığının doğrulanmasıdır (parent-same-exe + nonce); imza denetimi ek katmandır, tek kapı değildir. (3) Kanıt ve sürüm notu imzasız alpha olarak etiketlenir; imzalı yayın gibi sunulmaz.

Kaldırma: `CMakePresets.json` x64-dist'te `EF_UNSIGNED_ALPHA` satırını sil/OFF yap; `tests/windows/check_trust.ps1` (`-UnsignedAlpha off`) ve birim testi (`release_unsigned_only_with_unsigned_alpha_flag`) imzasız yayın ikilisinin reddedildiğini doğrular. İsteğe bağlı olarak seçenek, `unsigned_alpha` alanı ve `unsigned_alpha_marker()` kaynaktan tamamen silinir.

Gerekli testler: birim (`test_trust.cpp`: alpha açık/kapalı iki yönlü `#if`, Invalid/Untrusted/Unknown reddi; `test_setup_flow.cpp`: istek öncesi hata → NotStarted + temiz, yanıtsız istek → ResultUnknown); `windows.trust` (probe `--trust` kararı, kurcalanmış imza reddi, işaret varlığı); ekran dışı kurulum penceresi durumları. Gerçek UAC ile kurulum ürün sahibinin turunda (**cihazda doğrulanmadı**).

### ADR-033 ek notu — Pencere kapatma, tepsi Çık ve hizmetin kalıcılığı (2026-11-17)

- Pencere X düğmesi uygulamayı kapatmaz, tepsiye gizler. Tepsi menüsündeki Çık kullanıcı arayüzünü kapatır; ayarları geri almaz (Kapalı ayrı bir eylemdir).
- `EcuFpsHelper` hizmeti bilerek kalıcıdır (LocalSystem, ADR-033). Arayüz kapansa da çalışır; Hizmetler (services.msc) üzerinden durdurulur ya da kaldırmayla silinir. Hizmet zorla sonlandırılırsa SCM 5 sn sonra yeniden başlatır (hata eylemleri, başlatma türü ve DACL değiştirilmez).
- Yükseltilmiş yardımcı roller (broker, yakalama, temizlik, kurulum) başlatılırken `logs/exit.log` içine rol, çocuk PID ve sonuç kodu yazılır; "bu yükseltilmiş süreç nereden geldi" sorusu bu satırlardan izlenir. Temizlik işçisi 10 dakika hiç istek almazsa temiz çıkış yapar.

### ADR-033 ek notu (0.8.2): hizmet hata eylemleri kendi kendine onarilir
`EcuFpsHelper`, acilista kendi hata eylemleri politikasini (3xRESTART, 5 sn, sifirlama 3600 sn, cikis kodu hata sayilir) okur ve sapma varsa LocalSystem olarak onarir (`service/failure_actions.cpp`). Gerekce: guncelleme yeniden baslatmasi hizmeti kStopUpdateRestart ile durdurup SCM'in gunluk limitine takiliyordu; hizmet durunca Performans her seferinde UAC istiyordu. Kapsam: yalniz SERVICE_CONFIG_FAILURE_ACTIONS(+FLAG); hizmetin yapabildigi islem listesi degismedi. `check_no_install.ps1` izin listesine yalniz bu dosya eklendi (sahip onayi).

## ADR-035 — Rakip boşluk özellikleri: sahip kararıyla ürün kurallarının genişletilmesi (2026-11-17)

**Durum:** Kabul edildi (sahip kararı, 2026-11-17). AGENTS.md ürün değişmezleriyle çeliştiği yerde bu karar geçerlidir.

**Bağlam.** Rakip eksik analizi (`docs/superpowers/plans/2026-11-17-eksik-ozellikler.md`) bizde olmayan ya da yarım kalan özellikleri listeledi. Bunların bir kısmı daha önce kendi kurallarımızla yasaktı (güvenlik dışlaması, kalıcı hizmet kapatma, debloat, gizlilik ayarları, Windows Update denetimi, BIOS yazımı).

**Karar.**

1. Rakipte olup bizde olmayan her özellik uygulanır. **Kapsam dışı (yapılmayacak):** bulut yedek; altı ayrı sentetik önce/sonra skoru; aim trainer; Android uygulaması; FiveM paketi; ücretli uzaktan destek; ortaklık/referans programı; Trustpilot; program dışı elle destek/hizmet işleri.
2. Daha önce yasak olup artık **izinli** olanlar: Defender oyun klasörü dışlamaları; BIOS/XMP rehberi ve BIOS yazımı; geniş ayar kataloğu; hizmet/özellik kapatma sihirbazı; debloat; Windows Update denetimi; bekleme belleği boşaltma; oyun CPU önceliği; gizlilik/telemetri ayarları; Koruma Modu (yeniden uygulama).
3. **Korunan değişmezler:** her yazma journal'lı, geri okunur ve doğrulanabilir biçimde geri alınır; kullanıcı Evet/Hayır ile onaylar (sessiz yazma ve sessiz yeniden uygulama yok); anti-cheat atlatma yok, oyuna enjeksiyon yok, oyun belleği okunmaz, oyun sürecine tutamaç açılmaz; ölçülmemiş/uydurma sayı gösterilmez. Defender, güvenlik duvarı, SmartScreen, UAC, VBS/HVCI, Secure Boot ve TPM **kapatılmaz**; tek istisna kullanıcının açıkça seçtiği oyun klasörü tarama dışlamasıdır.
4. **Tek tuş ilkesi:** yeni özellikler mevcut tek Performans Açık akışına (gruplu plan satırları, makul varsayılanlar, gelişmiş ayrıntı kapalı) ya da mevcut sayfalara eklenir. Yeni üst düzey sayfa ya da profil eklenmez. Cihaz sınıfı (dizüstü/masaüstü, düşük donanım) uyarlaması tek planın içinde otomatik yapılır; ayrı mod değildir.
5. **Varsayılan kuralı:** bir plan grubunun varsayılanı yalnız kanıt düzeyi "ölçüldü" ya da "belgeli" VE risk "düşük" ise Evet'tir. "Tartışmalı" kanıtlı ya da orta/yüksek riskli gruplar varsayılan Hayır'dır ve onay ekranında "Gelişmiş" başlığı altında kapalı durur. Her grup kanıt ve risk etiketi taşır.
6. **Geri alınamayan işlem plana girmez.** Geri alması doğrulanamayan bir adım (ör. Edge'in tam kaldırılması, hazır paketi kalmamış bir uygulamanın silinmesi) uygulanmaz; geri alınabilir eşdeğeri (ilke değeri, kullanıcı başına kayıt silme + yeniden kaydetme) kullanılır.
7. **BIOS yazımı ertelendi.** Rakibin kullandığı AMI SCEWIN AMI'ye ait kapalı bir araçtır; üçüncü taraf bildiriminde yeniden dağıtım ya da kullanım izni yoktur. Açık seçenekler (UEFI `Setup` değişkenine IFR ofsetiyle yazma) kart ve BIOS sürümüne özeldir, doğrulanabilir geri almayı sağlamaz ve cihazı açılmaz bırakabilir; Lenovo/HP/Dell WMI BIOS arayüzleri oyun anakartlarını kapsamaz. Lisanslı ve cihazda doğrulanmış bir yol bulunana kadar yalnız salt okunur BIOS/XMP rehberi yapılır.
8. **Kanıt sınırı değişmedi:** bu makinede gerçek sistem yazması yapılmaz; yeni adaptörler birim/mock testle teslim edilir ve gerçek cihaz doğrulaması `state/HANDOFF.md` içinde DOĞRULANMADI olarak kayıtlı kalır. Mock testi gerçek Windows testi sayılmaz.

**Sonuçlar.** ADR-032'deki "BIOS yalnız bilgi" hükmü madde 7 ile uyumludur. `power_security.h` X05 (güvenlik zayıflatma yok) kuralı madde 3'teki tek istisna ile daralır. `docs/01_REQUIREMENTS.md` ve AGENTS.md metinleri uygulama ilerledikçe bu karara göre güncellenecektir.

## ADR-036 — Windows Update duraklatma / etkin saat: yalnız okuma + öneri (WIN-11)

Durum: accepted (kanıta dayalı karar; WIN-11 kabul maddesi 1). Tarih: 2026-11-18.

İlgili: WIN-11, WIN-06 (`core/adapters/windows/update_guard.*`), R24, ADR-035 (T03 `plan.update_hold` / `plan.update_off`).

Soru: R24 oyun oturumunda güncellemeyi ertelemeye ancak Microsoft'un belgelediği bir "duraklat / etkin saat" yazma sözleşmesi varsa izin verir. Böyle bir sözleşme var mı?

Bulgular (yalnız Microsoft Learn, erişim 2026-11-18):

- `IAutomaticUpdates::Pause` (WUA, wuapi.h): Windows 10'dan beri desteklenmiyor; çağrı her zaman S_OK döner ama hiçbir şey yapmaz. Geri okunarak doğrulanamaz. https://learn.microsoft.com/windows/win32/api/wuapi/nf-wuapi-iautomaticupdates-pause
- Policy CSP – Update: `PauseQualityUpdatesStartTime` / `PauseFeatureUpdatesStartTime` (başlangıç tarihinden itibaren 35 gün), `DeferQualityUpdatesPeriodInDays` (0–30), `ActiveHoursStart` / `ActiveHoursEnd` / `ActiveHoursMaxRange`. Hepsi **aygıt kapsamlı**, BT yöneticisinin MDM / Grup İlkesi sözleşmesidir (`Software\Policies\Microsoft\Windows\WindowsUpdate`, WindowsUpdate.admx); oturumluk değildir, en küçük duraklatma birimi gündür. https://learn.microsoft.com/windows/client-management/mdm/policy-csp-update
- "Configure Windows Update client policies": duraklatma durumu `HKLM\SOFTWARE\Microsoft\WindowsUpdate\UpdatePolicy\Settings` altında `PausedQualityDate` / `PausedQualityStatus` (0 duraklatılmadı, 1 duraklatıldı, 2 otomatik sürdürüldü) ile **okunabilir**; ilke yoksa kullanıcı Ayarlar > Windows Update'ten kendisi duraklatabilir. https://learn.microsoft.com/windows/deployment/update/waas-configure-wufb

Karar:

1. Ürün Windows Update **duraklatma, erteleme günü ve etkin saat** yazmaz. Kullanıcı uygulamasına açık, belgelenmiş, geri okunabilir ve oturumluk bir sözleşme yoktur; tek belgeli yazma yolu BT yöneticisinin aygıt ilkesidir ve güvenlik güncellemelerini 35 güne kadar durdurabilir.
2. Ürün okur ve önerir: yeniden başlatma bekliyor / indirme etkin / ilkeyle yönetiliyor (`advise_for_game_session`, `writes_anything=false`); duraklatma durumu gerekirse yalnız okunur. Eylem Windows Update sayfasını açmaktır (kullanıcı kendisi duraklatır). wuauserv durdurulmaz (WIN-06).
3. ADR-035 ile izinli **T03** grupları (`plan.update_hold`: bildirim + zorla yeniden başlatma yok + Teslim Optimizasyonu; `plan.update_off`: varsayılan Hayır) bu kararın dışında ve değişmeden kalır: duraklatma / etkin saat değil, journal'lı Evet/Hayır plan öğeleridir. Onların da gerçek UAC turu ve cihazda geri yükleme kanıtı yoktur (WIN-11 kabul 2 onlar için de geçerli).
4. Yazma ancak Microsoft kullanıcı uygulamasına yönelik belgeli bir duraklatma API'si yayımlarsa yeni ADR + gerçek UAC turu + geri yükleme kanıtıyla açılır.

Etki: kod davranışı değişmez (yeni yazma yok). Tek EXE / ağ / lisans etkisi yok. Test: mevcut `update_guard` birim testleri (`writes_anything=false`).

## ADR-037 — Hizmet/broker IPC tehdit modeli (STRIDE) ve sertleştirmeler (P9/B2)

Durum: accepted (birim test kanıtlı; gerçek hizmet turu yok). Tarih: 2026-11-18.

İlgili: ADR-033 (yardımcı hizmet güven modeli), SEC-02/SEC-03, `docs/03_SECURITY_PERMISSIONS.md` "STRIDE tehdit modeli" tablosu, kanıt `artifacts/evidence/P9-service-threat-model.md`.

Bağlam: ADR-033 hizmet güven modelini tanımladı ama satır satır tehdit modeli ve negatif/mutasyon kanıtı yoktu. P9/B2 her STRIDE satırına bir test kimliği bağladı ve testlerin bulduğu açıkları kapattı.

Bulgular (önce kırmızı test, sonra düzeltme):

1. **Acil fren atlatılabiliyordu (orta).** `UpdateReceiver::begin` içindeki `switch (evaluate_release(...))` `Halted` dalını içermiyordu; imzalı `"halt": true` manifest Offer gibi devam ediyor, `Halted` kontrolü sürüm/gömülü anahtar kontrolünden önce geldiği için `UnsignedBuild` de atlanıyordu. Kurulu (yönetici) istemci durdurulmuş bir sürümü hizmete zorla yükleyebilirdi. Düzeltme (B2 sahipliği): `core::service::halt_guarded` hizmet uzantı noktasını sarar; zarf imzası doğrulanır ve `halt` true ya da bool değilse UpdateBegin koordinatöre ulaşmadan reddedilir. Kök neden düzeltmesi (`case V::Halted:` → red) B4 sahipliğindeki `src/core/update/staged_update.cpp` için istendi.
2. **Hizmet uzantı noktası her türü görüyordu (düşük).** Cancel, UpdateStatus ve bilinmeyen tür değerleri `extra` geri çağrısına iletiliyordu. Artık yalnız UpdateBegin/Chunk/Commit/Healthy (8..11) iletilir.
3. **Broker oturumunda çekirdek çerçeve sınırı yoktu (düşük).** Sınır yalnız platform döngüsündeydi (64). `ServerSession` artık `kMaxSessionFrames = 64` (Hello dahil) sınırını kendisi uygular.
4. **Broker Hello'su fazladan alan kabul ediyordu; nonce karşılaştırması erken çıkışlıydı (düşük).** Hello tam olarak `{"nonce","pid"}`; nonce sabit zamanlı karşılaştırılır.
5. **İstemci kararı SID biçimini ve sıfır dosya kimliğini denetlemiyordu (düşük).** `check_client`: kullanıcı SID'i `S-1-5-21-…`/`S-1-12-1-…` değilse veya dosya kimliklerinden biri sıfırsa `QueryFailed`.

Karar: Yukarıdaki sertleştirmeler kalıcıdır; izin listesi genişletilmez. Sabit tohumlu 10.000 mutasyonlu oturum korpusu ve 10.000 mutasyonlu istek yükü korpusu birim testte kalır (0 çökme koşulu). Yeni bir IPC mesaj türü veya hizmet işlemi eklenirken STRIDE tablosuna satır + test kimliği eklenmesi zorunludur.

Kalan riskler: R-1 (kalıcı denetim günlüğü yok), I-1 (ret davranışı platformda, birim testi yok), E-4 (aynı yönetici hesabında enjeksiyon; ADR-033'te kabul). Gerçek kurulu hizmet üzerinde negatif tur sahip/VM işidir (UAC'ye ajan tıklamaz).

Etki: davranış yalnız reddetme yönünde daralır; meşru istemci (kurulu EXE, `{"proto","pid"}` / `{"nonce","pid"}` Hello, ≤ 64 çerçeve) etkilenmez. Tek EXE, ağ ve lisans etkisi yok. Yeni metin anahtarı yok.

## ADR-038 — Kod imzalama yolu: SignPath Foundation mı, Microsoft Store (MSIX) PoC mi, imzasız alpha mı (P9-B10)

Durum: **proposed — sahip kararı bekliyor** (2026-11-18). Karar verilene kadar yürürlükteki durum değişmez: sürüm **imzasız alpha** kalır (B-003, ADR-034).

İlgili: B-003 (imza yok), B-008 (GitHub Actions faturası), ADR-034 (`EF_UNSIGNED_ALPHA`), ADR-033 (tek EXE + yardımcı hizmet), ADR-028 (sürüm manifesti kendi ECDSA anahtarıyla imzalanır; Authenticode'dan bağımsızdır). Dayanak: `docs/audit/2026-11-18-imza-ci-arastirma.md` (kaynak bağlantıları ve "UNCERTAIN" notları orada).

Bağlam: Authenticode sertifikası yok; kullanıcı SmartScreen uyarısı görür ve itibar birikmez. Ücretsiz seçenekler araştırıldı: Azure Artifact Signing ücretli ve Türkiye listede yok; Certum ücretli (fiziksel kart, CI'da kullanılamaz); self-signed SmartScreen için imzasızla aynıdır. Geriye iki gerçekçi ücretsiz yol kaldı. EV/OV imza bile ilk indirmelerde uyarıyı kaldırmaz; itibar indirme hacmiyle birikir. Yani imza = yayıncı adı + zamanla itibar, anlık uyarısızlık değil.

### Karşılaştırma

| Ölçüt | A) SignPath Foundation (OSS) | B) Microsoft Store / MSIX (PoC) | C) İmzasız alpha (bugün) |
|---|---|---|---|
| Maliyet | 0 | Bireysel hesap ücretsiz (araştırmaya göre; Türkiye için doğrudan teyit **yok**) | 0 |
| Önkoşul | Public depo + OSI onaylı lisans, ticari çift lisans yok, tüm bileşenler açık kaynak, bakım aktif, MFA, imza politikası ve gizlilik sayfası (SignPath şartları "draft") | Hesap + kimlik doğrulama; `runFullTrust` kısıtlı yetenek incelemesi | Yok |
| Ürün kararı gerektirir | **Evet: kaynağı açmak** (depo bugün PRIVATE, lisans yok). Ticari/kapalı plan varsa uygun değil | **Evet: dağıtım modeli** MSIX olur; "tek portable EXE" değişmezi (AGENTS.md) ile çelişir | Hayır |
| SmartScreen | Sertifika "SignPath Foundation" adına; itibar yine birikmeli | Store'dan kurulumda uyarı yok (Microsoft belgesi) | Her indirmede uyarı |
| UAC / hizmet uyumu | Mevcut mimari değişmez (EXE imzalanır) | **Kanıtlanmadı:** LocalSystem yardımcı hizmet, yönetici gerektiren sistem ayarı yazımı, MSIX'in per-user tasarımı ve "sistem optimizasyon" araçlarına Store politikası (UNCERTAIN, araştırılmadı) | Değişmez |
| Risk | SignPath'in "kullanıcı sistem yapılandırmasını uygun uyarı olmadan değiştirmemeli" koşulu: ürün sistem ayarı yazıyor (Evet/Hayır onaylı, geri alınabilir; ama onay ölçütü öznel, UNCERTAIN) | Mimariyi bozabilir; PoC başarısız olabilir | Kullanıcı güveni ve indirme dönüşümü düşük |
| CI | GitHub Actions üzerinden imza isteği (sunucu tarafı HSM); B-008 çözülmeden çalışmaz | Paketleme yerelde yapılabilir | — |

### Karar (öneri; sahip onayı olmadan yürürlüğe girmez)

1. **Şimdi:** sürüm "imzasız alpha" kalır. Sürüm notu SHA-256, "Ek bilgi > Yine de çalıştır" adımları ve imzasız alpha ibaresini taşır (`publish_release.ps1` zaten üretir). Hiçbir yerde "imzalı", "güvenli sertifikalı" ya da SmartScreen uyarısızlığı vaadi yazılmaz.
2. **Sahip, aşağıdaki üç yoldan birini seçer** (plan sahip listesi madde 3):
   - **A. Kaynağı aç + SignPath:** OSI lisansı seç, depoyu public yap (önce lisans/üçüncü taraf/gizlilik ve rakip-adı taraması), SignPath Foundation başvurusu (organizasyon, proje, imza politikası, MFA). Başvuruda "sistem yapılandırması uyarısı" koşulu için Evet/Hayır onay akışı belgelenir. Kabul gelene kadar sürüm imzasız kalır.
   - **B. Store/MSIX PoC (kod yazmadan önce yalnız sınama):** izole VM'de MSIX paketi + `runFullTrust` + yönetici gerektiren adımlar + hizmet kurulumu denenir; sonuç `artifacts/evidence/`'a yazılır. PoC başarısızsa C'ye dönülür. PoC başarılı olsa bile "tek portable EXE" değişmezi için ayrı ADR gerekir.
   - **C. İmzasız kal:** dürüst etiket sürer; indirme sayfası ve sürüm notu yukarıdaki adımları anlatır.
3. **Hazır iskelet (devre dışı):** `.github/workflows/ci.yml` içinde `sign` işi `if: ${{ false }}` ile kapalıdır; `dist` işi imzasız EXE'yi ve SHA-256'yı yalnız artifact olarak yükler. A seçilirse iş etkinleştirilir; eylem sürümü resmî belgeden doğrulanıp sabitlenir (burada yazılı `v1` doğrulanmadı) ve `github-artifact-id` doldurulur.

### Etkinleştirme koşulları (A seçilirse; hepsi sağlanmadan `sign` açılmaz)

- Depo public, lisans dosyası var, SignPath başvurusu kabul edildi, `SIGNPATH_API_TOKEN` (secret), `SIGNPATH_ORGANIZATION_ID` ve `SIGNPATH_CONNECTOR_URL` (değişken) sahip tarafından tanımlandı.
- GitHub Actions faturası/runner sorunu çözüldü (B-008) veya imza isteği için GitHub-hosted runner kullanılabiliyor. Self-hosted runner public depoda fork PR'ı yüzünden **kullanılmaz**.
- İmzadan sonra: `EF_UNSIGNED_ALPHA` `x64-dist` preset'inde OFF yapılır (ADR-034 "Kaldırma"); `windows.trust` testi imzalı ikiliyle yeniden koşulur; imzalı EXE de `scripts/local_ci.ps1` yayın kapısından geçer (SHA-256 gerçek yayın dosyasına uyar). WinVerifyTrust + zaman damgası doğrulanır. Sürüm manifesti (ADR-028) ayrı anahtarla imzalanmaya devam eder.

### Sonuçlar ve sınırlar

- Bu ADR hiçbir yayını, başvuruyu, hesabı veya depo görünürlüğünü değiştirmez; kod imzalama **yoktur** ve bugün olduğu gibi söylenir.
- Maliyet kararı yok (ücretli seçenek önerilmedi). Para gerektiren yol (Azure/Certum) sahip isterse ayrı ADR ile yeniden açılır.
- Hukuki taraf: SignPath şartları ve Store politikası taslak/değişken kaynaklardır; uygulamadan önce güncel metinler sahip tarafından okunmalıdır (hukukçu onayı yok).
- Test: `scripts/test_release_sign.py` workflow'da `sign` işinin devre dışı olduğunu ve `runs-on` değişkenini denetler; yayın kapısı `scripts/local_ci.ps1 -SelfTest` ile sınanır.

## ADR-039 — Donanım dürüstlüğü: yetenek x üretici destek matrisi, AMD/Intel yazma yolu kapalı (fail-closed), ADLX lisans özeti (P9-B8)

Durum: accepted. Tarih: 2026-11-18.

İlgili: GPU-01, GPU-02, GPU-03 (B-006), GPU-04 (B-004), `artifacts/hardware-control-boundaries.md`, `docs/20_SUPPORT_MATRIX.md`, `src/core/capabilities/vendor_matrix.h`, `src/core/vendors/dispatch.*`.

Soru: Ürün hangi yetenekte hangi GPU üreticisi kimliğinde yazabilir, yalnız okuyabilir ya da hiç yapamaz; bu cevap kodda, testte ve belgede tek yerden nasıl sabitlenir ve gerçek cihaz kanıtı olmayan hiçbir şey nasıl "destekleniyor" yazılmaz?

Bulgular:

1. Katalogdaki 58 özelliğin hiçbiri `release_enabled` değildir; gerçek cihaz yazma kanıtı (QA-02) yoktur. NVIDIA DRS (G01, G02, G05, G06) kodda ve birim testinde vardır; G07 yazması kapalıdır (`kShaderCacheItemEnabled = false`). Bu PC'de NVAPI için gerçek cihaz kanıtı yalnız okumadır (GPU-02: sıcaklık, nvidia-smi ile karşılaştırma).
2. AMD ve Intel adaptörleri (`amd3d`, `igcl3d`) yalnız sahte API ile sınanır; ürün kodunda hiçbir yere bağlı değildir. `route_adapters` runtime yüklüyse AMD/Intel için de `Supported` döndürür; bu runtime varlığıdır, ayar yazma izni değildir.
3. Hibrit sistemde (bu PC: AMD iGPU + NVIDIA ayrık kart) DXGI sırası veya "ilk kart" oyunun çalıştığı kartı göstermez. NVIDIA global DRS planı (`nvidia_plan_items`) yalnız sürücü açılabildiği için üretilir; oyun AMD iGPU'da çalışıyorsa bu plan o oyuna fayda sağlamaz. Kapı bu yüzden bağdaştırıcı bazlıdır.
4. **ADLX lisansı.** AMD'nin GitHub deposu (GPUOpen-LibrariesAndSDKs/ADLX) lisansı olarak depo kökündeki "ADLX SDK License Agreement" PDF'ini gösterir; sayfa MIT veya başka standart lisans adı vermez (erişim 2026-11-18, yalnız depo ana sayfası okundu; bu dalda PDF'in tam metni okunmadı). Sözleşme şartları `state/BLOCKERS.md` B-006 kaydından özetlenir ve bu dalda yeniden doğrulanmadı: yalnız dahili kullanım, yalnız nesne kodu (object code) dağıtımı, dağıtımda Schedule B şartlarını içeren EULA zorunluluğu, başlık (kaynak) dağıtımı kısıtlı. Sonuç: ADLX başlıkları repoya konmaz, kod bağlanmaz; AMD yolu `unsupported` kalır. Not: `third_party/DEPENDENCIES.md` AMD satırı hâlâ "MIT" yazar; bu `docs/13_DEPENDENCIES_LICENSES.md` ve B-006 ile çelişir ve sahip kararı sonrası düzeltilmelidir (bu dal o dosyalara dokunmadı). Ryzen Master Monitoring SDK için de aynı lisans kabulü beklenir (`amd_cpu_monitoring_capability`).
5. **IGCL.** Intel tarafında bağlayıcı yok ve Intel GPU'lu test cihazı yok (B-004); `igcl3d` yalnız sözleşme ve sahte API testidir (GPU-04).

Karar:

1. Tek kaynak `vendor_matrix.h` tablosudur: her katalog özelliği x {NVIDIA, AMD, Intel} için `Yazma / Yalnız okuma / Desteklenmiyor` + sabit gerekçe anahtarı (`hw.matrix.*`) + kanal (`windows` / `vendor_driver`). Eksik satır test hatasıdır. `docs/20_SUPPORT_MATRIX.md` bu tablodan `ef_support_matrix --markdown` ile üretilir; elle düzenlenmez ve bayat olursa test kırılır.
2. "Yazma" kodda çalışan yol demektir; cihazda desteklendiği anlamına gelmez. Yazma satırlarının kanıt tavanı `Unit`'tir. `release_enabled` yalnız `SupportEvidence` (Hardware) ile açılır ve bu tablo onu hiçbir zaman açmaz. Mock sonucu gerçek cihaz sayılmaz.
3. AMD ve Intel kimliğinde `vendor_driver` kanalında yazma satırı yoktur. Kapı `vendor_driver_write_allowed(route, feature)`: rota `Supported` ve tablo satırı `Yazma + vendor_driver` ise açıktır; yönetici hakkı, yüklü runtime veya mock `Supported` sonucu bunu değiştirmez. Bilinmeyen/okunamayan üretici kimliği, yazılım ve uzak bağdaştırıcı `Vendor::None` olur ve hiçbir satır almaz.
4. Üretici kimliği yalnız seçilen bağdaştırıcının PCI üretici numarasından (`adapter_vendor`, `GpuAdapter::index` ile) gelir; ad, DXGI sırası ve "ilk kart" kimlik değildir.
5. ADLX/IGCL bağlanırsa yeni ADR, sahip lisans kararı (AMD), gerçek cihaz turu ve bu tablonun ile `test_vendor_matrix` içindeki "unwired" taramasının bilinçli güncellenmesi gerekir.
6. Denge testinde AMD/Intel için sürücü sınır API'sinin yokluğu hükmü engellemez; `vendor_limit_no_api` notu olarak görünür. Zaman damgası güvenilmezse satırlar `Belirlenemedi` kalır ve sayı uydurulmaz (fixture: `test_amd_balance_undetermined.cpp`; gerçek AMD cihaz sonucu değildir).

Etki: Davranış değişikliği yok (yeni yazma yolu açılmadı); yalnız tablo, kapı fonksiyonu, testler ve belge eklendi. Uygulama kodunun (`src/app`, `src/core/perf`) yazma planlarını bu kapıya bağlaması ayrı bir iştir ve hibrit sistemde NVIDIA planının yalnız oyunun NVIDIA bağdaştırıcısında çalıştığı doğrulandığında yapılmalıdır. Tek EXE / ağ / yeni bağımlılık etkisi yok.

## ADR-040 — Rakip eksik analizi: kapılı yeni yazmalar, salt okunur teşhisler ve yapılmayanlar (B0)

Durum: accepted (B0 zemini). Tarih: 2026-11-19.

İlgili: `docs/superpowers/plans/2026-11-19-rehber-eksikleri.md` (B0–B9), ADR-035 (m.5 varsayılan kuralı), ADR-039 (destek matrisi), `research/setting_catalog.json` W13–W16 ve X01/X08/X11 notları, kaynaklar S32, S108–S111.

Soru: Rakip eksik analizinde (bir rehber tipi ayar kütüphanesi ile ürün karşılaştırması; kaynağın adı repoya yazılmaz) çıkan 11 eksikten hangileri ürünün değişmezleri içinde (Evet/Hayır, kalıcı başlangıç kaydı, geri okuma, doğrulanabilir geri alma, yalnız broker izin listesi, FPS iddiası yok) yapılır, hangileri yalnız okunur, hangileri hiç yapılmaz?

Doğrulama (B0, 2026-11-19; resmî, ücretsiz Microsoft Learn sayfaları):

1. ADR numarası: bu dosyanın son kaydı ADR-039 idi; bu kayıt 040.
2. USB seçici askıya alma: Microsoft'un Teams Rooms sorun giderme sayfası (S108) USB alt grubu `2a737441-1930-4402-8d77-b2bebba308a3`, ayar `48e6b7a6-50f5-4782-a5d4-53bb8f07e226` ve AC indeksi `0` = "Devre dışı" eşlemesini verir. Karşı kanıt: sürücü belgesi (S109) seçici askıya almanın varsayılan açık olduğunu ve Microsoft'un sürücü geliştiricilerine kapatmamayı önerdiğini yazar (pil ve işlemci uyku durumları). Bu yüzden yalnız AC değeri değişir ve metin boşta güç artışını söyler.
3. Dinamik tick: BCDEdit /set belgesi (S32) `disabledynamictick [yes|no]` seçeneğini tanımlar ve "yalnız hata ayıklama için" notunu taşır; BCDEdit sayfaları BCD değişikliğinden önce BitLocker'ın askıya alınması gerekebileceğini yazar. Okuma-yazma yolunun (WMI BCD sağlayıcısı mı, sabit argümanlı `System32\bcdedit.exe` mi) seçimi B4 adım 0'dır; bu dalda seçilmedi.
4. SvcHost bölme: "Service host grouping in Windows 10" (S110) 1703'ten itibaren 3,5 GB üstü RAM'de hizmetlerin ayrı süreçte çalıştığını ve ayırmanın amacının güvenilirlik/yalıtım olduğunu yazar. **DOĞRULANMADI:** eşik değerinin kayıt adı (`SvcHostSplitThresholdInKB`) ve birimi bu sayfada geçmez; B3 resmî kaynak bulamazsa girdi eklenmez.
5. Win32PrioritySeparation: bu dalda resmî Microsoft belgesi bulunamadı. **DOĞRULANMADI:** istemci varsayılanı (2) ile hedef değerin (38) davranış denkliği; B2 adım 0'da doğrulanmazsa atlama kuralı eklenmez, doğrulanamazsa girdi de eklenmez.
6. TCP chimney: NDIS chimney API belgeleri (S111) özelliği "deprecated and should not be used" olarak işaretler.
7. Katalog/matris satır ekleme mekanizması: `research/setting_catalog.json` derleme zamanında gömülür (`EF_CATALOG_JSON`); `vendor_matrix.h` sabit boyutlu dizi (58 → 62); `test_vendor_matrix` ve `test_capabilities` sayıyı ve katalog-matris tutarlılığını denetler; `docs/20_SUPPORT_MATRIX.md` `ef_support_matrix --markdown` ile yeniden üretildi.

Karar:

1. **USB seçici askıya alma (W13, `plan.power_usb`):** yazılır; yalnız etkin plan, yalnız AC, hedef 0; mevcut `PowerSettingSet` yolu (güç API'si; `powercfg` süreci çağrılmaz). Kanıt Belgeli, risk Düşük → varsayılan Evet (ADR-035 m.5). Pilli cihazda da yalnız AC değeri değişir. Sahip için not: S109'daki "kapatmayın" önerisi nedeniyle bu öğe review'da yeniden tartılabilir; B0 planın kararını korudu.
2. **Win32PrioritySeparation (W14, `plan.foreground_boost`):** yazılabilir ama Tartışmalı/Düşük, varsayılan Hayır (Gelişmiş). Değer tek ve tablodadır; X11 (serbest değer paketleri) dışarıda kalır. Yukarıdaki doğrulama 5 geçmeden girdi eklenmez.
3. **SvcHostSplitThresholdInKB (W15, `plan.svchost_split`):** Tartışmalı/Orta, varsayılan Hayır, yeniden başlatma gerekir. Değer broker'ın KENDİ okuduğu kurulu RAM'dir (KB); istemciden gelen değer yalnız buna eşitse kabul edilir (`TweakRuntime`, `set_tweak_runtime_provider`). RAM okunamazsa veya DWORD'a sığmazsa öğe yok. Onay metni hizmet yalıtımının azaldığını söyler. Doğrulama 4 geçmeden girdi eklenmez.
4. **Dinamik tick (W16, `plan.boot_tick`):** `OperationId::BootOptionSet` (enum sonuna), tek hedef `boot:dynamictick`, tek değer `off`. Kod iskeleti var, `kBootOptionItemEnabled = false`. Açma koşulu: izole VM'de uygula → yeniden başlat → geri oku → Kapalı → yeniden başlat → geri oku tam tur kanıtı. BitLocker açık/okunamayan sistem sürücüsünde, pilli cihazda ve okunamayan BCD'de plana hiç girmez. Tartışmalı/Yüksek, Hayır/Gelişmiş. Broker: `admin_operation_allowed` yalnız biçimi (tablodaki çift) denetler, böylece journal'daki eski kaydın geri alma isteği bayrak kapansa da reddedilmez; `admin_apply_allowed` bayrak kapalıyken yeni yazmayı reddeder. B0'da çift tanımsızdır (hiçbir boot isteği izin listesine girmez).
5. **MSI modu: YAZILMAZ.** Değer aygıt INF'ine ve sürücüye bağlıdır; yanlış değer açılışı bozabilir ve o durumda uygulama içi geri alma çalışamaz (doğrulanabilir geri alma değişmezi); hedef sabit tabloya konamayan aygıt örnek yoludur; fayda kanıtı yok. Yalnız mevcut durum okunur (`readiness_interrupts.*`, B5); bulguda "Düzelt" yoktur. X08 dışarıda kalır.
6. **Zamanlayıcı / bekleme belleği:** süreç dışı zamanlayıcı zorlama (X02) ve arka planda otomatik bellek boşaltma yapılmaz (hafiflik hedefi, sessiz mutation yasağı, fayda kanıtı yok). Eşik bulgusu sabitleri B5'te kod sabiti olur (öneri: bekleme belleği toplam RAM'in yarısından büyük VE boş bellek toplamın onda birinden küçük); bulgu yalnız ölçülen baytları gösterir; "Düzelt" mevcut `maint:standby_purge` işini tek Evet/Hayır ile çağırır.
7. **TCP autotuning / chimney / Nagle:** yapılmaz. Oyun trafiği çoğunlukla UDP'dir; chimney Microsoft belgesinde kullanımdan kaldırılmıştır (S111); katalog X07. DNS ölçümü (T10) ve hazır liste (EF-X14) mevcuttur.
8. **NVIDIA ek ayarlar (`plan.nvidia_extra`):** her ayar ayrı kapalı bayrakla eklenir; kimlikler ve değerler NVAPI SDK başlığından, lisans SDK deposundan doğrulanır (B6 adım 0). Gerçek NVIDIA cihazında ürünün Evet/Hayır akışıyla tur kanıtı gelmeden bayrak açılmaz. Belgeli/Orta → Hayır. G07 M3 nedeniyle kapalı kalır.
9. **AMD/Intel düşük gecikme:** ADR-039 geçerli; yazma yok, yalnız rehber (B7).
10. **Üçüncü taraf araçlar:** gömülmez, indirilmez, önerilmez.

Uygulama kuralları (B0'da kodlandı):

- Yeni gruplar `plan_groups()` içinde `listed_when_empty = false` ile durur: öğe üretmedikleri sürece onay ekranında da Panel'in Ayrıntılar listesinde de satır almazlar (Panel 0.9.2 görünümü değişmez). Varsayılanlar ADR-035 m.5 kuralına uyar ve testle sabittir.
- Yeniden başlatma kararı broker'da tablodan verilir (`admin_needs_reboot`: HAGS, `BootOptionSet`, `needs_reboot` alanı olan katalog girdisi); istemcinin `op.needs_reboot` alanına güvenilmez. Bu, Kapalı sonrası "yeniden başlatma bekliyor" durumunun dürüst bildirilmesi içindir.
- Katalog satırları W13–W16 `not_started`, matriste her üreticide "Desteklenmiyor [E1]". Yazma yolu birleşince satır `win_write` ve `implementation_status` birlikte güncellenir (`vendor_matrix_follows_catalog_status_and_policy` testi tutarsızlığı yakalar).

Etki: Bu dalda davranış değişikliği yok; hiçbir yeni öğe plana girmez, hiçbir yeni yazma yolu açılmadı. Tek EXE, ağ ve bağımlılık etkisi yok. Kanıt tavanı: birim testi; gerçek cihazda hiçbir yazma denenmedi.

### ADR-040 ek not (B9, 2026-11-19): dalların bulduğu doğrulama sonuçları

Bu not yukarıdaki karar metnini DEĞİŞTİRİR; çelişen yerde bu not geçerlidir. Kanıt: `artifacts/evidence/R-B1.md` … `R-B8.md`, özet `artifacts/evidence/R-ozet.md`. Yeni ürün sözü yoktur; hiçbir bayrak açılmadı.

- **m.1 (USB seçici askıya alma):** varsayılan **Hayır** (Gelişmiş), kanıt etiketi Tartışmalı, risk Düşük. Gerekçe: Microsoft sürücü belgesi seçici askıya almayı kapatmamayı önerir (karşı kanıt, S109). Yukarıdaki "varsayılan Evet" ifadesi geçersizdir. Kodda yazma yolu vardır (bayrak yok; kullanıcı Evet/Hayır ile seçer); matris satırı "Yazma [A1]" (yalnız birim kanıtı), gerçek cihaz turu yapılmadı.
- **m.2 (Win32PrioritySeparation):** istemci varsayılanı 2 ile hedef 38 denkliği Microsoft belgesinde YOK (Win32_OperatingSystem sayfası yalnız 0-2 ön plan artışını belgeler). Girdi katalogda durur ama `kPrioritySeparationItemEnabled = false`; atlama kuralı (`WhenCurrentIsDefaultEquivalent`) eklenmedi. Matris: "Desteklenmiyor [A2]". Açma koşulu: denklik resmî kaynakla doğrulanır + gerçek cihaz turu.
- **m.3 (SvcHostSplitThresholdInKB):** değer adı/yeri/birimi resmî belgede bulunamadı (S110 yalnız SvcHostSplitDisable ve 3,5 GB üstü ayrımını anlatır). Katalog girdisi EKLENMEDİ ve RAM okuyucusu bağlanmadı; hesaplanan değer türü (`TweakValueSource::InstalledRamKb`) altyapısı kodda hazır, kullanan girdi yok. Matris: "Desteklenmiyor [E1]".
- **m.4 (dinamik tick):** BCD seçenek adı `disabledynamictick` ve kimliği `0x260000a5` belgelendi; yazma/silme sabit komut satırıyla (`bcdedit`, kabuk yok) kodlandı. **Okuma yolu doğrulanmadı** (`bcdedit /enum` çıktısı yerelleşir; WMI `{current}` nesne kimliği resmî belgede bulunamadı), bu yüzden okuma her zaman "desteklenmiyor" döner ve plan öğesi hiç çıkmaz. `kBootOptionItemEnabled = false`. Matris: "Desteklenmiyor [A2]". Açma koşulu: okuma yolunun doğrulanması + izole VM'de tam tur + yeni ADR notu.
- **m.8 (NVIDIA ek ayarlar):** iş parçacıklı iyileştirme (G13, `0x20C1221E`, hedef 1) ve FXAA kapalı (G14, `0x1074C972`, hedef 0) resmî NVAPI başlığıyla (MIT) doğrulandı; ikisi de bayrakla kapalı (`kThreadedOptimizationItemEnabled`, `kFxaaOffItemEnabled`). **"Ultra" düşük gecikme için resmî başlıkta DRS kimliği YOK**; ayar eklenmedi ve kimlik uydurulmadı. Matris: NVIDIA "Yalnız okuma [A2]".
- **Çapraz düzeltmeler (B9):** sapma grubu (`drift_group_key`): `power:usbss:ac` → `plan.power_usb`, G13/G14 → `plan.nvidia_extra`; geri yükleme raporunda `power:usbss:ac` için özel ad; broker `admin_apply_allowed` kapalı bayraklı katalog girdisini (`TweakValueSet`) motora girmeden reddeder, geri alma (Restore/UndoFix/Recover) her zaman izinli kalır.
- **Hâlâ DOĞRULANMADI:** bu ADR'nin hiçbir yeni yazması gerçek Windows/GPU cihazında denenmedi; mock/birim kanıtı gerçek cihaz kanıtı değildir.

### ADR-040 ek not 2 (Ar-Ge, 2026-11-19): resmî kaynak taraması

Kanıt: `artifacts/evidence/R-arge-2026-11-19.md`. Kod ve bayraklar değişmedi; yalnız karar güçlendi. Çelişen yerde bu not geçerlidir.

- **m.4 (dinamik tick) güncel:** Microsoft BCDEdit belgesi `disabledynamictick` (ve `useplatformtick`) seçeneğini yalnız hata ayıklama için tanımlar. Bu bir performans ayarı olarak sunulamaz; `kBootOptionItemEnabled` KAPALI kalır ve açılması önerilmez. Yazma kodu yalnız kayıtlı geri dönüşle ve ileride üretici tutum değişirse kullanılmak üzere durur.
- **m.3 (SvcHost) güncel:** Microsoft hizmet ayrımını güvenilirlik, güvenlik ve yalıtım özelliği olarak anlatır ve bellek artışını bilir; eşik değeri belgesizdir. Girdi eklenmez, kalıcı.
- **m.2 (öncelik ayrımı) güncel:** 38 ile 2 denkliği tarananlarda da bulunamadı. Bayrak kapalı; yeni resmî belge çıkarsa yeniden bakılır.
- **m.8 (NVIDIA Ultra) güncel:** NVIDIA NVAPI moderatörü bu ayar için NVAPI desteği olmadığını söyledi. Kimlik aranmayacak; madde "üretici desteklemiyor" olarak kapandı.
- **Oyun rehberi:** Fortnite satırları (V-Sync kapalı, kare sınırı, Reflex) Epic kaynaklarıyla uyumlu. Warzone Dinamik Çözünürlük resmî ama 2021 tarihli; güncel menü için cihaz doğrulaması sürer.
- **Sonuç:** hiçbir yeni bayrak açılmadı. Açılabilecek tek aday G13/G14 (NVAPI başlığında var) ve USB (W13) için eksik yalnız gerçek cihaz kanıtıdır.

## ADR-041 — Oyun sunucusu ping'i: kullanıcının yazdığı tek IPv4 adresine ICMP (R-C4, 2026-11-20)

Sahip kararı: "Evet, ekle". Rakip eksik analizindeki "sunucu ping ölçümü" maddesi kapandı. Güvenilir ve resmî bir oyun sunucusu adres listesi olmadığı için (ADR-040 ek not 2 ile aynı sınır) adres uygulamaya gömülmedi; kullanıcı yazar.

- **Ne:** Ölçümler > İnternet hızı kartında "Sunucu ping" düğmesi. Kip penceresi tek satır yazı alanı açar (yalnız rakam ve nokta, en çok 15 karakter, Delete = temizle, Ctrl+V = yapıştır). Evet, yalnız geçerli bir `a.b.c.d` ile açılır. Ardından yaklaşık 4 saniyede 20 ICMP yankı gönderilir; ortalama, dalgalanma ve kayıp gösterilir.
- **Ağ ilkesi:** Yeni ağ API'si EKLENMEDİ. Mevcut `run_ping_test` (IcmpSendEcho; `network_reader.cpp` tek izinli dosya) yeniden kullanılır. Alan adı çözülmez (`getaddrinfo`/DNS yok); yalnız sayılı IPv4. Kullanıcı verisi paketle gitmez. Gizlilik sayfası ve kurulum gizlilik metni bu satırla güncellendi.
- **Adres:** yalnız bu bilgisayarda (`game_server_ipv4`, tercih dosyası); okunurken yeniden doğrulanır. Kabul dışı: boş, 4 sekizli olmayan, baştaki sıfır, ilk sekizli 0/127/224 ve üstü. Özel (LAN) adresler geçerlidir.
- **Kapı:** hız testiyle aynı (ADR-021): oyun açıkken veya tespit güvenilir değilken adres bile sorulmaz; pencere açıkken oyun başladıysa Evet sonrası kapı yeniden denetlenir. Tek aktif test kuralı geçerlidir.
- **Dürüstlük:** 10'dan az yanıt = "veri yetersiz" (sunucu ping'i kapatmış olabilir); sayı uydurulmaz. Not metni: bu, yazılan adrese ICMP gecikmesidir; oyunun gösterdiği ping'den farklı olabilir ve oyun içi gecikmeyi vaat etmez. Eşik/derece yoktur (yalnız ölçülen sayılar).
- **Doğrulanmadı:** gerçek bir sunucuya gerçek ICMP, gerçek klavyeyle yazma/yapıştırma ve gerçek ekran okuyucu denenmedi. Birim ve ekran görüntüsü denetimi vardır; mock/birim kanıtı gerçek cihaz kanıtı değildir.

## ADR-042 — Marka değişikliği kullanıcı verisi göçü: kopyala, doğrula, tek adımda devret; hata halinde dürüst kurtarma durumu (R-F1, 2026-11-24)

Bağlam: ürün adı değişti, veri klasörü adı da değişti. Eski sürümün journal'ı "Kapalı"nın geri yükleyeceği önceki değerleri taşır; yeni uygulama boş klasörle açılırsa kullanıcı önceki değerlere dönme yolunu kaybeder.

Karar:
- Göç yeni uygulamanın ilk açılışında, yeni klasörü açan her şeyden önce, süreç başına bir kez çalışır (Ui, Setup, Recovery rolleri; Smoke/servis/broker/işçi rolleri çalıştırmaz). Aynı kullanıcının iki yeni süreci adlı kilitle sıralanır.
- **Taşıma değil kopya:** eski klasör hiç değişmez (silme, işaret yok); eski sürüme dönüş ve elle kurtarma mümkün kalır. Disk maliyeti kabul edildi; büyük indirme önbellekleri (`drivers\`, `setup\`) kopyalanmaz.
- **Doğrulama:** her dosya boyut + SHA-256 ile karşılaştırılır; işaret (`migration.json`: şema, sonuç, eski/yeni klasör adı, dosya sayısı, toplam bayt, sıralı dosya listesinin SHA-256'sı, enjekte saat, uygulama sürümü; tam yol/kullanıcı adı yok) hazırlık klasöründe geri okunur, sonra tek yeniden adlandırma ile devredilir. İşaret varsa göç bir daha çalışmaz (idempotent).
- **Geri alma:** her hatada yalnız kendi hazırlık klasörümüz silinir; işaretsiz yabancı hazırlık klasörüne dokunulmaz. Hata kaydı yeni klasörde durur ve sonraki açılış üzerine yazmadan birleştirerek yeniden dener.
- **Dürüst başarısızlık:** eski journal'da geri alınmamış veya okunamayan kayıt aktarılamadıysa `JournalBackend::recovery_needed()` true döner ve kart `RecoveryNeeded` + `perf.legacy_unmigrated` gösterir; boş yeni journal'la geri alma denemesi de "Kapalı"ya dönmez. Eski makine journal'ı (ProgramData) yalnız okunur, bekleyen kayıt aynı bildirimi verir.
- **Eski adlı Run değeri:** bizim sayılır ve yeniden adlandırılır (ADR-030 akışı); journal kayıtları yeniden yazılmaz.

Sonuç / sınırlar: birim kanıtı (bellek içi dosya sistemi, 15 test, önce boş uygulamaya karşı kırmızı). Gerçek bir eski kurulumdan yükseltme turu, gerçek kilitli dosya, gerçek disk dolu ve ProgramData makine journal'ı göçü DENENMEDİ / yapılmadı. Makine journal'ı göçü yükseltilmiş yardımcı ister (ayrı görev); o zamana kadar bekleyen eski yönetici adımları dürüstçe bildirilir ve yeni Performans uygulaması engellenir.

## ADR-043 — Eski adlı kurulumdan yeni markaya yükseltme: eski hizmetin gözetimi, yan yana kurulum, "Healthy" tek silme kapısı (R-F2, 2026-11-24)

Bağlam (kaynaktan gözlenen, 059b3ee3): 0.9.5 kurulu istemcisi ayrı bir Setup EXE'si **başlatmaz**. Eski UI yeni EXE baytlarını eski pipe'tan eski hizmete yollar; eski hizmet (LocalSystem) imza/SHA-256 doğrular, eski `ProgramData\<eski ad>\updates\state.json`'a `swapped` yazar ve **eski kurulum yolundaki EXE'yi yerinde değiştirir** (`exe` → `exe.old`, `exe.new` → `exe`). Eski UI yeni EXE'yi eski yolda `--updated-from=<eski sürüm>` ile, kullanıcı belirteciyle (yükseltilmemiş, etkileşimli oturum, çalışma klasörü eski kurulum klasörü, ortam miras) başlatır ve hemen çıkar; çıkış kodunu beklemez. Eski hizmet 60 sn içinde **eski pipe'tan** `UpdateHealthy(sürüm)` bekler (istemci görüntüsü = eski kurulu EXE, Administrators üyesi, etkileşimli). Gelmezse `recover_swap(false)` (exe.old geri) ve hizmete özgü çıkış 5. Gelirse durum `ui_healthy`, çıkış 4; SCM hata eylemi (3× RESTART 5 sn, sıfır dışı çıkış da hata) aynı ImagePath'i (`"<eski exe>" --service`) yeniden başlatır: artık **yeni ikili, eski hizmet adıyla, SYSTEM, oturum 0**. Bugünkü yeni kod bu zincirde başarısız olur ama güvenli kalır: yeni UI sağlık sinyalini yeni pipe'a yollar (eski hizmet 60 sn sonra geri alır) ve eski yoldan `--service` "yanlış konum" (1) ile durur.

Karar:
- **Eski çağrılar tanınır** (`src/core/legacy/legacy_install.*`, tablo `kLegacyLaunches`): eski yoldan `--updated-from` → UI + sağlık sinyali **eski pipe/eski hizmet adına**; eski yoldan `--service` → gözetimsiz yükseltme (pipe sunumu yok); boş / `--tray` → UI (Setup penceresine yönlenmez); `--uninstall` → yükseltme sürerken reddedilir (21). Aynı katı ayrıştırıcı (`parse_launch_args`).
- **SCM çıkış sözleşmesi** (eski kurulumun hata eylemi "istemci"dir): 0 = yeni hizmet devraldı, yeniden başlatılmaz; 5 = geri alındı, SCM eski EXE'yi başlatır (0.9.5 ile aynı anlam); 6 = geri alma tamamlanamadı (EXE kilitli), 5 sn sonra yeniden denenir.
- **Sıra (LegacyHost, `run_legacy_upgrade`):** korumalı yeni makine kökü + kalıcı kayıt (`<yeni ProgramData>\setup\legacy-upgrade.json`; yazılamazsa hiçbir şey yapılmaz, eski EXE geri gelir) → yeni kurulum klasörü + ACL, kendi görüntüsü → yeni EXE (SHA-256) → makine journal'ı göçü → yeni adla hizmet oluştur/başlat, sağlık (Running + kendi pipe'ı yanıt verir) → yeni ARP, yeni Başlat kısayolu (masaüstü yalnız eskisi bizimse) → **`healthy` kalıcı yazılır** → çıkış 0. Her oluşturmadan önce "oluşturdum" niyeti kayda yazılır; geri alma yalnız bu denemenin oluşturduklarını kaldırır. Çökme/güç kaybı: aynı deneme kaldığı yerden (her adım idempotent).
- **Temizlik yalnız `healthy` sonrasında ve yeni hizmetin ilk açılışında (NewService):** eski hizmet durdur + sil (ImagePath eski EXE ise), eski ARP (InstallLocation eski klasörse), eski kısayollar (yalnız hedefi eski EXE olan .lnk), eski kurulum klasöründe yalnız bizim adlarımız (EXE + `.new/.old/.updated/.new.failed`; kurulum manifesti yoktur), kilitliyse yeniden deneme sonra `MoveFileEx(DELAY_UNTIL_REBOOT)` (önce dosya, sonra boş klasör), yabancı dosya varsa klasör kalır. Eski ProgramData ve LocalAppData **yedek olarak kalır**. Eski Run değerini R-F1 `autostart_on_launch` taşır.
- **Makine journal'ı** (`machine_journal_migration.*`): `journal\**` (perf kökü + fixes, restore_point, maintenance, drivers, vendor_drivers) kopyalanır; `updates\` ve `setup\` kopyalanmaz. Önce yeni kök korumalı ACL (SY + BA), hazırlık `journal.migrating` bunu miras alır; boyut + SHA-256, tek adımda devir veya üzerine yazmadan birleştirme (farklı içerik = çakışma), devirden sonra journal ve her alt klasörün ACL'i geri okunur. İşaret `machine-migration.json` journal klasörünün **dışında** ve dosya listesini taşır: geri alma yalnız hâlâ aynı olan kopyaları siler. Journal baytları yeniden yazılmaz; Etkin işlemler (HAGS/güç/DRS) yeni sürümde "Kapalı" ile aynı baytlardan geri alınır. Kullanıcı süreci işaret varsa eski bekleyen kaydı "aktarıldı" sayar; yoksa R-F1 dürüst bildirimi sürer.

Durum tablosu:

| Durum | Sonuç | Çıkış | Eski kurulum |
|---|---|---|---|
| Kayıt yazılamıyor | hiçbir şey yapılmaz, exe.old geri | 5 | çalışır |
| Disk dolu / EXE kopyası doğrulanmadı | yeni artefaktlar geri, exe.old geri | 5 | çalışır |
| Makine journal göçü başarısız / ACL geri okuması kötü | kopyalar (işaretli) silinir, exe.old geri | 5 | çalışır, eski journal dokunulmadı |
| Yeni hizmet başlamadı / sağlıksız | yeni hizmet silinir, geri | 5 | çalışır |
| ARP/kısayol yazılamadı | hepsi geri | 5 | çalışır |
| Eski EXE geri getirilemedi (kilitli) | kayıt `rolling_back` | 6 (yeniden) | SCM tekrar dener |
| Yeni artefakt silinemedi | 3 denemeye kadar 6, sonra exe.old yine geri | 6 → 5 | çalışır (artık kayıtta) |
| `healthy`, eski hizmet durmadı | temizlik ertelenir | — | durur, silinmez |
| `healthy`, eski EXE kilitli | yeniden başlatmada silme | — | kısayol/ARP/hizmet temiz |
| Yeniden çalıştırma (her durumda) | idempotent; `healthy/done` tekrar hiçbir şey yapmaz | 0 | — |

Sonuç / sınırlar: saf durum makinesi + 16 birim testi (sahte SCM/kayıt/kısayol/ACL, bellek içi dosya sistemi; anahtar test önce boş uygulamaya karşı kırmızı). **Yapılmadı:** Win32 `UpgradeSystem` uygulaması ve kancalar (eski yoldan `--service` → `run_legacy_upgrade`; `--updated-from` sağlık sinyalinin eski pipe'a gitmesi; yeni hizmet açılışında NewService rolü). Bunlar olmadan bugünkü yükseltme yine güvenli biçimde geri alınır (eski sürüm çalışır, her açılışta güncelleme tekrar denenir). Gerçek Windows'ta doğrulanacak varsayımlar ADR'nin R-F2 kaydında listelidir.

**Ek (T3b, 2026-11-24) — Win32 bağlama yazıldı, gerçek cihazda DENENMEDİ.**
- **Bağlama:** `src/platform/windows/legacy_upgrade_system.*` (`WinUpgradeSystem`: `win_setup_ops` ad parametreli SCM/ARP/kısayol yardımcıları — yeni hizmetle aynı SID türü, gecikmeli otomatik, 3× RESTART ve geri okuma; `secure_dir` korumalı DACL; `verify_machine_journal_dir`; `MoveFileEx(DELAY_UNTIL_REBOOT)`; eski EXE geri: `recover_swap(false)` + eski `updates\state.json` 0.9.5 kodeğiyle Idle, tekrar oynatma tabanı korunur), `legacy_health_signal.*` (eski pipe istemcisi). Kancalar: `service_host::run_service` ham yol eski EXE ise ayrı dağıtıcı (eski hizmet adı, pipe sunumu yok, durdurma kabul edilmez); yeni hizmet açılışında arka plan temizlik iş parçacığı (kayıt yoksa anında biter; `WaitingForHost` 5 sn × 120, `CleanupPending` 60 sn × 30); `app_update::signal_update_healthy` eski yoldan `--updated-from` ise eski hizmete sinyal, taşınabilir değişim yolu (işaret + `.old` silme) çalışmaz. Saf kararlar `core/legacy/legacy_entry.*`, `legacy_health.*`.
- **Tehditler ve kararlar:**
  - *Sahte eski hizmet girişi* (kullanıcı/başka süreç `--service` ile başlatır, kopya/junction): yükseltme yalnız ham yol == son yol (`GetFinalPathNameByHandleW`) == eski kurulu EXE, yol zincirinde reparse yok, belirteç SYSTEM, `StartServiceCtrlDispatcherW` bağlandı, SCM'nin **eski** hizmet PID'i == kendi PID'i ve eski ImagePath tam `"<eski exe>" --service` iken. Yol kanonikleştirme göreli, sürücü-göreli, kök-göreli, UNC, `\\?\UNC\`, `\\.\`, `.`/`..`, boş bileşen ve ADS'yi reddeder; büyük/küçük harf ve sondaki ayırıcı yok sayılır.
  - *Güvenilmeyen görüntü*: yükseltilmiş rollerle aynı `broker_trust(kBuildPolicy, Authenticode)`; geçmezse yükseltme yok, eski EXE geri (5). Kopya SHA-256 kendi görüntüsüyle eşit olmadan devredilmez.
  - *Kayıt sahteciliği* (sahte `healthy` ile eski kurulumu sildirme): kayıt `<yeni ProgramData>\setup` altında, bu klasör `kAdminOnlySddl` (SY+BA) ile hazırlanır; yeni hizmet temizlikten önce klasörü `verify_machine_journal_dir` ile doğrular, aksi halde hiçbir şey yapmaz. Makine kökü `kMachineRootSddl` (SY+BA yazar, Kullanıcılar okur) kopyadan önce uygulanır ve geri okunur.
  - *Hatalı hedef* (kayıt/gelecek hata yüzünden yabancı hizmet, ARP, dosya): her yazıcı çağrı `upgrade_name_allowed`/`upgrade_path_allowed` izin listesinden geçer (eski adla hizmet oluşturulmaz; yeniden başlatmada silme yalnız eski kurulum klasörü altında; eski veri klasörüne ACL/yazma yok).
  - *Yeni IPC yüzeyi*: yok. Sinyal tarafı yalnız istemcidir: eski hizmet SCM'de RUNNING, ImagePath beklenen, pipe sunucu PID'i == SCM PID'i (ad gaspında veri gönderilmez); tel biçimi 0.9.5 ile birebir (altın bayt testi). Yeni hizmetin pipe kuralı gevşetilmedi: sağlık denetimi Hello göndermez (Running + PID + pipe sunucu PID'i).
  - *Komut enjeksiyonu*: süreç başlatılmaz; ImagePath yalnız `service_image_path` (tırnaklı) ile yazılır.
  - *Çalışan hizmetsiz kalma*: her hata `healthy`den önce geri alınır ve eski EXE gelir (5); geri getirilemezse 6 (SCM yeniden dener). **Kalan risk:** kimlik/yol doğrulanamayan "Refuse" durumlarında (junction, SYSTEM değil, yabancı PID) eski klasöre güvenilmediği için hiçbir şey yazılmaz ve çıkış 1'dir; SCM'nin 3 yeniden denemesinden sonra eski hizmet durur (eski UI UAC broker yoluyla çalışır). `exe.old` yoksa geri getirme `NotFound` → 6.
- **Testler (birim, sahte):** giriş kararı (önce saplamaya karşı 5 kırmızı vaka, sonra yeşil), yol kuralları, ImagePath, eski durum dosyası biçimi, temizlik planı, altın baytlar + sinyal sırası/retler, çıkış kodu sözleşmesi, `healthy` kapısı (8 faz + bozuk kayıtlar), durum makinesinin tüm hedeflerinin izin listesinde olduğu. `ef_unit_tests` 2439/0; tüm hedefler (uygulama, hizmet, yardımcı) bağlanır.
- **Gerçek turda doğrulanacak varsayımlar:** (1) SCM'nin 0.9.5 hata eylemiyle yeniden başlattığı süreçte `StartServiceCtrlDispatcherW` eski adla bağlanır ve `QueryServiceStatusEx` START_PENDING'de bizim PID'imizi verir; (2) 0.9.5 eski hizmeti `check_client`'ta eski yoldaki yeni EXE'yi kabul eder (imza/SHA kuralı) ve UpdateHealthy'ye `committed` döner; (3) çalışan eski hizmet görüntüsü varken `recover_swap(false)` yeniden adlandırması başarılı olur; (4) SYSTEM bağlamında `FOLDERID_*` yolları ve COM `IShellLink` (ortak Başlat/Masaüstü) çalışır; (5) `ensure_protected_dir(kMachineRootSddl)` geri okuması `install_dir_acl_ok`'tan geçer ve var olan yeni kök/journal ACL'lerini bozmaz; (6) yeni hizmetin başlangıçtaki 1,5 sn RUNNING denetimi ve pipe bağlantısı 60 sn içinde olur; (7) eski UI süreci eski EXE'yi kilitli tutarken temizlik yeniden başlatmada silmeye düşer ve yeniden başlatma sonrası klasör kalkar; (8) eski hizmet `healthy`den sonra çıkış 0 ile durur ve SCM yeniden başlatmaz; (9) imzasız alpha (ADR-034) yolunda `broker_trust` kararı release derlemede beklendiği gibidir; (10) Windows Defender/EDR'nin yeni hizmet oluşturma ve Program Files kopyasına tepkisi.

## ADR-044 — Geçiş yayını (legacy bridge): eski 0.9.5 istemcilerine yeni markalı EXE'yi bir kez eski biçimle ulaştırma (R-F3, 2026-11-24)

Bağlam (kaynaktan gözlenen, 059b3ee3): eski istemci (UI `update_support.cpp` + kurulu sürümde hizmet `service_host.cpp`/`staged_update.cpp`) yalnız `https://github.com/ykp61/iTunex-Pro-releases/releases/latest/download/manifest.json` okur ve şunları denetler: zarf `domain == "release"`, `key_id` gömülü anahtar (`release_key.h`), imza ECDSA P-256 r||s, mesaj `"iTunexPro:v1:" + domain + ":" + key_id + NUL + payload`; manifest alanları `version, arch, url(≤512), sha256(64), channel, publisher(≤128), min_os_build, size(1..2^31), manifest_version(≥1)`, isteğe bağlı `halt`(bool); sıra: `manifest_version > replay_floor(highest_seen)` (taban = en yüksek görülen − 1; eşit son manifest tolere edilir), `publisher == "iTunex Pro"`, `channel == "alpha"`, `arch == "x64"`, `min_os_build ≤ OS build`, URL https/host'ta `@` yok, `halt` → durdur, `version > çalışan sürüm`, gömülü anahtar var; ardından `release_url_allowed` (önek `…/iTunex-Pro-releases/releases/download/`, kalan yalnız `[A-Za-z0-9._/-]`, `..` yok), `update_host_allowed` (https, github.com / objects.githubusercontent.com / release-assets.githubusercontent.com, yönlendirme her adımda), boyut ≤ 64 MiB, indirilen ve hizmetin diskten geri okuduğu baytların SHA-256'sı. Yeni marka bu sabitlerin tümünü değiştirdi (önek `EcuFps:v1:`, yayıncı `Ecu Fps`, depo `Ecu-Fps-releases`); yayın anahtarı değişmedi (`rel-7e5df9066e6c47ad`, `release_key.h` 059b3ee3 ile aynı).

Karar:
- **Tek seferlik geçiş yayını eski depoya:** yük yeni markalı EXE; manifest `release_sign.py --legacy-bridge` ile eski önek, eski yayıncı, eski URL öneki + `v<sürüm>/iTunexPro-Setup-<sürüm>.exe`, `manifest_version` 20 (eski depodaki son 19), kanal/mimari/`min_os_build` normal yayınla aynı, aynı yayın anahtarı. Araç geçiş kipinde `manifest_version ≤ 19`, sürüm ≤ 0.9.5, farklı varlık adı, yeni yayıncı/URL ile imzalamaz. Eski adlar yalnız `release_sign.py` legacy bloğunda, `publish_release.ps1` legacy bloğunda ve `scripts/legacy_bridge*` dosyalarında geçer.
- **Neden eski önek:** eski istemcinin doğrulayıcısı ikili içinde sabittir; yeni önekli manifest imza aşamasında `VerifyFailed`/`bad_envelope` olur (aşağıdaki kanıt). Gömülü anahtar aynı olduğundan aynı özel anahtarla eski önekli imza yeterlidir; eski istemciye yeni anahtar veya kod ulaştırmanın başka yolu yoktur.
- **`publish_release.ps1 -LegacyBridge`:** hedef eski depo, varlık adı eski kurulum adı, Türkçe geçiş notu, `gh release create … --latest`; yerel CI kapısı, DryRun varsayılanı ve yalnız `-Publish` ile yayın kuralı aynı. Normal kip yeni depoya gider ve önceki `manifest_version`'ı **en az 20** sayar (yeni depoda ilk = 21): geçişle gelen istemci R-F1 tercih göçüyle tabanı 20 taşır; daha küçük `manifest_version` yeni depoda Replay olurdu.
- **Anahtar bulma:** gerçek anahtar marka değişikliğinden önce `%APPDATA%\iTunexProRelease` altında eski DPAPI entropisiyle mühürlüdür. `release_sign.py` yeni klasörde anahtar yoksa eski klasörü kullanır ve önce yeni, sonra eski entropiyi dener; eski klasörde anahtar varken `keygen` reddeder (aksi halde `release_key.h` sessizce yeni anahtara geçer ve kurulu istemciler güncelleme alamazdı). Bu, normal yayını da etkileyen bir rename hatasıydı.
- **Eski yoldan girişler (R-F2'nin kalan iki maddesi):** `legacy_launch_route` (saf) + `main.cpp`: eski kurulum yolundan boş (eski kısayol) ve `--tray` (eski Run) → Ui (kurulum penceresi değil); `--uninstall` (ve tanınmayan argüman) `<yeni ProgramData>\setup\legacy-upgrade.json` varken (erişilemeyen durum dahil) hiçbir şey yapmadan 21.

Kanıt (çevrimdışı, `scripts/legacy_bridge_proof.ps1`): 059b3ee3 geçici worktree'si; `scripts/legacy_bridge/old_client_probe.cpp` eski `json/catalog/envelope/update_check/staged_update/catalog_signature` kaynaklarıyla ve HEAD kaynaklarıyla iki kez derlenir; gömülü anahtar yerine geçici anahtar; UI kararı + indirme eşleşmesi + hizmet alıcısı (begin/chunk/commit, SHA geri okuma) çalışır. Sonuç 16/16 beklenen: geçiş → eski KABUL (`Mandatory`, hizmet `none`), HEAD RED (`bad_envelope`); normal → eski RED (`bad_envelope`), HEAD KABUL; eski üzerinde `manifest_version` 18 → `replay`, yeni yayıncı → `wrong_target`, yeni URL → `bad_url`, yanlış SHA → indirme reddi, 0.9.5 → `not_newer`, `halt` → `halted`. Mock/derleme kanıtıdır; gerçek eski kurulum turu değildir.

Kalan riskler (yalnız gerçek eski istemci turu doğrular): GitHub `latest` yönlendirmesi ve varlık indirmesinin eski HTTP istemcisinden geçmesi (yeni host adı çıkarsa eski izin listesi reddeder); eski UI'nin indirme sonrası yeni EXE'yi `--updated-from` ile başlatması ve eski hizmetin eski yoldaki **yeni markalı, imzasız** EXE'yi `check_client`'ta kabul edip `UpdateHealthy`'e `committed` dönmesi; SCM'nin yeni ikiliyi eski hizmet adıyla yeniden başlatması ve R-F2 yan yana kurulumunun tamamlanması; R-F1 göçünün `release_manifest_seen`'i (20) taşıması; SmartScreen/Defender'ın yerinde değişen imzasız EXE'ye tepkisi; geçiş yayını eski depoda `latest` kalmazsa (sonradan eski depoya başka yayın) eski istemcinin onu görmemesi. Geçiş tek kullanımlıktır: eski depoya sonradan yalnız `-LegacyBridge -Halt` (acil fren) konabilir. **Ön koşul:** eski depo GitHub'da yeniden **adlandırılmamalı**; yeni depo ayrı oluşturulmalı. Ad değişikliğinde GitHub eski URL'yi yeni depoya yönlendirir ve eski ile yeni istemci aynı `latest/manifest.json`'u okur (tek dosya iki öneki taşıyamaz). Bu durum depodan gözlenemedi (ağ yok); yayından önce sahip doğrular.

## ADR-045 — Dahili kod öneki `itx`/`ITX` → `ef`/`EF` (marka değişikliği faz B, R-F4, 2026-11-24)

Bağlam: Faz A görünür markayı Ecu Fps yaptı; kodda eski markadan türeyen `itx` öneki (ad alanı, makrolar, CMake hedefleri, ortam değişkenleri, dosya adları, görev numaraları) kalmıştı. Yeni kısa biçim `EF` (marka amblemiyle aynı).

Karar (tek mekanik geçiş, takma ad/uyumluluk katmanı yok):
- `namespace itx` → `ef`; `ITX_TEST/ITX_CHECK/ITX_*` makroları ve CMake/ortam değişkenleri → `EF_*` (ör. `EF_DATA_DIR`, `EF_TEST_BACKGROUND`, `EF_SETUP_ROLE`, `EF_UI_RENDERER`, `EF_LEGACY_DATA_DIR`, `EF_RELEASE_TRUST`, `EF_UNSIGNED_ALPHA`); `itx_*` CMake hedefleri, testler, yardımcı EXE'ler → `ef_*` (`ef_core`, `ef_unit_tests`, `ef_ui_snapshot`, `ef_frame_probe` ...); `itx` ile başlayan 54 dosya adı `git mv` ile `ef`/`EF`; görev numaraları `ITX-X25` → `EF-X25` (backlog/BOARD/progress/kanıt dosyası adları/CSV/betikler aynı geçişte; `task_record.py --check` ok); ikili içi işaretler `EF-UNSIGNED-ALPHA`, `EF-DEV-UNSIGNED-BROKER-ALLOWED` (yalnız derleme çıktısı/ikili dizesi; `check_trust.ps1` ve birim testiyle birlikte).
- Yerel değişken `ef` (`storage_test.cpp`) → `hfind` (ad alanıyla çakışırdı).
- Ortam değişkenlerinin **eski adları artık okunmaz** (`ITX_DATA_DIR` vb.): yalnız geliştirici/test düzeneği; son kullanıcı yolu etkilenmez. Yerel derleme klasörleri (`build\*`) yeniden yapılandırılmalıdır (önbellek anahtarları değişti).
- GitHub depo değişkeni `ITX_RUNS_ON` → `EF_RUNS_ON` (iş akışı ve belgeler). **Operatör adımı:** depoda eski adla tanımlı bir değişken varsa `EF_RUNS_ON` olarak yeniden oluşturulmalı; aksi halde iş akışı sessizce `windows-2025`'e düşer.

**Değişmeyen (kalıcı / tel / sunucu) dizeler — bilerek `itx` kalır:**
- `itx-bench.marker` (`storage_rules.h`, `app_files.h`): kullanıcı diskindeki benchmark artığı klasör işareti; eski sürümlerin bıraktığı artıklar temizlikte tanınmalı. İçeriği zaten `EcuFps-bench-v1`.
- `"itx-machine:"` öneki (`journal_fs.cpp`): makine kimliği özetinin girdisi; değişirse makine kimliği ve journal eşleşmesi bozulur (docs/04: "marka bağımsız").
- `ITXP` (`0x50585449`, `ipc/protocol.h`, `legacy_health.h`, `test_legacy_health.cpp`, ilgili belgeler): IPC çerçeve sihirli sayısı; eski 0.9.5 sağlık sinyali altın baytlarıyla birebir.
- Sunucu tarafı dağıtılmış ortam adları `ITX_KEY_PEPPER`, `ITX_KEK_HEX` (`services/ecufps-api/server.py`) ve `ITX_SITE_VERSION_FILE` (`site/server/update_version.py`): çalışan sunucuda tanımlı olabilir; yeniden adlandırma sessizce pepper/KEK'i boşaltır ve anahtar özetlerini geçersiz kılar. Sunucu yapılandırması taşınırken ayrı kararla yeniden adlandırılır.
- Eski dal adı `claude/itx-ui-live` (git geçmişi/dal adı; belgelerde anıldığı yerde aynen).
- Eski depo kaynakları `itx::` ad alanını kullanır: `scripts/legacy_bridge/old_client_probe.cpp` `LEGACY_NS` makrosuyla (varsayılan `ef`; `legacy_bridge_proof.ps1` eski derlemeye `/DLEGACY_NS=itx` verir). Kanıt matrisi değişmedi (16/16).

Kanıt: sıfırdan yapılandırılan `x64-debug` tüm hedefleriyle (+ `EXCLUDE_FROM_ALL` `ef_speedtest_probe`) hatasız derlendi; `ef_unit_tests` 2440/0 (öncekiyle aynı sayı); `ctest -N` 65 test, adlar yalnız `itx_`→`ef_` ön ekiyle aynı; `test_release_sign` 34/0; `services/ecufps-api/test_contracts.py` 17 (1 atlanır); `legacy_bridge_proof.ps1` 16/16 aynı kabul/red matrisi; `cmake --preset x64-dist` (geçici klasörde) `EF_RELEASE_TRUST`/`EF_UNSIGNED_ALPHA` çözer. Çalıştırılmadı: `windows.*` ctest (pencere açar), tam `x64-dist` derlemesi, gerçek cihaz.
