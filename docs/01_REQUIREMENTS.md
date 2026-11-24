# Güncel gereksinimler — v3.2

| ID | Gereksinim | Doğrulama |
|---|---|---|
| R01 | Performans Açık/Kapalı ve kompakt Evet/Hayır onayı | Gerçek uygulama/geri alma akışı, sıfır işlem için başarı yok |
| R02 | Windows ve sürücü yetenek algılama | Destek, izin reddi, bilinmeyen durum ayrı |
| R03 | NVIDIA/AMD/Intel adaptörleri | Her üretici için gerçek destek matrisi ve eksik sürücü davranışı |
| R04 | Başlangıç uygulamalarını geri alınabilir yönetme | Kullanıcı/makine kapsamı, 32/64 bit görünüm, görev/servis sınırları |
| R05 | Kalıcı işlem günlüğü ve kurtarma | Çökme, elektrik kesintisi simülasyonu, disk dolu, çatışma testleri |
| R06 | Tek portable EXE | Temiz Windows'ta kurulum gerekmeyen başlatma; EXE yanında app dosyası yok |
| R07 | Hafif native arayüz | Düşük cihaz bütçesi, DPI, erişilebilirlik, reduced motion |
| R08 | Uygulama Testi | İçerik yerel üretilir; sabit seed/sürüm/yük; CPU/GPU/RAM/VRAM/disk alt testleri |
| R09 | Oyunda Test | Destekli built-in benchmark/replay ve destek yoksa oynarken ölçüm |
| R10 | Darboğaz ve takılma teşhisi | Bileşen, kanıt, güven, etki, senaryo; yanlış pozitif ve bilinmeyen testleri |
| R11 | Önce/sonra doğrulama | Eş koşullar, tekrarlar, belirsizlik ve otomatik grup geri dönüşü |
| R12 | Oyun bazında otomatik seçim | Profil sürümü, değişiklik kapsamı, kalite onayı, çakışma koruması |
| R13 | Uzun test ve güncelleme karşılaştırması | Isınma sonrası davranış; driver/OS/game değişikliği etiketleri |
| R14 | ~~Hedef FPS değerlendirmesi~~ — kaldırıldı (sahip isteği, Uyumluluk B10, 2026-11-14) | — |
| R15 | Benzer sistemlerle karşılaştırma | Sürüm/koşul eşleştirme, gerçek örnek sayısı, yetersiz veri durumu |
| R16 | Geçmiş ve dışa aktarım | Yerel kayıt, kişisel veriyi ayıklayan kullanıcı kontrollü paylaşım |
| R17 | İmzalama ve bütünlük | DLL/IPC sınırları, sertifika sahibi, sürüm ve lisans envanteri |
| R18 | Tekrar anlatmadan devam | Tek başlangıç promptu, görev durumları, kanıt ve oturum devir kaydı |
| R19 | Tepside bekleme ve oyun başına tek onaylı otomatik mod | Görünür tepsi, tek örnek, Windows açılışında tepside başlama varsayılan açık (ADR-030; ilk açılış bildirimi, Ayarlar anahtarı); ilk algılamada Evet/Hayır, sonra uygula/kapanınca geri al |
| R20 | Her şeyi geri al ve verileri sil | Tüm aktif değişiklikler geri alınıp doğrulanmadan veri silinmez; sonrası iz kalmaz |
| R21 | Çökme kaydı ve destek paketi | Yerel sınırlı minidump ve log; kişisel veri ayıklanmış, kullanıcı kontrollü paket |
| R22 | Güvenli güncelleme kontrolü | İmza/hash doğrulama, onaysız indirme yok, başarısızlıkta eski sürüm, journal göçü |
| R23 | Laptop güç ve güvenlik durumu tanısı | Pil/şarj/OEM mod ve VBS/HVCI salt okuma; değiştirme yok |
| R24 | Oyun oturumunda Windows Update erteleme | Belgelenmiş ayar, servis kapatılmaz, oturum sonunda geri yükleme |
| R25 | Tema ve dil altyapısı | Tek koyu tema (açık tema ve tema seçimi yok), yüksek kontrast sistem paleti, string tablosu, tr-TR varsayılan, Türkçe casing testleri |
| R26 | Özenli arayüz, animasyon ve ilerleme | Karşılama, iskelet yükleyici, geçişler; uygulama/geri almada gerçek adımdan % ilerleme |
| R27 | Donanım ve platform sağlık envanteri | Depolama türü, PCIe link, ReBAR, RAM hızı/XMP tahmini, BIOS yaşı; salt okuma |
| R28 | Güncelleme ve sürücü denetimi | Sürücü sürümü/yaşı, bekleyen Windows Update; resmî bağlantı, sessiz kurulum yok |
| R29 | Bileşen darboğaz raporu | CPU/GPU/anakart/RAM/SSD/HDD için kanıtlı sonuç veya abstain |
| R30 | Tek tık sistem kontrolü ve iyileştirme | Tara → sınıflandır → Evet/Hayır → % ilerlemeyle uygula → ölç → rapor; BIOS/sürücü rehberli |
| R31 | Canlı kaynak halkaları | Genel bakışta CPU/GPU/RAM/disk kullanımı canlı halka göstergeleri (yalnız görünürken 1 Hz), tüm birimlerin boş alanı; ölçülemeyen değer boş halka + neden |
| R32 | Ayar bazlı onay ve halka anahtar | Yuvarlak halka anahtar (Açık mavi, Kapalı kırmızı, hafif animasyon); altında açılır 'neleri değiştirir' paneli, her ayar ayrı Evet/Hayır; seçilmeyen ayar uygulanmaz |
| R33 | Uygulama animasyonları ayarı | Ayarlar'da uygulama animasyonlarını kapatma seçeneği; varsayılan açık; kapalıyken azaltılmış hareket |
| R34 | Disk doluluk ve oyun boş alan uyarısı | Donanım listesinde diskler/birimler doluluk oranıyla; kritik/uyarı eşikleri; oyun kütüphanesi bulunursa oyun + güncelleme payına göre tavsiye boş alan |
| R35 | Disk temizliği | Yalnız yeniden oluşturulabilen geçici/önbellek kategorileri; boyut önizlemesi, kategori bazlı Evet/Hayır, % ilerleme, ölçülen kazanılan alan; kullanıcı belgelerine dokunulmaz (ADR-016) |
| R36 | Arka plan azaltma ve başlangıç tavsiyesi | Oyunla ilgisiz arka plan hizmetleri (izin listesi) oturumluk ve geri alınabilir durdurma; başlangıç uygulamaları için etki + tavsiye + tek tek Evet/Hayır |
| R37 | Önce/sonra kaynak raporu | Uygulamadan önce/sonra ölçülen CPU/GPU/RAM/disk farkı ve güven aralığı; anlamlı fark yoksa açıkça yazılır; FPS iddiası yalnız testten |
| R38 | Onaylı marka kimliği | 02/BOOST logo, EXE/pencere/tepsi ikonları gömülü; aktif tepsi ikonu yalnız doğrulanmış durumda; koyu/açık görev çubuğu, yüksek kontrast, DPI, Explorer yeniden başlama |

