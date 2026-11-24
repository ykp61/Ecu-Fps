#include "core/i18n/catalog.h"
#include "core/brand/tray_variant.h"
#include "ef_test.h"
#include "ui/brand/brand.h"

#include <algorithm>
#include <vector>

using namespace ef::ui;
namespace cb = ef::core::brand;
using ef::core::perf::Phase;

namespace {
int subpaths(const SvgShape& s) {
    int n = 0;
    for (const auto& c : s.cmds) n += c.op == 'M';
    return n;
}
// Alt yolun (M..Z) noktalari.
std::vector<SvgCmd> subpath(const SvgShape& s, int k) {
    std::vector<SvgCmd> out;
    int i = -1;
    for (const auto& c : s.cmds) {
        if (c.op == 'M') ++i;
        if (i == k && c.op != 'Z') out.push_back(c);
    }
    return out;
}
bool approx(float a, float b, float eps = 0.001f) { return a > b - eps && a < b + eps; }
}  // namespace

EF_TEST(brand_embedded_svgs_parse_within_viewbox) {
    using brand::Logo;
    for (auto l : brand::kAllLogos) {
        const auto r = parse_svg(brand::embedded_svg(l));
        EF_CHECK(r.ok());
        if (!r.ok()) continue;
        const auto& d = r.value();
        EF_CHECK(!d.shapes.empty() && d.view_w > 0 && d.view_h > 0);
        // Tum yollar viewBox icinde (donusumler dogru uygulandi)
        EF_CHECK(d.min_x >= -0.5f && d.min_y >= -0.5f && d.max_x <= d.view_w + 0.5f && d.max_y <= d.view_h + 0.5f);
        EF_CHECK(!brand::logo(l).shapes.empty());
        // 2.0.0 paketi SVG varsayilani nonzero kullanir: ortusen alt yollar (hiz cizgisi E govdesinden gecer) birlesir.
        for (const auto& s : d.shapes) EF_CHECK(!s.evenodd);
    }
    // Yatay logo (koyu zemin): 397x120; yalniz marka renkleri: yesil amblem #3DFF7A, camgobegi ok ucu #00D4FF,
    // wordmark #F1F5F9. currentColor yok (renkler SVG'den; yuksek kontrastta cizim mono renk verir).
    const auto& dark = brand::logo(Logo::LockupDark);
    EF_CHECK(dark.view_w == 397 && dark.view_h == 120);
    bool green = false, cyan = false, text = false, other = false;
    for (const auto& s : dark.shapes) {
        green |= s.fill && *s.fill == *Color::from_hex("#3DFF7A");
        cyan |= s.fill && *s.fill == *Color::from_hex("#00D4FF");
        text |= s.fill && *s.fill == *Color::from_hex("#F1F5F9");
        other |= !s.fill || (*s.fill != *Color::from_hex("#3DFF7A") && *s.fill != *Color::from_hex("#00D4FF") &&
                             *s.fill != *Color::from_hex("#F1F5F9"));
    }
    EF_CHECK(green && cyan && text && !other);
    // Yatay logo 160 DIP (kLockupMinWidthDip) genislikte: murekkep orani amblem + yazi (en az 3.5:1, en cok 4.5:1).
    const float ratio = (dark.max_x - dark.min_x) / (dark.max_y - dark.min_y);
    EF_CHECK(ratio > 3.5f && ratio < 4.5f);
    // Acik zemin kilidi ayni geometri, acik renkler (#0B8F3C amblem, #00708F ok ucu, #0B0F17 yazi).
    const auto& light = brand::logo(Logo::LockupLight);
    EF_CHECK(light.shapes.size() == dark.shapes.size() && light.view_w == 397 && light.view_h == 120);
    EF_CHECK(light.shapes.front().fill && *light.shapes.front().fill == *Color::from_hex("#0B8F3C"));
    // Tek renk kilit: tum yollar tek renk
    const auto& white = brand::logo(Logo::LockupWhite);
    for (const auto& s : white.shapes) EF_CHECK(s.fill && *s.fill == *Color::from_hex("#FFFFFF"));
}

