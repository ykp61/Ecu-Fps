# Ecu Fps 0.9.4 — sürüm notları TASLAĞI (imzasız alpha; yayımlanmadı)

Önceki adı: iTunex Pro

Tarih: taslak 2026-11-20 · Önceki yayın: 0.9.3 (2026-11-18) · Platform: Windows 10/11 x64 · Kaynak: `git log 30c09f69..HEAD` (0.9.3 yayın commit'inden sonraki 33 commit; CMake sürümü henüz 0.9.3, 0.9.4'e yükseltilmedi)

> **Bu değişikliklerin hiçbiri gerçek cihazda doğrulanmadı.** Aşağıdaki her madde yalnız sahte (fake) birim testleri,
> derleme ve ekran dışı görüntü denetimiyle denendi. Gerçek ayar yazımı (UAC), gerçek ağ ölçümü, gerçek oyun kapanışı,
> gerçek klavye/ekran okuyucu ve AMD/Intel cihazı turları yapılmadı. Hiçbir madde FPS, gecikme veya hız kazancı
> vaat etmez; etkisi ölçülmemiştir. İmzasız alpha: SmartScreen uyarı gösterebilir.

## Yenilikler

- **Widgets / Haberler paneli (yeni ayar grubu `plan.widgets`):** Windows 11'de Widgets/Haberler akışını kapatan tek
  kurum ilkesi değeri (`AllowNewsAndInterests = 0`). Varsayılan **Hayır**; yönetici izni (UAC) ister. Önce kayıt,
  uygula, geri oku; Kapalı ile eski değer geri yüklenir. Kurum ilkesi zaten tanımlıysa dokunulmaz. FPS etkisi
  belgelenmemiştir, beklenmez. Copilot ve Recall bilerek eklenmedi.
- **USB seçici askıya alma (yeni ayar grubu `plan.power_usb`):** Yalnız prizdeki (AC) güç planında, yalnız "kapalı"
  hedefiyle. Varsayılan **Hayır** (Gelişmiş altında): Microsoft sürücü belgesi bunu kapatmamayı önerir, bu yüzden kanıt
  etiketi "Tartışmalı". Boşta güç tüketimi artabilir. Geri alma raporunda kendi adıyla görünür.
- **Hız testinde yük altında gecikme:** İndirme ve yükleme sırasında aynı tek sunucuya (`speed.cloudflare.com`) küçük
  yan bağlantılarla gecikme ölçülür; kartta tek satır ("indirirken X, yüklerken Y", artış düşük/orta/yüksek). Eşikler
  (30/100 ms) ürün kararıdır, standart değildir. Bu hattın gecikmesidir; oyun sunucusu ping'i veya oyun içi gecikme
  değildir. Yeni ağ sunucusu eklenmedi.
- **Oyun sunucusu ping'i (ADR-041):** Hız kartında "Sunucu ping" düğmesi. Yazdığınız tek bir IPv4 adresine mevcut
  ICMP yolu ile 20 örnek gönderilir; ortalama, dalgalanma ve kayıp gösterilir; 10'dan az yanıt = "veri yetersiz". Alan
  adı çözülmez, IPv6 yoktur, sonuç kaydedilmez. Adres yalnız yerel tercihte saklanır; gizlilik sayfası buna göre
  güncellendi. Hız testiyle aynı kapı: oyun açıkken başlamaz.
- **Oyun kapanınca temizlik önerisi:** Oyun gerçekten kapandığında salt okuma bir tarama yapılır; seçili kategoriler
  500 MB ve üzerindeyse ve son öneriden 6 saat geçtiyse tepsi bildirimi gelir. Hiçbir dosya silinmez; silme yine ayrı
  Evet/Hayır ister. Ayarlar → Gelişmiş'te anahtar var (varsayılan açık). Oyun açılırken otomatik temizlik yapılmaz.
