# Ecu Fps / Boost Line kullanım kılavuzu

Marka sürümü 2.0.0 · 2026-11-24 · Onaylanan konsept: **04 / BOOST LINE** (önceki 1.0.0 "02 / BOOST" çift ok ambleminin yerine geçer).

## Kimlik

Amblem eğik (10°) bir **E + F ligatürüdür**: E'nin orta çubuğu F'nin içinden geçen yarım kalınlıkta bir **hız çizgisine** dönüşür ve camgöbeği bir **ok ucuyla** biter. Tüm geometri elle yazılmış vektördür (256 birimlik tuval, 1/16 birim ızgara): harf kalınlığı 36, hız çizgisi 18 (0,5x), F gövdesindeki boşluklar 12, köşe pahı 12 (45°).

Ürün adı her yerde **Ecu Fps** yazılır: büyük `E`, küçük `cu`, boşluk, büyük `F`, küçük `ps`. Logo harfleri amblemin kalınlık/pah sisteminden çizilmiş özgün yollardır; kurulabilir bir font değildir. Uygulama başlığını Segoe UI ile yazmak serbesttir; fakat logo görselindeki harflerin yerine rastgele font koyma.

## Varyantlar

| Varyant | Dosya (`assets/branding/ecu-fps/`) | Kullanım |
| --- | --- | --- |
| Yatay, koyu zemin | `logo/svg/ecu-fps-lockup-dark.svg` | Uygulama üst menüsü, kurulum sihirbazı, hoş geldin kartı, site başlığı |
| Yatay, açık zemin | `logo/svg/ecu-fps-lockup-light.svg` | Açık belge / web yüzeyi |
| Tek renk beyaz / mürekkep | `logo/svg/ecu-fps-lockup-white.svg`, `-ink.svg` | Yüksek kontrast, tek renk baskı |
| Dikey | `logo/svg/ecu-fps-stacked-{dark,light}.svg` | Hakkında veya geniş tanıtım alanı |
| Yalnız amblem | `logo/svg/ef-mark.svg` (`-light`, `-currentcolor`) | Dar üst menü, güncelleme kapısı, ikon |
| Küçük boyut optik çizim | `tray/svg/optical/tray-{16..64}-{active,neutral}.svg` | Tepsi ve 16–24 px simge |