EF_TEST(brand_ef_mark_geometry_matches_boost_line_spec) {
    using brand::Logo;
    // ef-mark.svg: 256 birim tuval; yol 1 = E + hiz cizgisi + F (4 alt yol, yesil), yol 2 = ok ucu (ucgen, camgobegi).
    const auto& m = brand::logo(Logo::MarkColor);
    EF_CHECK(m.view_w == 256 && m.view_h == 256 && m.shapes.size() == 2);
    if (m.shapes.size() != 2) return;
    EF_CHECK(m.shapes[0].fill && *m.shapes[0].fill == *Color::from_hex("#3DFF7A"));
    EF_CHECK(m.shapes[1].fill && *m.shapes[1].fill == *Color::from_hex("#00D4FF"));
    EF_CHECK(subpaths(m.shapes[0]) == 4 && subpaths(m.shapes[1]) == 1);
    // Murekkep kutusu (1/16 birim izgara): x 20..236, y 50.5625..205.4375
    EF_CHECK(approx(m.min_x, 20) && approx(m.max_x, 236) && approx(m.min_y, 50.5625f) && approx(m.max_y, 205.4375f));
    // E (alt yol 0): 10 kose; ust kol kalinligi 35.75 (harf kalinligi 36, brand-tokens mark_stroke_units_of_256)
    const auto e = subpath(m.shapes[0], 0);
    EF_CHECK(e.size() == 10);
    if (e.size() == 10) EF_CHECK(approx(e[2].y - e[0].y, 35.75f));
    // Hiz cizgisi (alt yol 1): dortgen, kalinlik 17.875 = harf kalinliginin yarisi (speed_line_ratio_of_stroke 0.5);
    // 10 derece italik: ust kenar alt kenardan saga kaymis.
    const auto sl = subpath(m.shapes[0], 1);
    EF_CHECK(sl.size() == 4);
    if (sl.size() == 4) {
        EF_CHECK(approx(sl[0].y, 119.0625f) && approx(sl[1].y, 119.0625f) && approx(sl[2].y, 136.9375f) && approx(sl[3].y, 136.9375f));
        EF_CHECK(sl[0].x > sl[3].x && sl[1].x > sl[2].x);
        const float tan10 = (sl[0].x - sl[3].x) / (sl[3].y - sl[0].y);
        EF_CHECK(tan10 > 0.17f && tan10 < 0.18f);  // tan(10 derece) = 0.1763
    }
    // Ok ucu: hiz cizgisinin ortasinda (y 128) ve amblemin en sag noktasi.
    const auto tip = subpath(m.shapes[1], 0);
    EF_CHECK(tip.size() == 3);
    if (tip.size() == 3) {
        EF_CHECK(approx(tip[1].x, 236) && approx(tip[1].y, 128));
        EF_CHECK(approx((tip[0].y + tip[2].y) / 2, 128));
    }
    // currentColor amblem: ayni geometri, renk cizimde verilir.
    const auto& c = brand::logo(Logo::MarkCurrent);
    EF_CHECK(c.shapes.size() == m.shapes.size());
    for (std::size_t i = 0; i < c.shapes.size() && i < m.shapes.size(); ++i) {
        EF_CHECK(!c.shapes[i].fill.has_value());
        EF_CHECK(c.shapes[i].cmds.size() == m.shapes[i].cmds.size());
        for (std::size_t k = 0; k < c.shapes[i].cmds.size() && k < m.shapes[i].cmds.size(); ++k)
            EF_CHECK(c.shapes[i].cmds[k].op == m.shapes[i].cmds[k].op && c.shapes[i].cmds[k].x == m.shapes[i].cmds[k].x &&
                      c.shapes[i].cmds[k].y == m.shapes[i].cmds[k].y);
    }
}

