# Test ve kanıt stratejisi

Bu devir hazırlanırken uygulama testi yapılmadı. Belgeler, görev bağlantıları ve paket bütünlüğü denetlendi. Aşağıdakiler yapılacak ürün testleridir.

## Kanıt seviyeleri

| Seviye | Kanıt | Neyi kanıtlamaz |
|---|---|---|
| Unit/fixture | Saf karar, istatistik, parser ve hata mantığı | Windows API/donanımın gerçekten çalışmasını |
| Windows integration | OS API, ACL, UAC, journal, pencere davranışı | Tüm vendor/driver uyumluluğunu |
| Hardware | Belirli gerçek donanım/driver/game üzerinde davranış | Test edilmeyen bütün bilgisayarları |
| Release artifact | Aynı EXE hash'iyle temiz makine, imza, paket testi | Daha eski binary üzerindeki testi yeni binary'ye taşımayı |

Her kanıt commit/binary hash, OS/driver/hardware sürümü, test adı, komut/prosedür, zaman, beklenen/gözlenen sonuç ve dosya yolunu içermelidir. Evidence path yokken done kaydı reddedilir. Test çalıştırılmadıysa not_run nedeni yazılır. Coverage sayısı tek başına kalite kanıtı değildir.

## Zorunlu senaryolar

- Rollback: her kalıcı yazma noktasında kesinti; kısmi uygulama; tekrar tekrar geri alma; eski/yeni şema; disk dolu; snapshot başarısız; dış değişiklik; default/inherited değer.
- Yetki: normal kullanıcı, admin, UAC reddi, farklı admin hesabı, iki oyuncu oturumu, bozuk IPC, replay ve yetkisiz pipe istemcisi.
- Startup: 32/64 bit Run, tür korunması, shortcut karantina, görev Enabled, bağımlı servis, paketli unsupported, RunOnce korunması.
- Vendor: yok/unsupported/permission_denied/API failure, adapter seçimi, hibrit GPU, driver update ve gerçek değer restore.
- Benchmark: sabit seed/yük, warmup, clock discontinuity, dropped events, N çok küçük, FPS cap, frame generation, cancel ve orphan test file.
- Teşhis: sağlıklı sistem/cap normal kullanımı, CPU/GPU/RAM/disk ve karma örnekler; eksik veride abstain; kontrollü tekrar.
- UI: 100/125/150/200 DPI, klavye, screen reader, reduced motion, Türkçe metin, minimum pencere, focus ve uzun onay.
- Portable: USB/salt okunur EXE klasörü, Unicode/uzun yol, EXE taşıma, başka PC, kapatma/çökme, data quota.

## Cihaz matrisi

Intel klasik ve hibrit CPU; AMD tek/çok CCD ve X3D; NVIDIA/AMD/Intel GPU; iGPU ortak bellek ve hibrit laptop; 4/8/16/64 GB RAM; HDD/SATA/NVMe; çoklu ekran/HDR/VRR; AC/batarya; Home/Pro. Bu liste mevcut test cihazı envanteri değildir. Erişim yoksa BLOCKERS'a yazılır ve destek matrisi dar kalır.

## Performans hedefleri

Ölçüm kapalı arka plan süreçlerinin toplam private working set'i <=50 MiB; ana UI açık <=150 MiB; 60 sn ortalama toplam makine CPU'su <0.2%; sürekli boşta disk yazımı yok; zayıf SSD referansında usable UI ~2 sn ve p95 etkileşim <100 ms. Ölçülmüş sonuç değiller. Collector, worker, koruma ve UI süreçleri toplamı değerlendirilir. Test sahnesi yükü ayrı raporlanır.

Oyun etkisi için araç kapalı / yalnız kontrol / ölçüm açık koşulları eş ayarda kıyaslanır. Grafik çizim hızı ile ham frame kaydı birbirinden ayrılır. Darboğazı gözlemleyen aracın kendisi sorunu yaratıyorsa sonuç geçersizleşir.

## İyileşme kararı

Önce 5 A + 5 B eş koşu; run bazında belirsizlik, etki büyüklüğü ve gerileme denetlenir. İlk aday eşik ortalama FPS +%3 veya yüzde 1 düşük +%5, güven aralığı sıfırın üzerinde ve diğer temel ölçütte anlamlı bozulma yok; bunlar kalibre edilecek tasarım eşikleridir, garanti değildir. Çok sayıda ayar tarayıp yalnız en iyi rastlantıyı seçme; ayrı doğrulama koşusu gerekir. Farklı kalite sonuçları ayrı raporlanır.

Regresyon doğrulanırsa son değişiklik grubu journal ile geri alınır, yeniden ölçülür. Test gürültüsü, sıcaklık/arka plan/sürücü farkı nedeniyle belirsiz sonuç "iyileşme doğrulanamadı" olarak kalır.
