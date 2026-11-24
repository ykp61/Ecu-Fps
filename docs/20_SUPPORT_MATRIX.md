# Donanım destek matrisi (yetenek x üretici)

Bu dosya `ef_support_matrix --markdown` çıktısıdır; elle düzenlenmez (`test_vendor_matrix` dosyayı üretilenle karşılaştırır).
Kaynak tablo: `src/core/capabilities/vendor_matrix.h`.

## Nasıl okunur

- **Yazma**: kodda bu üretici kimliğinde çalışan bir yazma yolu var. Bu, cihazda desteklendiği veya sürümde açık olduğu anlamına GELMEZ: kanıt tavanı birim/sahte arka uç testidir; gerçek cihaz yazma kanıtı ayrı kayıttır ve sürüm kapısı (`release_enabled`) hepsinde kapalıdır.
- **Yalnız okuma**: tespit, okuma veya rehber; yazma yolu yok ya da kapalı.
- **Desteklenmiyor**: bu üretici kimliğinde yol yok. Gerekçe kodu aşağıdaki tablodadır. Yönetici hakkı bu satırları değiştirmez.
- Üretici kimliği PCI üretici numarasından gelir (NVIDIA 10DE, AMD 1002, Intel 8086). Bilinmeyen veya okunamayan kimlik satır almaz ve hiçbir yazma yolu açmaz.
- **AMD ve Intel kimliğinde üretici sürücüsü (ADLX / IGCL) yazma yolu açık değildir.** AMD/Intel sütunlarındaki "Yazma" satırları yalnız üreticiden bağımsız Windows yollarıdır.
- Hibrit sistemde (ör. AMD iGPU + NVIDIA ayrık kart) satır, oyunun gerçekten çalıştığı bağdaştırıcının üreticisine göre seçilir; DXGI sırası veya "ilk kart" kimlik değildir.

## Matris

| Kimlik | Ayar | NVIDIA | AMD | Intel |
|---|---|---|---|---|
| W01 | AC güç modu | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W02 | Güç planı geçişi | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W03 | Windows Oyun Modu | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W04 | Oyun için doğru GPU | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W05 | Pencereli oyun optimizasyonu | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W06 | HAGS | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W07 | Arka plan oyun kaydı | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W08 | Desteklenen ekran yenileme hızı | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W09 | Windows görsel efektleri | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W10 | Oyun süreci güç kısıtlaması | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W11 | Süreç önceliği | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W12 | Arka plan iş yükü | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W13 | USB seçici askıya alma (AC) | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| W14 | Ön plan önceliği (Win32PrioritySeparation tek değer) | Desteklenmiyor [A2] | Desteklenmiyor [A2] | Desteklenmiyor [A2] |
| W15 | Hizmet ana bilgisayarı bölme eşiği | Desteklenmiyor [E1] | Desteklenmiyor [E1] | Desteklenmiyor [E1] |
| W16 | Dinamik zamanlayıcı tıkı (BCD) | Desteklenmiyor [A2] | Desteklenmiyor [A2] | Desteklenmiyor [A2] |
| G01 | NVIDIA oyun güç profili | Yazma [A1] | Desteklenmiyor [D1] | Desteklenmiyor [D1] |
| G02 | NVIDIA düşük gecikme | Yazma [A1] | Desteklenmiyor [D1] | Desteklenmiyor [D1] |
| G03 | AMD sürücü Anti-Lag | Desteklenmiyor [D1] | Desteklenmiyor [C1] | Desteklenmiyor [D1] |
| G04 | Intel oyun profili | Desteklenmiyor [D1] | Desteklenmiyor [D1] | Desteklenmiyor [C2] |
| G05 | V-Sync ve VRR ilişkisi | Yazma [A1] | Desteklenmiyor [C1] | Desteklenmiyor [C2] |
| G06 | FPS sınırlayıcı | Yazma [A1] | Desteklenmiyor [C1] | Desteklenmiyor [C2] |
| G07 | Shader önbelleği politikası | Yalnız okuma [A2] | Desteklenmiyor [C1] | Desteklenmiyor [C2] |
| G08 | Tasarruf kaynaklı FPS sınırı | Desteklenmiyor [D1] | Desteklenmiyor [C1] | Desteklenmiyor [D1] |
| G09 | AMD Radeon Boost | Desteklenmiyor [D1] | Desteklenmiyor [C1] | Desteklenmiyor [D1] |
| G10 | GPU otomatik ayarlama | Desteklenmiyor [D3] | Desteklenmiyor [D3] | Desteklenmiyor [D3] |
| G11 | GPU güç ve fan sınırları | Desteklenmiyor [D3] | Desteklenmiyor [D3] | Desteklenmiyor [D3] |
| G12 | Resizable BAR ve SAM | Yalnız okuma [B1] | Yalnız okuma [B1] | Yalnız okuma [B1] |
| G13 | NVIDIA iş parçacıklı iyileştirme | Yalnız okuma [A2] | Desteklenmiyor [D1] | Desteklenmiyor [D1] |
| G14 | NVIDIA FXAA kapalı | Yalnız okuma [A2] | Desteklenmiyor [D1] | Desteklenmiyor [D1] |
| O01 | Gölge ve efekt ayarları | Yalnız okuma [B3] | Yalnız okuma [B3] | Yalnız okuma [B3] |
| O02 | Işın izleme ayarları | Desteklenmiyor [E1] | Desteklenmiyor [E1] | Desteklenmiyor [E1] |
| O03 | Doku ve VRAM baskısı | Yalnız okuma [B3] | Yalnız okuma [B3] | Yalnız okuma [B3] |
| O04 | Render çözünürlüğü | Yalnız okuma [B3] | Yalnız okuma [B3] | Yalnız okuma [B3] |
| O05 | DLSS FSR XeSS ölçekleme | Yalnız okuma [B2] | Yalnız okuma [B2] | Yalnız okuma [B2] |
| O06 | Kare üretimi | Desteklenmiyor [E1] | Desteklenmiyor [E1] | Desteklenmiyor [E1] |
| O07 | Reflex Anti-Lag 2 XeLL | Yalnız okuma [B3] | Yalnız okuma [B3] | Yalnız okuma [B3] |
| O08 | Grafik API ve sunum modu | Yalnız okuma [B2] | Yalnız okuma [B2] | Yalnız okuma [B2] |
| H01 | CPU PBO Curve Optimizer XTU | Desteklenmiyor [D3] | Desteklenmiyor [D3] | Desteklenmiyor [D3] |
| H02 | RAM XMP EXPO ve zamanlamalar | Yalnız okuma [B1] | Yalnız okuma [B1] | Yalnız okuma [B1] |
| H03 | Çekirdek parkı affinity E-core | Desteklenmiyor [E1] | Desteklenmiyor [E1] | Desteklenmiyor [E1] |
| H04 | Pagefile ve bellek yönetimi | Yalnız okuma [B2] | Yalnız okuma [B2] | Yalnız okuma [B2] |
| H05 | Sürücü ve chipset güncellemesi | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| H06 | Isı ve güç sınırı izlemesi | Yalnız okuma [B4] | Desteklenmiyor [C1] | Desteklenmiyor [C2] |
| X01 | HPET BCD dynamic tick paketleri | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X02 | Sürekli timer resolution zorlama | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X03 | Realtime önceliği | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X04 | RAM temizleme standby boşaltma | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X05 | Defender HVCI VBS Secure Boot kapatma | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X06 | Servisleri topluca devre dışı bırakma | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X07 | DNS MTU TCP paketlerini FPS ayarı sayma | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X08 | MSI IRQ ve kayıt defteri sihirli değerleri | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X09 | Dosya ve shader önbelleği silme | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X10 | Otomatik BIOS ve firmware yazma | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X11 | Win32PrioritySeparation değerleri | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X12 | Multimedia SystemProfile/Tasks değerleri | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| X13 | MPO devre dışı bırakma (OverlayTestMode) | Desteklenmiyor [D2] | Desteklenmiyor [D2] | Desteklenmiyor [D2] |
| B01 | Kullanıcı Run girişleri | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| B02 | Makine Run girişleri | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| B03 | Startup klasörü kısayolları | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| B04 | Oturum açma zamanlanmış görevleri | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| B05 | Üçüncü taraf servis başlangıcı | Yazma [A1] | Yazma [A1] | Yazma [A1] |
| B06 | Paketli uygulama başlangıcı | Desteklenmiyor [E2] | Desteklenmiyor [E2] | Desteklenmiyor [E2] |
| B07 | Oturumluk arka plan hizmeti durdurma | Yazma [A1] | Yazma [A1] | Yazma [A1] |

