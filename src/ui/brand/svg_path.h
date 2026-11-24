#pragma once
// BRAND-01: onayli marka SVG'leri (2.0.0 "Boost Line" EF) icin minimal ayristirici. Yeni SVG motoru degildir:
// yalniz paketin kullandigi alt kume desteklenir — <svg viewBox>, ic ice <g transform="translate(x y)
// scale(s)">, <g|path fill="#RRGGBB|currentColor"> ve fill-rule="nonzero|evenodd" (SVG varsayilani nonzero),
// <path [transform="translate(x y)"] d="..."> ve mutlak M/L/H/V/Q/Z komutlari. Desteklenmeyen sey hata
// dondurur (sessizce yanlis cizim yok).
// Logo yazisi fontla dizilmez; saglanan cizgisel yollar kullanilir.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "ui/theme/theme.h"

namespace ef::ui {

struct SvgCmd {
    char op = 'M';  // M L Q Z (H/V L'ye cevrilir)
    float x = 0, y = 0, cx = 0, cy = 0;  // Q icin kontrol noktasi (cx, cy)
};

struct SvgShape {
    std::optional<Color> fill;  // nullopt = currentColor (cizimde verilen renk)
    std::vector<SvgCmd> cmds;   // viewBox koordinatinda (donusumler uygulanmis)
    bool evenodd = false;       // fill-rule: false = nonzero (SVG varsayilani; EF amblemindeki ortusen alt yollar birlesir)
};

struct SvgDoc {
    float view_w = 0, view_h = 0;
    std::vector<SvgShape> shapes;
    // Tum noktalarin sinir kutusu (viewBox koordinati)
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
};

contracts::Result<SvgDoc> parse_svg(std::string_view text);

}  // namespace ef::ui
