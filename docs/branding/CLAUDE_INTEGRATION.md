# Claude Code entegrasyon talimatı

Bu belge mevcut uygulamanın talimatlarına ek olan bir marka entegrasyon sözleşmesidir. Paket uygulama kaynak kodu içermez. Kullanıcının seçimi **02 / BOOST**, ürünün yazımı **Ecu Fps** olarak kesinleşmiştir.

## 1. Mevcut projeyi tanı

Önce mevcut çalışma ağacını, yerel talimatları, kullanılan UI çatısını, kaynak derleyicisini, uygulama kimliklerini ve mevcut tray yöneticisini incele. Dosya adlarını tahmin ederek yeni bir uygulama çatısı kurma. Mevcut değişiklikleri silme veya üzerine yazma. Kaynak kimlikleri için `design/asset-map.json` sayıları öneridir; çakışma varsa mevcut isimlendirmeye uyarla.

Uygulama bulunmuyorsa `assets/branding/ecu-fps/` gibi mevcut yapıya uygun bir hedefte varlıkları ve entegrasyon notunu hazırla; kullanıcıdan ayrı bir motor kodlama isteği olmadan performans yazılımını başlatma.

## 2. Kaynak dosyaların önceliği

1. Kullanıcının adı ve 02 seçimi.
2. Bu paketin `assets/` SVG/ICO dosyaları; üretimde esas alınır.
3. `design/asset-map.json` ve `design/brand-tokens.json`.
4. `docs/BRAND_GUIDE_TR.md` ve bu entegrasyon sözleşmesi.
5. `reference/02-boost-approved.png`, yalnız görsel yönün geçmiş referansı.

Referans panosundan kırpılmış ikon kullanma. Önizlemedeki etiketler, çizgiler, kartlar ve açıklamalar uygulama varlığı değildir. `preview/` HTML dosyasını uygulama arayüzü olarak paketleme.

## 3. Varlıkları derleme kaynaklarına bağla

| Yüzey | Kaynak | Beklenen davranış |
| --- | --- | --- |
| EXE / Explorer | `assets/app/ecu-fps.ico` | Asıl uygulama ikon kaynağı |
| Pencere / Alt+Tab | Aynı ICO'nun uygun boyutu | Küçük/büyük pencere ikonunu mevcut çatı üzerinden ayarla |
| Uygulama üst alanı | `ecu-fps-lockup-dark.svg` | Sabit oran; logo yazısını tekrar fontla dizme |
| Dar menü | `ef-mark.svg` | Tam wordmark okunmuyorsa yalnız amblem |
| Hakkında ekranı | Yatay veya stacked logo | İhtiyaç duyulan tek çözünürlüğü yükle |
| Sistem tepsisi | `assets/tray/tray-*.ico` | İşletim sistemi zeminine ve doğrulanmış duruma uygun seçim |

Native Win32 projesinde ICO'ları `.rc` içindeki ICON kaynakları olarak bağla; ana uygulama ikonunun Shell tarafından kullanılan birincil grup olmasını doğrula. Diğer çatılarda ilgili uygulama/pencere ikon özelliğini ve gömülü kaynak yaklaşımını kullan. Yeni ağır UI bağımlılığı ekleme.

Logo için mevcut çatı SVG'yi destekliyorsa sağlanan basit yolları kullan. SVG desteği yoksa tek uygun PNG'yi göm veya yolları mevcut çizim altyapısına aktar. Sadece logo çizmek için WebView/Electron, yeni SVG motoru veya font paketi ekleme. PNG'lerin tamamını gereksiz yere EXE'ye gömme. Gömülen görseli ilk kullanımda çöz; her karede yeniden çözme.

Derleme sonrası dağıtım klasöründe bu görevin gerektirdiği yan dosya olmamalı. EXE'yi geçici temiz bir klasöre taşıyarak görsel kaynakların hâlâ geldiğini doğrula. Paket, kurtarma günlüğü gibi mevcut uygulama verilerini kaldırma veya EXE içine hapsetme talimatı değildir.

## 4. Sistem tepsisi ve gerçek durum

Kullanıcı modları yalnız **Performans açık / Performans kapalı** kalır. Bu marka paketi yeni performans profili eklemez.

