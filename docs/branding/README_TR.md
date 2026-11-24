# Ecu Fps — Boost marka paketi

Sürüm 2.0.0 · 24 Kasım 2026 · Onaylanan tasarım: **04 / BOOST LINE** (EF amblemi; 1.0.0 02 / BOOST yerine)

Bu klasör, Ecu Fps uygulamasına eklenecek gerçek logo ve ikon varlıklarını içerir. Bir uygulama veya kurulum paketi değildir. SVG, PNG ve çok çözünürlüklü ICO dosyaları teslim edilmiştir.

## Claude Code ile kullan

1. ZIP'i aç; bu klasörü mevcut uygulama deposuna veya deponun yanına koy.
2. Claude Code'u uygulamanın bulunduğu klasörde başlat.
3. `CLAUDE_PROMPT.txt` içeriğini gönder; marka klasörü başka yerdeyse gerçek yolunu ekle.
4. Claude önce `CLAUDE_INTEGRATION.md` ve mevcut projenin kendi talimatlarını okusun.

Uygulama henüz yoksa bu paket üzerinden performans motoru başlatılmamalı. Varlıkların hazırlanmış olması, Windows ayarlarının uygulanması veya benchmark modüllerinin yazılması için izin sayılmaz.

## İçindekiler

| Konum | İçerik |
| --- | --- |
| `assets/app/ecu-fps.ico` | Asıl EXE / Explorer / görev çubuğu ikonu; 15 çözünürlük |
| `assets/tray/*.ico` | 6 sistem tepsisi sürümü; her birinde 7 çözünürlük |
| `assets/logo/svg/` | Şeffaf amblem, yatay ve dikey logo; düzenlenebilir vektör yolları |
| `assets/logo/png/` | Şeffaf PNG logo çıktıları |
| `assets/app/png/`, `assets/tray/png/` | ICO içindeki boyutların bağımsız PNG karşılıkları |
| `design/brand-tokens.json` | Renk, tipografi, boyut ve hareket değerleri |
| `design/asset-map.json` | Dosya rolleri, önerilen kaynak kimlikleri ve durum eşlemesi |
| `design/geometry.json` | Amblem, küçük boyut çizimi ve özel logo harflerinin kaynak geometrisi |
| `preview/index.html` | İnternetsiz açılan dosya kataloğu ve doğal boyut karşılaştırması |
| `preview/brand-overview.png` | Toplu görsel önizleme |
| `docs/` | Kullanım, entegrasyon kaynakları, doğrulama ve kabul listesi |
| `reference/` | Kullanıcının seçtiği orijinal 02 numaralı konsept |

## Önce kullanılacak dosyalar

- Koyu uygulama başlığı: `assets/logo/svg/ecu-fps-lockup-dark.svg`
- Açık yüzey: `assets/logo/svg/ecu-fps-lockup-light.svg`
- Küçük alan / dar menü: `assets/logo/svg/ef-mark.svg`
- EXE: `assets/app/ecu-fps.ico`
- Koyu görev çubuğu, performans açık: `assets/tray/tray-dark-active.ico`
- Koyu görev çubuğu, kapalı: `assets/tray/tray-dark-neutral.ico`
- Açık görev çubuğu: aynı adların `light` sürümleri

SVG yazıları çizgisel yollara çevrilmiştir; logo için font kurulmaz. Arayüz metinlerinde sistemdeki Segoe UI ailesi kullanılır. Klasörün tümünü uygulamanın yanında dağıtma: yalnız gereken varlıkları derleme kaynaklarına göm. Önizleme, referans ve belgeler son kullanıcı EXE'sine eklenmez.

Dosya yapısı ve ikon çözümlemesi kontrol edilmiştir. Gerçek Windows Explorer, görev çubuğu ve DPI davranışı Claude'un uygulama entegrasyonu sırasında doğrulanmalıdır; bu pakette Windows üzerinde çalıştırılmış uygulama testi yoktur.
