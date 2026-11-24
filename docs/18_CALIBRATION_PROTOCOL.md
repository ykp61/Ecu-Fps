# Darboğaz sınıflandırıcısı — kalibrasyon protokolü (DIAG-06 / BM-02)

Durum: **2026-11-10 (EF-X20) — araç, gruplu bölme, sızıntı denetimi ve `diagcal_v2` hazır; gerçek
donanım veri seti henüz yok.**
Bu nedenle üretim kapısı kapalıdır: CPU/GPU çalışma anı hükmü yayınlanmaz
(`diag.gate.no_real_holdout`). Sentetik fikstür gerçek doğruluk sayılmaz.

Amaç: “darboğazı ~%80 doğrulukla tespit” hedefini ölçülebilir hale getirmek. Sınıflandırıcı
(`compute.cpp`) pencere başına GPU/CPU/cap sınıfı üretir. Bu sınıfın güvenilirliği ancak
**etiketleri sınıflandırıcıdan bağımsız** gerçek donanım pencereleriyle ölçülebilir.

## 1. Kapı kuralları

| Kural | Değer | Kaynak |
|---|---|---|
| En az gerçek holdout örneği | 30 | `kMinRealHoldout` |
| Yanlış pozitif oranı üst sınırı | %5 — pay ve payda **yalnız kapıya uygun gerçek holdout** örnekleri (sentetik veya uygun olmayan örnek oranı seyreltemez; EF-X20 doğrulama bulgusu 1) | `kMaxFalsePositiveRate`, `eligible_false_positive` |
| Sentetik örnek | Gerçek sayılmaz; kapıyı açamaz | `release_status` |
| Bölme (v2) | `machine_group` FNV özeti → holdout (yaklaşık üçte bir, makine bazında); kalan makinelerde `run_group` özeti → tune/validation | `split_for` |
| Bölme (v1) | `id` FNV özeti → tune/holdout; v1 gerçek örnek kapıya **uygun değildir** | `split_of` |
| Kapıya uygun gerçek örnek | `real` + iki grup dolu + `machine_digest` dolu + `label_method` ∈ {`controlled_experiment`, `expert_review`}. Özetsiz örnek okunur ama uygun değildir (`missing_digest` olarak raporlanır; bulgu 3) | `gate_eligible` |
| Farklı holdout makinesi | ≥ 3 | `kMinHoldoutMachines` |
| Sınıf dengesi | Holdout'ta ≥ 10 sınırlayıcı ve ≥ 10 healthy/insufficient uygun örnek | `kMinHoldoutPerSide` |
| Cihaz ailesi kapsamı | Aile (CPU üreticisi × GPU ailesi × kasa tipi + depolama) holdout'ta ≥ 10 uygun örnek **ve ≥ 2 farklı `machine_group`** ile temsil edilmeli (tek PC aileyi kapsamaz); bilinmeyen aile kapsam dışıdır | `kMinFamilyHoldoutSamples`, `kMinFamilyHoldoutMachines`, `release_status_for_device` |
| Bölme bütünlüğü | Sızıntı/yinelenmiş makine bulunursa kapı kapalı (`diag.gate.split_leakage`), araç çıkış kodu 5 | `validate_split_integrity` |
| Üretim | Kapı yalnız derlemeye gömülü doğrulanmış özetle açılır | `validated_calibration.h` |

Kapı sırası: `no_real_holdout` → `label_or_group_missing` → `machine_coverage` → `class_imbalance` →
`false_positive_rate` → `ready`; bütünlük hatası her durumda `split_leakage` verir. Kapı açık olsa bile
cihaz ailesi kapsanmıyorsa `diag.gate.device_family_uncovered` (CPU/GPU hükmü yok). Üretimdeki
`diagnosis_release_status()` cihaz ailesini henüz envanterden almaz (`DeviceFamily{}` = bilinmiyor);
bu yüzden ileride gerçek bir başlık gömülse bile aile bağlanana kadar kapı kapalı kalır (fail-closed).

