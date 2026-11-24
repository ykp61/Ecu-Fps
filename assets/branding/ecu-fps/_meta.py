# -*- coding: utf-8 -*-
"""Writes asset-map.json, brand-tokens.json, PROVENANCE.md and preview/index.html (all UTF-8)."""
import os, json, html
import _build as B

DATE = "2026-11-24"

def write(files, C):
    repo_brand = os.path.join(B.REPO, "assets", "branding", "ecu-fps")
    def load(p):
        with open(p, encoding='utf-8') as f: return json.load(f)
    # ---------------- asset-map.json (same keys as the repo file, new values) ----------------
    am = load(os.path.join(repo_brand, "asset-map.json"))
    am["version"] = "2.0.0"
    am["app"].update({
        "file": "assets/app/ecu-fps.ico", "sizes": B.APP_SIZES,
        "format": "ICO 32bpp BGRA DIB with AND mask; 256px PNG",
        "small_optical_unplated_through_px": 0,
        "mark_color": B.GREEN, "plate_color": B.PLATE, "plate_border_color": B.BORDER,
        "svg": "assets/app/svg/ecu-fps-app-tile.svg",
        "secondary_accent_color": B.CYAN, "flat_through_px": 32,
        "flat_svg": "assets/app/svg/ecu-fps-app-tile-flat.svg", "png_256": "assets/app/png/ecu-fps-256.png",
        "finish": "<=32 px and forced-colors/prefers-contrast: flat plate, solid mark. >=36 px: plate gradient, soft inner glow, border, hairline highlight (>=48 px).",
    })
    am["logo"] = {
        "horizontal_dark": "assets/logo/svg/ecu-fps-lockup-dark.svg", "horizontal_light": "assets/logo/svg/ecu-fps-lockup-light.svg",
        "monochrome_white": "assets/logo/svg/ecu-fps-lockup-white.svg", "monochrome_ink": "assets/logo/svg/ecu-fps-lockup-ink.svg",
        "symbol": "assets/logo/svg/ef-mark.svg", "symbol_currentcolor": "assets/logo/svg/ef-mark-currentcolor.svg",
        "stacked_dark": "assets/logo/svg/ecu-fps-stacked-dark.svg",
        "stacked_light": "assets/logo/svg/ecu-fps-stacked-light.svg", "symbol_light": "assets/logo/svg/ef-mark-light.svg",
    }
    am["tray"]["sizes"] = B.TRAY_SIZES
    for k, v in am["tray"]["variants"].items():
        v["file"] = "assets/tray/tray-%s.ico" % k; v["color"] = B.TRAY[k]
        v["svg"] = "assets/tray/svg/tray-%s.svg" % k
        if "shape" in v: v["shape"] = "filled, with arrowhead" if "active" in k else "filled, no arrowhead"
    am["tray"]["state_shape"] = ("filled mono mark in both states; state is carried by colour AND shape: active = green with the arrowhead, "
                                 "neutral = grey, speed line ends blunt (no arrowhead). High-contrast icons use the full mark.")
    am["tray"]["generator"] = "_build.py in this folder (app, tray, web, setup; every ICO is regenerated)"
    am["tray"]["optical_sources"] = "assets/tray/svg/optical/tray-{size}-{active|neutral}.svg (currentColor, pixel-grid art per size)"
    am["web"] = {"favicon_svg": "assets/web/favicon.svg", "favicon_ico": "assets/web/favicon.ico", "favicon_ico_sizes": [16, 32, 48],
                 "apple_touch_icon": "assets/web/apple-touch-icon.png", "og_image": "assets/web/og-image.png", "logo_512": "assets/web/logo-512.png",
                 "site_header": "assets/web/site-header.svg", "site_header_dark": "assets/web/site-header-dark.svg", "site_header_light": "assets/web/site-header-light.svg"}
    am["setup"] = {"banner_svg": "assets/setup/setup-banner.svg", "banner_png": "assets/setup/setup-banner.png", "banner_size": [164, 314],
                   "header_png": "assets/setup/setup-header.png", "header_svg": "assets/setup/setup-header.svg", "header_size": [300, 114]}
    am["production_exclusions"] = ["reference/", "preview/", "docs/", "design/geometry.json", "_build.py", "_meta.py", "PROVENANCE.md"]
    am["native_windows_validation"] = "pending"
    B.W("asset-map.json", json.dumps(am, ensure_ascii=False, indent=2) + "\n")

    # ---------------- brand-tokens.json ----------------
    bt = load(os.path.join(repo_brand, "brand-tokens.json")); dt = load(os.path.join(B.REPO, "design", "tokens.json"))
    dcol = dt.get("colors", dt)
    for k in list(bt["colors"].keys()):
        if k in dcol and isinstance(dcol[k], str): bt["colors"][k] = dcol[k]
    bt["colors"].update({"icon_mark": B.GREEN, "icon_plate": B.PLATE, "icon_plate_border": B.BORDER,
                         "tray_dark_active": B.TRAY["dark-active"], "tray_dark_neutral": B.TRAY["dark-neutral"],
                         "tray_light_active": B.TRAY["light-active"], "tray_light_neutral": B.TRAY["light-neutral"],
                         "tray_preview_dark_taskbar": B.TASK_DARK, "tray_preview_light_taskbar": B.TASK_LIGHT,
                         "icon_secondary": B.CYAN, "icon_mark_light": B.GREEN_L, "icon_secondary_light": B.CYAN_L,
                         "cyan": dcol.get("cyan", B.CYAN), "orange": dcol.get("orange", "#FF8A1F")})
    bt["brand"] = "Ecu Fps"; bt["brand_concept"] = "04 / BOOST LINE"; bt["brand_version"] = "2.0.0"
    bt["logo"].update({"wordmark": "custom_outlined_paths", "font_required": False, "svg_master": "assets/logo/svg/ecu-fps-lockup-dark.svg",
                       "italic_skew_deg": B.SKEW_DEG, "mark_stroke_units_of_256": B.MASTER["S"], "speed_line_ratio_of_stroke": 0.5,
                       "knockout_gap_ratio_of_stroke": round(B.MASTER["G"] / B.MASTER["S"], 4), "chamfer_ratio_of_stroke": round(B.MASTER["C"] / B.MASTER["S"], 4)})
    bt["contrast"] = C
    B.W("brand-tokens.json", json.dumps(bt, ensure_ascii=False, indent=2) + "\n")

    # ---------------- PROVENANCE.md ----------------
    prov = f"""# Ecu Fps — marka varlıkları: köken ve lisans

Tarih: {DATE}

## Köken
- Sahip, beş taslak arasından **4 numaralı konsepti ("Boost Line")** seçti: eğik E+F ligatürü, E'nin orta çubuğu F'nin içinden geçen hız çizgisine dönüşür ve ok ucuyla biter.
- Bu klasördeki her şey o konseptin üretim sürümüdür. Tüm geometri **elle yazılmış vektördür**: koordinatlar `_build.py` içinde sayısal olarak tanımlıdır.
- **Yapay zekâ görsel üreticisi kullanılmadı.** Fotoğraf, stok görsel, iz sürme (trace) veya dış kaynaklı çizim yok.
- **Üçüncü taraf logo taklidi yok.** Yeşil renk uygulamanın kendi "Performans Açık" rengidir (`#3DFF7A`); biçim (italik EF ligatürü, yarım kalınlıkta hız çizgisi, camgöbeği ok ucu) özgündür.
- Rasterler (ICO/PNG) aynı vektör tanımından, betikteki 8x/4x süper örneklemeli rasterleyiciyle üretildi (Pillow + numpy). Ağ erişimi kullanılmadı.

## Geometri sistemi
- Tuval 256 birim; koordinatlar 1/16 birime oturtulur. İtalik eğim {B.SKEW_DEG:g}°.
- Harf kalınlığı 36; hız çizgisi 18 (0,5x); F gövdesindeki boşluklar 12 (kalınlık/3); köşe pahı 12 (kalınlık/3, 45°); ok ucu 45° kenarlı.
- ≤32 px uygulama simgesi ve tüm tepsi simgeleri için boyuta özel, piksel ızgarasına oturtulmuş çizim (16 ve 20 px tamamen elle; italik, hız çizgisinin arkasına gizlenen 1 px'lik basamaktır).

## Yazı tipleri
- **Wordmark "Ecu Fps":** yazı tipi kullanılmadı. Harfler (E, c, u, F, p, s) işaretin kalınlık/pah sisteminden üretilmiş özgün geometrik çizimlerdir; SVG'lerde yalnız `<path>` vardır.
- **Slogan "{B.TAGLINE}"** (og-image, site-header): **JetBrains Mono Medium 2.304**, SIL Open Font License 1.1. Depodaki `third_party/fonts/jetbrains-mono/` alt kümesinden (OFL.txt ve SOURCE.txt orada) fontTools ile dış hatlara çevrildi; SVG'ye yazı tipi gömülmedi, çalışma zamanında yazı tipi gerekmez. OFL, yazı tipiyle üretilen grafiklerde kısıtlama getirmez; Reserved Font Name beyanı yok.
- Sistem ya da sahipli yazı tipine (Segoe UI vb.) bağımlılık yok.

## Renkler (kaynak: `design/tokens.json`)
| Rol | Değer |
|---|---|
| İşaret (koyu zemin) | `{B.GREEN}` |
| İşaret (açık zemin) | `{B.GREEN_L}` |
| İkincil vurgu: ok ucu (koyu) | `{B.CYAN}` |
| İkincil vurgu: ok ucu (açık) | `{B.CYAN_L}` (camgöbeğinin açık zemin için koyulaştırılmış türevi; yeni değer) |
| Simge plakası / kenarlık | `{B.PLATE}` / `{B.BORDER}` |
| Uygulama zemini / yüzeyler | `{B.BG}` / `{B.SURF}` / `{B.SURF2}` |
| Metin | `{B.TEXT}` / `{B.TEXT2}` |
| Tepsi | koyu açık `{B.TRAY['dark-active']}`, koyu kapalı `{B.TRAY['dark-neutral']}`, açık açık `{B.TRAY['light-active']}`, açık kapalı `{B.TRAY['light-neutral']}` |

Büyük karoda işaret, `#8CFFB1 → {B.GREEN} → #22E391` dikey geçişiyle boyanır (alt uç hafif camgöbeğine kayar); düz sürümde tek renk `{B.GREEN}` kullanılır.

## Kontrast (WCAG 2.x oranı)
""" + "\n".join(f"- `{k}`: {v}:1" for k, v in C.items()) + """

## Yeniden üretim
`python _build.py <depo_kökü>` (Pillow, numpy, fontTools). Çıktı deterministiktir.

## Doğrulanmamış olanlar
- Gerçek Windows görev çubuğu/Gezgin üzerinde (farklı DPI ve temalarda) yerinde doğrulama yapılmadı; önizlemeler betik rasterleri ve başsız tarayıcı görüntüleridir.
- Marka tescili/benzerlik araştırması yapılmadı.
"""
    B.W("PROVENANCE.md", prov)

    # ---------------- preview/index.html ----------------
    def c4(name):
        try:
            with open(os.path.join(B.HERE, "..", "4", name), encoding='utf-8') as f: return f.read()
        except OSError: return ""
    e = html.escape
    sw = [("background", B.BG), ("surface", B.SURF), ("surface_raised", B.SURF2), ("icon_plate", B.PLATE), ("text_primary", B.TEXT), ("text_secondary", B.TEXT2),
          ("accent / işaret", B.GREEN), ("işaret (açık zemin)", B.GREEN_L), ("cyan / ok ucu", B.CYAN), ("ok ucu (açık zemin)", B.CYAN_L),
          ("violet (kullanılmadı)", "#A64DFF"), ("orange (kullanılmadı)", "#FF8A1F")]
    swh = "".join(f'<div class="sw"><i style="background:{c}"></i><b>{e(nm)}</b><code>{c}</code></div>' for nm, c in sw)
    def icons(cls):
        big = "".join(f'<figure><img src="img/app-{s}.png" width="{s}" height="{s}" alt=""><figcaption>{s}</figcaption></figure>' for s in (256, 96, 48, 32, 24, 20, 16))
        zoom = "".join(f'<figure><img class="px" src="img/app-{s}.png" width="{s*8}" height="{s*8}" alt=""><figcaption>{s} px · 8x</figcaption></figure>' for s in (16, 20, 24, 32))
        return f'<div class="row {cls}">{big}</div><div class="row {cls}">{zoom}</div>'
    def tray(bg, fg, act, neu, label):
        def im(v, s, z): return f'<img class="px" src="img/tray-{v}-{s}.png" width="{s*z}" height="{s*z}" alt="">'
        cells = ""
        for v, nm in ((act, "Açık"), (neu, "Kapalı")):
            if not v: continue
            cells += (f'<div class="tcell"><span>{nm} — gerçek boyut 16/20/24/32</span><div class="trow">{im(v,16,1)}{im(v,20,1)}{im(v,24,1)}{im(v,32,1)}</div>'
                      f'<span>4x</span><div class="trow">{im(v,16,4)}{im(v,20,4)}{im(v,24,4)}{im(v,32,4)}</div></div>')
        return f'<div class="tray" style="background:{bg};color:{fg}"><h4>{label}</h4><div class="tgrid">{cells}</div></div>'
    rows = "".join(f"<tr><td><code>{e(k)}</code></td><td>{v}:1</td><td>{'≥ 3:1' if v >= 3 else '&lt; 3:1'}</td></tr>" for k, v in C.items())
    allf = sorted(set(files + ["asset-map.json", "brand-tokens.json", "PROVENANCE.md"]))
    flist = "".join(f'<li><a href="../{f}">{f}</a></li>' for f in allf if not f.startswith("preview/"))
    page = f"""<!doctype html>
<html lang="tr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ecu Fps — marka kimliği (son)</title>
<link rel="icon" href="../web/favicon.svg" type="image/svg+xml">
<style>
:root{{--bg:{B.BG};--s1:{B.SURF};--s2:{B.SURF2};--tx:{B.TEXT};--t2:{B.TEXT2};--g:{B.GREEN};--c:{B.CYAN};--ln:#1E2836}}
*{{box-sizing:border-box}} body{{margin:0;background:var(--bg);color:var(--tx);font:14px/1.5 "Segoe UI Variable","Segoe UI",system-ui,sans-serif}}
main{{max-width:1280px;margin:0 auto;padding:32px 28px 80px}} h1{{font-size:26px;margin:0 0 4px}} h2{{font-size:18px;margin:44px 0 12px;padding-top:20px;border-top:1px solid var(--ln)}}
h3{{font-size:14px;color:var(--t2);font-weight:600;margin:18px 0 8px}} h4{{margin:0 0 10px;font-size:13px;font-weight:600}} p{{color:var(--t2);max-width:86ch;margin:6px 0}}
code{{font:12px ui-monospace,Consolas,monospace;color:var(--t2)}} a{{color:var(--c)}}
.row{{display:flex;gap:22px;align-items:flex-end;flex-wrap:wrap;padding:20px;border-radius:10px;margin:8px 0;border:1px solid var(--ln)}}
.dark{{background:{B.TASK_DARK}}} .app{{background:var(--bg)}} .surf{{background:var(--s1)}} .light{{background:{B.TASK_LIGHT};color:#111}} .white{{background:#fff;color:#111}}
figure{{margin:0;text-align:center}} figcaption{{font-size:11px;color:#8A99AD;margin-top:6px}} .light figcaption,.white figcaption{{color:#465364}}
.px{{image-rendering:pixelated}} img{{display:block}}
.sws{{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}} .sw{{background:var(--s1);border:1px solid var(--ln);border-radius:10px;padding:10px;display:grid;gap:4px}}
.sw i{{height:44px;border-radius:6px;border:1px solid #ffffff22}} .sw b{{font-size:12px;font-weight:600}}
.ba{{display:grid;grid-template-columns:1fr 1fr;gap:14px}} .ba>div{{border:1px solid var(--ln);border-radius:10px;padding:18px;background:var(--s1)}} .ba .r{{display:flex;gap:24px;align-items:center;flex-wrap:wrap}}
.ba svg,.ba img{{flex:none}}
.tray{{padding:16px 18px;border-radius:10px;margin:8px 0;border:1px solid var(--ln)}} .tgrid{{display:flex;gap:48px;flex-wrap:wrap}} .tcell span{{font-size:11px;opacity:.75;display:block;margin:6px 0 4px}}
.trow{{display:flex;gap:14px;align-items:flex-end}}
.bar{{display:flex;justify-content:flex-end;align-items:center;gap:14px;height:48px;padding:0 16px;border-radius:8px;margin:8px 0;font-size:12px}}
table{{border-collapse:collapse}} td,th{{border-bottom:1px solid var(--ln);padding:6px 14px 6px 0;text-align:left;font-size:13px}}
.full{{max-width:100%;height:auto;border-radius:8px;border:1px solid var(--ln)}} ul.files{{columns:3;font-size:12px;padding-left:18px}} @media(max-width:800px){{.ba{{grid-template-columns:1fr}}ul.files{{columns:1}}}}
</style></head><body><main>
<h1>Ecu Fps — marka kimliği (üretim sürümü)</h1>
<p>Konsept 4 “Boost Line”ın uygulamanın kendi renkleriyle yeniden çizilmiş hâli. Tarih {DATE}. Tüm görseller bu klasördeki dosyalardan yüklenir; ağ gerekmez.</p>

<h2>Uygulamanın renkleri (bağlam)</h2>
<div class="sws">{swh}</div>
<p>İşaret rengi uygulamadaki “Performans Açık” yeşilidir. Tek ikincil vurgu camgöbeğidir ve yalnız ok ucunda (ve alt şeritlerin ucunda) kullanılır. Kehribar tamamen kaldırıldı.</p>

<h2>Önce / sonra</h2>
<div class="ba">
<div><h4>Konsept 4 (taslak, kehribar)</h4><div class="r"><div style="width:160px">{c4('app-icon.svg').replace('width="256" height="256"','width="160" height="160"')}</div><div style="width:260px">{c4('lockup-dark.svg').replace('<svg ','<svg style="width:100%;height:auto" ',1)}</div></div></div>
<div><h4>Son (yeşil + camgöbeği ok ucu)</h4><div class="r"><img src="../app/svg/ecu-fps-app-tile.svg" width="160" height="160" alt="Ecu Fps simgesi"><img src="../logo/svg/ecu-fps-lockup-dark.svg" width="300" alt="Ecu Fps"></div></div>
</div>
<p>Değişenler: tek köşe pahı sistemi; hız çizgisi harf kalınlığının yarısı ve E'nin orta çubuğundan doğuyor; F gövdesinde kalınlık/3 boşluk; 45° ok ucu; sistem yazı tipi yerine özgün çizilmiş wordmark; plaka geçişi, iç parıltı ve ince üst ışık.</p>

<h2>Uygulama simgesi</h2>
<h3>Koyu görev çubuğu ({B.TASK_DARK})</h3>{icons('dark')}
<h3>Açık görev çubuğu ({B.TASK_LIGHT})</h3>{icons('light')}
<h3>SVG karo (36 px ve üstü: premium; 32 px ve altı / yüksek karşıtlık: düz)</h3>
<div class="row app"><figure><img src="../app/svg/ecu-fps-app-tile.svg" width="256" height="256" alt=""><figcaption>app-tile.svg 256</figcaption></figure>
<figure><img src="../app/svg/ecu-fps-app-tile.svg" width="96" height="96" alt=""><figcaption>96</figcaption></figure>
<figure><img src="../app/svg/ecu-fps-app-tile.svg" width="32" height="32" alt=""><figcaption>32 (düz)</figcaption></figure>
<figure><img src="../app/svg/ecu-fps-app-tile-flat.svg" width="256" height="256" alt=""><figcaption>app-tile-flat.svg</figcaption></figure>
<figure><img src="../app/png/ecu-fps-256.png" width="256" height="256" alt=""><figcaption>ecu-fps-256.png</figcaption></figure></div>

<h2>Tepsi simgeleri</h2>
<p>Açık = performans açık (yeşil, ok uçlu). Kapalı = gri, ok ucu yok. Yüksek karşıtlıkta tam işaret tek renk.</p>
{tray(B.TASK_DARK, '#ddd', 'dark-active', 'dark-neutral', 'Koyu görev çubuğu')}
{tray(B.TASK_LIGHT, '#222', 'light-active', 'light-neutral', 'Açık görev çubuğu')}
{tray('#000000', '#fff', 'hc-white', None, 'Yüksek karşıtlık — siyah (tray-hc-white.ico)')}
{tray('#FFFFFF', '#000', 'hc-black', None, 'Yüksek karşıtlık — beyaz (tray-hc-black.ico)')}
<div class="bar" style="background:{B.TASK_DARK};color:#ddd"><img src="img/tray-dark-neutral-16.png" width="16" height="16" alt="kapalı"><img src="img/tray-dark-active-16.png" width="16" height="16" alt="açık"><img src="img/app-24.png" width="24" height="24" alt=""><span>18:42</span></div>
<div class="bar" style="background:{B.TASK_LIGHT};color:#222"><img src="img/tray-light-neutral-16.png" width="16" height="16" alt="kapalı"><img src="img/tray-light-active-16.png" width="16" height="16" alt="açık"><img src="img/app-24.png" width="24" height="24" alt=""><span>18:42</span></div>

<h2>Logo kilitleri</h2>
<div class="row app"><img src="../logo/svg/ecu-fps-lockup-dark.svg" height="96" alt="Ecu Fps"><img src="../logo/svg/ecu-fps-stacked-dark.svg" height="170" alt="Ecu Fps"><img src="../logo/svg/ef-mark.svg" height="140" alt="EF"><img src="../logo/svg/ecu-fps-lockup-white.svg" height="60" alt="Ecu Fps"><img src="../logo/svg/ecu-fps-lockup-dark.svg" height="28" alt="Ecu Fps"></div>
<div class="row white"><img src="../logo/svg/ecu-fps-lockup-light.svg" height="96" alt="Ecu Fps"><img src="../logo/svg/ecu-fps-stacked-light.svg" height="170" alt="Ecu Fps"><img src="../logo/svg/ef-mark-light.svg" height="140" alt="EF"><img src="../logo/svg/ecu-fps-lockup-ink.svg" height="60" alt="Ecu Fps"><img src="../logo/svg/ecu-fps-lockup-light.svg" height="28" alt="Ecu Fps">
<span style="color:#7B2FD0;display:inline-flex"><img src="../logo/svg/ef-mark-currentcolor.svg" height="60" alt="EF"></span></div>

<h2>Web</h2>
<div class="row surf"><figure><img src="../web/favicon.svg" width="16" height="16" alt=""><figcaption>svg 16</figcaption></figure><figure><img src="../web/favicon.svg" width="32" height="32" alt=""><figcaption>svg 32</figcaption></figure><figure><img src="../web/favicon.svg" width="48" height="48" alt=""><figcaption>svg 48</figcaption></figure>
<figure><img src="../web/favicon.ico" width="16" height="16" alt=""><figcaption>ico 16</figcaption></figure><figure><img src="../web/favicon.ico" width="32" height="32" alt=""><figcaption>ico 32</figcaption></figure>
<figure><img src="../web/apple-touch-icon.png" width="180" height="180" style="border-radius:40px" alt=""><figcaption>apple-touch-icon 180 (iOS maskesiyle)</figcaption></figure>
<figure><img src="../web/logo-512.png" width="200" height="200" alt=""><figcaption>logo-512.png</figcaption></figure></div>
<h3>og-image.png (1200×630)</h3><img class="full" src="../web/og-image.png" width="1200" height="630" alt="Ecu Fps — {e(B.TAGLINE)}">
<h3>site-header (koyu / açık; site-header.svg tarayıcı temasına uyar)</h3>
<img class="full" src="../web/site-header-dark.svg" width="1200" height="200" alt=""><img class="full" src="../web/site-header-light.svg" width="1200" height="200" alt="" style="margin-top:8px">

<h2>Kurulum</h2>
<div class="row surf"><figure><img src="../setup/setup-banner.png" width="164" height="314" alt=""><figcaption>setup-banner.png 164×314</figcaption></figure>
<figure><img src="../setup/setup-banner.svg" width="164" height="314" alt=""><figcaption>setup-banner.svg</figcaption></figure>
<figure><img src="../setup/setup-header.png" width="300" height="114" alt=""><figcaption>setup-header.png 300×114 (2x)</figcaption></figure>
<figure><img src="../setup/setup-header.png" width="150" height="57" alt=""><figcaption>150×57 olarak</figcaption></figure></div>

<h2>Kontrast</h2>
<table><tr><th>Çift</th><th>Oran</th><th>Hedef 3:1</th></tr>{rows}</table>

<h2>Dosyalar</h2>
<ul class="files">{flist}</ul>
</main></body></html>
"""
    B.W("preview/index.html", page)
    print("meta written")
