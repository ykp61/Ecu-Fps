#!/usr/bin/env python3
"""Ecu Fps uygulama ve tepsi ICO denetleyicisi (marka 2.0.0 "Boost Line", italik EF amblemi).

Kullanim (depo kokunden):
  python -I scripts/build_brand_icons.py --check    # diskteki ICO'lari denetler (Pillow gerekmez)
  python -I scripts/build_brand_icons.py            # ICO/PNG/SVG'leri yeniden uretir (paket ureticisini cagirir)

Uretim: assets/branding/ecu-fps/_build.py (onayli marka paketinin deterministik ureticisi; Pillow, numpy,
fontTools gerekir; ag erisimi yok). Uretici ciktilarini kendi klasorune yazar (app/, logo/svg/, tray/, web/,
setup/, preview/). preview/ depoya alinmaz. Renkler brand-tokens.json'dan okunur; bu dosyada sabit renk yoktur.

Tepsi durumu renk VE bicimle ayrilir: acik = yesil, ok uclu amblem; kapali/dogrulaniyor = gri, hiz cizgisi kut
biter (ok ucu yok). Amblem her durumda doludur. Yuksek kontrast ICO'lari tam amblemdir (tek renk).
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRAND = ROOT / "assets" / "branding" / "ecu-fps"

APP_SIZES = [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 128, 256]
TRAY_SIZES = [16, 20, 24, 32, 40, 48, 64]

# Tepsi varyanti -> token anahtari
TRAY_VARIANTS = {
    "dark-active": "tray_dark_active",
    "dark-neutral": "tray_dark_neutral",
    "light-active": "tray_light_active",
    "light-neutral": "tray_light_neutral",
}


def tokens() -> dict:
    return json.loads((BRAND / "brand-tokens.json").read_text(encoding="utf-8"))


def rgb(hex_color: str) -> tuple[int, int, int]:
    h = hex_color.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def contrast(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
    """WCAG 2.x karsitlik orani (1..21)."""
    def lum(c):
        lin = [v / 255.0 for v in c]
        lin = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in lin]
        return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
    hi, lo = sorted((lum(a), lum(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def ico_read(path: Path):
    """[(boyut, 'DIB'|'PNG', pikseller)]; pikseller yalniz DIB icin [(r,g,b,a)] satir-ana, ustten alta."""
    d = path.read_bytes()
    reserved, kind, count = struct.unpack("<HHH", d[:6])
    if reserved != 0 or kind != 1:
        raise ValueError(f"{path.name}: ICO degil")
    out = []
    for i in range(count):
        w, _h, _cc, _r, _pl, bpp, size, off = struct.unpack("<BBBBHHII", d[6 + 16 * i:22 + 16 * i])
        w = w or 256
        blob = d[off:off + size]
        if blob[:4] == b"\x89PNG":
            out.append((w, "PNG", None))
            continue
        hs, bw, bh, _planes, bits = struct.unpack("<IiiHH", blob[:16])
        if bits != 32 or bpp != 32 or bw != w or bh != 2 * w:
            raise ValueError(f"{path.name}: {w} px girdisi 32 bpp DIB degil")
        px = []
        for y in range(w - 1, -1, -1):
            row = blob[hs + y * w * 4:hs + (y + 1) * w * 4]
            px.extend((row[x * 4 + 2], row[x * 4 + 1], row[x * 4], row[x * 4 + 3]) for x in range(w))
        out.append((w, "DIB", px))
    return out


def enclosed(px, w) -> int:
    """Disaridan ulasilamayan tam seffaf piksel sayisi (amblem icinde kapali bosluk); dolu amblemde 0."""
    seen = [False] * (w * w)
    stack = [i for i in range(w * w) if (i % w in (0, w - 1) or i // w in (0, w - 1)) and px[i][3] < 128]
    for i in stack:
        seen[i] = True
    while stack:
        i = stack.pop()
        x, y = i % w, i // w
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            j = ny * w + nx
            if 0 <= nx < w and 0 <= ny < w and not seen[j] and px[j][3] < 128:
                seen[j] = True
                stack.append(j)
    return sum(1 for i in range(w * w) if not seen[i] and px[i][3] < 64)


def coverage(px) -> float:
    """Alfa agirlikli kaplama (piksel cinsinden)."""
    return sum(p[3] for p in px) / 255.0


def check() -> int:
    """Diskteki ICO'lar marka 2.0.0 paketiyle uyumlu mu? Boyutlar, bicim, renkler, acik/notr bicim ayrimi."""
    tk = tokens()
    c = tk["colors"]
    amap = json.loads((BRAND / "asset-map.json").read_text(encoding="utf-8"))
    errors: list[str] = []

    def need(ok: bool, msg: str) -> None:
        if not ok:
            errors.append(msg)

    need(tk.get("brand_version") == amap.get("version") == "2.0.0", "brand-tokens / asset-map surumu 2.0.0 degil")
    cov: dict[tuple[str, int], float] = {}
    for name, key in TRAY_VARIANTS.items():
        frames = ico_read(BRAND / "tray" / f"tray-{name}.ico")
        need([f[0] for f in frames] == TRAY_SIZES and amap["tray"]["sizes"] == TRAY_SIZES, f"tray-{name}: boyutlar {[f[0] for f in frames]}")
        need(amap["tray"]["variants"][name]["color"].upper() == c[key].upper(), f"asset-map {name} rengi token ile ayni degil")
        want = rgb(c[key])
        # Simge kendi gorev cubugu zemininde okunmali (grafik nesne esigi 3:1).
        ground = rgb(c["tray_preview_dark_taskbar" if name.startswith("dark") else "tray_preview_light_taskbar"])
        ratio = contrast(want, ground)
        need(ratio >= 3.0, f"tray-{name}: {c[key]} gorev cubugu zemininde {ratio:.2f}:1 (en az 3:1)")
        for w, kind, px in frames:
            need(kind == "DIB", f"tray-{name} {w}px DIB degil")
            if px is None:
                continue
            need(all(p[:3] == want for p in px if p[3] > 0), f"tray-{name} {w}px: renk {c[key]} degil")
            need(px[0][3] == 0 and px[w * w - 1][3] == 0, f"tray-{name} {w}px: plaka olmamali")
            need(enclosed(px, w) == 0, f"tray-{name} {w}px: amblem dolu olmali (kapali bosluk var)")
            cov[name, w] = coverage(px)
    # Durum renkle ayrilir: acik durum doygun yesil, notr durum gri (kanal farki ile olculur).
    for theme in ("dark", "light"):
        act, neu = rgb(c[f"tray_{theme}_active"]), rgb(c[f"tray_{theme}_neutral"])
        need(max(act) - min(act) >= 120 and act[1] == max(act), f"tray {theme}: acik durum doygun yesil degil")
        need(max(neu) - min(neu) <= 32, f"tray {theme}: notr durum gri degil")
        # ... ve bicimle: acik durumda ok ucu var, notrde yok (her boyutta kaplama farki en az 2 piksel).
        for w in TRAY_SIZES:
            a, n = cov.get((f"{theme}-active", w), 0), cov.get((f"{theme}-neutral", w), 0)
            need(a - n >= 2, f"tray {theme} {w}px: acik/notr bicim farki yok (ok ucu {a:.1f} vs {n:.1f})")

    app = ico_read(BRAND / "app" / "ecu-fps.ico")
    need([f[0] for f in app] == APP_SIZES and amap["app"]["sizes"] == APP_SIZES, "app boyutlari")
    need(app[-1][1] == "PNG", "app 256 px PNG degil")
    mark, plate, cyan = rgb(c["icon_mark"]), rgb(c["icon_plate"]), rgb(c["icon_secondary"])
    flat_max = amap["app"].get("flat_through_px", 32)
    for w, kind, px in app[:-1]:
        need(kind == "DIB", f"app {w}px DIB degil")
        rgbs = {p[:3] for p in px if p[3] == 255}
        # Her boyutta plaka (2.0.0: kucuk boyutta da plakali; small_optical_unplated_through_px = 0)
        need(sum(1 for p in px if p[3] == 255 and abs(p[0] - plate[0]) + abs(p[1] - plate[1]) + abs(p[2] - plate[2]) <= 24) > w * w // 4,
             f"app {w}px: grafit plaka yok")
        if w <= flat_max:
            # Duz surum: tek renk amblem + camgobegi ok ucu tam renkleriyle bulunur
            need(mark in rgbs, f"app {w}px: amblem rengi {c['icon_mark']} yok")
            need(cyan in rgbs, f"app {w}px: ok ucu rengi {c['icon_secondary']} yok")
        else:
            # Gecisli surum: yesil baskin ve camgobegi baskin pikseller bulunur
            need(any(p[1] > 200 and p[1] > p[0] + 80 and p[1] > p[2] + 40 for p in px if p[3] == 255), f"app {w}px: yesil amblem yok")
            need(any(p[2] > 200 and p[0] < 60 and p[2] >= p[1] for p in px if p[3] == 255), f"app {w}px: camgobegi ok ucu yok")
    # Yuksek kontrast ICO'lari: tek renk (token), dolu tam amblem; durum ipucu metninde.
    for name, key in (("hc-white", "high_contrast_white"), ("hc-black", "high_contrast_black")):
        frames = ico_read(BRAND / "tray" / f"tray-{name}.ico")
        need([f[0] for f in frames] == TRAY_SIZES, f"tray-{name}: boyutlar")
        for w, kind, px in frames:
            need(kind == "DIB", f"tray-{name} {w}px DIB degil")
            if px is None:
                continue
            need(all(p[:3] == rgb(c[key]) for p in px if p[3] > 0), f"tray-{name} {w}px: renk {c[key]} degil")
            need(enclosed(px, w) == 0, f"tray-{name} {w}px: yuksek kontrast amblemi dolu olmali")
            # Tam amblem: kaplama acik durum ICO'suyla ayni
            need(abs(coverage(px) - cov.get(("dark-active", w), 0)) < 1, f"tray-{name} {w}px: tam amblem degil")
    design = json.loads((ROOT / "design" / "tokens.json").read_text(encoding="utf-8"))["colors"]
    for key in list(TRAY_VARIANTS.values()) + ["accent", "cyan"]:
        need(design[key].upper() == c[key].upper(), f"design/tokens.json {key} marka tokeni ile ayni degil")

    for e in errors:
        print("HATA:", e)
    print(f"brand icons: {'OK' if not errors else 'FAIL'} (app {len(APP_SIZES)} boyut, tray 4x{len(TRAY_SIZES)} boyut, hc x2)")
    return 1 if errors else 0


def build() -> int:
    """Paket ureticisini depo koku ile cagirir (JetBrains Mono alt kumesi third_party/fonts'tan okunur)."""
    return subprocess.call([sys.executable, "-I", str(BRAND / "_build.py"), str(ROOT)])


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="diskteki ICO'lari denetle (yazmaz)")
    a = ap.parse_args()
    if a.check:
        return check()
    return build()


if __name__ == "__main__":
    sys.exit(main())
