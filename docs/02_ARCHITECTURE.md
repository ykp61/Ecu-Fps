# Mimari sözleşme

## Bileşenler ve sorumluluk

| Bileşen | Sorumluluk | Sınır |
|---|---|---|
| Normal UI | Onay, durum, test akışı ve sonuç | Yetkisiz süreç; serbest yönetici komutu üretemez |
| Yetkili yürütücü | Aynı EXE'nin UAC ile açılan rolü | Derlenmiş izinli işlemler; dar IPC |
| Recovery | Journal inceleme ve geri alma | Lisans/internet olmadan çalışır |
| Capability | OS/driver/donanım/oyun destek matrisi | Bilinmeyeni destekleniyor saymaz |
| Settings adapters | Windows, startup, vendor, game işlemleri | Snapshot → apply → verify → rollback |
| Measurement | Kare olayları, sayaçlar, sensör zaman çizgisi | UI çizim hızıyla örnek toplama ayrıdır |
| Benchmark worker | Yerel sahne ve alt testleri yürütme | Aynı EXE rolü; iptal, süre/bellek sınırı |
| Diagnosis | Kanıta dayalı sınırlayıcı etken açıklaması | Telemetri yokken tahminî sağlık raporu vermez |
| Optimizer | Uygulanabilir değişiklik grupları ve A/B kararı | Journal olmadan yazmaz; kalite onayını korur |
| History/report | Yerel geçmiş, karşılaştırma, export | Toplama asgari; paylaşma kullanıcı eylemi |

Öncelikli uygulama seçimi C++20, Win32, Direct2D/DirectWrite ve statik CRT'dir. Benchmark temel yolu Direct3D 11 adayıdır; DX12 özellikleri ayrıca capability ile açılır. Tam özellik seviyeleri ENV/BENCH görevlerinde ölçülüp destek matrisine işlenir. Yazılım rasterizer'ında alınan sonuç fiziksel GPU skoru sayılmaz.

Tek indirilen EXE birden fazla süreç rolüyle çalışabilir. Kalıcı Windows servisi veya gizli zamanlanmış görev ilk portable sözleşmeye dahil değildir. PresentMon analiz kütüphanesinin uygulamaya bağlanması araştırılacak; hazır servis+overlay paketini gizlice kurarak tek EXE iddiası yapılmayacak.

## Planlanan kaynak ağacı

| Gelecekteki yol | İçerik |
|---|---|
| src/app ve src/ui | Giriş rolleri ve native UI |
| src/core ve src/contracts | Platformdan ayrılmış kararlar ve modeller |
| src/platform/windows | Win32, yetki, disk, zaman, oturum |
| src/security ve src/recovery | IPC, journal, kurtarma |
| src/adapters/windows,startup,nvidia,amd,intel,games | Tipli adaptörler |
| src/measurement,benchmark,diagnosis,optimizer | Ölçüm ve uygulama döngüsü |
| src/history ve src/report | Kayıt ve aktarım |
| tests/unit,integration,windows,hardware | Kanıt türüne göre testler |
| resources ve third_party | Gömülü varlıklar ve lisans bilgisi |

Bu yollar hedef mimaridir; bu devir paketinde kaynak kodu varmış gibi değerlendirme.

## Yaşam döngüsü

UI kapanınca görünür çizim durur. Tam çıkışta aktif ayarları geri yükleyip çıkma esastır; çakışmada açık sonuç gerekir. Test iptalinde worker ve geçici veri temizlenir. Kontrol süreci hiç çalışmıyorken yeni müdahale yapılamaz; sonraki açılış journal kurtarmasını başlatır.

Portable klasör salt okunur/USB olabilir. Yazılabilir veri konumları EXE'den bağımsızdır. Aynı cihazın önceki oturumu bulunur; başka bilgisayara taşınan EXE eski cihaz ayarlarını yeni cihaza taşımaz.