Bütünlük denetimi (`validate_split_integrity`) şu **engelleyici** sorunları raporlar (`issues`; kapı
`split_leakage`, araç çıkışı 5): `run_group_multi_machine` (aynı koşu iki makinede),
`duplicate_window_cross_split` (aynı ölçüm farklı kimlikle karşı kümede), `duplicate_window_multi_machine`
(aynı ölçüm başka `machine_group` altında — aynı bölmede bile; makine/sınıf sayısını şişirmeyi yakalar),
`duplicate_window` (aynı ölçüm aynı makine ve bölmede yeni kimlik/koşu adıyla; FP oranını sulandırmayı yakalar),
`duplicate_holdout_machine` / `duplicate_machine` (aynı `machine_digest` iki makine adı altında),
`holdout_machine_leak` (savunma).

**Engellemeyen not** (`notes`; çıkış kodunu değiştirmez): `ungrouped_real_sample` — grupsuz (v1) gerçek
örnek. Bu sızıntı değildir; örnek zaten kapıya uygun sayılmaz. Bu yüzden v1 gerçek veri seti
`no_real_holdout` (< 30 gerçek) veya `label_or_group_missing` (≥ 30 gerçek, uygun < 30) ile kapalı kalır,
`split_leakage` / çıkış 5 vermez (EF-X20 doğrulama bulgusu 4).

Yinelenmiş makine denetimi `machine_digest`'e dayanır; bu nedenle v2'de özet kapı uygunluğu için
zorunludur (bulgu 3). Özet **operatör beyanıdır**: uygulama donanım parmak izi toplamaz/hesaplamaz.
Operatör aynı PC için farklı özet verirse yalnız pencere izi (`duplicate_window_*`) kopya ölçümü yakalar;
aynı PC'de yeni ölçümlerle farklı adlar altında toplanan veri bu denetimle **kanıtlanamaz** — bu yüzden
gerçek çok makineli veri seti katılımcı/oturum kaydıyla (EF-X22/X24) birlikte toplanır.

Yanlış pozitif: etiket Healthy/Insufficient iken sınıflandırıcının sınırlayıcı (GPU/CPU/cap) demesi.
Çekimserlik (Insufficient/Mixed cevap) yanlış sayılmaz; ayrı raporlanır. Kapı oranı yalnız uygun gerçek
holdout örneklerinden hesaplanır: `eligible_false_positive / eligible_real`. `holdout ... fp=` ve
`model compute ... fp_rate=` satırları tüm (karışık) holdout kümesinin rapor değerleridir, kapı değildir.

Raporlanan oranlar (`ef_diag_calibrate`):

- `accuracy` = doğru / tüm holdout (çekimserler yanlış sayılır) — en temkinli ölçü.
- `answered_accuracy` = doğru / (tüm − çekimser) — cevap verilenlerde isabet.
- `coverage` = cevap verilen / tüm — çekimserlik oranının tersi.

%80 hedefi için **answered_accuracy ≥ 0.80 ve coverage ≥ 0.50** araştırma kabul ölçütü olarak
kullanılır; kapı ayrıca yanlış pozitif oranını arar. Bu iki ölçüt birlikte raporlanmadan
“%80 doğruluk” iddiası yazılmaz.

## 2. Veri seti biçimi (`diagcal_v1`)

Tek JSON dosyası; kare dizisi yerine pencere özeti taşır (sınıflandırıcı yalnız bu özeti kullanır).

```json
{
  "schema_version": 1,
  "format": "diagcal_v1",
  "samples": [
    {
      "id": "real-2026-11-08-gpu-001",
      "provenance": "real",
      "label": "gpu_bound",
      "avg_fps": 118.4,
      "p99_ms": 9.1,
      "frames": 240,
      "refresh_hz": 144.0,
      "fps_cap_known_none": true,
      "vsync": "off",
      "frame_interval_cv": 0.05,
      "gpu_busy_ms": 7.9,
      "metrics": { "gpu_engine_3d_pct": 96.0, "cpu_max_core_pct": 58.0 }
    }
  ]
}
```

Alanlar:

- `id` — opak, tekil; FNV bölmesini belirler. Kişi/oyun/makine adı yazılmaz.
- `provenance` — `real` | `synthetic`. Yalnız `real` kapıya sayılır.
- `label` — `gpu_bound` | `cpu_or_engine_bound` | `frame_cap_or_present_wait` | `mixed` | `healthy` | `insufficient`.
- `avg_fps` — pencerenin ölçülen ortalama FPS’i (>0).
- `frames` — pencere içindeki geçerli aralık sayısı; en az 100 (sınıflandırıcı eşiği).
- `p99_ms`, `refresh_hz`, `fps_cap`, `fps_cap_known_none`, `vsync`, `frame_interval_cv`,
  `gpu_busy_ms` — isteğe bağlı, ölçüldüyse yazılır; uydurulmaz.