EF_TEST(brand_optical_marks_fit_pixel_grid) {
    using brand::Logo;
    // Kucuk ikon cizimi boyuta ozel: 16/20 px tamamen tam piksel, 24 px 1/16 izgara; hepsi currentColor ve ok ucu dahil
    // (iki yol: govde + ok ucu).
    const struct {
        Logo l;
        float px;
        bool integer;
    } cases[] = {{Logo::MarkOptical16, 16, true}, {Logo::MarkOptical20, 20, true}, {Logo::MarkOptical24, 24, false}};
    for (const auto& k : cases) {
        const auto& d = brand::logo(k.l);
        EF_CHECK(d.view_w == k.px && d.view_h == k.px && d.shapes.size() == 2);
        for (const auto& s : d.shapes) {
            EF_CHECK(!s.fill.has_value());
            for (const auto& c : s.cmds) {
                if (c.op == 'Z') continue;
                const float q = k.integer ? 1.f : 16.f;
                EF_CHECK(c.x * q == static_cast<float>(static_cast<int>(c.x * q)) && c.y * q == static_cast<float>(static_cast<int>(c.y * q)));
                EF_CHECK(c.x >= 0 && c.y >= 0 && c.x <= k.px && c.y <= k.px);
            }
        }
        // Ok ucu amblemin en sag ucunda
        float body_max = 0, tip_max = 0;
        for (const auto& c : d.shapes[0].cmds) body_max = std::max(body_max, c.x);
        for (const auto& c : d.shapes[1].cmds) tip_max = std::max(tip_max, c.x);
        EF_CHECK(tip_max > body_max);
    }
    EF_CHECK(brand::mark_for_px(16) == Logo::MarkOptical16 && brand::mark_for_px(17) == Logo::MarkOptical20);
    EF_CHECK(brand::mark_for_px(20) == Logo::MarkOptical20 && brand::mark_for_px(24) == Logo::MarkOptical24);
    EF_CHECK(brand::mark_for_px(25) == Logo::MarkCurrent && brand::mark_for_px(256) == Logo::MarkCurrent);
}

