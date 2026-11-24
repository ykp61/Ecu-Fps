# İki kaynaklı benchmark

Kullanıcı yalnız Uygulama Testi veya Oyunda Test seçer. Süre ve alt aşamalar test kapsamıdır; performans profili değildir. Tahmini süre ve oluşturulacak disk verisi test öncesi açıklanır, test her aşamada iptal edilebilir.

## Uygulama Testi

Sahne, geometriler ve kontrollü içerik yerel üretilir. Gömülü küçük varlıklar kullanılabilir; internet veya AI model indirme şartı yoktur. Ön ölçüm workload tier seçer, ardından tier/seed/camera/render ayarları kilitlenir. A/B koşuları arasında otomatik kalite veya workload değiştirilemez. Farklı tier/API/sürüm sonuçları eşdeğer sayılmaz.

CPU alt testi: tek iş parçacığı ve çok iş parçacığı iş yükleri ayrı; compiler dead-code elimination ve yalnız cache ölçme hataları önlenir; correctness checksum kullanılır. Fizik/simülasyon skoru gerçek oyundaki FPS tahmini değildir.

GPU alt testi: raster/geometri/shader yükleri ayrı ölçülür; GPU timestamp sonuçlarında geçerlilik kontrol edilir. D3D11 disjoint örnekleri elenir. Donanım renderer, GPU kimliği, feature level ve sunum modu kaydedilir. WARP/remote/VM fallback fiziksel GPU benchmark'ı olarak puanlanmaz. [S92 S93]

RAM alt testi: işletim sistemine ve kullanıcı işlerine pay bırakan tahsis bütçesi; hata/iptalde serbest bırakma; cache etkisi ve erişim düzenleri raporlanır. Kontrollü tahsis performans ölçümüdür, RAM fiziksel arıza taraması değildir.

VRAM alt testi: kendi sürecimizin bütçe ve kullanımını izleyerek aşamalı kaynak yükleme. DXGI bütçesi kart üzerindeki fiziksel kapasiteyle aynı kavram değildir; QueryVideoMemoryInfo kendi uygulama kullanımını raporlar, başka oyunun kesin bellek kullanımı sanılmaz. Haricî oyunda farklı doğrulanmış ölçüm yolu gerekir. [S94]

Depolama alt testi: test edilecek gerçek volume/cihaz kimliği seçilir; EXE USB'de diye USB varsayılan test diski yapılmaz. Ayrılmış benzersiz test dosyası oluşturulur; boş alan alt sınırı, toplam byte, süre ve yazma bütçesi belirlenir. Kullanıcı dosyasına/ham diske yazılmaz. Reparse point/symlink/hardlink üzerinden hedef kaçışı engellenir. Sıkıştırılabilir sıfır verisiyle yanıltıcı hız üretilmez. Buffer/cache/queue-depth/block-size profili sonuçla saklanır. Dosya boyu ve boş alan yeterli değilse test kısmi/atlandı; küçük cache testi sürdürülebilir disk hızı diye sunulmaz. [S95]

Sahne+depolama birleşik testi asset streaming takılmalarını inceleyebilir; file generation/decompression/IO süreleri ayrı tutulur. Test ön hazırlığı ölçüme karışmaz. Uzun testte başlangıç/ısınmış durum karşılaştırılır. Tek bir evrensel sıcaklık limiti bütün CPU/GPU'ya uygulanmaz; destekli üretici sınırları, hata ve kullanıcı iptali kullanılır. İzlenemeyen sensör açıkça belirtilir.

### CPU FP tolerans politikası (EF-X22.02)

- Tolerans **0 ULP**'dir (`kCpuFpToleranceUlps`, `src/benchmark/cpu/cpu_workload.h`). FP çekirdekleri (nbody, matmul, ray) yalnız IEEE doğru yuvarlanan `+ - * / sqrt` kullanır; `ef_benchmark` `/fp:precise` ile derlenir ve `#pragma fp_contract(off)` FMA birleştirmesini kapatır. Sonuç ISA'dan (SSE2/AVX2/AVX-512) ve derleyici sürümünden bağımsız bit-aynı olacak biçimde tasarlanmıştır.
- Altın checksum ya da tek/çok iş parçacığı checksum uyuşmazlığı **her zaman hata**dır; "yakın" sonuç kabul edilmez, skor üretilmez.
- CRT'ye bağlı fonksiyon (sin, exp, pow…) eklenen bir çekirdek ya checksum dışında kalır ya da iş yükü sürümü yükseltilip çekirdek başına belgelenmiş bir tolerans alır; bu belge güncellenmeden eklenmez.
- Kanıt sınırı: altın değerler yalnız geliştirme bilgisayarında birim testle doğrulanır. Farklı üretici ya da AVX-512 olmayan CPU'da gerçek koşu yapılmadı; uyuşmazlık görülürse tolerans büyütülmez, önce neden (derleyici/ISA yolu) bulunur.
- GPU tarafında tolerans tablosu boştur (`gpu_verify` tam eşitlik); üretici/sürücü toleransı ancak gerçek AMD/Intel GPU ölçümüyle eklenir.

## Oyunda Test

1. Sürümü doğrulanmış oyun adaptörü varsa kendi benchmark'ını başlat.
2. Uyumlu replay/demo varsa sabit senaryo kullan.
3. Diğer oyunlarda kullanıcı oynarken dış ölçüm yap.

Entegrasyon iddiası her oyun için ayrı kanıt ister. Her oyunda sahne üretme, maç otomasyonu veya sessiz injection yapılmaz. Oyun/maç zorla kapatılmaz. Otomatik menü/yükleme ayırımı yalnız doğrulanmış adaptörde kullanılır; diğerlerinde bölüm işaretleme ve belirsizlik vardır.

PresentMon analiz kütüphanesi adaydır. Versiyon, API ve HAGS gibi doğruluk koşulları run manifest'e yazılır. Eksik CPU/GPU metriği sıfır olarak işlenmez. [S34]

## Ölçüm protokolü

Her koşu: benchmark/game/app/driver/OS sürümü, renderer, GPU, çözünürlük, render scale, kalite, VSync/VRR/cap, frame generation, sıcaklık bağlamı, AC/batarya, örnek sayısı, geçerli süre, eksik olay sayısı ve ölçüm kaynakları.

Ortalama render FPS = geçerli render interval sayısı / geçerli süre. Yüzde 1 düşük FPS = en yavaş ceil(N*0.01) geçerli kare süresinin ortalamasına 1000/ms dönüşümü; kullanılan tanım sürümlenir. p95/p99 frametime ayrı. NaN, negatif, kayıp/duplike ve eksik aralık politikaları açık. Yükleme dışlaması başarısız kareleri gizlemek için kullanılmaz. Doğal render, display ve generated frame ayrı.

Hızlı tarama ilk işaret içindir. Optimizasyonun iyileşme iddiası için aynı sahnede en az 5 A + 5 B koşusu, ısınma ve sıralama kontrolü hedeflenir. Koşu istatistikleri esas alınır; aynı koşudaki binlerce frame bağımsız deney gibi sayılmaz. Farklı kaliteyle kazanım ayrı kategoriye yazılır. Ham FPS'ten uçtan uca input lag türetilmez.

## Test sonunda

İptal/başarı/çökme temizliği, sınırlı saklama, eksik sonuç işaretleri; testten kalan settings override varsa journal ile geri dönüş. Arayüz düşük frekansta güncellenir, ham frame olayları tam toplanır. Benchmark boşta UI bütçesinin dışında; collector overhead ayrıca raporlanır.