- `metrics` — yalnız `gpu_engine_3d_pct`, `cpu_max_core_pct`, `cpu_main_thread_pct`; pencereyle
  hizalı ve çözünürlüğü yeterli (RootCandidate) değerler.

Okuyucu bilinmeyen alanı, bilinmeyen metrik adını, tekrarli `id`’yi, aralık dışı değeri reddeder.
Yeni şema sürümü tahmin edilmez (`SchemaTooNew`).

### 2.1 `diagcal_v2` (EF-X20)

`"schema_version": 2, "format": "diagcal_v2"`. v1 alanlarına ek olarak her örnekte **zorunlu**:

- `machine_group`, `run_group` — opak, `[a-z0-9_-]`, ≤ 64 karakter. Ana makine adı, seri no, kullanıcı
  adı yazılmaz. Holdout makine bazında, tune/validation koşu bazında ayrılır.
- `label_method` — `controlled_experiment` | `expert_review`. `rule_derived` **reddedilir** (ölçüm
  kuralından türetilen etiket aynı kuralı doğrulayamaz).
- `cpu_vendor` (`intel|amd|other|unknown`), `gpu_vendor` (`nvidia|amd|intel|other|unknown`),
  `form_factor` (`desktop|laptop|unknown`), `storage_kind` (`ssd|hdd|unknown`). Tahmin edilmez; bilinmiyorsa
  `unknown` (aile kapsamına girmez).
- `machine_digest` — küçük harf hex (8–64), opak donanım özeti; yinelenmiş makineyi yakalar. Okuyucuda
  isteğe bağlıdır ama **kapı uygunluğu için zorunludur** (yoksa örnek uygun değildir, `missing_digest`).
  Seri no, UUID, MAC veya ana makine adı yazılmaz; operatör kendi seçtiği opak değeri verir.

`label` v2'de ince etikettir:

| İnce etiket | Compute sınıfı |
|---|---|
| `gpu_bound` | `gpu_bound` |
| `cpu_or_engine_bound`, `cpu_main_thread` | `cpu_or_engine_bound` |
| `frame_cap_or_present_wait`, `fps_cap`, `vsync`, `engine_frame_limit` | `frame_cap_or_present_wait` |
| `mixed`, `healthy`, `insufficient` | aynı adla |
| `ram_pressure`, `storage_wait` | compute dışı (memory_storage modeli; puanlanmaz) |
| `thermal_power` | compute dışı (platform_limits modeli; puanlanmaz) |
| `unknown` | compute dışı |

Compute dışı örnekler `total`'a girmez (`out_of_scope`). v1 belgeleri okunmaya devam eder; v1 belgede v2
alanı bilinmeyen alandır. Yalnız v1 alanlı belge yine v1 olarak yazılır; v1 ve v2 örnek aynı belgeye
karışamaz (eksik grup/yöntem uydurulmaz).

## 3. Toplama protokolü (etiket bağımsız olmalı)

Etiket, sınıflandırıcının çıktısından **değil**, deney tasarımından gelir. Her koşu tek bir
baskın sınırlayıcı üretecek şekilde kurulur; koşu başına pencere özetleri kaydedilir.

| Etiket | Kontrollü koşul (gerçek donanım) | Not |
|---|---|---|
| `gpu_bound` | Sahne yükü yüksek kademe; GPU %90+, CPU çekirdeği doygun değil | Uygulama Testi sahnesi |
| `cpu_or_engine_bound` | GPU boşta kalacak şekilde tek iş parçacığı/çekirdek doyurulur | Yük dışarıdan, ölçülen oyun/benchmark aynı |
| `frame_cap_or_present_wait` | Bilinen FPS sınırı veya VSync açık; aralıklar cap periyodunun katı | Cap/VSync durumu doğrulanmış |
| `healthy` | Cap yok, kare akışı kararlı, GPU/CPU sınırlayıcı değil | “Sorun yok” iddiası için yeterli kanıt |
| `insufficient` | Gerekli sayaç/çözünürlük yok veya kanıt çelişiyor | Çekimser kalma testi |
| `mixed` | İki bağımsız kaynak aynı pencerede | Nadir; etiket insan kararıyla |

