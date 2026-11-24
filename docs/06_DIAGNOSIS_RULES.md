# Darboğaz teşhis sözleşmesi

Teşhis belirli oyun/sahne/ayar/zaman aralığı için verilir. Model adlarından sabit darboğaz yüzdesi hesaplanmaz. Limitlenen bileşen arızalı bileşen değildir. Yüksek kullanım tek başına problem değildir. [S91]

## Sonuç sınıfları

- no_significant_issue: bu senaryoda hedefi bozan belirgin etken görülmedi; mutlak "PC'de sorun yok" değildir.
- limiting_resource: desteklenen birincil sınırlayıcı etken.
- mixed: farklı aralıklarda veya birlikte birden çok etken.
- insufficient_data: kısa test, eksik/uyumsuz metrik, FPS kilidi veya başka belirsizlik.

Her bulgu: sınıf, hedef bileşen, zaman penceresi, gözlem, destekleyen kanıt kimlikleri, karşıt kanıtlar, güven seviyesi ve nedeni, çözülebilir ayar grupları, fiziksel sınır ve yeniden test koşulu taşır. Veri yokken sağlık yüzdesi veya parça bozuk sonucu üretilmez.

## Kural adayları

| Etken | Birlikte aranacak kanıt | Yanlış teşhisi engelleyen kontrol |
|---|---|---|
| GPU işlem sınırı | GPU işi, frame bütçesi, sahne karmaşıklığı, GPU talebini azaltan eş senaryo | Cap/VSync, present wait ve sensör gecikmesini ayır |
| CPU/oyun iş parçacığı | İş parçacığı zaman çizgisi, GPU boşlukları, CPU iş yüküne duyarlılık | Toplam CPU düşükken de sınır olabilir; GPU boşluğu tek başına CPU kanıtı değil |
| RAM kapasite baskısı | Available/commit, paging ve takılmanın birlikte tekrarı | Hard fault tek başına RAM arızası veya kapasite eksikliği değil |
| Bellek bant genişliği/gecikme | Alt testler ve kontrollü koşu karşılaştırması | Kanal sayısını yalnız DIMM sayısından çıkarma; donanım sayacı yoksa olası olarak kalır |
| VRAM baskısı | Geçerli bütçe/kullanım, taşıma/eviction işareti, stutter ve tekrar | Fiziksel VRAM doluluğu tek başına kesin sınır değil; shared iGPU RAM ile çift sayma |
| SSD/HDD beklemesi | Oyunun IO gecikmesi/kuyruğu, dosya yükleme ve stutter zamanlaması | Yükleme süresini sabit FPS limitiyle karıştırma; arka plan IO'sunu ayır |
| Isıl/güç kısıtı | Destekli limit flag, yük, frekans ve sıcaklığın birlikte değişimi | Sadece sıcaklıktan throttling veya PSU arızası deme |
| Anakart/PCIe bağlantısı | Yük altında negotiated gen/lane, desteklenen maksimum ve iş yüküne etkisi | Boşta güç tasarrufundaki düşük hız normal olabilir; lane paylaşımı bağlamı gerekir |
| Arka plan rekabeti | Aynı zamanlı başka süreç yükü, kontrollü kapatma/yeniden test | Microsoft/üçüncü taraf imzası tek başına kapatma gerekçesi değil |
| FPS cap / sunum sınırı | Kararlı hedef aralığı, cap/present/VRR bağlamı | Düşük GPU kullanımı CPU darboğazı sanılmamalı |

Hard fault Windows'un sayfayı diskten almasıdır; donanım hatası anlamına gelmez. [S96]

## Karışık yük ısı/güç tanısı (EF-X22.02)

Uygulama: `src/core/diagnosis/combined_load.*`. Girdi aynı koşunun sahne alt testi (tek başına) ve sahne+depolama karışık yük pencereleridir (GPU üretici ısı/güç bayrağı, yük, saat).

- Her pencere kümesi ayrı ayrı `balance_vendor_limit` ile değerlendirilir: bayrak + yük altında (≥ `kGpuLoadPct`) en az 2 pencere + saatin yük altındaki en yüksek değerin `kClockDropRatio` altına düşmesi birlikte gerekir. Yalnız sıcaklık ya da yalnız bayrak kısıt sayılmaz.
- Isı ve güç bayrakları ayrı raporlanır (`thermal_seen`, `power_seen`).
- Sınıflar: `not_supported` (üretici bayrak API'si yok: AMD/Intel GPU, ya da hiçbir pencerede bayrak alanı yok), `insufficient` (yük/saat penceresi yetersiz ya da tek başına sahne tabanı değerlendirilemedi), `no_limit` (karışık yükte doğrulanmış kısıt yok; mutlak "sorun yok" değildir), `combined_only` (kısıt yalnız karışık yükte: ortak ısı/güç bütçesi olası), `also_solo` (kısıt tek başına sahnede de var; karışık yüke özgü değil).
- CPU ısı/güç sensörü üründe yoktur: sonuç her zaman "CPU ölçülmedi" notunu taşır. PSU arızası ya da "parça bozuk" hükmü üretilmez.
- **Yayın kapısı (fail-closed):** `no_limit` / `combined_only` / `also_solo` hükümleri yalnız teşhis kapısı (`ReleaseStatus.ready`, en az 30 bağımsız gerçek donanım holdout örneği ve docs/18 koşulları) açıkken yayımlanır. Kapı kapalıyken metin `diag.combined.withheld` ve kapının nedeni (`diag.gate.*`) gösterilir. `not_supported` ve `insufficient` hüküm değil çekimserlik olduğundan her zaman gösterilebilir. Kapı bu dilimde kapalıdır (gerçek holdout 0).

## Güven ve nedensellik

Güven düşük/orta/yüksek olarak kanıt kapsamına göre hesaplanır; rastgele yüzde yoktur. Eşikler test veri setinde kalibre edilir. Düşük sensör örneklemesinden milisaniyelik kesin sebep uydurulmaz. Birlikte görülme olası ilişki; kontrollü tekrar/tek değişken deneyi daha güçlü doğrulama sağlar. Bir bileşen alt testinin düşük skoru bütün oyunları aynı oranda sınırlamaz.

FPS hedefi yok ve uygulama hızlıysa rapor yalnız kapasite sınırını açıklayabilir. Çözüm varsa destekli geri alınabilir işlem paketi için Evet/Hayır sunulur. Fiziksel kapasite sınırı ayarla düzeltildi diye yazılmaz.

## Kalibrasyon seti

Sağlıklı/cap sınırlı; GPU ağırlıklı; CPU tek thread; memory pressure; disk streaming; mixed; shader compile; background capture; thermal flag; kayıp sensor/ETW; HAGS/API değişkeni ve iGPU ortak bellek örnekleri. Etiketler gerekçeli, provenance'lı ve ayrı holdout kümeli olmalı. Yanlış pozitif, yanlış negatif, abstain ve kapsam raporlanır. Simüle örnekler gerçek cihaz kanıtı yerine geçmez.
