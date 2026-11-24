#include "ui/theme/theme.h"

#include <algorithm>
#include <cmath>

#include "core/serialization/json.h"

namespace ef::ui::embedded_data {
extern const std::string_view tokens;
}

namespace ef::ui {

using contracts::ErrorCode;
using contracts::Result;

namespace {
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
double channel(float c) {
    const double v = c;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
}  // namespace

std::optional<Color> Color::from_hex(std::string_view h) {
    if (h.size() != 7 || h[0] != '#') return std::nullopt;
    float out[3];
    for (int i = 0; i < 3; ++i) {
        const int hi = hexval(h[1 + i * 2]), lo = hexval(h[2 + i * 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = static_cast<float>(hi * 16 + lo) / 255.f;
    }
    return Color{out[0], out[1], out[2], 1.f};
}

Color mix(const Color& a, const Color& b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

double relative_luminance(const Color& c) {
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

double contrast_ratio(const Color& a, const Color& b) {
    const double la = relative_luminance(a), lb = relative_luminance(b);
    const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

Color ensure_contrast(const Color& c, const Color& bg, const Color& toward, double target) {
    for (int i = 0; i <= 20; ++i) {
        const Color x = mix(c, toward, static_cast<float>(i) / 20.f);
        if (contrast_ratio(x, bg) >= target) return x;
    }
    return toward;
}

namespace {

Result<Color> color_at(const core::json::Value& colors, std::string_view key) {
    const auto* v = colors.find(key);
    if (!v || v->type() != core::json::Value::Type::String) return Result<Color>::fail(ErrorCode::ParseError, std::string(key));
    auto c = Color::from_hex(v->as_string());
    if (!c) return Result<Color>::fail(ErrorCode::ParseError, std::string(key));
    return *c;
}

Result<Palette> palette(const core::json::Value& colors) {
    Palette p;
    struct Slot {
        const char* key;
        Color Palette::*field;
    };
    static constexpr Slot slots[] = {
        {"background", &Palette::background}, {"sidebar", &Palette::sidebar},
        {"surface", &Palette::surface},       {"surface_raised", &Palette::surface_raised},
        {"border", &Palette::border},         {"text_primary", &Palette::text_primary},
        {"text_secondary", &Palette::text_secondary}, {"accent", &Palette::accent},
        {"on_accent", &Palette::on_accent},   {"violet", &Palette::violet},
        {"success", &Palette::success},       {"warning", &Palette::warning},
        {"error", &Palette::error},
    };
    for (const auto& s : slots) {
        auto c = color_at(colors, s.key);
        if (!c.ok()) return c.error();
        p.*(s.field) = c.value();
    }
    // DS-08: colors.tray_*/high_contrast_* bilincli olarak burada okunmaz; tepsi/ikon hattinin derleme
    // girdisidir (build-assets), calisma zamani paletinin parcasi degildir.
    // Istege bagli: focus_ring yoksa accent (eski token dosyalari); varsa gecerli olmali.
    p.focus_ring = p.accent;
    if (colors.find("focus_ring")) {
        auto c = color_at(colors, "focus_ring");
        if (!c.ok()) return c.error();
        p.focus_ring = c.value();
    }
    // Istege bagli yeni yuvalar (eski token dosyalari icin geri dusus); varsa gecerli olmali.
    p.orange = p.warning;
    p.cyan = p.accent;
    p.hero = p.surface;
    static constexpr Slot optional_slots[] = {
        {"orange", &Palette::orange}, {"cyan", &Palette::cyan}, {"hero", &Palette::hero}};
    for (const auto& s : optional_slots) {
        if (!colors.find(s.key)) continue;
        auto c = color_at(colors, s.key);
        if (!c.ok()) return c.error();
        p.*(s.field) = c.value();
    }
    // Veri rolleri (spec 2026-11-12 F): istege bagli; yoksa mevcut renklere duser, varsa gecerli olmali.
    p.data_cpu = p.warning;
    p.data_gpu = p.cyan;
    p.data_net_down = p.cyan;
    p.data_net_up = p.violet;
    p.data_info = p.text_secondary;
    static constexpr Slot data_slots[] = {{"data_cpu", &Palette::data_cpu},
                                          {"data_gpu", &Palette::data_gpu},
                                          {"data_net_down", &Palette::data_net_down},
                                          {"data_net_up", &Palette::data_net_up},
                                          {"data_info", &Palette::data_info}};
    for (const auto& s : data_slots) {
        if (!colors.find(s.key)) continue;
        auto c = color_at(colors, s.key);
        if (!c.ok()) return c.error();
        p.*(s.field) = c.value();
    }
    return p;
}

std::optional<float> num(const core::json::Value* v) {
    if (!v || (v->type() != core::json::Value::Type::Int && v->type() != core::json::Value::Type::Double)) return std::nullopt;
    return static_cast<float>(v->as_double());
}

std::optional<std::pair<float, float>> pair_of(const core::json::Value* v) {
    if (!v || v->type() != core::json::Value::Type::Array || v->items().size() != 2) return std::nullopt;
    auto a = num(&v->items()[0]), b = num(&v->items()[1]);
    if (!a || !b) return std::nullopt;
    return std::make_pair(*a, *b);
}

}  // namespace

Result<Theme> parse_theme(std::string_view text) {
    using R = Result<Theme>;
    auto parsed = core::json::parse(text);
    if (!parsed.ok()) return parsed.error();
    const auto& root = parsed.value();
    Theme t;
    const auto* colors = root.find("colors");
    if (!colors) return R::fail(ErrorCode::ParseError, "colors");
    auto dark = palette(*colors);
    if (!dark.ok()) return dark.error();
    t.dark = dark.value();
    const auto* type = root.find("type");
    if (!type) return R::fail(ErrorCode::ParseError, "type");
    static constexpr const char* role_keys[] = {"page_title", "state_title", "section", "body", "secondary", "metric"};
    static_assert(std::size(role_keys) + 2 == kTokenRoleCount);  // + istege bagli "label", "hero"
    const auto read_role = [&](const char* key, TypeRole& out) {
        const auto* r = type->find(key);
        if (!r || r->type() != core::json::Value::Type::Array || r->items().size() != 3) return false;
        auto size = num(&r->items()[0]), line = num(&r->items()[1]);
        const auto& w = r->items()[2];
        if (!size || !line || w.type() != core::json::Value::Type::String) return false;
        out = {*size, *line, w.as_string() == "Semibold" ? Weight::Semibold : Weight::Regular};
        return true;
    };
    for (std::size_t i = 0; i < std::size(role_keys); ++i)
        if (!read_role(role_keys[i], t.type[i])) return R::fail(ErrorCode::ParseError, role_keys[i]);
    // Bolum etiketi (type.label) istege baglidir: eski token dosyalarinda yoksa 11/14 yari kalin; varsa gecerli olmali.
    TypeRole& label = t.type[static_cast<std::size_t>(Role::Label)];
    label = {11, 14, Weight::Semibold};
    if (type->find("label") && !read_role("label", label)) return R::fail(ErrorCode::ParseError, "label");
    // B15: sonuc kartinin buyuk sayisi (type.hero) istege baglidir: yoksa 38/44 yari kalin; varsa gecerli olmali.
    TypeRole& hero = t.type[static_cast<std::size_t>(Role::Hero)];
    hero = {38, 44, Weight::Semibold};
    if (type->find("hero") && !read_role("hero", hero)) return R::fail(ErrorCode::ParseError, "hero");
    // BOOST Panel rolleri (token degil; maketin HTML/CSS kaynagindan): {boyut, satir, agirlik, mono, harf araligi, css}.
    {
        using W = Weight;
        const auto set = [&](Role r, float size, W w, bool mono = false, float tracking = 0.f) {
            t.type[static_cast<std::size_t>(r)] = {size, size * 1.5f, w, mono, tracking, true};
        };
        set(Role::Mono9, 9, W::Medium, true);
        t.type[static_cast<std::size_t>(Role::Mono9)].line = 11;  // leading-none (9) yerine 11: satir > boyut kurali
        set(Role::Mono10, 10, W::Medium, true);
        set(Role::Mono11, 11, W::Medium, true);
        set(Role::Mono11Semi, 11, W::Semibold, true);
        set(Role::Mono12Semi, 12, W::Semibold, true);
        set(Role::Caps11, 11, W::Semibold, false, 0.55f);
        set(Role::Small11, 11, W::Regular);
        set(Role::Small12, 12, W::Regular);
        set(Role::Medium10, 10, W::Medium);
        set(Role::Medium11, 11, W::Medium);
        set(Role::Medium12, 12, W::Medium);
        set(Role::Medium13, 13, W::Medium);
        set(Role::Semi12, 12, W::Semibold);
        set(Role::Semi13, 13, W::Semibold);
        set(Role::Title14, 14, W::Semibold);
        set(Role::Num20, 20, W::Bold);
        t.type[static_cast<std::size_t>(Role::Num20)].line = 24;  // leading-[24px]
        set(Role::Num22, 22, W::Bold);
        t.type[static_cast<std::size_t>(Role::Num22)].line = 26;  // leading-[26px]
        set(Role::Num24, 24, W::Bold);
        t.type[static_cast<std::size_t>(Role::Num24)].line = 28;  // leading-[28px]
        set(Role::Small13, 13, W::Regular);
        set(Role::Semi18, 18, W::Semibold);
        set(Role::Num18, 18, W::Bold);
        set(Role::Semi20, 20, W::Semibold);
        t.type[static_cast<std::size_t>(Role::Semi20)].line = 26;  // leading-[26px]
        set(Role::Mono12, 12, W::Medium, true);
    }
    if (const auto* fams = root.find("font_families"))
        for (const auto& f : fams->items())
            if (f.type() == core::json::Value::Type::String) t.font_families.push_back(f.as_string());
    if (t.font_families.empty()) t.font_families = {"Segoe UI"};
    if (const auto* sp = root.find("spacing"))
        for (const auto& s : sp->items())
            if (auto v = num(&s)) t.spacing.push_back(*v);
    // DS-08: radii.dialog + motion_ms.dialog_open uygulamanin kendi cizdigi kip penceresinde tuketilir (spec 2026-11-12 E;
    // Screen::modal_layer). motion_ms.dialog_close token sozlesmesinde kalir (pencere yanitla birlikte aninda kapanir).
    if (const auto* r = root.find("radii")) {
        if (auto v = num(r->find("control"))) t.radius_control = *v;
        if (auto v = num(r->find("card"))) t.radius_card = *v;
        if (auto v = num(r->find("dialog"))) t.radius_dialog = *v;
    }
    // DS-13: bilesen geometrisi token'lari (canli halka, guc halkasi kalinligi, kaydirma cubugu).
    if (const auto* gm = root.find("geometry")) {
        if (auto v = num(gm->find("live_ring_diameter")); v && *v >= 32 && *v <= 160) t.live_ring_d = *v;
        if (auto v = num(gm->find("live_ring_thickness")); v && *v >= 1 && *v <= 16) t.live_ring_thick = *v;
        if (auto v = num(gm->find("power_ring_thickness")); v && *v >= 1 && *v <= 16) t.power_ring_thick = *v;
        if (auto v = num(gm->find("power_button_diameter")); v && *v >= 48 && *v <= 160) t.power_button_d = *v;
        if (auto v = num(gm->find("test_button_diameter")); v && *v >= 48 && *v <= 120) t.test_button_d = *v;
        if (auto v = num(gm->find("card_lift")); v && *v >= 0 && *v <= 4) t.card_lift = *v;  // motion_policy: en cok 4 DIP
        if (auto v = num(gm->find("status_dot")); v && *v >= 4 && *v <= 10) t.status_dot = *v;
        if (auto v = num(gm->find("scrollbar_thumb_width")); v && *v >= 2 && *v <= 16) t.scrollbar_thumb_w = *v;
        if (auto v = num(gm->find("scrollbar_inset")); v && *v >= 0 && *v <= 32) t.scrollbar_inset = *v;
        if (auto v = num(gm->find("topbar_h")); v && *v >= 32 && *v <= 96) t.topbar_h = *v;
        if (auto v = num(gm->find("strip_h")); v && *v >= 0 && *v <= 8) t.strip_h = *v;
        if (auto v = num(gm->find("topbar_tab_h")); v && *v >= 24 && *v <= 96) t.topbar_tab_h = *v;
        if (auto v = num(gm->find("logo_mark_h")); v && *v >= 16 && *v <= 64) t.logo_mark_h = *v;
        if (auto v = num(gm->find("logo_lockup_w")); v && *v >= 160 && *v <= 400) t.logo_lockup_w = *v;
    }
    // ADR-033: kurulum penceresi olculeri. Aralik disi deger yok sayilir (varsayilan kalir).
    if (const auto* su = root.find("setup")) {
        if (auto p = pair_of(su->find("window")); p && p->first >= 360 && p->first <= 800 && p->second >= 400 && p->second <= 900) {
            t.setup.window_w = p->first;
            t.setup.window_h = p->second;
        }
        if (auto v = num(su->find("margin")); v && *v >= 8 && *v <= 48) t.setup.margin = *v;
        if (auto v = num(su->find("logo_w")); v && *v >= 160 && *v <= 400) t.setup.logo_w = *v;
        if (auto v = num(su->find("logo_h")); v && *v >= 24 && *v <= 96) t.setup.logo_h = *v;
        if (auto v = num(su->find("row_h")); v && *v >= 32 && *v <= 64) t.setup.row_h = *v;
        if (auto v = num(su->find("progress_h")); v && *v >= 2 && *v <= 16) t.setup.progress_h = *v;
        if (auto v = num(su->find("step_dot")); v && *v >= 6 && *v <= 16) t.setup.step_dot = *v;
    }
    // Siki yerlesim olculeri (spec 2026-11-11 §2). Aralik disi deger yok sayilir (varsayilan kalir).
    if (const auto* dn = root.find("density")) {
        if (auto v = num(dn->find("page_margin")); v && *v >= 0 && *v <= 48) t.page_margin = *v;
        if (auto v = num(dn->find("card_gap")); v && *v >= 0 && *v <= 32) t.card_gap = *v;
        if (auto v = num(dn->find("card_padding")); v && *v >= 4 && *v <= 32) t.card_pad = *v;
        if (auto v = num(dn->find("header_strip_h")); v && *v >= 24 && *v <= 64) t.header_h = *v;
        if (auto p = pair_of(dn->find("table_row")); p && p->first >= 24 && p->second >= p->first) {
            t.row_h = p->first;
            t.row2_h = p->second;
        }
        if (auto v = num(dn->find("content_max_width")); v && *v >= 640 && *v <= 4096) t.content_max_w = *v;
    }
    if (const auto* c = root.find("controls")) {
        if (auto p = pair_of(c->find("toggle"))) { t.toggle_w = p->first; t.toggle_h = p->second; }
        if (auto p = pair_of(c->find("hero_toggle"))) { t.hero_toggle_w = p->first; t.hero_toggle_h = p->second; }
        if (auto v = num(c->find("button_height"))) t.button_h = *v;
        if (auto v = num(c->find("button_height_inline"))) t.button_row_h = *v;
        if (auto v = num(c->find("button_min_width"))) t.button_min_w = *v;
        if (auto v = num(c->find("touch_hit_target_min"))) t.touch_min = *v;
        if (auto v = num(c->find("control_height_max")); v && *v >= 24) t.control_max_h = *v;
        if (auto v = num(c->find("keyboard_focus_ring_width")); v && *v >= 1 && *v <= 4) t.focus_ring_w = *v;
        if (auto p = pair_of(c->find("sidebar_width"))) { t.sidebar_min = p->first; t.sidebar_max = p->second; }
        if (auto p = pair_of(c->find("min_window"))) { t.min_window_w = p->first; t.min_window_h = p->second; }
        if (const auto* sa = c->find("strip_animation"); sa && sa->type() == core::json::Value::Type::Bool)
            t.strip_animation = sa->as_bool();
    }
    if (const auto* m = root.find("motion_ms")) {
        auto ms = [&](const char* k, int& out) {
            if (auto v = num(m->find(k))) out = static_cast<int>(*v);
        };
        ms("hover", t.motion.hover);
        ms("press", t.motion.press);
        ms("toggle", t.motion.toggle);
        ms("page", t.motion.page);
        ms("details", t.motion.details);
        ms("dialog_open", t.motion.dialog_open);
        ms("dialog_close", t.motion.dialog_close);
        ms("status", t.motion.status);
        ms("optional_logo_reveal", t.motion.logo_reveal);
        ms("live_value", t.motion.live_value);
        if (auto v = num(m->find("value_ease")); v && *v >= 0 && *v <= 1000) t.motion.value_ease = static_cast<int>(*v);
        if (auto v = num(m->find("count")); v && *v >= 0 && *v <= 300) t.motion.count = static_cast<int>(*v);  // spec F: <= 300 ms
        if (auto v = num(m->find("breathe_period")); v && *v >= 1000 && *v <= 20000) t.motion.breathe_period = static_cast<int>(*v);
        if (auto v = num(m->find("heartbeat_period")); v && *v >= 600 && *v <= 10000) t.motion.heartbeat_period = static_cast<int>(*v);
        if (auto v = num(m->find("card_lift")); v && *v >= 0 && *v <= 1000) t.motion.card_lift = static_cast<int>(*v);
        if (auto v = num(m->find("pulse_period")); v && *v >= 600 && *v <= 10000) t.motion.pulse_period = static_cast<int>(*v);
        if (auto v = num(m->find("ring_fill")); v && *v >= 0 && *v <= 2000) t.motion.ring_fill = static_cast<int>(*v);
        if (auto v = num(m->find("result_count")); v && *v >= 0 && *v <= 700) t.motion.result_count = static_cast<int>(*v);  // spec C: <= 700 ms
        if (auto v = num(m->find("bars_fill")); v && *v >= 0 && *v <= 2000) t.motion.bars_fill = static_cast<int>(*v);
        if (auto v = num(m->find("cleanup_ring")); v && *v >= 0 && *v <= 2000) t.motion.cleanup_ring = static_cast<int>(*v);
        if (auto v = num(m->find("row_expand")); v && *v >= 0 && *v <= 1000) t.motion.row_expand = static_cast<int>(*v);
        if (auto v = num(m->find("compare_bars")); v && *v >= 0 && *v <= 2000) t.motion.compare_bars = static_cast<int>(*v);
        if (auto v = num(m->find("strip_period")); v && *v >= 1000 && *v <= 60000) t.strip_period_ms = static_cast<int>(*v);
    }
    if (const auto* e = root.find("easing"); e && e->items().size() == 4)
        for (std::size_t i = 0; i < 4; ++i)
            if (auto v = num(&e->items()[i])) t.easing[i] = *v;
    if (const auto* c = root.find("continuous_idle_animation"); c && c->type() == core::json::Value::Type::Bool)
        t.continuous_idle_animation = c->as_bool();
    return t;
}

std::string_view embedded_tokens_json() noexcept { return embedded_data::tokens; }

float clamp_text_scale(float factor) noexcept {
    if (!(factor >= 1.f)) return 1.f;  // NaN / < 1 dahil
    return factor > kMaxTextScale ? kMaxTextScale : factor;
}

Theme with_text_scale(const Theme& base, float factor) {
    const float f = clamp_text_scale(factor);
    Theme t = base;
    if (f == 1.f) return t;
    // Yazi boyutu oranla buyur (DirectWrite kesirli DIP'i destekler); satir yuksekligi ise TAM DIP'e (asagi) yuvarlanir.
    // Neden (P9-B6): 18 x 1,25 = 22,5 gibi kesirli satir, ust uste binen metin bloklarinin altindaki dugmeyi y = 469,5 gibi
    // yarim DIP'e koyuyordu; dugme kenari ve etiket taban cizgisi farkli yone yuvarlanir, %100 olcekte "Yes" etiketi
    // kutuda 1 DIP asagi kaliyordu (BUTTON_RULE 14,5 / 12,5). Tam sayi satirla yerlesim y'leri tam sayi kalir. Satir hicbir
    // zaman tasarim satirinin altina inmez.
    for (auto& r : t.type) {
        r.size *= f;
        r.line = std::max(r.line, std::floor(r.line * f));
        r.tracking *= f;
    }
    // Metin tasiyan denetimler satir yuksekligi farki kadar buyur: tasarimdaki ic bosluk aynen kalir, etiket dikeyde tam
    // ortada kalir. Iki satirli olcu iki kat buyur. Fark cift sayiya yukari yuvarlanir (ust-alt bosluk esit kalsin); govde
    // satiri tam sayi oldugundan 18 -> 22 icin fark 4'tur (onceden 22,5 -> 6: dugme 1,5 DIP fazla uzuyordu).
    const float d = 2.f * std::ceil((t.role(Role::Body).line - base.role(Role::Body).line) / 2.f);
    t.button_h += d, t.button_row_h += d, t.row_h += d, t.header_h += d, t.topbar_tab_h += d;
    t.control_max_h += d;  // tek denetim tek satir metin tasir
    t.row2_h += 2 * d;
    return t;
}

const Theme& default_theme() {
    static const Theme theme = [] {
        auto r = parse_theme(embedded_data::tokens);
        return r.ok() ? r.value() : Theme{};
    }();
    return theme;
}

}  // namespace ef::ui