Kurallar:

1. Bölme gruplarla yapılır (v2): bir makinenin tüm koşuları ya holdout'tadır ya değildir; bir koşu
   tune ile validation arasında bölünmez. Örnekler elle başka kümeye taşınmaz, başka makine grubuna
   kopyalanmaz (bütünlük denetimi yakalar).
2. Etiket için kullanılan dış kanıt (cap ayarı, yük komutu, GPU/CPU sayaç hedefi) kayıt altına
   alınır; veri setine yalnız opak `id` ve ölçümler girer.
3. Kişisel veri, kullanıcı adı, dosya yolu, oyun adı, IP yazılmaz. Veri seti yerelde kalır.
4. Pencereler en az 100 kare; kısa drop pencereleri bu protokolün dışındadır (oyun içi drop
   raporu ayrı `drops_v2` yoludur; v2 shader/PSO derleme şüphesi etiketini ekler).
5. En az 30 **gerçek holdout** örneği ve sınıf dağılımında her iki sınıftan yeterli örnek olmalı;
   tek sınıftan 30 örnek kapıyı açmaz (yanlış pozitif ölçülemez).

## 4. Araç

```bat
:: değerlendir (0 = kapı açık, 3 = veri geçerli ama kapı kapalı, 2 = hata, 5 = bölme bütünlüğü hatası)
build\x64-release\tests\ef_diag_calibrate.exe tests\fixtures\diagcal\synthetic_v1.json

:: kapı açıksa derlemeye gömülecek başlığı üret
build\x64-release\tests\ef_diag_calibrate.exe <gercek-veri.json> --emit-header --collected-on 2026-11-08
```

Üretilen `validated_calibration.h` içeriği `src/core/diagnosis/validated_calibration.h` dosyasına
konur; veri seti SHA-256’sı başlıkta ve commit mesajında yer alır. Başlık ancak:
gerçek holdout ≥ 30, yanlış pozitif ≤ %5 ve veri seti hash’i doğrulandığında değiştirilir.
Sentetik veri için araç başlık üretmeyi reddeder.

Araç `contracts::Result` ile reddeder; hiçbir örneği sessizce atlamaz. ctest:
`windows.diagcal` sentetik fixture’ın kapıyı açmadığını ve boş veri setinin hata olduğunu doğrular;
EF-X20 ile `synthetic_v2.json` (exit 3, `integrity ok=1`) ve `synthetic_v2_leak.json` (exit 5,
`run_group_multi_machine`) da denetlenir.

EF-X20 çıktı satırları (mevcut satır biçimleri korunur): `dataset samples=N format=diagcal_v1|v2`,
`tune|validation|holdout ...`, `holdout confusion label=<etiket> <tahmin sayıları>` (sütun sırası:
gpu_bound, cpu_or_engine_bound, frame_cap_or_present_wait, mixed, healthy, insufficient), `model <ad>
scored=0|1 ...`, `integrity ok=0|1 issues=N notes=M`, `integrity issue=<kod> id=<id>`, `integrity
note=<kod> id=<id>`, `gate ...`, `holdout eligible_real=.. machines=.. positives=.. negatives=..
covered_families=0x........` ve `holdout eligible_fp=.. eligible_correct=.. eligible_abstain=..
ineligible_real=.. missing_digest=..`.
`--emit-header` yalnız kapı açıkken (bütünlük dahil) başlık üretir; başlık uygun gerçek holdout sayısını,
makine/sınıf sayılarını ve kapsanan aile bitlerini taşır. Başlıktaki `holdout_total`, `false_positive`,
`abstained`, `correct` alanları da **yalnız uygun gerçek** holdout kümesindendir; üretim kapısı oranı
`false_positive / real_holdout` ile hesaplar (`holdout_total` ile seyreltmez).
ctest `windows.diagcal` ayrıca `real_v1_ungrouped.json` ile v1 gerçek örneğin çıkış 3, `integrity ok=1`
ve `integrity note=ungrouped_real_sample` verdiğini denetler.

