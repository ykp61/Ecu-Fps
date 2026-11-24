# Plan değişiklikleri

## 0.9.4 (taslak) — 20 Kasım 2026

- ADR-040: rehber eksikleri (B0-B9): `plan.widgets`, `plan.power_usb` (varsayılan Hayır), MSI/bekleme belleği salt okuma teşhisi, Fortnite/Warzone rehber satırları; W14/W16/G13/G14 bayrakla kapalı, W15 girdisiz (resmî belge yok).
- ADR-041: oyun sunucusu ping'i (kullanıcının yazdığı IPv4'e ICMP), yük altında gecikme (hız testi), oyun kapanınca temizlik önerisi (silme yok).
- Cihazda doğrulanmadı: ayrıntı ve liste `docs/RELEASE_NOTES.md` 0.9.4 bölümünde.

## 0.3.0 (taslak) — 13 Kasım 2026

- ADR-033: kurulum = aynı EXE (`EcuFps-Setup-0.3.0.exe`), Program Files kurulumu, Başlat menüsü ve Uygulamalar
  listesi kaydı; tek UAC ile işlemsel kurulum/kaldırma (journal + geri alma).
- LocalSystem yardımcı hizmet: izin listesindeki işlemler UAC'siz; hizmet yoksa UAC broker'ına geri düşme.
- Zorunlu güncelleme kurulu kopyada hizmet üzerinden (yeniden doğrulama, yedek, sağlık sinyali, geri dönüş).
- Kaldırma: geri yükleme → program (tek UAC) → Run kaydı → kullanıcı verisi; ARP `--uninstall` Ayarlar'daki aynı onayı açar.
- Taşınabilir ZIP bırakıldı; `publish_release.ps1` kurulum dosyası + imzalı manifest üretir (yayın yapılmadı).
- Cihazda doğrulanmadı: bkz. `docs/09_BUILD_RELEASE.md` "Sahibin gerçek turu 0.3.0".
## v3 — 4 Kasım 2026

- Claude Code ve Codex için ortak giriş, tek başlangıç promptu ve devam kayıtları.
- 16 iş dalı; bağımlılıklı görevler, somut çıktılar ve kabul kanıtları.
- Uygulamanın ürettiği test sahnesi ve gerçek oyun testi ayrı akışlar.
- CPU/GPU/RAM/VRAM/SSD/HDD ve ölçülebilen platform kısıtları için teşhis şartnamesi.
- Tüm son öneriler v1 kapsamına taşındı; teknik erişim ve gerçek veri engelleri korunuyor.
- Yerel benchmark doğruluğu, kasıtlı test yükü ile boşta hafiflik ayrımı; geçici disk verisi sözleşmesi.
- Güncel tasarım token'ları ve küçük kontrol boyutları; yeni görsel üretilmedi.
- v2 arşivi değiştirilmeden korundu. Uygulama kodu veya EXE üretilmedi.

## v2 — 3 Kasım 2026

54 ayar/başlangıç kaydı, 90 kaynak; portable, yetkiler, geri dönüş ve koruma araştırması; iki eski UI konsepti. Bunlar teknik fizibilite/uygulama başarısı olarak değerlendirilmez.