Uygulama koyu palette renkli dosyaları olduğu gibi çizer (yeşil amblem, camgöbeği ok ucu, #F1F5F9 yazı); yüksek kontrast paletinde aynı geometriyi tek renk (sistem metin rengi) çizer.

## Oran ve boşluk

- Amblem tuvali 256 × 256; boyalı alan x 20–236, y 50,6–205,4. Yatay logo 397 × 120 (12 birim iç boşluk).
- En-boy oranı sabit kalır. Esnetme, çevirme, aynalama veya eğimi değiştirme yok.
- Logonun çevresinde amblem genişliğinin en az %10'u kadar boş alan bırak.
- Yatay logo arayüzde en az 160 DIP genişlikte kullanılır. Daha dar yerde yalnız amblem.
- Tam wordmark EXE veya tepsi içine sıkıştırılmaz; küçük alanda yalnız amblem kullanılır.

## Küçük ikon davranışı

Uygulama ICO'su (15 boyut) her boyutta grafit (#10151E) plakalıdır. 32 px ve altında düz plaka ve tek renk amblem; 36 px ve üstünde plaka geçişi, yumuşak iç ışık, #0B8F3C sınır ve 48 px üstünde ince parlak kenar. 256 px girdisi PNG'dir.

Tepsi ikonlarında plaka ve yazı yoktur. 16 ve 20 px çizimleri tamamen piksel ızgarasına elle çizilmiştir (italik, hız çizgisinin arkasına gizlenen 1 px'lik basamaktır); 24–64 px boyuta özel optik çizimdir. Uygulama yüksek kontrastta tepsi simgesini kendisi çizerken aynı optik çizimleri (16/20/24 px) ve üstünde ana amblemi kullanır (`brand::mark_for_px`).

**Tepsi durumu renk VE biçimle ayrılır.** Performans doğrulanmış olarak açık = yeşil, ok uçlu amblem. Kapalı / doğrulanıyor / kurtarma gerekli = gri amblem, hız çizgisi küt biter (ok ucu yok). Koyu görev çubuğu: #3DFF7A / #7B8490. Açık görev çubuğu: #0B8F3C / #808994. Her renk kendi görev çubuğu zemininde en az 3:1 karşıtlık taşır; durum ayrıca ipucu ve menü metninde yazar. Yüksek kontrast ICO'ları (#FFFFFF / #000000) tam amblemdir.

ICO, PNG ve SVG dosyaları paket üreticisiyle (`assets/branding/ecu-fps/_build.py`; Pillow, numpy, fontTools; deterministik) üretilir; `python -I scripts/build_brand_icons.py` bu üreticiyi çağırır. `--check` diskteki ICO'ları denetler (boyutlar, 32 bpp DIB, token renkleri, açık/nötr renk ve ok ucu farkı, görev çubuğu karşıtlığı, yüksek kontrast tam amblem; ctest `windows.brand_icons`).

## Renkler

| Ad | Hex | Rol |
| --- | --- | --- |
| Boost yeşili | #3DFF7A | Amblem (koyu zemin), koyu görev çubuğunda açık durum |
| Camgöbeği | #00D4FF | Ok ucu (koyu zemin); ikincil vurgu |
| Koyu yeşil | #0B8F3C | Amblem (açık zemin), ikon plaka sınırı, açık görev çubuğunda açık durum |
| Koyu camgöbeği | #00708F | Ok ucu (açık zemin) |
| Grafit | #10151E | İkon plakası |
| Zemin / yüzey | #05070B / #0B0F17 / #121824 | Uygulama zemini, kart, yükseltilmiş yüzey |
| Metin | #F1F5F9 / #8A99AD | Birincil / yardımcı yazı; wordmark (koyu zemin) #F1F5F9 |
| Nötr tepsi | #7B8490 / #808994 | Koyu / açık görev çubuğunda nötr durum |

Büyük uygulama karosunda amblem #8CFFB1 → #3DFF7A → #22E391 dikey geçişiyle boyanır; düz sürümde tek renk #3DFF7A. Renkler `design/tokens.json` paletiyle aynıdır; marka değişikliği UI tokenlarını değiştirmez.

Ana yeşil (#3DFF7A) açık zemin üzerinde küçük durum metni olarak kullanılmaz; açık yüzeyde #0B8F3C kullanılır. Dekoratif kenarlık klavye odağı için yeterli sayılmaz. Renkler performans başarısının kanıtı değildir.

## Arayüz yazısı ve ölçüler

Sistem fontu: Segoe UI Variable, bulunmuyorsa Segoe UI. Font dosyası dağıtılmaz. Başlık 26/34, bölüm 18/26, gövde 14/20, yardımcı yazı 12/18 DIP; ayrıntılar tokenlarda.

Masaüstü düğme yüksekliği 40 DIP, normal asgari genişlik 112 DIP. İkon düğmesi 32 × 32 DIP görsel alana sahip olabilir; dokunma kullanılan arayüzde hedef alan en az 44 × 44 DIP olur. Anahtar 48 × 28 DIP. Büyük çağrı düğmeleri kimliğin zorunlu parçası değildir.

## Hareket ve efekt

| Olay | Süre | Görsel davranış |
| --- | --- | --- |
| Hover | 80 ms | Yüzey rengi |
| Basma | 60 ms | Renk; geometri sıçraması yok |
| Anahtar | 120 ms | Konum + renk |
| Sayfa içeriği | 140 ms | Hafif opacity; yüklemeyi bekletmez |
| Detay / diyalog açma | 160 ms | Opacity, gerekiyorsa en çok 4 DIP yer değişimi |
| Diyalog kapama | 100 ms | Opacity |
| Durum değişimi | 120 ms | Metin/renk güncellemesi |

Eğri: cubic-bezier(0.16, 1, 0.3, 1). İlk logo gösterimi isteğe bağlı en fazla 160 ms opacity geçişi yapabilir; açılışı bekleten splash animasyonu yoktur. Tray tamamen statiktir. Hareket azaltma: 0 ms. Oyun/benchmark sırasında süs animasyonu yok. Ürün eylemleri animasyon tamamlanmasını beklemez.

Glow, sürekli tarama çizgisi, RGB çevrim, parlayan metin, parçacık, arka plan video, blur ve ses efekti bu paketin parçası değildir. Bunları sonradan eklemek marka entegrasyonu görevinin gereği sayılmaz.

**İstisna (ADR-027, ürün sahibi onayı):** uygulama arayüzünde yalnız iki efekt, yalnız şu kurallarla izinlidir. (1) Parlama: ana düğme, açık anahtar ve hız testi kadranı/büyük sayı; 3 halkalı dolgu taklidi; kartlarda yok; yüksek kontrastta ve hareket azaltmada çizilmez. (2) Üstte 3 DIP hareketli RGB şerit (#FF2D55 → #FFCC00 → #3DFF7A → #00D4FF → #A64DFF), 8 sn'de bir tur; pencere görünmez/odakta değil, oyun açık, ölçüm veya hız testi sürüyor, pilde, hareket azaltma açık, uygulama animasyonları ya da "RGB ışık şeridi" kapalıysa durur; yüksek kontrastta tek renk çizgidir. Tray statik kalır. Diğer efektler yasak olmaya devam eder. Kurallar: `docs/superpowers/specs/2026-11-11-zehir-yesili-redesign-design.md` §2–§3.


## Kaçınılacak kullanımlar

Hız çizgisini veya ok ucunu kesmek, E ile F'yi ayırmak, eğimi kaldırmak ya da ters çevirmek, kenarlara kontur vermek, ok ucunu yeşile boyamak (koyu zeminde camgöbeği, tek renk kullanımda aynı renk), ekran kartı üreticisi logoları eklemek, PNG'yi JPEG'e çevirmek, şeffaf alanı siyah kutuyla doldurmak ve önizleme sayfalarından kırpıp kullanmak kimliği bozar. Üretim dosyaları `assets/branding/ecu-fps/` altındadır.