### 4.1 Uygulama içi dışa aktarma (DIAG-10)

Uygulama, tamamlanan her Uygulama Testi sahnesinden `diagcal_v1` örneği üretebilir (`core/diagnosis/
calibration_export.*` + `App` kancası). Kanca yalnız açıkça yapılandırıldığında yazar; kullanıcı modunda
sessiz toplama yoktur:

| Değişken | Anlam |
|---|---|
| `EF_CALIB_EXPORT` | Yerel JSON dosya yolu (yoksa kanca kapalı) |
| `EF_CALIB_LABEL` | Deney tasarımından gelen etiket: `gpu_bound`, `cpu_or_engine_bound`, `frame_cap_or_present_wait`, `mixed`, `healthy`, `insufficient` |
| `EF_CALIB_PROVENANCE` | `synthetic` ise sentetik yazılır; varsayılan `real` |
| `EF_CALIB_LABEL_METHOD` | (v2) `controlled_experiment` veya `expert_review`; başka değer (ör. `rule_derived`) → yazma yok. Verilirse örnek `diagcal_v2` olur |
| `EF_CALIB_MACHINE_GROUP` | (v2) opak makine grubu, `[a-z0-9_-]`, ≤ 64; ana makine adı/seri/kullanıcı adı değil |
| `EF_CALIB_RUN_GROUP` | (v2) opak koşu grubu, aynı kural; her koşuya yeni değer |
| `EF_CALIB_FINE_LABEL` | (v2, isteğe bağlı) ince etiket; `EF_CALIB_LABEL` ile aynı compute sınıfına eşlenmeli |
| `EF_CALIB_MACHINE_DIGEST` | (v2, isteğe bağlı, opt-in) operatörün verdiği opak özet, küçük harf hex 8–64. Uygulama donanım parmak izi **toplamaz**; verilmezse örnek yazılır ama kapıya uygun değildir. Geçersiz biçim → yazma yok |

Kurallar:

- Etiket **sınıflandırıcı çıktısından türetilmez**; operatör koşulları (yük kademesi, cap/VSync durumu)
  önceden belirler ve etiketi verir. Etiket yoksa örnek yazılmaz.
- Örnek kimliği opaktır (`app-<unix_ms>-<sıra>`); kullanıcı/oyun/makine adı veya yol yazılmaz.
- `frames` = sahnenin gerçekten skorlanan geçerli aralık sayısı; < 100 ise örnek üretilmez.
- `gpu_busy_ms` yalnız GPU zaman damgaları okunduğunda ve ≥100 geçerli kare varsa yazılır. Değer kare
  başına ilk–son GPU komut aralığıdır: **kendi işimizin üst sınırı** (araya giren yabancı GPU işi dahil),
  kullanım yüzdesi değildir; rapora "mesgul süre" diye sunulmaz.
- Kullanım yüzdeleri (`gpu_engine_3d_pct`, `cpu_max_core_pct`) yalnız ortalama PDH aralığı ≤ 250 ms ise
  yazılır (MEAS-03); daha yavaş örneklemede alan boş kalır.
- Mevcut hedef dosya önce sıkı doğrulayıcıdan geçirilir; bozuksa **ezilmez**, yazma yapılmaz. Yazma
  geçici dosya + atomik değiştirme ile yapılır ve `kMaxCalibrationSamples` sınırına uyar.

`synthetic` provenance yalnız test/araç doğrulaması içindir; kapıya sayılmaz.

EF-X20 v2 kuralları: grup boş/geçersizse, ince etiket `EF_CALIB_LABEL` ile çelişiyorsa veya etiket sınır
ailesindense (`frame_cap_or_present_wait`, `fps_cap`, `vsync`, `engine_frame_limit`) örnek **yazılmaz**.
Bu kanca FPS sınırı/VSync durumunu ölçmez (sahne "sınır yok, VSync kapalı" varsayımıyla yazılır); sınır
etiketi uydurma veri olurdu (`cap_state_unmeasured`). Cihaz ailesi bu kancada çıkarılmaz/tahmin edilmez;
`unknown` yazılır. Not: v1 kancası `frame_cap_or_present_wait` etiketini hâlâ kabul eder (önceden var
olan durum; DIAG-10 takibi).