## Kapsamın yorumu

R01-R38 v1 hedefidir (R19-R38 2026-11-04 kullanıcı kararlarıyla eklendi). BIOS ayarları ve sürücü kurulumu otomatik yazılmaz; tanı + rehber. R15 gerçek veri hazır değilse modül veri yetersiz gösterir; veri havuzu kurulmadan topluluk karşılaştırması tamamlandı sayılamaz. Özelliği kapsamdan sessizce çıkarma veya çalışmayan düğmeyle tamamlandı işaretleme.

CPU/RAM tuning, GPU OC/fan ve firmware yazımları önceki teknik kısıtlarını korur: resmî kontrol, destekli cihaz, kurtarma ve stabilite kanıtı olmadan etkinleştirilmez. "Hepsi v1" cümlesi belgesiz API uydurma izni değildir. Araştırma sonucu desteklenmiyorsa neden ve uygulanabilir alternatif kaydedilir.

## Korunan alanlar

Steam/oyun kayıtları, shader cache, güvenlik mekanizmaları, ses/ağ/depolama/termal ve anti-cheat bağımlılıkları korunur. Kritik servisleri topluca kapatma, RAM temizleme, HPET/BCD paketleri ve aynı kaliteyi koruyormuş gibi gizli çözünürlük düşürme yoktur.

## Doğru durum dili

Kapalı / Hazırlanıyor / Uygulanıyor / Açık / Yeniden başlatma gerekli / Geri alınıyor / Kurtarma gerekli / Uygulanabilir işlem yok. Bunlar işlem durumu, yeni kullanıcı modu değildir. Teşhiste belirgin sorun görülmedi / sınırlayıcı kaynak / karma / veri yetersiz ayrımı bulunur.