EF_TEST(brand_svg_parser_rejects_unsupported) {
    EF_CHECK(!parse_svg("").ok());
    EF_CHECK(!parse_svg(R"svg(<svg viewBox="0 0 10 10"><g fill="#000000"><path d="m0 0h1z"/></g></svg>)svg").ok());   // goreli komut
    EF_CHECK(!parse_svg(R"svg(<svg viewBox="0 0 10 10"><g fill="#000000"><path d="M0 0C1 1 2 2 3 3Z"/></g></svg>)svg").ok());  // kubik
    EF_CHECK(!parse_svg(R"svg(<svg viewBox="0 0 10 10"><g transform="rotate(4)" fill="#000000"><path d="M0 0H1Z"/></g></svg>)svg").ok());
    EF_CHECK(!parse_svg(R"svg(<svg viewBox="0 0 10 10"><path d="M0 0H1Z"/></svg>)svg").ok());  // dolgu yok
    EF_CHECK(!parse_svg(R"svg(<svg viewBox="0 0 10 10"><path d="M0 0H1Z" fill="red"/></svg>)svg").ok());  // adli renk yok
    EF_CHECK(!parse_svg(R"svg(<svg viewBox="0 0 10 10"><path d="M0 0H1Z" fill="#000000" fill-rule="inherit"/></svg>)svg").ok());
    const auto ok = parse_svg(R"svg(<svg viewBox="0 0 10 10"><g transform="translate(1 2) scale(2)" fill="#FF0000"><path transform="translate(1 0)" d="M0 0H1V1L0 1Q0 0.5 0 0Z"/></g></svg>)svg");
    EF_CHECK(ok.ok());
    const auto& c = ok.value().shapes[0].cmds;
    EF_CHECK(c[0].op == 'M' && c[0].x == 3 && c[0].y == 2);  // 2*(0+1)+1, 2*0+2
    EF_CHECK(c[1].op == 'L' && c[1].x == 5 && c[1].y == 2);  // H1 -> (1+1)*2+1
    EF_CHECK(c[4].op == 'Q' && c.back().op == 'Z');
    EF_CHECK(!ok.value().shapes[0].evenodd);  // SVG varsayilani nonzero
    // path uzerindeki fill / fill-rule grubu ezer; grup degeri sonraki kardese tasinmaz.
    const auto pf = parse_svg(R"svg(<svg viewBox="0 0 10 10"><g fill="#FF0000" fill-rule="evenodd"><path d="M0 0H1V1Z" fill="currentColor" fill-rule="nonzero"/><path d="M0 0H2V2Z"/></g><path d="M0 0H3V3Z" fill="#00FF00"/></svg>)svg");
    EF_CHECK(pf.ok());
    if (pf.ok() && pf.value().shapes.size() == 3) {
        const auto& sh = pf.value().shapes;
        EF_CHECK(!sh[0].fill.has_value() && !sh[0].evenodd);
        EF_CHECK(sh[1].fill && *sh[1].fill == *Color::from_hex("#FF0000") && sh[1].evenodd);
        EF_CHECK(sh[2].fill && *sh[2].fill == *Color::from_hex("#00FF00") && !sh[2].evenodd);
    } else {
        EF_CHECK(false);
    }
}

EF_TEST(brand_tray_active_only_when_verified_on) {
    for (auto ph : {Phase::Off, Phase::Planning, Phase::Confirming, Phase::Applying, Phase::On, Phase::ConfirmRestore, Phase::Restoring,
                    Phase::RecoveryNeeded}) {
        for (auto tb : {cb::TaskbarTheme::Dark, cb::TaskbarTheme::Light}) {
            const auto c = cb::choose_tray(true, ph, tb, false, true, true);
            const bool active_variant = c.variant == cb::TrayVariant::DarkActive || c.variant == cb::TrayVariant::LightActive;
            EF_CHECK(active_variant == (ph == Phase::On));
            EF_CHECK(c.active == (ph == Phase::On));
            // Gorev cubugu zeminine gore (uygulama temasindan bagimsiz)
            const bool light_variant = c.variant == cb::TrayVariant::LightActive || c.variant == cb::TrayVariant::LightNeutral;
            EF_CHECK(light_variant == (tb == cb::TaskbarTheme::Light));
        }
    }
    // Baslangic durumu bilinmiyorsa aktif yok ve "dogrulaniyor"
    const auto unknown = cb::choose_tray(false, Phase::On, cb::TaskbarTheme::Dark, false, true, true);
    EF_CHECK(!unknown.active && unknown.tip_key == "tray.tip_checking");
    EF_CHECK(cb::choose_tray(true, Phase::Applying, cb::TaskbarTheme::Dark, false, true, true).tip_key == "tray.tip_checking");
    EF_CHECK(cb::choose_tray(true, Phase::RecoveryNeeded, cb::TaskbarTheme::Dark, false, true, true).tip_key == "tray.tip_recovery");
    EF_CHECK(cb::choose_tray(true, Phase::Off, cb::TaskbarTheme::Dark, false, true, true).tip_key == "tray.tip_off");
    // Yuksek kontrast: sistem rengi; cizilemezse zemine gore siyah/beyaz yedek; durum ipucuda
    auto hc = cb::choose_tray(true, Phase::On, cb::TaskbarTheme::Light, true, true, true);
    EF_CHECK(hc.variant == cb::TrayVariant::HcSystem && hc.tip_key == "tray.tip_on");
    EF_CHECK(cb::choose_tray(true, Phase::Off, cb::TaskbarTheme::Light, true, false, true).variant == cb::TrayVariant::HcWhite);
    EF_CHECK(cb::choose_tray(true, Phase::Off, cb::TaskbarTheme::Light, true, false, false).variant == cb::TrayVariant::HcBlack);
    for (auto k : {"tray.tip_on", "tray.tip_off", "tray.tip_checking", "tray.tip_recovery"}) EF_CHECK(ef::core::i18n::id_of(k).has_value());
}
