# Sürekli geliştirme ve devir akışı

## Yetkili kayıtlar

tasks/backlog.json görev durumunun tek kaynağıdır. tasks/BOARD.md okunabilir görünümüdür. state/progress.json aktif iş ve başlangıç yetkisini; state/STATE.md kısa özeti; state/HANDOFF.md sıradaki uygulanabilir adımı; state/BLOCKERS.md dış engelleri tutar. Çelişkide kod/Git ve gerçek test kanıtı incelenir, kayıtlar düzeltilir; eski bir "tamamlandı" cümlesi kanıt sayılmaz.

## Görev seçimi

Varsa in_progress görevini kurtar. Yoksa dependencies listesinin tamamı done olan planned görevler arasından en düşük order değerini seç. Yalnız gerçek dış engelde blocked yap ve bağımsız göreve geç. review durumunu done gibi tüketme. Tek oturumda bir görev aktiftir. Kullanıcı durdurana veya gerçek sınıra ulaşana kadar olağan geliştirmeyi sürdür.

Bir görevin implementation kabulü mock ile tamamlanabiliyorsa bu görevde açıkça belirtilir; donanım yayına açılışı ayrıca QA kapısında bekler. Yapılmayan fiziksel testi done gösterme. Eğer sözleşme değişikliği gerekiyorsa gerekçeli karar kaydet, ilgili görev ve testleri güncelle; kapsamı sessiz daraltma.

## Checkpoint sırası

1. Çalışan komutlar ve değişiklikleri evidence belgesine yaz.
2. backlog.json görev status/evidence/result alanlarını güncelle.
3. BOARD.md görünümünü güncelle.
4. progress.json ve STATE.md aktif/sonraki işi eşleştir.
5. HANDOFF.md: değişen dosyalar, testler, başarısızlıklar, kesin sonraki adım.
6. SESSION_LOG.md kısa tarihli kayıt; varsa commit SHA.

Değişiklikler küçük ve kurtarılabilir tutulur. Git yoksa ENV aşamasında başlatılır; mevcut repo yeniden kurulmaz. Kullanıcının onaysız değişiklikleri üzerine reset/clean yapılmaz. Yerel commit politikası mevcut kullanıcı yetkisine uyar. Uzak push/merge/yayın ve ücretli kaynak bağımsız işlemlerdir.

## Kota ve oturumlar

Belgeler geçmişi yeniden anlatma yükünü azaltır. Başlatma, context compaction veya abonelik limitlerini kaldırmaz. Kesintiden önce aktif görevin kısmi gerçeği ve sonraki adımı diske yazılır. Oturum kapandıysa aynı klasörde kısa "kayıtlardan devam" isteği yeterlidir. Claude Code oturum devamı resmî belgede açıklanır; yeni hesap/oturum için geçmiş yerine dosyalar esas alınır. [S97 S98]

## Bağlam bütçesi

Kök talimatları kısa tut. Ayrıntı ilgili sözleşme ve dal dosyasında kalsın. Tüm 90+ kaynağı her seferinde yeniden tarama; yapılacak API ve sürüm değişmişse o kaynağı yeniden doğrula. PNG/PDF arşivi tasarım gerektiğinde aç. Uzun komut çıktılarını loga koy, raporda sonucu ve yolu ver.

## Kullanıcıya soru gereken durumlar

Eksik Windows/gerçek test cihazı, dış hesap/sertifika, ücretli hizmet, yayın hedefi veya birbiriyle çelişen ürün kararı. Tasarımın küçük teknik ayrıntısı, yerel kaynak düzenleme, olağan build/test ve dosya kaydı için sürekli soru sorma. Bu paket herhangi bir ajan aracının güvenlik iznini atlatmaz.