Geçmiş kaydı (EF-X20): her yeni ölçüm kaydı `diagnosis_version` damgası taşır
(`compute_v1:uncalibrated` veya `compute_v1:diagcal-<sha256 ilk 16>`). Damga yalnız etikettir; oyun içi
yolu hazır yapmaz. Eski kayıtlarda alan yoktur (bilinmiyor).

## 5. Bilinen eksikler (bu protokolle kapanmadan “tamam” sayılmaz)

- **Gerçek veri yok:** kapı bu yüzden kapalı. Veri toplanana kadar `validated_calibration.h`
  varsayılan (boş) kalır. DIAG-10 dışa aktarma hattı veri **toplamayı** sağlar; etiketleri deney
  tasarımına göre operatör verir.
- **Ana iş parçacığı erişimi:** hedef oyunun her iş parçacığı açılamıyorsa ana iş parçacığı
  kanıtlanamaz ve `cpu_main_thread_pct` eksik kalır (korumalı oyunlarda beklenen). Bu durumda CPU
  kararı yalnız çekirdek doygunluğuna dayanır; eksik metrik kanıt sayılmaz.
- **`gpu_busy_ms` yorumu:** değer kare başı GPU komut aralığıdır (üst sınır); yabancı süreçlerin
  araya giren GPU işi dahildir. Kullanım yüzdesiyle karıştırılmaz; "kısa" yönlü kanıt daha güçlüdür.
- **Oyun içi örnekleme çözünürlüğü:** 50 ms (20 Hz) PDH; ≥100 ms drop pencereleri kök neden
  olabilir, daha kısa pencereler dürüstçe “bağlam” kalır.
- **Dışa aktarma etkileşimi:** kanca ortam değişkeniyle açılır (R&D); kullanıcı arayüzünde düğme
  yoktur. Otomatik etiketleme yoktur ve eklenmeyecektir (etiket bağımsızlığı kuralı).
- **Bellek/depolama ve platform modelleri puanlanmaz (EF-X20):** `ram_pressure`, `storage_wait`,
  `thermal_power` etiketleri sayılır ama bu modellerin girdileri veri setinde yoktur; araç
  `scored=0 reason=inputs_not_in_dataset` yazar, oran üretmez.
- **Cihaz ailesi bağlı değil (EF-X20):** `DeviceFamily` henüz envanterden doldurulmuyor; üretim kapısı
  bilinmeyen aileyle çağrılır ve kapalı kalır. Kapsam dışı gerçek donanımla doğrulama bu bağlantıdan
  sonra yapılır.
- **Ölçülmüş sınır/VSync dışa aktarımı yok (EF-X20-T02):** gerçek sınırlı/VSync koşusu için yenileme,
  sunum senkronu ve sınır durumu ölçülerek yazılmalı; ayrı ölçüm görevi gerekir.

## 6. Kapı açıldıktan sonra

- `diagnosis_release_status()` gömülü özeti okur; kapı açıksa `make_report` ve
  `component_report` CPU/GPU hükmü üretebilir.
- Çalışma anı sınıfı (runtime class) Uygulama Testi kaydından beslenir (DIAG-11); sınıf yoksa
  bileşen raporu yine çekimser kalır.
- Özet tarih/veri seti hash’i ile sürümlenir; yeni veri seti yeni başlık ve yeni kanıt ister.
  Eski özet sessizce geçerli sayılmaz.
- Kalibrasyon EXE'ye derlenir (`validated_calibration.h`) ve EXE sürümüyle birlikte geri alınır. Ayrı
  dağıtılan imzalı kalibrasyon paketi yoktur (tek EXE kuralı).

## 7. Gerçek oyun ABBA turu (Performans Açık / Kapalı)

Durum: **protokol ve araç hazır; gerçek oyunda çalıştırılmış tur yok.** Bu bölüm sahibin ana bilgisayarında
bir turun nasıl yapılacağını ve sonucun nasıl hesaplanıp yazılacağını tanımlar. Gerçek tur yalnız sahibin
yanında ve isteğiyle yapılır; ajan bu turu kendisi çalıştırmaz (`docs/19_GAME_ACCEPTANCE_ROUND.md` ile aynı kural).
Bugüne kadarki iki gerçek ölçüm (güç planı A/B ve Arena sahnesi) "Fark yok" verdi; gerçek oyunda ölçüm yoktur.
Bu tur **tek bilgisayar** için kanıt üretir ve bir **FPS vaadi** doğurmaz.

