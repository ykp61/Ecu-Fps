# -*- coding: utf-8 -*-
"""Ecu Fps brand build (concept 4 'Boost Line', production pass). Hand-authored vector geometry.
Deterministic: python _build.py [repo_root]  -> writes every deliverable next to this file.
Needs Pillow, numpy, fontTools. No network, no image generator."""
import os, io, math, struct, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("ECUFPS_REPO", "")
FONT = os.path.join(REPO, "third_party", "fonts", "jetbrains-mono", "JetBrainsMono-Medium-subset.ttf")

# ---------------- palette (from design/tokens.json) ----------------
GREEN = "#3DFF7A"; GREEN_L = "#0B8F3C"; CYAN = "#00D4FF"; CYAN_L = "#00708F"
PLATE = "#10151E"; BORDER = "#0B8F3C"; BG = "#05070B"; SURF = "#0B0F17"; SURF2 = "#121824"
TEXT = "#F1F5F9"; TEXT2 = "#8A99AD"; INK = "#0B0F17"; INK2 = "#465364"
TRAY = {"dark-active": "#3DFF7A", "dark-neutral": "#7B8490", "light-active": "#0B8F3C",
        "light-neutral": "#808994", "hc-white": "#FFFFFF", "hc-black": "#000000"}
TASK_DARK = "#1B1B1B"; TASK_LIGHT = "#EEF1F4"
SKEW_DEG = 10.0; T = math.tan(math.radians(SKEW_DEG))
TAGLINE = "Oyun performansı, dürüstçe."

def rgb(h): h = h.lstrip('#'); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
def lum(h):
    f = lambda c: c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = [f(v / 255) for v in rgb(h)]; return 0.2126 * r + 0.7152 * g + 0.0722 * b
def contrast(a, b):
    la, lb = lum(a), lum(b); return round((max(la, lb) + 0.05) / (min(la, lb) + 0.05), 2)
def q(v): return round(v * 16) / 16
def n(v):
    s = ("%.4f" % q(v)).rstrip('0').rstrip('.'); return '0' if s in ('-0', '') else s