- `active` ikonunu yalnız performans işlemi başarıyla tamamlanıp gerçek durum doğrulandığında göster.
- `neutral` ikonunu kapalı durumda kullan. Durum bilinmiyor, uygulanıyor, geri alınıyor veya hata varsa da nötr ikon ve gerçeği belirten açıklama göster. Nötr ikonun kullanıldığı her durumun "kapalı" olduğu varsayılmamalı.
- Tooltip örnekleri: `Ecu Fps — Performans açık`, `Ecu Fps — Performans kapalı`, `Ecu Fps — Durum doğrulanıyor`, `Ecu Fps — Geri alma kontrolü gerekli`.
- Renk tek başına durum bilgisi olmamalı; tooltip ve erişilebilir menü metni gerçek durumu taşımalı.
- Açık/koyu ikon seçimi uygulamanın kendi temasına değil Windows görev çubuğunun zeminine göre yapılmalı. Seçimi belirlemek için kullanılan yöntemi hedef Windows sürümünde doğrula; belgelenmemiş sabit registry anahtarına garanti gibi davranma.
- Yüksek kontrastta sistem ön plan renklerini tercih et; sağlanan siyah/beyaz ICO'lar uygun zemin için yedektir. Her özel yüksek kontrast paletinin yalnız siyah/beyaz olduğu varsayılmaz. Gerekirse sağlanan basit yolları sistem rengiyle çiz ve HICON oluştur.
- Simgeyi sürekli döndürme, nefes aldırma veya yanıp söndürme. Değişikliği olayla bildir; boşta periyodik yeniden çizim veya tema sorgusu çalıştırma.

Native Windows entegrasyonunda Shell_NotifyIconW ile ekle/güncelle/kaldır yaşam döngüsünü, NOTIFYICON_VERSION_4 sürümünü, kararlı kimliği ve Explorer yeniden oluşturulduğunda TaskbarCreated bildirimini ele al. Uygulamada var olan GUID veya AUMID'yi sırf görsel ad değişti diye yenileme. Sürüm ayarını yeniden eklemelerden sonra da uygula. Üretimde standart Shell davranışını koru; tray görünürlüğünü zorla kalıcı yapmaya çalışma.

Uygun boyutu ICO içinden yükle; LoadIconMetric veya mevcut çatıdaki doğrulanmış DPI yöntemini kullan. HICON sahipliğini izleyerek ilgili API'nin gerektirdiği temizliği yap. DPI/tema değişimini ve birden fazla monitörü uygulamanın mevcut mesaj döngüsüne bağla. Arka planda ikinci bir ikon yöneticisi yaratma.

Sol tıklama / klavye aktivasyonu ana pencereyi açabilir; sağ tık erişilebilir menüyü açar. Performans değişikliği için mevcut Evet/Hayır onay akışını koru; tepsi ikonuna tek tıklama Windows ayarlarını doğrudan değiştirmesin.

## 5. Görünen ad ve önceki planla uyum

Pencere başlığı, Hakkında, tooltip ve uygun ProductName/FileDescription değerleri **Ecu Fps** olmalı. İsimle uyumlu yeni çıktı adı `EcuFps.exe` önerilir; mevcut dağıtım düzeniyle çelişiyorsa uyarlamayı raporla.

Önceki plan paketindeki geçici `PERFORMANS` adı yalnız kullanıcıya görünen ürün adı bağlamında güncellenir. Depo klasörleri, journal yolları, IPC kimlikleri, mutex adları, görev zamanlayıcı adları, kayıt şemaları, GUID/AUMID veya kullanıcı verisi konumları üzerinde kör global replace yapma. Bunların taşınması ayrı bir veri göçü kararı gerektirir.

Önceki pakette `design/REFERENCE_NOTES.md` logo hazırlanmamış diyorsa ilgili satırı bu paketin onaylı kimliğiyle güncelle. `state/` ve görev kaydına yalnız gerçekten yapılan marka entegrasyonunu yaz; performans modüllerini tamamlanmış işaretleme. Eski tasarım tokenlarını bütün dosyayı silerek değil mevcut yapıyla birleştir.

## 6. Hafiflik ve hareket

`brand-tokens.json` hareket değerleri üst sınırlı tasarım hedefleridir. Animasyon etkileşimi bekletmez ve ayar uygulama sonucunu gizlemez. Sürekli efekt, video, parçacık, blur veya animasyonlu tray yoktur. Hareket azaltma etkinse süreler sıfır olur. Oyun testi sırasında dekoratif animasyon durur. Logo ilk çizildikten sonra statiktir.

## 7. Tamamlama çıktısı

`docs/ACCEPTANCE_CHECKLIST.md` maddelerini uygulamaya göre değerlendir. Değişen dosyaları, derleme sonucunu, kullanılan kaynakları, çalışan kontrolleri ve atlanan Windows kontrollerini raporla. Derleyici veya Windows bulunmuyorsa başarı uydurma. Uygun ortamda Explorer, pencere, Alt+Tab ve tray için ekran görüntüsüyle gerçek sonucu belgelemek önerilir.