### 7.1 Değişmez kurallar

- Yalnız **çevrimdışı, tek oyunculu, korumasız** oyun. Çevrim içi, eşleştirmeli veya korumalı maça girilmez
  (CS2 Premier/Faceit, VALORANT, EasyAntiCheat/BattlEye oyunları dahil). Koruma kapatılmaz, atlatılmaz.
- EasyAntiCheat, EasyAntiCheat_EOS, BEService, vgc veya FACEIT servisi **çalışıyorsa tur durur**
  (betik her adımda denetler).
- Betik ve ürün tarafında oyun belleği okunmaz, oyuna enjeksiyon yapılmaz, oyun içi katman konmaz. FPS kaynağı **oyunun kendi
  benchmark / sonuç ekranıdır** (`game_reported`); ETW kare ölçümü bu turda kullanılmaz.
- **Betik AYAR YAZMAZ.** Performans Açık/Kapalı geçişini sen ürünün kendi "Evet / Hayır" akışıyla yaparsın;
  betik yalnız sırayı söyler, koruma servislerini denetler, senin girdiğin sayıları kaydeder ve hesaplar.
  UAC'ye yalnız sen basarsın (betik ve ajan tıklamaz; ret kaydedilir, döngüde yeniden denenmez).
- Ürün oyun çalışırken ayar yazmaz (ADR-021): her kol geçişinde önce oyunu kapat, sonra Performans'ı değiştir,
  geri okuma "doğrulandı" deyince oyunu aç.
- Onay ekranında "yeniden başlatma gerekir" işaretli bir grup varsa o grubu Hayır de ya da bu turu yapma:
  yeniden başlatma isteyen gruplar hızlı kol geçişine uymaz; onlar için ayrı protokol gerekir.
- Sen "dur" dersen, başka oturumda oyun oynamaya başlarsan veya koruma servisi çıkarsa tur hemen durur.
- Okunamayan veya girilmeyen her değer boş kalır; tahmin yazılmaz. Atlanan adım `not_run` sayılır, geçti sayılmaz.

### 7.2 Tasarım

- **A = Performans Kapalı** (Ecu Fps'ten önceki değerler; ilk A'dan önce Performans hiç açılmamış ya da Kapalı ile
  geri yüklenmiş olmalı). **B = Performans Açık** (onay ekranındaki Evet/Hayır seçimleri tüm B kollarında aynı).
- **Blok** dört koşudur: tek numaralı blok **ABBA**, çift numaralı blok **BAAB** (sıra etkisi ve zamanla kayma dengelenir).
  Tur **en az 3 blok** (6 A + 6 B koşu) olmalıdır; önerilen 4-5 blok. Betik: `scripts\game_round.ps1 -Plan -Blocks 3`.