def W(rel, data):
    p = os.path.join(HERE, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
    if isinstance(data, str):
        with open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(data)
    else:
        with open(p, 'wb') as f: f.write(data)

# ---------------- geometry helpers ----------------
def area(c): return sum(c[i][0] * c[(i + 1) % len(c)][1] - c[(i + 1) % len(c)][0] * c[i][1] for i in range(len(c))) / 2
def orient(c, positive=True): return c if (area(c) > 0) == positive else c[::-1]
def tf(cs, s=1.0, tx=0.0, ty=0.0): return [[(x * s + tx, y * s + ty) for x, y in c] for c in cs]
def skew(cs, y0, t=T): return [[(x + (y0 - y) * t, y) for x, y in c] for c in cs]
def bbox(cs):
    xs = [x for c in cs for x, _ in c]; ys = [y for c in cs for _, y in c]; return min(xs), min(ys), max(xs), max(ys)
def rect(x0, y0, x1, y1): return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
def stroke(pts, w):
    """Outline of a constant-width stroke along a polyline (miter joins, butt caps)."""
    h = w / 2; L = []; R = []
    def nrm(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]; d = math.hypot(dx, dy); return (dy / d, -dx / d)
    for i, p in enumerate(pts):
        if i == 0: m = nrm(pts[0], pts[1]); k = 1
        elif i == len(pts) - 1: m = nrm(pts[-2], pts[-1]); k = 1
        else:
            a = nrm(pts[i - 1], p); b = nrm(p, pts[i + 1]); k = 1 / (1 + a[0] * b[0] + a[1] * b[1]); m = (a[0] + b[0], a[1] + b[1])
        L.append((p[0] + m[0] * k * h, p[1] + m[1] * k * h)); R.append((p[0] - m[0] * k * h, p[1] - m[1] * k * h))
    return L + R[::-1]

# ---------------- the mark ----------------
# S letter stroke, Lw speed line (= S/2), G knockout gap (= S/3), C chamfer (= S/3), ctr counter height
MASTER = dict(S=36, Lw=18, G=12, C=12, yT=50, ctr=33, aE=76, gap=20, fW=80, hh=30, hl=30)
def mark(p, head=True, t=T):
    """{'body': contours (E, F, speed line), 'head': contours}; x origin = E stem left."""
    S, Lw, G, C, yT, ctr, aE, gap, fW, hh, hl = (p[k] for k in ('S', 'Lw', 'G', 'C', 'yT', 'ctr', 'aE', 'gap', 'fW', 'hh', 'hl'))
    yB = yT + 2 * S + 2 * ctr + Lw; mid = yT + S + ctr + Lw / 2; l0 = mid - Lw / 2; l1 = mid + Lw / 2
    xF = aE + gap; xFe = xF + fW; xA = xFe + p.get('ax', 0)
    E = [(C, yT), (aE, yT), (aE, yT + S), (S, yT + S), (S, yB - S), (aE, yB - S), (aE, yB), (C, yB), (0, yB - C), (0, yT + C)]
    if C == 0: E = E[:7] + [E[8]]
    line = rect(S / 2, l0, xA + (hl * 0.3 if head else p.get('blunt', hl * 0.5)), l1)
    if G > 0:
        Fu = [(xF + C, yT), (xFe, yT), (xFe, yT + S), (xF + S, yT + S), (xF + S, l0 - G), (xF, l0 - G), (xF, yT + C)]
        F = [Fu[:6] if C == 0 else Fu, rect(xF, l1 + G, xF + S, yB)]
    else:
        Fu = [(xF + C, yT), (xFe, yT), (xFe, yT + S), (xF + S, yT + S), (xF + S, yB), (xF, yB), (xF, yT + C)]
        F = [Fu[:6] if C == 0 else Fu]
    body = skew([E, line] + F, mid, t)
    hd = skew([[(xA, mid - hh), (xA + hl, mid), (xA, mid + hh)]], mid, t) if head else []
    return dict(body=body, head=hd)
def mark_in(box_w, p, head=True):
    m = mark(p, head); full = mark(p, True); x0, _, x1, _ = bbox(full['body'] + full['head'])
    dx = (box_w - (x1 - x0)) / 2 - x0
    return dict(body=tf(m['body'], 1, dx, 0), head=tf(m['head'], 1, dx, 0))
def mark_fit(x, y, w=None, h=None, head=True):
    """Master mark scaled to bbox width w (or height h); bbox top-left at x,y."""
    m = mark(MASTER, head); full = mark(MASTER, True); x0, y0, x1, y1 = bbox(full['body'] + full['head'])
    s = (w / (x1 - x0)) if w else (h / (y1 - y0))
    return dict(body=tf(m['body'], s, x - x0 * s, y - y0 * s), head=tf(m['head'], s, x - x0 * s, y - y0 * s),
                w=(x1 - x0) * s, h=(y1 - y0) * s, s=s)
MW, MH = (lambda b: (b[2] - b[0], b[3] - b[1]))(bbox(mark(MASTER)['body'] + mark(MASTER)['head']))

# optical small-size parameter sets (pixel units; every horizontal edge on a whole pixel)
OPT = {  # unplated (tray / premium tile mark); key = box size
    20: dict(S=3, Lw=2, G=1, C=1, yT=3, ctr=3, aE=6, gap=2, fW=6, hh=3, hl=3),
    24: dict(S=4, Lw=2, G=1, C=1, yT=3, ctr=4, aE=7, gap=2, fW=8, hh=3.5, hl=3.5),
    32: dict(S=5, Lw=2, G=2, C=1, yT=4, ctr=6, aE=10, gap=2, fW=11, hh=4.5, hl=4.5),
    40: dict(S=6, Lw=3, G=2, C=2, yT=5, ctr=7, aE=12, gap=3, fW=13, hh=5, hl=5),
    48: dict(S=7, Lw=3, G=2, C=2, yT=7, ctr=8, aE=14, gap=3, fW=15, hh=6, hl=6),
    64: dict(S=10, Lw=5, G=3, C=3, yT=9, ctr=10, aE=21, gap=5, fW=21, hh=8, hl=8),
}
OPT_PLATED = {  # flat app tile; key = tile size
    20: dict(S=2, Lw=1, G=0, C=0, yT=4, ctr=3, aE=5, gap=1, fW=5, hh=2.5, hl=2.5),
    24: dict(S=3, Lw=2, G=1, C=1, yT=5, ctr=3, aE=6, gap=2, fW=6, hh=3, hl=3),
    30: dict(S=3, Lw=2, G=1, C=1, yT=7, ctr=4, aE=7, gap=2, fW=8, hh=3.5, hl=3.5),
    32: dict(S=4, Lw=2, G=1, C=1, yT=7, ctr=4, aE=8, gap=2, fW=9, hh=4, hl=4),
}
def auto_params(N, frac=0.70):
    """Master proportions scaled to a mark about frac*N wide, every dimension rounded to whole pixels."""
    k = frac * N / MW; S = max(2, round(36 * k)); Lw = max(1, round(S / 2)); G = max(1, round(S / 3))
    ctr = max(G + 1, round(33 * k)); hh = round(30 * k * 2) / 2
    p = dict(S=S, Lw=Lw, G=G, C=G, ctr=ctr, aE=S + round(40 * k), gap=max(2, round(20 * k)), fW=S + round(44 * k), hh=hh, hl=hh)
    p['yT'] = (N - (2 * S + 2 * ctr + Lw)) // 2
    return p
def hand16(plated, head=True):
    """16 px art, fully on the pixel grid: the italic is a one-pixel step hidden behind the speed line."""
    if plated:  # 12 px wide inside the 16 px plate
        body = [rect(3, 3, 5, 8), rect(3, 3, 7, 5), rect(2, 9, 4, 14), rect(2, 12, 6, 14),
                rect(8, 3, 10, 8), rect(8, 3, 12, 5), rect(7, 9, 9, 14), rect(2, 8, 12, 9)]
        hd = [rect(12, 7, 13, 10), rect(13, 8, 14, 9)]
    else:       # 14 px wide, unplated (tray)
        body = [rect(2, 2, 4, 7), rect(2, 2, 7, 4), rect(1, 9, 3, 14), rect(1, 12, 6, 14),
                rect(8, 2, 10, 7), rect(8, 2, 13, 4), rect(7, 9, 9, 14), rect(1, 7, 13 if head else 14, 9)]
        hd = [rect(13, 6, 14, 10), rect(14, 7, 15, 9)]
    return dict(body=body, head=hd if head else [])
def hand20(plated, head=True):
    if plated:  # 14 px wide inside the 20 px plate
        body = [rect(4, 4, 6, 9), rect(4, 4, 9, 6), rect(3, 10, 5, 15), rect(3, 13, 8, 15),
                rect(10, 4, 12, 9), rect(10, 4, 15, 6), rect(9, 10, 11, 15), rect(3, 9, 15, 10)]
        hd = [rect(15, 8, 16, 11), rect(16, 9, 17, 10)]
    else:       # 18 px wide, unplated (tray)
        body = [rect(2, 3, 5, 9), rect(2, 3, 8, 6), rect(1, 11, 4, 17), rect(1, 14, 7, 17),
                rect(10, 3, 13, 9), rect(10, 3, 16, 6), rect(9, 11, 12, 17), rect(1, 9, 16 if head else 18, 11)]
        hd = [rect(16, 7, 17, 13), rect(17, 8, 18, 12), rect(18, 9, 19, 11)]
    return dict(body=body, head=hd if head else [])
def opt_mark(N, plated=False, head=True):
    if N == 16: return hand16(plated, head)
    if N == 20: return hand20(plated, head)
    return mark_in(N, (OPT_PLATED if plated else OPT)[N], head)
def offset_mark(m, dx, dy): return dict(body=tf(m['body'], 1, dx, dy), head=tf(m['head'], 1, dx, dy))

# ---------------- wordmark: custom geometric letters built from the mark's stroke/chamfer system ----------------
WW = 15.0; WH = WW / 2; WCH = 6.0; WC = 9.0; XT = 28.0; TRACK = 13.0; WSPACE = 34.0
def _glyph(ch):
    w, h, c, k = WW, WH, WCH, WC
    if ch == 'E': return [[(k, 0), (56, 0), (56, w), (w, w), (w, 50 - h), (47, 50 - h), (47, 50 + h), (w, 50 + h), (w, 100 - w), (56, 100 - w), (56, 100), (k, 100), (0, 100 - k), (0, k)]], 56
    if ch == 'F': return [[(k, 0), (55, 0), (55, w), (w, w), (w, 50 - h), (46, 50 - h), (46, 50 + h), (w, 50 + h), (w, 100), (0, 100), (0, k)]], 55
    if ch == 'c': return [stroke([(49, XT + h), (h + c, XT + h), (h, XT + h + c), (h, 100 - h - c), (h + c, 100 - h), (49, 100 - h)], w)], 49
    if ch == 'u': return [stroke([(h, XT), (h, 100 - h - c), (h + c, 100 - h), (55 - h, 100 - h)], w), rect(55 - w, XT, 55, 100)], 55
    if ch == 'p': return [rect(0, XT, w, 128), stroke([(h, XT + h), (55 - h - c, XT + h), (55 - h, XT + h + c), (55 - h, 100 - h - c), (55 - h - c, 100 - h), (h, 100 - h)], w)], 55
    if ch == 's':
        yt, ym, yb = XT + h, (XT + 100) / 2, 100 - h
        return [stroke([(50, yt), (h + c, yt), (h, yt + c), (h, ym - c), (h + c, ym), (50 - h - c, ym), (50 - h, ym + c), (50 - h, yb - c), (50 - h - c, yb), (0, yb)], w)], 50
    raise KeyError(ch)
def wordmark(cap, x=0.0, y=0.0):
    """(contours 'Ecu', contours 'Fps', right edge x, height incl. descender); y = cap top, x = left edge."""
    out = {'Ecu': [], 'Fps': []}; cx = 0.0
    for word in ('Ecu', 'Fps'):
        for ch in word:
            cs, adv = _glyph(ch); out[word] += tf(cs, 1, cx, 0); cx += adv + TRACK
        cx += WSPACE - TRACK
    x0 = bbox(skew(out['Ecu'] + out['Fps'], 100))[0]; s = cap / 100.0
    r = {k: tf(skew(v, 100), s, x - x0 * s, y) for k, v in out.items()}
    return r['Ecu'], r['Fps'], bbox(r['Ecu'] + r['Fps'])[2], 128 * s
def wm_width(cap): return wordmark(cap)[2]

# ---------------- OFL text outlines (JetBrains Mono Medium) for the tagline ----------------
_font = None
def text_outline(s, size, x, y):
    """Outline `s` at em size (px), baseline y. Returns [(outers, holes)] per glyph and the advance."""
    global _font
    from fontTools.ttLib import TTFont
    from fontTools.pens.basePen import BasePen
    if _font is None: _font = TTFont(FONT)
    gs = _font.getGlyphSet(); cmap = _font.getBestCmap(); k = size / _font['head'].unitsPerEm
    class P(BasePen):
        def __init__(s_, g): BasePen.__init__(s_, g); s_.cs = []; s_.cur = None
        def _moveTo(s_, p): s_.cur = [p]; s_.cs.append(s_.cur)
        def _lineTo(s_, p): s_.cur.append(p)
        def _curveToOne(s_, a, b, c):
            p0 = s_.cur[-1]
            for i in range(1, 9):
                t = i / 8; u = 1 - t
                s_.cur.append((u ** 3 * p0[0] + 3 * u * u * t * a[0] + 3 * u * t * t * b[0] + t ** 3 * c[0], u ** 3 * p0[1] + 3 * u * u * t * a[1] + 3 * u * t * t * b[1] + t ** 3 * c[1]))
        def _qCurveToOne(s_, a, b):
            p0 = s_.cur[-1]
            for i in range(1, 7):
                t = i / 6; u = 1 - t
                s_.cur.append((u * u * p0[0] + 2 * u * t * a[0] + t * t * b[0], u * u * p0[1] + 2 * u * t * a[1] + t * t * b[1]))
    items = []; cx = x
    for ch in s:
        g = cmap[ord(ch)]; pen = P(gs); gs[g].draw(pen)
        cs = [[(cx + px * k, y - py * k) for px, py in c] for c in pen.cs if len(c) > 2]
        if cs:
            sgn = area(max(cs, key=lambda c: abs(area(c)))) > 0
            items.append(([c for c in cs if (area(c) > 0) == sgn], [c for c in cs if (area(c) > 0) != sgn]))
        cx += gs[g].width * k
    return items, cx - x

# ---------------- scene: one description -> SVG and raster ----------------
class Scene:
    def __init__(s, w, h): s.w = w; s.h = h; s.items = []
    def poly(s, outers, paint, holes=()):
        if outers: s.items.append(('poly', [orient(c) for c in outers], [orient(c, False) for c in holes], paint))
        return s
    def rrect(s, x, y, w, h, r, paint): s.items.append(('rrect', (x, y, w, h, r), paint)); return s
    def ring(s, x, y, w, h, r, sw, paint): s.items.append(('ring', (x, y, w, h, r, sw), paint)); return s
    def glyphs(s, items, paint):
        for o, hl in items: s.poly(o, paint, hl)
        return s
    def add_mark(s, m, body_paint, head_paint=None):
        s.poly(m['body'], body_paint)
        if m['head']: s.poly(m['head'], head_paint or body_paint)
        return s
    def svg_body(s, pid='g'):
        defs = []; out = []
        def op(a): return ('%.3f' % a).rstrip('0').rstrip('.')
        def paint_attr(p, kind='fill'):
            if p[0] == 'solid': return '%s="%s"%s' % (kind, p[1], '' if p[2] >= 1 else ' %s-opacity="%s"' % (kind, op(p[2])))
            if p[0] == 'cur': return '%s="currentColor"' % kind
            i = '%s%d' % (pid, len(defs))
            st = ''.join('<stop offset="%s" stop-color="%s"%s/>' % (n(o), c, '' if a >= 1 else ' stop-opacity="%s"' % op(a)) for o, c, a in p[2])
            if p[0] == 'lin': defs.append('<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="%s" y1="%s" x2="%s" y2="%s">%s</linearGradient>' % ((i,) + tuple(n(v) for v in p[1]) + (st,)))
            else: defs.append('<radialGradient id="%s" gradientUnits="userSpaceOnUse" cx="%s" cy="%s" r="%s">%s</radialGradient>' % ((i,) + tuple(n(v) for v in p[1]) + (st,)))
            return '%s="url(#%s)"' % (kind, i)
        for it in s.items:
            if it[0] == 'poly':
                d = ''.join('M' + 'L'.join('%s %s' % (n(x), n(y)) for x, y in c) + 'Z' for c in it[1] + it[2])
                out.append('<path d="%s" %s/>' % (d, paint_attr(it[3])))
            elif it[0] == 'rrect':
                x, y, w, h, r = it[1]
                out.append('<rect x="%s" y="%s" width="%s" height="%s"%s %s/>' % (n(x), n(y), n(w), n(h), ' rx="%s"' % n(r) if r else '', paint_attr(it[2])))
            else:
                x, y, w, h, r, sw = it[1]
                out.append('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="none" stroke-width="%s" %s/>' % (n(x + sw / 2), n(y + sw / 2), n(w - sw), n(h - sw), n(max(r - sw / 2, 0)), n(sw), paint_attr(it[2], 'stroke')))
        return ('<defs>%s</defs>' % ''.join(defs) if defs else ''), ''.join(out)
    def svg(s, title):
        d, b = s.svg_body()
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %s %s" width="%s" height="%s" role="img" aria-label="%s"><title>%s</title>%s%s</svg>\n'
                % (n(s.w), n(s.h), n(s.w), n(s.h), title, title, d, b))
    def render(s, scale=1.0, ss=None, bg=None):
        Wp = int(round(s.w * scale)); Hp = int(round(s.h * scale))
        if ss is None: ss = 8 if max(Wp, Hp) <= 128 else (4 if max(Wp, Hp) <= 640 else 3)
        k = scale * ss
        canvas = np.zeros((Hp, Wp, 4), np.float32)  # premultiplied
        if bg: canvas[:, :, :3] = np.array(rgb(bg), np.float32) / 255; canvas[:, :, 3] = 1
        yy, xx = np.mgrid[0:Hp, 0:Wp].astype(np.float32); ux = (xx + 0.5) / scale; uy = (yy + 0.5) / scale
        def mask_of(it):
            m = Image.new('L', (Wp * ss, Hp * ss), 0); d = ImageDraw.Draw(m)
            if it[0] == 'poly':
                for c in it[1]: d.polygon([(x * k, y * k) for x, y in c], fill=255)
                for c in it[2]: d.polygon([(x * k, y * k) for x, y in c], fill=0)
            else:
                x, y, w, h, r = it[1][:5]
                d.rounded_rectangle([x * k, y * k, (x + w) * k - 1, (y + h) * k - 1], radius=r * k, fill=255)
                if it[0] == 'ring':
                    sw = it[1][5]
                    d.rounded_rectangle([(x + sw) * k, (y + sw) * k, (x + w - sw) * k - 1, (y + h - sw) * k - 1], radius=max(r - sw, 0) * k, fill=0)
            return np.asarray(m.resize((Wp, Hp), Image.BOX), np.float32) / 255
        def paint_of(p):
            if p[0] == 'solid':
                return np.broadcast_to(np.array(rgb(p[1]), np.float32) / 255, (Hp, Wp, 3)), np.full((Hp, Wp), p[2], np.float32)
            if p[0] == 'lin':
                x1, y1, x2, y2 = p[1]; dx, dy = x2 - x1, y2 - y1; t = ((ux - x1) * dx + (uy - y1) * dy) / (dx * dx + dy * dy)
            else:
                cx, cy, r = p[1]; t = np.sqrt((ux - cx) ** 2 + (uy - cy) ** 2) / r
            t = np.clip(t, 0, 1); offs = [o for o, _, _ in p[2]]
            cols = np.array([[v / 255 for v in rgb(c)] + [a] for _, c, a in p[2]], np.float32)
            ch = [np.interp(t, offs, cols[:, i]) for i in range(4)]
            return np.stack(ch[:3], -1).astype(np.float32), ch[3].astype(np.float32)
        for it in s.items:
            m = mask_of(it); col, a = paint_of(it[-1]); a = (a * m)[..., None]
            canvas[:, :, :3] = col * a + canvas[:, :, :3] * (1 - a); canvas[:, :, 3:] = a + canvas[:, :, 3:] * (1 - a)
        al = canvas[:, :, 3:]; rgbv = np.where(al > 0, canvas[:, :, :3] / np.maximum(al, 1e-6), 0)
        return Image.fromarray(np.clip(np.concatenate([rgbv, al], -1) * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA')

def solid(c, a=1.0): return ('solid', c, a)
def lin(x1, y1, x2, y2, *stops): return ('lin', (x1, y1, x2, y2), list(stops))
def rad(cx, cy, r, *stops): return ('rad', (cx, cy, r), list(stops))

# ---------------- app tile ----------------
def tile_premium(N, x=0.0, y=0.0, sc=None, radius=True):
    """Premium tile in an N-unit box: deep plate gradient, soft inner glow, border, hairline highlight, mark."""
    sc = sc or Scene(N, N); u = N / 256.0
    r = 56 * u if radius else 0; bw = max(1.0, round(4 * u)) if N < 200 else 4 * u
    sc.rrect(x, y, N, N, r, lin(0, y, 0, y + N, (0, "#1C2534", 1), (0.5, PLATE, 1), (1, "#080B11", 1)))
    sc.rrect(x, y, N, N, r, rad(x + N * 0.5, y + N * 0.5, N * 0.47, (0, GREEN, 0.17), (0.55, GREEN, 0.06), (1, GREEN, 0)))
    if radius:
        sc.ring(x, y, N, N, r, bw, lin(0, y, 0, y + N, (0, "#1FC25E", 1), (0.45, BORDER, 1), (1, "#086B2D", 1)))
        if N >= 48:
            hw = max(1.0, 1.25 * u)
            sc.ring(x + bw, y + bw, N - 2 * bw, N - 2 * bw, r - bw, hw, lin(0, y, 0, y + N * 0.5, (0, "#FFFFFF", 0.26), (1, "#FFFFFF", 0)))
    if N < 128:
        p = auto_params(N); m = offset_mark(mark_in(N, p), x, y)
        cap0 = y + p['yT']; cap1 = cap0 + 2 * p['S'] + 2 * p['ctr'] + p['Lw']
    else:
        mw = N * 0.68; mh = mw * MH / MW
        m = mark_fit(x + (N - mw) / 2 - 1.5 * u, y + (N - mh) / 2, w=mw); cap0 = y + (N - mh) / 2; cap1 = cap0 + mh
    return sc.add_mark(m, lin(0, cap0, 0, cap1, (0, "#8CFFB1", 1), (0.42, GREEN, 1), (1, "#22E391", 1)),
                       lin(0, cap0, 0, cap1, (0.25, "#7BEBFF", 1), (0.5, CYAN, 1), (0.8, "#00B2E2", 1)))
FLAT_R = {16: 3, 20: 4, 24: 5, 30: 6, 32: 7}
def tile_flat(N, unit=1.0):
    """Flat tile: optical art for N px, drawn at `unit` user units per pixel."""
    sc = Scene(N * unit, N * unit); r = FLAT_R[N]
    sc.rrect(0, 0, N * unit, N * unit, r * unit, solid(PLATE))
    if N >= 24: sc.ring(0, 0, N * unit, N * unit, r * unit, unit, solid(BORDER))
    m = opt_mark(N, plated=True); m = dict(body=tf(m['body'], unit), head=tf(m['head'], unit))
    return sc.add_mark(m, solid(GREEN), solid(CYAN))
def tile_image(N): return (tile_flat(N) if N <= 32 else tile_premium(N)).render(1.0)

# ---------------- ICO writer (32bpp BGRA DIB + AND mask; 256 as PNG) ----------------
def ico_bytes(images):
    ents = []
    for im in images:
        w, h = im.size
        if w >= 256:
            b = io.BytesIO(); im.save(b, 'PNG', optimize=True); data = b.getvalue()
        else:
            a = np.asarray(im.convert('RGBA'))[::-1]; bgra = np.ascontiguousarray(a[:, :, [2, 1, 0, 3]]).tobytes()
            rowb = ((w + 31) // 32) * 4; mask = bytearray()
            for row in a:
                bits = np.packbits((row[:, 3] == 0).astype(np.uint8)).tobytes(); mask += bits + b'\0' * (rowb - len(bits))
            data = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0) + bgra + bytes(mask)
        ents.append((w, h, data))
    out = struct.pack('<HHH', 0, 1, len(ents)); off = 6 + 16 * len(ents); body = b''
    for w, h, data in ents:
        out += struct.pack('<BBBBHHII', w % 256, h % 256, 0, 0, 1, 32, len(data), off); off += len(data); body += data
    return out + body

APP_SIZES = [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 128, 256]
TRAY_SIZES = [16, 20, 24, 32, 40, 48, 64]

def build():
    files = []
    def out(rel, data): W(rel, data); files.append(rel)
    def png(rel, im):
        b = io.BytesIO(); im.save(b, 'PNG', optimize=True); out(rel, b.getvalue())
    HC = '<style>.f{display:none}@media (max-width:32px),(forced-colors:active),(prefers-contrast:more){.p{display:none}.f{display:inline}}</style>'
    def adaptive_tile(title):
        d1, b1 = tile_premium(256).svg_body('p'); d2, b2 = tile_flat(32, unit=8.0).svg_body('f')
        return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256" role="img" aria-label="%s"><title>%s</title>%s%s<g class="p">%s</g><g class="f">%s</g></svg>\n' % (title, title, HC, d1, b1, b2))

    # ---------- app ----------
    app = {N: tile_image(N) for N in APP_SIZES}
    out('app/ecu-fps.ico', ico_bytes([app[N] for N in APP_SIZES]))
    out('app/svg/ecu-fps-app-tile.svg', adaptive_tile('Ecu Fps'))
    out('app/svg/ecu-fps-app-tile-flat.svg', tile_flat(32, unit=8.0).svg('Ecu Fps'))
    png('app/png/ecu-fps-256.png', app[256])
    for N in APP_SIZES: png('preview/img/app-%d.png' % N, app[N])

    # ---------- logo ----------
    def lockup(kind, stacked=False):
        col = {'dark': (TEXT, GREEN, GREEN, CYAN), 'light': (INK, GREEN_L, GREEN_L, CYAN_L),
               'white': ('#FFFFFF',) * 4, 'ink': (INK,) * 4}[kind]
        pad = 12.0
        if not stacked:
            mh = 96.0; m = mark_fit(pad, pad, h=mh); cap = 50.0
            e, f, wr, _ = wordmark(cap, pad + m['w'] + 30, pad + (mh - cap) / 2)
            sc = Scene(math.ceil(wr + pad), mh + 2 * pad)
        else:
            cap = 44.0; ww = wm_width(cap); mh = 104.0; m0 = mark_fit(0, 0, h=mh)
            Wd = math.ceil(max(ww, m0['w']) + 2 * pad); m = mark_fit((Wd - m0['w']) / 2, pad, h=mh)
            e, f, wr, wh = wordmark(cap, (Wd - ww) / 2, pad + mh + 30); sc = Scene(Wd, math.ceil(pad + mh + 30 + cap * 1.28 + pad))
        sc.add_mark(m, solid(col[2]), solid(col[3])); sc.poly(e, solid(col[0])); sc.poly(f, solid(col[1]))
        return sc
    for k in ('dark', 'light', 'white', 'ink'): out('logo/svg/ecu-fps-lockup-%s.svg' % k, lockup(k).svg('Ecu Fps'))
    for k in ('dark', 'light'): out('logo/svg/ecu-fps-stacked-%s.svg' % k, lockup(k, True).svg('Ecu Fps'))
    def mark_scene(bp, hp):
        sc = Scene(256, 256); w = 216.0; h = w * MH / MW; return sc.add_mark(mark_fit((256 - w) / 2, (256 - h) / 2, w=w), bp, hp)
    out('logo/svg/ef-mark.svg', mark_scene(solid(GREEN), solid(CYAN)).svg('EF'))
    out('logo/svg/ef-mark-light.svg', mark_scene(solid(GREEN_L), solid(CYAN_L)).svg('EF'))
    out('logo/svg/ef-mark-currentcolor.svg', mark_scene(('cur',), ('cur',)).svg('EF'))

    # ---------- tray ----------
    def tray_scene(N, active, paint):
        return Scene(N, N).add_mark(opt_mark(N, False, head=active), paint, paint)
    for name, colr in TRAY.items():
        active = 'neutral' not in name
        imgs = [tray_scene(N, active, solid(colr)).render(1.0) for N in TRAY_SIZES]
        out('tray/tray-%s.ico' % name, ico_bytes(imgs))
        for N, im in zip(TRAY_SIZES, imgs): png('preview/img/tray-%s-%d.png' % (name, N), im)
        ms = Scene(256, 256); w = 232.0; h = w * MH / MW
        ms.add_mark(mark_fit(12, (256 - h) / 2, w=w, head=active), solid(colr), solid(colr))
        out('tray/svg/tray-%s.svg' % name, ms.svg('Ecu Fps ' + ('açık' if active else 'kapalı')))
    for N in TRAY_SIZES:
        for st in ('active', 'neutral'):
            out('tray/svg/optical/tray-%d-%s.svg' % (N, st), tray_scene(N, st == 'active', ('cur',)).svg('Ecu Fps %d px' % N))

    # ---------- web ----------
    out('web/favicon.svg', adaptive_tile('Ecu Fps'))
    out('web/favicon.ico', ico_bytes([app[16], app[32], app[48]]))
    at = Scene(180, 180); tile_premium(180, sc=at, radius=False); png('web/apple-touch-icon.png', at.render(1.0).convert('RGB'))
    png('web/logo-512.png', tile_premium(256).render(2.0))
    def speed_rule(sc, x0, x1, y, wline, gcol, ccol):
        hh = wline * 1.5
        sc.poly([rect(x0, y - wline / 2, x1 + 1, y + wline / 2)], lin(x0, 0, x1, 0, (0, gcol, 0), (0.35, gcol, 0.9), (1, gcol, 1)))
        sc.poly([[(x1, y - hh), (x1 + hh, y), (x1, y + hh)]], solid(ccol))
    def accent_bar(sc, y, h, w, split, g, c):
        sc.rrect(0, y, w, h, 0, lin(0, 0, w, 0, (0, g, 1), (split, g, 1), (split, c, 1), (1, c, 1)))
    og = Scene(1200, 630)
    og.rrect(0, 0, 1200, 630, 0, lin(0, 0, 0, 630, (0, SURF, 1), (1, BG, 1)))
    og.rrect(0, 0, 1200, 630, 0, rad(330, 300, 520, (0, GREEN, 0.10), (0.6, GREEN, 0.025), (1, GREEN, 0)))
    ts = 264; cap = 118.0; ww = wm_width(cap); gapx = 72; x0 = (1200 - (ts + gapx + ww)) / 2; ty = 172
    tile_premium(ts, x0, ty, og)
    wx = x0 + ts + gapx; e, f, wr, _ = wordmark(cap, wx, ty + 30); og.poly(e, solid(TEXT)); og.poly(f, solid(GREEN))
    gl, adv = text_outline(TAGLINE, 34, wx + 2, ty + 30 + cap + 80); og.glyphs(gl, solid(TEXT2))
    accent_bar(og, 622, 8, 1200, 0.84, GREEN, CYAN)
    png('web/og-image.png', og.render(1.0, ss=3).convert('RGB'))
    def header(kind):
        dk = kind == 'dark'; sc = Scene(1200, 200); g = GREEN if dk else GREEN_L; c = CYAN if dk else CYAN_L
        sc.rrect(0, 0, 1200, 200, 0, lin(0, 0, 0, 200, (0, SURF, 1), (1, BG, 1)) if dk else lin(0, 0, 0, 200, (0, "#FFFFFF", 1), (1, "#F1F4F7", 1)))
        mh = 88.0; m = mark_fit(64, (200 - mh) / 2, h=mh); sc.add_mark(m, solid(g), solid(c))
        cap = 46.0; e, f, wr, _ = wordmark(cap, 64 + m['w'] + 28, (200 - cap) / 2)
        sc.poly(e, solid(TEXT if dk else INK)); sc.poly(f, solid(g))
        _, adv = text_outline(TAGLINE, 22, 0, 0); gl, _ = text_outline(TAGLINE, 22, 1136 - adv, 108); sc.glyphs(gl, solid(TEXT2 if dk else INK2))
        accent_bar(sc, 197, 3, 1200, 0.88, g, c)
        return sc
    hd, hl = header('dark'), header('light')
    out('web/site-header-dark.svg', hd.svg('Ecu Fps — ' + TAGLINE)); out('web/site-header-light.svg', hl.svg('Ecu Fps — ' + TAGLINE))
    d1, b1 = hd.svg_body('d'); d2, b2 = hl.svg_body('l')
    out('web/site-header.svg', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 200" width="1200" height="200" role="img" aria-label="Ecu Fps"><title>Ecu Fps — %s</title><style>.l{display:none}@media (prefers-color-scheme:light){.d{display:none}.l{display:inline}}</style>%s%s<g class="d">%s</g><g class="l">%s</g></svg>\n' % (TAGLINE, d1, d2, b1, b2))

    # ---------- setup ----------
    sb = Scene(164, 314)
    sb.rrect(0, 0, 164, 314, 0, lin(0, 0, 0, 314, (0, SURF2, 1), (0.55, SURF, 1), (1, BG, 1)))
    sb.rrect(0, 0, 164, 314, 0, rad(82, 104, 150, (0, GREEN, 0.12), (0.6, GREEN, 0.03), (1, GREEN, 0)))
    tile_premium(96, 34, 56, sb)
    cap = 27.0; ww = wm_width(cap); e, f, _, _ = wordmark(cap, (164 - ww) / 2, 184); sb.poly(e, solid(TEXT)); sb.poly(f, solid(GREEN))
    speed_rule(sb, 30, 128, 262, 2, GREEN, CYAN)
    accent_bar(sb, 310, 4, 164, 0.8, GREEN, CYAN)
    out('setup/setup-banner.svg', sb.svg('Ecu Fps kurulum')); png('setup/setup-banner.png', sb.render(1.0).convert('RGB'))
    sh = Scene(300, 114)
    sh.rrect(0, 0, 300, 114, 0, lin(0, 0, 0, 114, (0, SURF2, 1), (1, SURF, 1)))
    mh = 54.0; m = mark_fit(26, (114 - mh) / 2, h=mh); sh.add_mark(m, solid(GREEN), solid(CYAN))
    cap = 29.0; e, f, _, _ = wordmark(cap, 26 + m['w'] + 18, (114 - cap) / 2); sh.poly(e, solid(TEXT)); sh.poly(f, solid(GREEN))
    accent_bar(sh, 111, 3, 300, 0.85, GREEN, CYAN)
    out('setup/setup-header.svg', sh.svg('Ecu Fps')); png('setup/setup-header.png', sh.render(1.0).convert('RGB'))

    C = {
        'mark_green_on_plate': contrast(GREEN, PLATE), 'mark_green_on_app_bg': contrast(GREEN, BG),
        'cyan_on_plate': contrast(CYAN, PLATE), 'plate_on_white': contrast(PLATE, '#FFFFFF'),
        'light_green_on_white': contrast(GREEN_L, '#FFFFFF'), 'light_cyan_on_white': contrast(CYAN_L, '#FFFFFF'),
        'text_on_app_bg': contrast(TEXT, BG), 'text2_on_app_bg': contrast(TEXT2, BG), 'ink_on_white': contrast(INK, '#FFFFFF'),
        'tray_dark_active_on_1B1B1B': contrast(TRAY['dark-active'], TASK_DARK), 'tray_dark_neutral_on_1B1B1B': contrast(TRAY['dark-neutral'], TASK_DARK),
        'tray_light_active_on_EEF1F4': contrast(TRAY['light-active'], TASK_LIGHT), 'tray_light_neutral_on_EEF1F4': contrast(TRAY['light-neutral'], TASK_LIGHT),
        'tray_light_neutral_previous_808994_on_EEF1F4': contrast('#808994', TASK_LIGHT),
        'tray_hc_white_on_black': contrast('#FFFFFF', '#000000'), 'tray_hc_black_on_white': contrast('#000000', '#FFFFFF'),
    }
    return files, C

if __name__ == '__main__':
    files, C = build()
    print(len(files), 'files'); print(C)
    if os.path.exists(os.path.join(HERE, '_meta.py')):
        import _meta; _meta.write(files, C)