- **Teşhis (salt okuma):** Oyun ekran kartı ve ağ bağdaştırıcısı için MSI kesme durumu (Düzelt düğmesi yok, yazma yok);
  bekleme belleği baskısı eşiği aşılırsa bulgu + tek Evet/Hayır ile mevcut bellek bakım işini çalıştıran "Düzelt".
  Okunamayan değer "veri yetersiz", yoksa "desteklenmiyor" olarak ayrı gösterilir.
- **Oyun içi ayar rehberi:** Fortnite ve Warzone için satırlar (V-Sync, kare sınırı, Reflex, Dinamik Çözünürlük);
  resmî kaynak etiketli, menü yolu ve sayı yoktur. AMD/Intel düşük gecikme rehberi "Satıcı sürücü rehberi" kartında
  düğmesiz bilgi satırı olarak görünür (menü adları cihazda doğrulanmadı).
- **Kapalı, hazır altyapı (kullanıcıya görünmez):** Ön plan önceliği (`Win32PrioritySeparation`), dinamik tick (BCD) ve
  NVIDIA ek ayarları (iş parçacıklı iyileştirme, FXAA) kodda var ama bayrakla KAPALI; destek matrisinde "Desteklenmiyor"
  / "Yalnız okuma" görünür. Bayrakla kapalı bir girdi yönetici hizmetinde de motora girmeden reddedilir. Hizmet ana
  bilgisayarı bölme eşiği (W15) için resmî belge bulunamadığından girdi hiç eklenmedi.

## Cihazda doğrulanmadı

- `plan.widgets` ve `plan.power_usb` için gerçek Windows'ta Evet/Hayır → UAC → yaz → geri oku → Kapalı → geri oku turu.
- Standby "Düzelt" düğmesinin gerçek uçtan uca koşusu ve MSI okumasının farklı cihazlardaki sonucu.
- Yük altında gecikme satırının gerçek ağda görünmesi; sunucu ping'inin gerçek sunucuya, klavye/IME/yapıştırma ve
  ekran okuyucu ile denenmesi.
- Oyun kapanınca temizlik bildiriminin gerçek oyun kapanışında görülmesi (karar mantığı birim testli; gerçek kapanış denenmedi).
- Fortnite/Warzone ayar adlarının güncel menüde ve gerçek Epic/Steam kurulumunda doğrulanması.
- AMD/Intel cihazında menü adları; bayrakla kapalı girdilerin (W14, W16, G13, G14) açılış turları (yeniden başlatmalı
  tur izole makinede).
- Ekran görüntüsü denetimi ekran dışı yapıldı; gerçek monitör/DPI ve Narrator turu yok.

## Bilinen sınırlar

