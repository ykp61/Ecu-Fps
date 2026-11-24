# Oyun kabul turu: EF-X18, EF-X20 (EF-X15 kaldırıldı)

Bu tur yalnız kullanıcı başlatınca ve yanındayken yapılır. Ajan bu turu kendisi çalıştırmaz. Kod ve birim
testleri hazır (`34e7d02`, `0dddb66`, `f24ebfb`, düzeltmeler `43600fe`, `fb9836d`, `1510cbb`, `8abfb38`).
Bu turda sistem/oyun/sürücü ayarı yazılmaz; yalnız ölçülür ve kaydedilir.

## Değişmez kurallar

- **Yalnız çevrimdışı, tek oyunculu, koruma yazılımı olmayan bir oyun** veya uygulamanın kendi Uygulama Testi.
  Çevrim içi, eşleştirmeli veya korumalı maça girilmez (CS2 Premier/Faceit, Valorant, EAC/BattlEye oyunları dahil).
  Koruma kapatılmaz, atlatılmaz. Sıfır ban riski garantisi yoktur.
- EasyAntiCheat, EasyAntiCheat_EOS, BEService veya vgc servisi **çalışıyorsa tur durur**.
- Oyun `builtin_matrix()` içinde olmamalı (CS2, Valorant vb. listede; hepsi Unverified ve kapalı).
- Ürünün Oyunda Test kaynağı kapalı kalır: doğrulanmış satır yok, kullanıcı geçersiz kılma düğmesi yok.
  X18 ölçümü geliştirici aracı `ef_game_capture.exe` ile yapılır (ürün ZIP'inde yok).
  Sonradan matris satırı eklemek ayrı, incelenmiş bir kod değişikliğidir.
- UAC'ye yalnız kullanıcı basar. Ret `capture_uac_denied` olarak kaydedilir, döngüde tekrar denenmez.
- Kullanıcı "dur" derse veya başka oturumda oyun oynamaya başlarsa tur hemen durur.
- Okunamayan her ayar `unknown` yazılır; tahmin yazılmaz. Atlanan adım `skipped`/`not_run` kalır, geçti sayılmaz.
- `validated_calibration.h` doldurulmaz; `--emit-header` kullanılmaz. Tek PC'nin verisi kapıyı açamaz.

## Hazırlık

1. Temiz ağaçtan derleme: `git status` boş, `build_id` sonunda `-dirty` yok (build_id artık her derlemede
   yeniden üretilir).
2. Yönetici PowerShell'de ön kontrol (salt okuma, hiçbir şey değiştirmez):
   `powershell -File scripts\game_round.ps1 -Check`
   Kayıt: tarih, commit, build_id, OS build.UBR, GPU ve sürücü, koruma servisleri, güç kaynağı, açık ETW oturumları.
3. Kanıt klasörü: `artifacts/evidence/game-round-<tarih>/` (betik oluşturur; ham çıktılar buraya).

## A. EF-X18 gerçek oyun ölçümü (yönetici, geliştirici aracı)

Oyunu kullanıcı kendisi açar, sabit sahneye (aynı kayıt/konum veya oyunun kendi benchmark'ı) getirir.

| Adım | Komut | Beklenen | Kaydet |
|---|---|---|---|
| A1 taban (T02) | `scripts\game_round.ps1 -Capture <oyun.exe> -Seconds 60` | `outcome=complete`; kayıp olay yoksa geçerli | build_id, oyun adı/mağaza kimliği/sürümü, koruma (yok), `api_confidence`, sahne, çözünürlük, render ölçeği, FPS limiti, VSync/VRR, upscaler ve modu, kare üretimi, HAGS, render/displayed/generated kare sayıları, `display_intervals`, `events_lost`/`buffers_lost`/`overflowed`/`dropped` ayrı, geçerlilik ve dışlamalar, ort./%1 düşük/p99 |
| A2 ek yük (T03) | Aynı sahne, ≥3 ABAB çift: açık kolda benchmark sırasında `-Capture`, kapalı kolda yalnız benchmark | Yalnız medyan/min/maks fark; "düşük yük" hükmü yok | Her kol: collector açık/kapalı, kaynak `game_reported`, koşul parmak izi (iki kolda aynı ayarlardan), geçerli mi, oyunun kendi ort. FPS'i, varsa p99. ETW FPS'i hiçbir kolda kullanılmaz. Ayar değişen çift atılır; 3'ten az geçerli çift = `InsufficientPairs`. Oyunun benchmark'ı yoksa T03 `not_run (ETW'den bağımsız FPS kaynağı yok)` |
| A3 oyun kapanışı (T04) | `-Capture <oyun.exe> -Seconds 60`, ~15. saniyede oyundan normal çıkış (ilerleme kayıtlı) | `outcome=game_exited`, `exited_after_s`≈15, toplam süre 60'tan çok kısa, manifest geçersiz (`game_exited`), ETW oturumu kalmaz | Betiğin önce/sonra ETW oturum farkı; artık yardımcı süreç yok. Ürün UI/IPC yolu bu turda **denenmedi** diye yazılır |
| A4 kapı (T01, isteğe bağlı) | Uygulamada korumalı bir oyun kurulu ama maçta değilken Ölçümler → Oyunda Test | Kaynak kapalı, neden yazılı; UAC/yardımcı süreç yok | Görülen neden metni |

Kayıp olay/tampon varsa koşu geçersiz kaydedilir; en fazla bir kez tekrarlanır, iki kayıt da tutulur.

## B. EF-X15 canlı FPS paneli — kaldırıldı

Sahip isteğiyle (Uyumluluk B10, 2026-11-14) canlı FPS paneli, "Test ayarları" bölümü ve hedef FPS üründen
kaldırıldı; bu turun B adımları artık uygulanmaz. Eski geçmiş kayıtlarındaki `live_panel=preview_on` koşulu
okunmaya devam eder: bu kayıtlar panelsiz koşularla karşılaştırılmaz ("Canlı panel" farkı notu).

## C. EF-X20 kalibrasyon verisi (yalnız Uygulama Testi, oyun yok)

Kapıyı açmaz; yalnız gerçek, gruplu ve etiketi kontrollü deneyle verilmiş örnek toplar.

Ortam (yeni bir uygulama oturumundan önce, aynı PowerShell'de):

```
$env:EF_CALIB_EXPORT='<yerel yol>\diagcal_v2.json'
$env:EF_CALIB_LABEL_METHOD='controlled_experiment'
$env:EF_CALIB_MACHINE_GROUP='op-m1'          # opak ad; bilgisayar/kullanıcı adı veya seri no değil
$env:EF_CALIB_MACHINE_DIGEST='<operatörün bu makine için seçtiği sabit onaltılık değer>'
$env:EF_CALIB_RUN_GROUP='run-01'             # her koşuda yeni
$env:EF_CALIB_LABEL='gpu_bound'; $env:EF_CALIB_FINE_LABEL='gpu_bound'
```

| Adım | Eylem | Beklenen | Kaydet |
|---|---|---|---|
| C1 | GPU sınırlı: en ağır sahne, FPS sınırlayıcı/overlay yok (kullanıcı onaylar) | Örnek yazılır | run_group |
| C2 | Sağlıklı hafif yük: `healthy`/`healthy`, en hafif sahne | Örnek yazılır | run_group |
| C3 | Her koşulu ≥3 ayrı koşu | — | — |
| C4 (negatif) | `EF_CALIB_LABEL=frame_cap_or_present_wait`, `EF_CALIB_FINE_LABEL=vsync`, bir koşu | Dosya değişmez (ölçülmemiş limit durumu reddedilir) | Önce/sonra SHA-256 |
| C5 | `scripts\game_round.ps1 -Calibrate <dosya>` | `format=diagcal_v2`, `integrity ok=1`, kapı kapalı (`diag.gate.no_real_holdout` veya `machine_coverage`), çıkış kodu 3. 5 = sızıntı: incele, örnek düzenleme | Dataset SHA-256, ince etiket başına sayı, gate/holdout/model/karışıklık satırları |
| C6 | Uygulama tanı kartı | Kapı kapalı metni; CPU/GPU arıza hükmü yok | Ekran metni |

Atlama: sahne iptal, yetersiz kare, PDH aralığı aşımı, ısıl kısma veya güç kaynağı değişimi → `veri yetersiz`.
`rule_derived` etiketi kullanılmaz; örnekler elle düzenlenmez veya makineler arası kopyalanmaz.
Bu tur kanıtlayamaz: gerçek donanımda T02 (ölçülmüş limit/VSync dışa aktarımı yok), T03 (aile bağlantısı yok),
çok aileli kapsam (Intel/AMD, dizüstü, HDD) ve kapının açılması (≥3 makine, ≥10/10 dengeli uygun holdout).

## Sonuç raporu

Her adım için geçti / başarısız / atlandı / çalıştırılmadı. Kanıt dosyaları:
`artifacts/evidence/EF-X18-REAL-<tarih>.md`, `EF-X15-REAL-<tarih>.md`, `EF-X20-REAL-<tarih>.md` ve ham
çıktılar. `integration/ecufps-expansion-v1/status.json` içinde `hardware_evidence` yalnız gözlenenle güncellenir;
durum `review` üstüne çıkmaz. Birim/mock geçişleri gerçek cihaz kanıtından ayrı yazılır.

## Ek: başlatıcı kapsam tablosu (P9-B3, 2026-11-18)

Bu tablo tur adımı değildir; kitaplık taramasının (`src/core/games/library_scan.*`) hangi başlatıcıyı neyle kanıtladığını gösterir.
Fixture'lar `tests/fixtures/launchers/**` altındadır ve **sentetiktir** (gerçek başlatıcı örneği değildir). Aşırı büyük girdiler
depoyu şişirmemek için testte üretilir. "Gerçek cihaz" sütunu yalnız geliştirme PC'sinde salt okuma taramayla görülen şeyi söyler;
başka bir sürüm, kullanıcı veya donanım için kanıt değildir. Sahte dosya sistemi/kayıt defteri testi gerçek cihaz testi sayılmaz.

| Başlatıcı | Ayrıştırıcı / okuma | Fixture korpusu (geçerli / bozuk / kesik / boş / aşırı büyük) | Fail-closed test | Gerçek cihaz (bu PC, salt okuma) | Üründe |
|---|---|---|---|---|---|
| Steam | `classify_steam_appmanifest`, `parse_steam_libraryfolders`, `steam_vdf_complete` | `steam/*.acf`, `*.vdf`: 3 / 3 / 3 / 1 / okuma sınırı (`TooLarge`) | `b3_steam_*`, `library_*` | Okundu: 5 oyun, 1 yardımcı paket dışlandı, 0 hata | Açık |
| Epic | `parse_epic_manifest` (`.item` JSON) | `epic/*.item`: 2 / 5 / 2 / 1 / gerçek dosya sisteminde 1 MiB+1 | `b3_epic_*`, `library_epic_*` | Okundu: 3 oyun, 0 hata | Açık |
| Riot | `parse_riot_product_install_path` (yaml) | `riot/*.yaml`: 2 / 5 / 2 / 1 / okuma sınırı (`TooLarge`) | `b3_riot_*`, `t05_riot_*` | Okundu: 1 ürün (yalnız bilinen iki ürün kimliği taranır) | Açık |
| Xbox | `parse_microsoftgame_config_exe` (+ paket envanteri) | `xbox/*.config`: 3 / 5 (yorum satırındaki exe dahil) / 2 / 1 / okuma sınırı (`TooLarge`) | `b3_xbox_*`, `x11_*` | Paket API çalıştı (`--xbox`): oyun paketi yok. **DOĞRULANMADI** (GDK oyunu kurulu değil) | Kapalı (`kXboxPackagesValidated=false`) |
| GOG | kayıt defteri `GOG.com\Games` | `registry/gog_*.json`: 1 / 1 / 1 (yarım numaralandırma) / denied / missing / 4001 kayıt | `b3_gog_*`, `x11_t01_*` | Anahtar yok. **DOĞRULANMADI** | Kapalı (`kRegistryProvidersValidated=false`) |
| Ubisoft | kayıt defteri `Ubisoft\Launcher\Installs` | `registry/ubisoft_*.json`: aynı set | `b3_ubisoft_*` | Anahtar yok. **DOĞRULANMADI** | Kapalı (`kRegistryProvidersValidated=false`) |
| Battle.net | `battlenet_list_from_uninstall_entries` (Uninstall girişleri; süzme çekirdekte) | `registry/battlenet_*.json`: 1 / 1 (süzülen) / 1 / yarım numaralandırma | `b3_battlenet_*` | Okuma yolu çalıştı, Battle.net kurulu değil (kaynak yok). **DOĞRULANMADI** | Açık, ancak gerçek kayıt biçimi doğrulanmadı |
| EA app | yalnız kayıt defteri kurulum yolu (`Install Dir`); şifreli/belgelenmemiş manifest **okunmaz** | `registry/ea_*.json`: 1 / 1 / 1 (yarım) / denied / missing | `b3_ea_*` | EA kurulu değil, anahtar yok. **DOĞRULANMADI** | Kapalı: sağlayıcı "Unsupported" (`kEaRegistryValidated=false`); açılsa bile exe/sürüm/boyut "Unsupported" kalır |
| Elle eklenen EXE | `check_manual_exe` | `test_library_xbox_manual.cpp` | mevcut | Uygulanamaz | Açık |

Çapraz kaynak tekilleştirme: aynı kurulum klasörünü (`game_watch_key`) bildiren birden çok başlatıcı satırı tek satıra iner
(Steam, Epic, GOG, Ubisoft, Xbox, Battle.net, Riot, EA sırasıyla ilk gelen kalır; `LibraryScanResult::duplicates_merged`).
Sağlayıcının kendi "gördüm" sayısı değişmez, güven hesabı (Partial/Failed) etkilenmez. Testler: `b3_dedupe_*`.

Bilinen sınırlar (kanıtsız genelleme yok): Epic `.item` içindeki DLC/eksik kurulum işaretleri okunmuyor; Battle.net ve GOG/Ubisoft/EA
kayıt yerleşimleri kamuya açık bilgiye dayanır, bu PC'de görülmedi; birden fazla Windows kullanıcısı veya Steam kütüphane ayrımı
(HKEY_USERS yolu) gerçek cihazda sınanmadı. EA için 2026-11-18'de bu PC'de yalnız anahtarın yokluğu gözlendi.
