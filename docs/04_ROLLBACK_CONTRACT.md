# İşlem günlüğü ve geri alma

## Değişmez

Her mutation öncesinde geri alma için gerekli eski durum dayanıklı kayda yazılır. Kayıt tamamlanmadan yazma yapılmaz. Bütün Windows ayarlarını tek atomik transaction içinde değiştirdiğimizi varsaymayız; her adaptör idempotent telafi sağlar.

İşlem durumları: prepared → applying → applied → verifying → active; hata veya kapatma ile rolling_back → restored ya da recovery_required. Her adımın intent/result kayıtları vardır. Çökme sonrası yalnız son status'a güvenilmez; gerçek hedef okunur.

## Snapshot gereksinimi

Eski/yeni değer; var/yok durumu; ham veri türü; inherited/explicit ayrımı; hedef kullanıcı ve cihaz; OS/driver/game sürümü; adaptör ve şema sürümü; zaman; izin; reboot koşulu; asıl hedef kimliği. PID tek başına yetmez; süreç creation time da tutulur.

Oyun yapılandırmasında yalnız değişen alanlar geri alınır. Kullanıcının sonra değiştirdiği farklı alanlar eski bütün dosyayla ezilmez. Uygulamanın son yazdığı değerle güncel değer farklıysa conflict; kullanıcıya Evet/Hayır sorulur. Hayır, tam eski hâle dönüldü anlamına gelmez.

## Sıra

1. Hedef ve capability doğrula; işlem kilidi al.
2. Her adımın snapshot'ını topla; tüm kayıtları dayanıklılaştır.
3. Adım intent kaydını yaz, uygula, sonucu geri oku, result kaydet.
4. Tamamlanan küme için active işaretini yaz.
5. Hata/iptal/kapalı durumunda ters bağımlılık sırasıyla telafi et.
6. Geri okuma başarısızsa kaydı koru; kullanıcıya recovery_required göster.

## Hata koşulları

UAC reddi; disk dolu; bozuk/yarım journal; daha yeni şema; app/driver/game güncellemesi; güç kesintisi; iki oturum; USB çıkarma; dosya değişimi; global/inherited NVIDIA değerleri; sonradan oluşan kullanıcı override'ı test edilir. Her yazma ile kayıt arasına crash noktası yerleştirilir.

Kurtarma anahtarının aynı kullanıcı, farklı UAC admin ve makine kapsamındaki erişimi test edilmelidir. Lisans süresi/çevrimdışı durum recovery'yi kilitlemez. Eski şema bilinmiyorsa yazma başlamaz; eski EXE yeni journal'ı tahmin ederek değiştirmez.

## Sınır

Uygulama ve yardımcı süreçler çalışmıyorsa otomatik kurtarma görevi gerçekleşmez. İlk portable model gizli servis kurmaz. Sonraki başlatma kurtarır; açılmayan Windows için mutlak otomatik düzeltme sözü verilmez. Bu nedenle boot/firmware yazımı varsayılan işlem kümesinde bulunmaz.

## Veri klasörü göçü (marka değişikliği, R-F1, ADR-042)

Önceki ürün adıyla kurulu kullanıcıların verisi eski `%LOCALAPPDATA%\<eski ad>` klasöründedir (`history.json`, `preferences.json`, `journal\` (+ `startup\`, `fixes\`), `logs\`, `crash\`). Yeni uygulama ilk açılışta, journal deposu / adaptörler / dil tercihi yeni klasörü açmadan önce bir kez göç eder (`core/legacy/user_data_migration.*`, `platform/windows/legacy_migration.*`, çağrı `main.cpp` Ui/Setup/Recovery).

- **Eski klasöre yazılmaz:** silme, taşıma, işaret dosyası yok. Eski sürüme dönüş çalışmaya devam eder.
- **Kopyala → doğrula → devret:** dosyalar `<yeni>.migrating` hazırlık klasörüne kopyalanır (kendi `.migration-staging` işaretiyle), her biri kaynakla boyut + SHA-256 karşılaştırılır; kaynak kopya sırasında değişirse dosya bir kez yeniden denenir. Hepsi doğruysa `migration.json` hazırlık klasörüne yazılıp geri okunur ve hazırlık klasörü tek adımda yeni klasör olur. Yeni klasör boşsa ya da önceki başarısız denemenin `migration-failed.json` kaydını taşıyorsa üzerine yazmadan dosya dosya birleştirilir (aynı içerik = zaten taşınmış; farklı journal kaydı = hata; journal dışı farklı dosyada yeni klasördeki korunur).
- **Journal içeriği değişmez:** kayıtlar bayt bayt kopyalanır; şema/sürüm alanları, makine kimliği (`itx-machine:` önekli özet, marka bağımsız) ve işlem kimlikleri aynı kalır.
- **Hata:** okuma/yazma/doğrulama/disk dolu/yeniden adlandırma hatasında hazırlık klasörü silinir, eski veri dokunulmadan kalır, yeni klasöre küçük `migration-failed.json` (aşama, hata, eski kayıt sayıları; yol yok) ve `logs\migration.log` yazılır; sonraki açılış yeniden dener. Eski sürüm açıkken (eski tek örnek / kurulum kilidi) kopya denenmez.
- **Dürüst durum:** eski kullanıcı journal'ında geri alınmamış (Active/yarım/RecoveryRequired) veya okunamayan kayıt varken göç başarısızsa ya da yeni klasör zaten doluysa, Performans kartı temiz "Kapalı" göstermez: `RecoveryNeeded` + `perf.legacy_unmigrated` bildirimi. Eski makine journal'ı (`%ProgramData%\<eski ad>`) yalnız okunur; bekleyen kayıt varsa aynı bildirim (göçü yükseltilmiş yardımcı ister; ayrı görev).
- **Kopyalanmayan:** `drivers\` ve `setup\` (yeniden indirilen büyük önbellekler; kullanıcı durumu yok). Junction/symlink izlenmez (göç reddedilir).
- **Test kipi:** `EF_DATA_DIR` yönlendirmesinde eski klasör yalnız `EF_LEGACY_DATA_DIR` ile ve aynı `%TEMP%` kuralıyla verilir; `EF_TEST_BACKGROUND=1` iken gerçek profil asla göç ettirilmez.
- **Eski ad taşıyan kalıcı dizgiler:** journal hedefleri (`startup:<konum>:<hex ad>` dahil) eski bir Run değerinin adını taşıyabilir; geri alma tam o adı geri yazar (doğru davranış, çeviri yok). Uygulamanın kendi "Windows ile başlat" Run değeri journal'da değil tercihtedir: eski adlı değer bizim sayılır, tercih açıksa yeni ad + bu EXE ile yeniden yazılır (geri okunarak), eski adlar silinir; kaldırma akışı eski adları da siler.