## Gerekçe kodları

| Kod | Anahtar | Açıklama |
|---|---|---|
| A1 | `hw.matrix.write_unit_only` | Kodda yazma yolu var; yalnız birim/sahte arka uç testi. Gerçek cihazda yazma kanıtı yok; sürüm kapısı kapalı. |
| A2 | `hw.matrix.write_gated_off` | Yazma yolu kodda var ama kapalı: resmî belgede birim/anlam doğrulanana kadar plana girmez. |
| B1 | `hw.matrix.read_bios_guide` | Yalnız okuma tahmini ve BIOS rehberi; Windows'tan resmî yazma yolu yok. |
| B2 | `hw.matrix.read_detection_only` | Yalnız tespit/okuma; yazma yolu yok. |
| B3 | `hw.matrix.read_game_config_only` | Oyun ayar dosyası yalnız okunur (şu an yalnız CS2); oyun ayarı yazımı kapalı. |
| B4 | `hw.matrix.read_telemetry_device_read` | Sürücü telemetrisi okunur; bu PC'de gerçek cihazda okuma doğrulandı (yazma yok). |
| C1 | `hw.matrix.amd_sdk_license_pending` | AMD SDK lisansı (ADLX / Ryzen Master) ürün sahibince kabul edilmedi; başlık ve bağlayıcı yok. |
| C2 | `hw.matrix.igcl_no_binding_no_device` | Intel IGCL bağlayıcısı yok ve Intel GPU'lu test cihazı yok; yalnız sözleşme + mock testi. |
| D1 | `hw.matrix.other_vendor_feature` | Bu ayar başka üreticinin sürücü özelliğidir; bu üretici kimliğinde karşılığı yok. |
| D2 | `hw.matrix.excluded_policy` | Ürün politikasıyla dışlandı; hiçbir üreticide açılmaz. |
| D3 | `hw.matrix.policy_no_oc_voltage_fan_write` | Ürün değişmezi: genel voltaj / fan / aşırı hız yazımı yok; yönetici hakkı bunu açmaz. |
| E1 | `hw.matrix.not_implemented` | Henüz uygulanmadı; kodda yol yok. |
| E2 | `hw.matrix.os_no_documented_api` | Windows bu işlem için belgeli, güvenilir bir API sunmuyor. |

Adlandırma notu: AMD ve Intel için "Yazma" satırı olan özellikler yalnız `Windows` kanalındadır; `vendor_driver` kanalında yalnız NVIDIA yazma satırı vardır (NVAPI DRS: G01, G02, G05, G06).
