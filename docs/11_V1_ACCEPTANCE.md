# v1.0 kabul şartları

Durum: **2026-11-08 — imzasız alpha adayı.** Yerel üretim ve testler tamamlandı; aşağıdaki
kapıların bir bölümü gerçek donanım/UAC turları ve dış hesap kararları nedeniyle **not_run** veya
**blocked_external** durumundadır. Bu dosya v1.0 tamamlandı iddiası değildir.

Durum sözlüğü: `implemented_unverified` (kod + yerel test var, gerçek cihaz turu yok),
`verified_local` (bu PC'de gerçek koşu yapıldı), `blocked_external`, `not_run`.

| Kapı | Gerekli kanıt | 2026-11-08 durumu |
|---|---|---|
| Ürün | R01-R38 çalışan davranış + destek sınırı | implemented_unverified: tüm ana modüller derleniyor ve birim/ctest/snapshot geçiyor; gerçek ayar yazımı yalnız UAC turuyla doğrulanabilir |
| Recovery | Her mutation için snapshot/verify/restore; crash/full disk/conflict | implemented_unverified: journal, çökme uzlaştırma, geri alma birim testli; gerçek UAC yazım turu PENDING_USER #12 |
| Yetki | UAC reddi, farklı admin, IPC saldırı girdileri, release trust | implemented_unverified: broker/capture el sıkışması ve şema testli; REL-01 tamamlandı (PID+oluşturma zamanı+oturum+EXE kimliği, TOCTOU kapalı); gerçek UAC reddi turu PENDING_USER #2/#12 |
| Windows/startup | İzinli kapsamda gerçek uygulama ve eski duruma dönüş | blocked_external: HKLM/SCM yazımı kullanıcı UAC bekler (PENDING_USER #5/#7) |
| Üretici | NVIDIA/AMD/Intel gerçek cihaz testleri | blocked_external: NVIDIA DRS yazımı UAC bekler (#3); AMD ADLX lisans kararı (#4/B-006); Intel GPU yok (B-004) |
| İki benchmark | Yerel üretilen içerik + gerçek kare ölçümü; iptal/cleanup; run manifest | verified_local (kısmi): Hızlı Uygulama Testi bu PC'de gerçek koştu (~68 sn); Tam mod 5 dk ve oyun içi ETW turu not_run (UAC/oyun) |
| Teşhis | CPU/GPU/RAM/VRAM/IO/platform kanıtı; örnek doğruluğu | implemented_unverified: `diagcal_v1` veri seti, sıkı doğrulayıcı ve `ef_diag_calibrate` aracı hazır (docs/12); gerçek donanım holdout yok → yayın kapısı kapalı (`diag.gate.no_real_holdout`) |
| Optimizasyon | Aynı kalite A/B, tekrarlı kanıt, geri dönüş, belirsiz davranış | verified_local (kısmi): gerçek güç planı A/B deneyi "belirsiz" sonucu ve gerileme→geri alma yolu test edildi; HAGS/DRS gerçek yazımı UAC bekler |
| Ek v1 modülleri | Geçmiş, güncelleme farkı, uzun test, benzer sistem (hedef FPS B10 ile kaldırıldı) | implemented_unverified: hepsi UI + birim testli; R13 gerçek 10 dk koşu yok; R15 gerçek akran verisi yok (B-007, dürüst "veri yok") |
| UX | Kompakt kontroller, Türkçe, klavye/screen reader, yüksek DPI, düşük cihaz | implemented_unverified: ekran dışı snapshot tüm sayfalar × TR/EN × koyu/HC, taşma/a11y 0; gerçek Narrator turu not_run |
| Portable | Aynı EXE hash'i temiz Windows'ta; yanında bağımlılık yok; korumalı veri modeli | implemented_unverified: paket probu import tablosu + SHA-256 eşleşmesi doğrulanıyor; temiz makine smoke not_run |
| Dağıtım | Lisans envanteri, imza, sembol politikası, güncelleme/kurtarma uyumu | blocked_external: B-003 imza yok; güncelleme B-003/B-008 ve imza doğrulaması olmadan yayınlanmaz |

## Feature status (özet)

- **Performans Açık/Kapalı:** implemented_unverified. Plan, Evet/Hayır, journal, geri okuma ve birebir geri alma yolu kodda ve birim testte; gerçek UAC yazım turu kullanıcı bekler.
- **Temizlik:** implemented_unverified. Önizleme, birim kimliği, bütçe/iptal, kendi test dosyaları kategorisi; gerçek silme turu kullanıcı onayı ister.
- **Uygulama Testi:** verified_local (Hızlı). Tam mod ve düşük cihaz UX'i not_run.
- **Oyunda Test / takılma raporu:** implemented_unverified ve **ADR-021 kapısı arkasında**. Uyumluluk matrisi doğrulanmamış oyunlarda canlı ölçüm kapalıdır; neden UI'da yazılır, override yoktur. Oyun içi örnekleme 20 Hz'e (50 ms) çıkarıldı; ≥100 ms drop pencereleri kök neden adayı olabilir, daha kısa pencereler dürüstçe bağlam kalır. Arka plan süreç CPU yükü (`other_process_cpu_pct`) üretiliyor. Gerçek yükseltilmiş ETW turu yapılmadı.
- **Oyun Hazırlığı düzeltmeleri / HAGS / NVIDIA DRS:** implemented_unverified; gerçek yazım PENDING_USER #12.
- **Geçmiş, karşılaştırma, dışa aktarma:** implemented_unverified; export yalnız yerel, izin listesi alanları.
- **R15 benzer sistem:** implemented_unverified. Yerel havuz yokken daima "veri havuzu kurulmadı / N=0" gösterir; yüzdelik/sıralama üretmez. Topluluk havuzu olmadan karşılaştırma tamamlandı sayılamaz (B-007).
- **Hedef FPS:** kaldırıldı (sahip isteği, Uyumluluk B10, 2026-11-14). Canlı FPS paneli (EF-X15) de kaldırıldı.
- **Uzun test:** implemented_unverified. Bütçe/iptal testli; sensör yokken açık "güvenli izleme sınırı"; sağlık raporu değildir.

## Bilinen kısmi / uygulanmayan gereksinimler (2026-11-08 denetimi)

Bunlar gizlenmez; v1 tamamlandı sayılmadan önce kapatılmalı veya kapsam kararı verilmelidir.

- **R22 güncelleme denetimi (2026-11-20 düzeltildi):** ADR-028 ile ürün içi zorunlu güncelleme vardır: imzalı manifest
  (projenin kendi ECDSA P-256 anahtarı, açık yayın deposu), SHA-256/imza/replay doğrulaması, atomik değişim ve sağlıksız
  açılışta eski sürüme dönüş (ADR-033 ile kurulu kopyada hizmet üzerinden). Kalan: Authenticode imzası yok (B-003,
  imzasız alpha) ve gerçek cihazda geri dönüş (RollBack) turu yapılmadı (REL-04, REL-03). Önceki "ürün ağsızdır"
  cümlesi 0.1.0 dönemine aitti; ADR-027/028 ile geçersizdir.
- **R24 Windows Update erteleme:** WIN-06 yalnız **okur ve önerir**; Windows Update hizmeti durdurulmaz,
  belgelenmiş bir "duraklat/etkin saat" yazma sözleşmesi doğrulanmadan yazma yapılmaz (`writes_anything=false`).
  Bu yüzden R24 kısmi: tavsiye var, oturumluk erteleme yazımı yok.
- **R36 arka plan hizmeti durdurma:** başlangıç girdileri için etki + tek tek Evet/Hayır tamam; izin listesindeki
  hizmetlerin oturumluk durdurulması Performans Açık planına bağlıdır (WIN-10). Kullanıcı "Başlangıç ve arka plan"
  sayfasında hizmeti işaretler; durdurma Evet/Hayır onaylı Performans Açık planında broker (tek UAC, makine journal'i)
  üzerinden uygulanır, geri okunur ve Performans Kapalı/çıkışta önceki çalışma durumuna geri yüklenir. Broker yalnız
  derlenmiş izin listesi + `svcstop:<ad>` biçimi + `stopped` değerini kabul eder; bağımlı hizmet/kuyruk denetimi
  geçmeyen hedef plana girmez. Gerçek UAC/SCM turu yapılmadan yayın kapısı açılmaz.
- **R12 / R19 otomatik mod:** ADR-021 gereği oyun algılanınca otomatik uygulama ve oyun başına onaysız otomatik
  mod yoktur; her mutasyon Evet/Hayır ister. `core/perf/profile` (GameProfile/DriftGuard) yalnız birim testlidir,
  ürüne bağlı değildir; R12/R19'un "otomatik" kısmı ADR-021 ile kapsam dışıdır.
- **R15/R13 gerçek veri:** akran havuzu ve gerçek 10 dk uzun koşu yok; modüller dürüst "veri yok" durumundadır.
- **Darboğaz sınıflandırıcı kalibrasyonu (BM-02):** veri seti biçimi, doğrulayıcı, değerlendirici araç ve
  derlemeye gömülü kapı mekanizması hazır (docs/18_CALIBRATION_PROTOCOL.md). DIAG-10 ile uygulama içi
  `diagcal_v1` dışa aktarma (yerel, PII yok), GPU zaman damgası tabanlı `gpu_busy_ms` ve hedef oyunun ana iş
  parçacığı CPU yüzdesi üretimi eklendi. Ancak **gerçek donanım holdout örneği yok** ve etiketler dışarıdan
  (deney tasarımı) verilmeden veri seti oluşmaz. Bu nedenle %80 doğruluk iddiası yayınlanmaz; kapı kapalıdır.

## Kalan dış engeller

- B-003 kod imzalama sertifikası/sağlayıcı erişimi (unsigned alpha SmartScreen uyarır).
- B-008 CI faturalandırma (GitHub Actions iş başlatmıyor).
- B-004 Intel GPU yok; B-006 AMD ADLX lisans kararı; B-007 gerçek akran verisi yok.
- PENDING_USER: gerçek UAC/HAGS/DRS/SCM turları, ETW yönetici turu, restore point, gerçek disk temizliği, Narrator turu.
- Oyun uyumluluk matrisi (CS2 VAC/FACEIT dahil) doğrulanmadan canlı oyun ölçümü ürün kapısından çıkamaz.

## Release kararı

Yerel **imzasız alpha** paketi `dist/EcuFps-0.1.0-win64.zip` olarak üretildi
(SHA-256 `7ee3e9b8…2274`; `artifacts/release-manifest.json`); yayın için barındırma/alan adı kararı
kullanıcıya aittir. GitHub Releases yolu araştırıldı ve yerel hazırlık hattı kuruldu
(`research/RELEASE_HOSTING.md`, `scripts/release.ps1`, `dist/SHA256SUMS`); depo özel olduğu için
public indirme kararı verilmeden yayın yapılmaz. Paket v1.0 olarak sunulamaz. Etkinleştirilen bir
özelliğin gerçek donanım kanıtı yoksa yayın engelidir.
Geçmeyen kapı gizlenmez; destek matrisi ve release notu bu dosyayla aynı gerçeği yazmak zorundadır.