- Yeni yazma yolları (Widgets, USB) ve yeni ağ yolları (yük altında gecikme, sunucu ping'i) hiç gerçek cihazda
  çalışmadı; varsayılanları Hayır/kapalıdır. Etkileri ölçülmemiştir, kazanç garantisi verilmez.
- 0.9.4 zorunlu güncelleme olarak herkese gider (ADR-028); yayın kapısı için tam kanıt koşusunun hizmet kapalıyken
  geçmesi gerekir (ayrıntı: `state/HANDOFF.md`).

## Doğrulama

Derleme 0 hata; `ef_unit_tests` 2363/0, `ctest` sıralı 65/65 (4 test atlandı: opt-in/gerçek GPU), `ef_ui_snapshot`
taşma/büyük kontrol/eksik kontrol/erişilebilirlik 0 (kayıt: `state/HANDOFF.md`, `artifacts/evidence/R-C.md`).
Bu sayılar yalnız birim/pencere testidir; gerçek cihaz kabulü değildir. SHA-256 ve boyut yayın sırasında
`manifest.json` ile birlikte yazılacak (henüz üretilmedi).

---
# Ecu Fps 0.3.0 — sürüm notları TASLAĞI (imzasız alpha; yayımlanmadı)

Tarih: taslak 2026-11-13 · Dosya: `EcuFps-Setup-0.3.0.exe` (uygulamanın kendisi; ayrı kurulum çerçevesi yok) ·
Platform: Windows 10/11 x64

> **İmzasız alpha (B-003).** SmartScreen uyarı gösterebilir. Bu sürümün kurulum, yardımcı hizmet, güncelleme ve
> kaldırma akışları ajan oturumlarında yalnız sahte (fake) testler ve ekran dışı görüntülerle denendi; aşağıdaki
> "Cihazda doğrulanmadı" listesindeki her madde gerçek bir bilgisayarda henüz yapılmadı.

## Yenilikler

- **Kurulum:** `EcuFps-Setup-0.3.0.exe` kurulum penceresini açar (en güncel sürüm denetimi, imzalı manifest; daha
  yeni sürüm varsa onu uygulama içinde indirip doğrular). "Kur" → uygulama içi Evet/Hayır → **tek UAC**. Kurulum
  `C:\Program Files\Ecu Fps` altına yapılır, Başlat menüsü kısayolu ve Uygulamalar listesi kaydı eklenir; hata
  olursa yapılan her adım geri alınır.
- **Yardımcı hizmet:** Ayarlar artık **UAC'siz** uygulanır. Hizmet yalnız kurulu uygulamadan gelen ve izin listesinde
  olan sabit işlemleri yapar; her işlem yine sizin Evet/Hayır onayınızla başlar ve kayda (journal) yazılır. Hizmet
  yoksa uygulama eskisi gibi her işlem için UAC ister.
- **Zorunlu güncelleme** sürer: kurulu kopyada yeni sürüm uygulama içinde indirilir, hizmet imzayı ve SHA-256'yı yeniden
  doğrular ve UAC'siz değiştirir; yeni sürüm sağlıklı açılmazsa eski sürüme döner.
- **Taşınabilir ZIP yok.** 0.2.x taşınabilir kopyalar zorunlu güncellemeyle 0.3.0'a geçer ve "Kurulum gerekli · Kur"
  bildirimi gösterir; verileriniz (`%LOCALAPPDATA%\EcuFps`) korunur.
- **Kaldırma:** Ayarlar → Kaldır veya Uygulamalar listesi. Önce değişen ayarlar geri yüklenir; sonra tek UAC ile program,
  hizmet, kısayol ve kayıtlar kaldırılır; en son başlangıç kaydı ve veriler silinir. Yönetici izni verilmezse hiçbir şey
  silinmez; program kaldırılamazsa verileriniz korunur. Çalışan dosya yeniden başlatmada silinir.

## Cihazda doğrulanmadı

Gerçek UAC ile kurulum; Program Files izinleri; Başlat menüsü kısayolu; Uygulamalar listesinden kaldırma; hizmetin
otomatik başlaması ve yeniden başlatılması; UAC'siz Performans Açık/Kapalı; hizmet durunca UAC'ye geri düşme; gerçek
GitHub'dan 0.3.0 → 0.3.1 güncellemesi ve geri dönüşü; 0.2.1 taşınabilirden geçiş; yeniden başlatmada kalan dosyanın
silinmesi; SmartScreen davranışı. Kontrol listesi: `docs/09_BUILD_RELEASE.md` → "Sahibin gerçek turu 0.3.0".

## Doğrulama

SHA-256 ve boyut yayın sırasında `manifest.json` ile birlikte yazılacak (henüz üretilmedi).

---
# Ecu Fps 0.1.0 — sürüm notları (imzasız alpha)

Tarih: 2026-11-10 · Derleme `93e4c30d983a` · Paket: `EcuFps-0.1.0-win64.zip` · Platform: Windows 10/11 x64

> **Bu bir v1.0 değildir.** İmzasız alpha adayıdır (B-003). SmartScreen uyarı gösterebilir.
> Gerçek UAC/yönetici turları, oyun uyumluluk matrisi ve bazı donanım testleri henüz yapılmadı;
> ayrıntı ve kapı durumu: `docs/11_V1_ACCEPTANCE.md`.

## Paket içeriği

- `EcuFps.exe` — tek portable EXE (statik CRT; yanında DLL/config/asset gerekmez)
- `LICENSE.txt`, `NOTICE.txt` (NVAPI + PresentMon MIT), `README.txt`

Kurulum yoktur; EXE'yi bir klasöre çıkarıp çalıştırın. Kaldırmak için uygulama içindeki
**Ayarlar → Kaldırma/veri silme** akışını kullanın; veri `%LOCALAPPDATA%\EcuFps` altındadır.

## Doğrulama

- `EcuFps-0.1.0-win64.zip` — SHA-256 `7ee3e9b86044af877b17abe4384bb420e86096fe4cecb8f7b5ccc8b80e3c2274`
  (1.919.662 bayt)
- İçindeki `EcuFps.exe` — SHA-256 `f8bc64c65792644340ae62de0288cfb8339ed8e451e29d43fde47427164aa4a2`
  (4.725.760 bayt)

1. ZIP'i indirin, SHA-256'yı yukarıdaki değerle karşılaştırın.
2. EXE'yi çalıştırın; ilk açılışta donanım/OS envanteri salt okunur.
3. Yönetici işlemleri yalnız siz **Evet** dediğinizde ve ayrı UAC onayıyla çalışır.

## Bu sürümde çalışanlar

- **Performans Açık/Kapalı:** plan önizleme, Evet/Hayır, kalıcı başlangıç kaydı, geri okuma ve
  birebir geri alma. Desteklenmeyen/veri yetersiz öğeler ayrı gösterilir.
- **Başlangıç hizmetleri:** seçilen oturumluk hizmetler yalnız Performans Açık onayıyla durdurulur;
  Kapalı/çıkışta önceki çalışma durumuna geri getirilir (R36).
- **Erişilebilirlik/UI:** kontrast eşikleri (metin 4.5:1, sınır/odak 3:1), tek koyu tema, yüksek
  kontrast sistem paleti, klavye odak halkası, ekran okuyucu duyuruları; token'lı halka/kaydırma
  geometrisi. Sekiz işlevsel sayfa responsive çok sütun düzeninde; 960x540 ve 1180x720,
  %100/%125/%150/%200 DPI, tr-TR/en-US taşma ve kontrol denetimi geçti.
- **Sistem kontrolü:** tek tık tarama; otomatik düzeltilebilir öğeler (kullanıcı kapsamı) ve
  rehberli öğeler (BIOS/sürücü) ayrılır; öğe başına seçim, uygulamak için tek Evet/Hayır.
- **Uygulama Testi:** Hızlı (~68 sn) ve Tam (~5 dk) gerçek yük ölçümü; CPU/GPU/RAM/disk
  bileşen kartları. Skor uydurulmaz; ölçülemeyen bileşen "desteklenmiyor/veri yetersiz" der.
- **Oyunda Test:** gerçek kare aralıkları, takılma (drop) pencere analizi ve kök neden
  göstergesi **yalnız doğrulanmış oyunlarda**. Uyumluluk matrisi doğrulanmamış oyunlarda
  (CS2 VAC/FACEIT dahil) canlı ölçüm kapalıdır; neden kartı yazılır, override yoktur.
- **Temizlik:** birim seçimi, önizleme, bütçe/iptal, dosya kimliği doğrulaması, kendi test
  dosyaları kategorisi. Silme yalnız Evet/Hayır onayıyla ve önizlemedeki kategoriler için yapılır.
- **Başlangıç ve arka plan:** başlangıç girdileri için etki + tavsiye + tek tek Evet/Hayır
  (kullanıcı kapsamı, journal'lı geri alma). İzin listesindeki hizmetler işaretlenebilir; oturumluk
  durdurma Performans Açık onayıyla broker üzerinden uygulanır ve Kapalı/çıkışta geri yüklenir.
- **Oyun Hazırlığı:** ekran Hz/HDR, oyun modu, GPU sürücü yaşı, BIOS ipuçları; düzeltmeler
  journal'lı ve geri alınabilir. HAGS/NVIDIA DRS yönetici grubu tek UAC ile (gerçek yazım
  turu kullanıcı bekliyor).
- **Geçmiş ve dışa aktarma:** yerel test geçmişi, iki test karşılaştırması, aynı koşullu önceki
  kayıtla sürüm kapılı karşılaştırma, izin listeli JSON dışa aktarma (ağa yükleme yok).
- **Destek paketi / çökme kaydı:** yerel dosya; hata raporu otomatik gönderilmez.
- **Uzun test, hedef FPS, benzer sistem:** sırasıyla bütçeli izleme, gerçek render aralığı
  değerlendirmesi ve dürüst "veri havuzu yok" durumu. Sensör yoksa sağlık iddiası yoktur.

## Bilinen sınırlar (bu alpha)

- **İmzasız:** Authenticode yok (B-003). Kurumsal dağıtım/otomatik güncelleme yoktur.
- **Güncelleyici yok (R22):** ürün ağsızdır; sürüm denetimi/indirme yapmaz. *(Yalnız 0.1.0 için geçerliydi; sonraki
  sürümlerde ADR-028 ile imzalı zorunlu güncelleme vardır.)*
- **Windows Update erteleme (R24):** yalnız okur/önerir; hizmet durdurmaz, yazmaz.
- **Arka plan hizmeti durdurma (R36):** izin listesindeki hizmetler Başlangıç sayfasından işaretlenir;
  Performans Açık onayıyla broker üzerinden oturumluk durdurulur ve Kapalı/çıkışta geri yüklenir.
  Gerçek UAC/SCM turu yapılmadan yayın kapısı kapalıdır.
- **Otomatik mod (R12/R19):** ADR-021 gereği oyun algılanınca otomatik uygulama yoktur.
- **Oyunda Test:** etkin canlı yakalama uyumluluk matrisi doğrulanana kadar kapalıdır;
  display timestamp yoksa render/ekran ayrımı "yetersiz" kalır. Canlı örnekleme 50 ms'dir;
  yalnız ≥100 ms drop pencereleri kök neden adayı olabilir, daha kısa pencereler bağlam sayılır.
- **Darboğaz sınıflandırıcı (BM-02):** gerçek donanım kalibrasyon verisi yok; CPU/GPU kök neden
  hükmü yayınlanmaz (`diag.gate.no_real_holdout`). Datasets/araç/protokol docs/18_CALIBRATION_PROTOCOL.md'dedir.
- **Benzer sistem (R15):** topluluk veri havuzu yok; N=0, yüzdelik/sıralama üretilmez.
- **Sensörler:** sıcaklık/voltaj sensörü okunmuyorsa uzun test "güvenli izleme sınırı" der;
  sağlık raporu değildir.
- **Donanım kapsamı:** NVIDIA yolu bu PC'de sınırlı doğrulandı; AMD ADLX lisans kararı (B-006)
  ve Intel GPU (B-004) yok. AMD/Intel yolları destek matrisinde kapalıdır.
- **Narrator/NVDA turu ve temiz Windows smoke** yapılmadı; ekran dışı UIA/taşma denetimi geçti.

## Bilinen düzeltmeler

- Sistem kontrolü raporundan sonra "Başlat" düğmesi yanlışlıkla devre dışı çiziliyordu;
  yeniden tarama artık görünür biçimde etkin.

## Geri bildirim

Hata bildirirken **Ayarlar → Destek paketi** ile üretilen yerel ZIP'i ekleyin; paket yalnız
izin listeli, redakte edilmiş alanları içerir ve otomatik yüklenmez.
