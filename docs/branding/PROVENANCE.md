# Ecu Fps — marka varlıkları: köken ve lisans

Tarih: 2026-11-24

## Köken
- Sahip, beş taslak arasından **4 numaralı konsepti ("Boost Line")** seçti: eğik E+F ligatürü, E'nin orta çubuğu F'nin içinden geçen hız çizgisine dönüşür ve ok ucuyla biter.
- Bu klasördeki her şey o konseptin üretim sürümüdür. Tüm geometri **elle yazılmış vektördür**: koordinatlar `_build.py` içinde sayısal olarak tanımlıdır.
- **Yapay zekâ görsel üreticisi kullanılmadı.** Fotoğraf, stok görsel, iz sürme (trace) veya dış kaynaklı çizim yok.
- **Üçüncü taraf logo taklidi yok.** Yeşil renk uygulamanın kendi "Performans Açık" rengidir (`#3DFF7A`); biçim (italik EF ligatürü, yarım kalınlıkta hız çizgisi, camgöbeği ok ucu) özgündür.
- Rasterler (ICO/PNG) aynı vektör tanımından, betikteki 8x/4x süper örneklemeli rasterleyiciyle üretildi (Pillow + numpy). Ağ erişimi kullanılmadı.

## Geometri sistemi
- Tuval 256 birim; koordinatlar 1/16 birime oturtulur. İtalik eğim 10°.
- Harf kalınlığı 36; hız çizgisi 18 (0,5x); F gövdesindeki boşluklar 12 (kalınlık/3); köşe pahı 12 (kalınlık/3, 45°); ok ucu 45° kenarlı.
- ≤32 px uygulama simgesi ve tüm tepsi simgeleri için boyuta özel, piksel ızgarasına oturtulmuş çizim (16 ve 20 px tamamen elle; italik, hız çizgisinin arkasına gizlenen 1 px'lik basamaktır).

## Yazı tipleri
- **Wordmark "Ecu Fps":** yazı tipi kullanılmadı. Harfler (E, c, u, F, p, s) işaretin kalınlık/pah sisteminden üretilmiş özgün geometrik çizimlerdir; SVG'lerde yalnız `<path>` vardır.
- **Slogan "Oyun performansı, dürüstçe."** (og-image, site-header): **JetBrains Mono Medium 2.304**, SIL Open Font License 1.1. Depodaki `third_party/fonts/jetbrains-mono/` alt kümesinden (OFL.txt ve SOURCE.txt orada) fontTools ile dış hatlara çevrildi; SVG'ye yazı tipi gömülmedi, çalışma zamanında yazı tipi gerekmez. OFL, yazı tipiyle üretilen grafiklerde kısıtlama getirmez; Reserved Font Name beyanı yok.
- Sistem ya da sahipli yazı tipine (Segoe UI vb.) bağımlılık yok.

## Renkler (kaynak: `design/tokens.json`)
| Rol | Değer |
|---|---|
| İşaret (koyu zemin) | `#3DFF7A` |
| İşaret (açık zemin) | `#0B8F3C` |
| İkincil vurgu: ok ucu (koyu) | `#00D4FF` |
| İkincil vurgu: ok ucu (açık) | `#00708F` (camgöbeğinin açık zemin için koyulaştırılmış türevi; yeni değer) |
| Simge plakası / kenarlık | `#10151E` / `#0B8F3C` |
| Uygulama zemini / yüzeyler | `#05070B` / `#0B0F17` / `#121824` |
| Metin | `#F1F5F9` / `#8A99AD` |
| Tepsi | koyu açık `#3DFF7A`, koyu kapalı `#7B8490`, açık açık `#0B8F3C`, açık kapalı `#808994` |

Büyük karoda işaret, `#8CFFB1 → #3DFF7A → #22E391` dikey geçişiyle boyanır (alt uç hafif camgöbeğine kayar); düz sürümde tek renk `#3DFF7A` kullanılır.

## Kontrast (WCAG 2.x oranı)
- `mark_green_on_plate`: 13.75:1
- `mark_green_on_app_bg`: 15.15:1
- `cyan_on_plate`: 10.33:1
- `plate_on_white`: 18.29:1
- `light_green_on_white`: 4.19:1
- `light_cyan_on_white`: 5.65:1
- `text_on_app_bg`: 18.4:1
- `text2_on_app_bg`: 6.95:1
- `ink_on_white`: 19.18:1
- `tray_dark_active_on_1B1B1B`: 12.95:1
- `tray_dark_neutral_on_1B1B1B`: 4.55:1
- `tray_light_active_on_EEF1F4`: 3.7:1
- `tray_light_neutral_on_EEF1F4`: 3.13:1
- `tray_light_neutral_previous_808994_on_EEF1F4`: 3.13:1
- `tray_hc_white_on_black`: 21.0:1
- `tray_hc_black_on_white`: 21.0:1

## Yeniden üretim
`python _build.py <depo_kökü>` (Pillow, numpy, fontTools). Çıktı deterministiktir.

## Doğrulanmamış olanlar
- Gerçek Windows görev çubuğu/Gezgin üzerinde (farklı DPI ve temalarda) yerinde doğrulama yapılmadı; önizlemeler betik rasterleri ve başsız tarayıcı görüntüleridir.
- Marka tescili/benzerlik araştırması yapılmadı.

## Depoya entegrasyon (2026-11-24)
- Paket `assets/branding/ecu-fps/` altına teslim edildiği klasör yapısıyla kopyalandı (`app/`, `logo/svg/`, `tray/`, `tray/svg/optical/`, `web/`, `setup/`, `asset-map.json`, `brand-tokens.json`, üretici `_build.py` + `_meta.py`). `preview/` depoya alınmadı. ICO dosyaları teslim edildiği gibidir (uygulama 15 boyut, tepsi 6 x 7 boyut); yeniden kodlanmadı.
- Uygulama içi çizim (Direct2D vektör): `logo/svg/ecu-fps-lockup-{dark,light,white}.svg`, `logo/svg/ef-mark.svg`, `logo/svg/ef-mark-currentcolor.svg` ve tepsi/küçük ikon için `tray/svg/optical/tray-{16,20,24}-active.svg` derleme zamanında EXE'ye gömülür (`src/ui/CMakeLists.txt`). Ayrıştırıcı SVG varsayılanı `nonzero` dolgu kuralını uygular (hız çizgisi E gövdesiyle örtüşür; çift-tek kuralı orada boşluk açardı).
- Web: `site/favicon.svg`, `site/favicon.ico`, `site/apple-touch-icon.png`, `site/og-image.png` bu klasörün `web/` çıktılarının kopyasıdır; sayfa başlığındaki logo `ecu-fps-lockup-dark.svg` yollarının satır içi kopyasıdır.
- 1.0.0 çift ok amblemi (`boost-mark-*`, `boost-optical-small.svg`) kaldırıldı. Yalnız ölçüm sahnesi `arena_v1` geometrisi karşılaştırılabilirlik için `assets/benchmark/arena_v1-chevrons.svg` içinde dondurulmuştur; marka varlığı değildir.
- ICO denetimi: `python -I scripts/build_brand_icons.py --check` (ctest `windows.brand_icons`); yeniden üretim aynı betiğin argümansız çağrısıyla paket üreticisine devredilir.
