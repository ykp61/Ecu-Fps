# Son önerilerin v1 kapsamı

Kullanıcı bütün önerilen modüllerin ilk sürümde başlatılmasını istedi. Bu belge önceki "sonraki aşama" ayrımını günceller; fiziksel donanım ve veri gereksinimlerini ortadan kaldırmaz.

## Geçmiş ve güncelleme etkisi

Her koşu app/benchmark/game/driver/OS, ayarlar ve kalite fingerprint'iyle saklanır. Güncelleme sonrası düşüş iki farklı sahneyi karşılaştırarak kesin driver hatası sayılmaz. Koşullar eşleşirse fark, belirsizlik ve güncelleme zaman çizgisi gösterilir. Eski sürücü kurma otomatik FPS düğmesine gizlenmez.

## Uzun test

Isınma öncesi/sonrası frekans, kare süresi ve limit flag incelenir. Artan süre, byte ve bellek bütçeleri önceden bellidir. User cancel, worker çökmesi ve sensör kaybı yönetilir. Bu modül bütün donanım hatalarını veya güç kaynağı sağlığını teşhis etmez.

## Hedef FPS (kaldırıldı)

Sahip isteğiyle Uyumluluk B10 (2026-11-14) ile üründen kaldırıldı; aşağıdaki metin tarihsel kayıttır.

Hedef kullanıcı ölçüm tercihidir; yeni performans modu değildir. Kare bütçesi 1000/hedef FPS ms ile değerlendirilir. Hedefe ulaşma süresi, uzun frame oranı, doğal render/display farkı verilir. Hedefin monitör hızından farklı olması mümkün; tek başına yanlış ayar denmez.

## Benzer sistem karşılaştırması

V1 motoru yerel, sürümlü ve provenance'ı kayıtlı gerçek dataset içe alabilmeli. Aynı benchmark/tier/API/kalite/çözünürlük koşulları eşleştirilir. Laptop TDP, RAM düzeni, termal koşul ve sürücü farkı kohort ayrımıdır. Yeterli örnek yoksa kıyas üretilmez; minimum örnek/dağılım kuralları kalibrasyonla belirlenir.

Topluluk havuzu yayınlanacaksa açık opt-in, seri no/kullanıcı adı/oyun hesabı ayıklama, boyut ve şema denetimi, sahte sonuç ayıklama, silme/retention, API yetkilendirme ve işletme maliyeti gerekir. EXE'ye ortak gizli anahtar gömülmez. Sunucu açma/yayın kullanıcı erişimi gerektirir; bu durum blocker'dır, modül yokmuş gibi gelecek sürüme itilmez.

## Export

Yerel rapor ve makine okunur sonuçlar kullanıcı isteğiyle dışarı çıkar. Ayrı dosya export kullanıcının seçtiği çıktı olduğundan tek EXE dağıtım sözleşmesini bozmaz. Benchmark gizli dosya yolu/hesap kimliği paylaşılmaz. Recovery journal hiçbir zaman normal benchmark export'u değildir.
