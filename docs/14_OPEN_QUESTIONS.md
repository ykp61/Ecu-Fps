# Açık teknik sorular ve varsayımlar

Bu liste kullanıcının hepsine şimdi cevap vermesini gerektirmez. Ajan resmî doküman ve yerel deneyle çözebildiğini çözer; yalnız dış erişim veya ürün tercihi gerekeni sorar.

| Konu | Geçici karar | Çözüm yeri |
|---|---|---|
| Native tek EXE + SDK | C++ aday; extraction yok | ENV / GPU / RELEASE |
| Windows Game Mode/HAGS/GPU preference yazımı | Doğrulanmış write contract olmadan yazma yok | WIN |
| PresentMon hizmet bağımlılığı | Kütüphane entegrasyonu öncelikli | MEASURE |
| Haricî oyunun VRAM bütçesi | Kendi süreç DXGI API'si yeterli varsayılmaz | MEASURE / DIAG |
| İlk oyun adaptörleri | CS2 aday; ikinci oyun mevcut erişime göre | GAME |
| CPU/RAM firmware kontrolü | Envanter/teşhis açık; yazma capability kanıtına bağlı | GPU / DIAG |
| Anakart sensör kapsamı | Ölçülebilen platform sınırlaması; kesin arıza iddiası yok | INV / DIAG |
| Benzer sistem gerçek verisi | Yerel sürümlü import; topluluk hizmeti dış bağımlılık | INSIGHTS |
| Sertifika/yayın sahibi | Bilinmiyor; signing sırrı istenmez | RELEASE |
| Düşük donanımlı test cihazı | Henüz teyit edilmedi | QA |
| Marka adı ve son logo | Karar: Ecu Fps (2026-11-04, kullanıcı). Logo açık | Logo kullanıcı istediğinde; kod ilerlemesine engel değil |
| Otomatik mod ve arka plan | Karar (2026-11-04, kullanıcı): görünür tepsi + oyun başına tek onay; açılışta başlama isteğe bağlıydı; ADR-030 (2026-11-13) ile varsayılan açık + ilk açılış bildirimi + Ayarlar anahtarı; gizli servis yok | SEC-05 / UI-06 / OPT-06 |
| Ana PC'de gerçek ayar testi | Karar (2026-11-04, kullanıcı): her gerçek mutation testinden önce Sistem Geri Yükleme noktası + Evet/Hayır | ENV-06 |

Bir soruyu kapatırken kaynak, deney, karar ve kalan sınır yazılır. Eksik API çözülmediği hâlde "yetki tamam" kaydı açılmaz.