- Her koşu: aynı sahne (kayıtlı konum ya da oyunun kendi benchmark'ı), aynı çözünürlük, aynı FPS sınırı ve
  VSync/VRR ayarı, aynı güç kaynağı (masaüstü veya şebeke), aynı oyun sürümü ve sürücü, bu tur boyunca yeniden
  başlatma yok. Gerçekçi ayar seç; sahne GPU'ya mı CPU'ya mı bağlı, bunu not al (tahmin etme).
- **ısınma:** her açılışta ilk geçiş sayılmaz; ikinci geçişin sonucu girilir. Koşular arası bekleme tüm koşularda aynıdır.
- Isıl kısma, kare düşmesi, arka plan görevi (güncelleme, tarama, indirme) görülen koşu **Geçerli = hayır** yazılır ve hesaba girmez.
  Geçersiz koşu tekrar edilmez; blok içinde bir kolda geçerli koşu kalmazsa blok atılır.

### 7.3 Ölçülen

Her koşu için oyunun kendi raporladığı **ortalama FPS** (birincil) ve varsa **%1 düşük FPS** (ikincil, hükmü etkilemez).
Sayılar elle girilir (`-Round`); kaynak `game_reported` kaydedilir. Başka bir araçtan (ETW, overlay) sayı alınmaz.

### 7.4 Hesap ve hüküm

1. Her blokta: B ortalaması − A ortalaması (yalnız geçerli koşular) = **blok farkı**.
2. n = geçerli blok sayısı. Ortalama blok farkı d̄, örnek standart sapması s, **%95 güven aralığı**
   d̄ ± t(0,975; n−1) · s / √n. t değerleri betikte sabit tablodur (`Get-TQuantile95`; df 1-30, üstü 1,96).
3. Hüküm (`-Analyze` bunu yazar):
   - n < 3: **veri yetersiz**.
   - Aralık sıfırı içeriyorsa: **Fark yok**. Bu, "fark olmadığının" kanıtı değil, bu ölçümde ayırt edilemediğidir.
   - Aralığın tamamı > 0: "Performans Açık daha yüksek ölçüldü"; tamamı < 0: "daha düşük ölçüldü".
     Her iki durumda da "yalnız bu oyun, bu sahne, bu bilgisayar" etiketi yazılır.
4. Yüzde yalnız aralığın iki ucu da aynı işaretliyse ve aynı etiketle yazılır. "%X hızlanır" gibi genel cümle yazılmaz;
   site ve ürün metni yalnız kayıtlı sonuca göre, sahibin onayıyla güncellenir. Sonuç "Fark yok" ise metin "Fark yok" demeye devam eder.

### 7.5 Sonuç şablonu

`-Plan` boş şablonu `artifacts/evidence/game-round-<tarih>/result-template-<saat>.md` olarak üretir. Şablon boş bırakılır;
doldurulmamış şablon sonuç sayılmaz.

| Alan | Değer |
|---|---|
| Tarih | |
| commit | |
| build_id | |
| Ecu Fps sürümü | |
| İşletim sistemi (build.UBR) | |
| Ekran kartı ve sürücü | |
| Güç kaynağı | |
| Oyun adı | |
| Oyun sürümü | |
| Oyun korumasız mı (çevrimdışı, tek oyunculu) | |
| Sahne / benchmark | |
| Çözünürlük | |
| FPS sınırı | |
| VSync / VRR | |
| Performans grupları (onay ekranındaki Evet/Hayır seçimleri) | |

| Blok | Sıra | Kol | Ortalama FPS | %1 düşük | Geçerli mi | Not |
|---|---|---|---|---|---|---|
| | | | | | | |

| Ölçü | Değer |
|---|---|
| Geçerli blok sayısı | |
| Blok farkı (B−A) ortalama | |
| %95 güven aralığı | |
| Atlanan koşular ve nedeni | |
| Hüküm (veri yetersiz / Fark yok / yönlü) | |
| Kapsam sınırı | Yalnız bu oyun, bu sahne, bu bilgisayar. FPS vaadi değildir. |

### 7.6 Betik kullanımı

```
powershell -File scripts\game_round.ps1 -Check                         # salt okuma ön kontrol (docs/19)
powershell -File scripts\game_round.ps1 -Plan -Blocks 3                # sıra + boş şablon
powershell -File scripts\game_round.ps1 -Round <oyun.exe> -Blocks 3    # adım adım tur; her adımda CSV yazılır
powershell -File scripts\game_round.ps1 -Analyze <round-....csv>       # blok farkı + %95 GA + hüküm
```

`-Round` korumalı oyun listesindeki bir EXE'yi veya çalışan koruma servisini reddeder, "EVET" onayı olmadan başlamaz
ve `-Capture` gibi hiçbir sistem ayarını değiştirmez. Çıktılar yalnız `artifacts/evidence/game-round-<tarih>/` altına yazılır.
Betiğin hesabı sentetik veriyle elle yeniden hesaplanarak doğrulandı (`artifacts/evidence/P9-site.md`); bu doğrulama gerçek oyun sonucu değildir.

### 7.7 Bu tur neyi kanıtlamaz

Başka bilgisayarları, başka oyunları, çevrim içi veya korumalı oyunları, yeniden başlatma isteyen grupları ve Arena
test sahnesi dışındaki herhangi bir "FPS artışı" iddiasını. Tek tur yalnız o gün, o sürücü ve o ayarlar için geçerlidir.
