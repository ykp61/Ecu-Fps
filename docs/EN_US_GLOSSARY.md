# en-US terim sözlüğü

Kısa başvuru: İngilizce metinler (`design/strings/en-US.json`) aynı kavram için hep aynı terimi kullanır. Üstteki tablo
`tests/unit/core/test_i18n_parity.cpp` ile denetlenir: tr-TR metni Türkçe terimi içeriyorsa aynı anahtarın en-US metni İngilizce
terimi (büyük/küçük harf fark etmez) içermelidir ve her terim katalogda kullanılmalıdır. Yeni terim eklerken önce buraya, sonra
metinlere yazın.

## Denetlenen terimler

| Türkçe | English | Not |
|---|---|---|
| Oyunda Test | In-Game Test | Ölçüm kaynağı 2 (gerçek oyunda kare süreleri) |
| Uygulama Testi | App Test | Ölçüm kaynağı 1 (uygulamanın kendi sahnesi); "Application Test", "Built-in test" yazılmaz |
| Uzun test | Long test | Isınma karşılaştırması; "Endurance" yalnız kod adıdır, kullanıcıya gösterilmez |
| Darboğaz | Bottleneck | Denge testinin sonucu |
| Etkiyi ölç | Measure effect | Performans Açık/Kapalı deneyi; sonuç vaadi değil |
| Veri yetersiz | Not enough data | "Desteklenmiyor" ile karıştırılmaz (aşağıya bakın) |

## Denetlenmeyen, ama sabit kullanılan karşılıklar

- Performans Açık / Kapalı = Performance On / Off. Başka mod adı (sessiz, dengeli, rekabetçi) yoktur.
- Evet / Hayır = Yes / No. Onay düğmeleri bu iki sözcüktür.
- Kapalı (geri alma) = Off: uygulamanın önceki değerleri geri yüklemesi. "Reset", "factory reset", "default" yazılmaz.
- Geri al = Undo; Geri yükle = Restore.
- Desteklenmiyor = Not supported (bu cihazda bu özellik yok); Veri yetersiz = Not enough data (ölçüm var ama karar için az); Ölçülemedi = Not measured.
- Ölçümler = Measurements; Geçmiş = History; Ayarlar = Settings; Oyunlar = Games; Panel = Dashboard.
- Sürücü = driver; Yönetici izni (UAC) = administrator permission (UAC).

## Üslup kuralları

- Vaat yok: "faster", "boost", "+N FPS", "% daha hızlı", "guaranteed" gibi ifadeler yazılmaz; ölçülmüş sonuç yalnız "In this experiment ... 95% confidence" kalıbıyla verilir (metin testi bunu tarar).
- Sayılar ve birimler: ondalık nokta (0.5), ms, MB, GB, FPS; Türkçede ondalık virgül. Yüzde: en-US "{pct}%", tr-TR "%{pct}".
- Hitap: tr-TR "sen/siz" karışıktır (Ayarlar "sen"); en-US her zaman "you".
- Marka ve sabit adlar (Ecu Fps, NVIDIA, Windows Update, Cloudflare) çevrilmez.
- Anahtar, yer tutucu ({n}, {e}...) ve sayıları tr-TR ile en-US arasında aynı tutun; test bunu denetler.

## Taşma kontrolü

`ef_ui_snapshot.exe` her çalıştırmada tr-TR ve en-US katalogları ile tüm sayfaları, pencere boyutlarını ve ölçekleri (100-200%)
çizip metin taşmasını denetler; `EF_TEXT_SCALE=1.25` Windows "Metin boyutu" büyütmesini ekler. Kanıt:
`artifacts/evidence/P9-i18n-privacy.md`.
