#include "ui/brand/svg_path.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace ef::ui {

using contracts::ErrorCode;
using R = contracts::Result<SvgDoc>;

namespace {

struct Xform {
    float s = 1, tx = 0, ty = 0;  // nokta' = s * nokta + t
    Xform then(const Xform& inner) const { return {s * inner.s, s * inner.tx + tx, s * inner.ty + ty}; }
};

std::optional<std::string_view> attr(std::string_view tag, std::string_view name) {
    std::size_t pos = 0;
    while (true) {
        pos = tag.find(name, pos);
        if (pos == std::string_view::npos) return std::nullopt;
        const bool boundary = pos == 0 || std::isspace(static_cast<unsigned char>(tag[pos - 1]));
        const std::size_t eq = pos + name.size();
        if (boundary && eq + 1 < tag.size() && tag[eq] == '=' && tag[eq + 1] == '"') {
            const std::size_t end = tag.find('"', eq + 2);
            if (end == std::string_view::npos) return std::nullopt;
            return tag.substr(eq + 2, end - eq - 2);
        }
        pos = eq;
    }
}

// Sayi okuyucu: bosluk/virgul atlar, isaretli ondalik.
bool read_num(std::string_view s, std::size_t& i, float& out) {
    while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    if (i >= s.size()) return false;
    const char* b = s.data() + i;
    const auto r = std::from_chars(b, s.data() + s.size(), out);
    if (r.ec != std::errc{}) return false;
    i = static_cast<std::size_t>(r.ptr - s.data());
    return true;
}

// "translate(a b) scale(c)" -> Xform. Baska donusum: hata.
std::optional<Xform> parse_transform(std::string_view t) {
    Xform x;
    std::size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && t[i] == ' ') ++i;
        if (i >= t.size()) break;
        if (t.substr(i, 10) == "translate(") {
            i += 10;
            float a = 0, b = 0;
            if (!read_num(t, i, a)) return std::nullopt;
            std::size_t j = i;
            if (!read_num(t, j, b)) b = 0; else i = j;
            while (i < t.size() && t[i] == ' ') ++i;
            if (i >= t.size() || t[i] != ')') return std::nullopt;
            ++i;
            x = x.then(Xform{1, a, b});
        } else if (t.substr(i, 6) == "scale(") {
            i += 6;
            float a = 0;
            if (!read_num(t, i, a)) return std::nullopt;
            while (i < t.size() && t[i] == ' ') ++i;
            if (i >= t.size() || t[i] != ')') return std::nullopt;  // yalniz tekdüze olcek
            ++i;
            x = x.then(Xform{a, 0, 0});
        } else {
            return std::nullopt;
        }
    }
    return x;
}

std::optional<std::vector<SvgCmd>> parse_d(std::string_view d, const Xform& xf) {
    std::vector<SvgCmd> out;
    std::size_t i = 0;
    float cx = 0, cy = 0;  // gecerli nokta (yerel)
    char op = 0;
    auto P = [&](float x, float y, char o, float qx = 0, float qy = 0) {
        out.push_back({o, xf.s * x + xf.tx, xf.s * y + xf.ty, xf.s * qx + xf.tx, xf.s * qy + xf.ty});
    };
    while (true) {
        while (i < d.size() && (d[i] == ' ' || d[i] == ',')) ++i;
        if (i >= d.size()) break;
        const char c = d[i];
        if (std::isalpha(static_cast<unsigned char>(c))) {
            if (!std::strchr("MLHVQZ", c)) return std::nullopt;  // goreli/egri disi komut yok
            op = c;
            ++i;
            if (op == 'Z') {
                out.push_back({'Z'});
                continue;
            }
        } else if (op == 0 || op == 'Z') {
            return std::nullopt;
        }
        float a = 0, b = 0, e = 0, f = 0;
        switch (op) {
            case 'M':
            case 'L':
                if (!read_num(d, i, a) || !read_num(d, i, b)) return std::nullopt;
                cx = a, cy = b;
                P(a, b, op);
                if (op == 'M') op = 'L';  // ardisik koordinatlar L sayilir
                break;
            case 'H':
                if (!read_num(d, i, a)) return std::nullopt;
                cx = a;
                P(cx, cy, 'L');
                break;
            case 'V':
                if (!read_num(d, i, a)) return std::nullopt;
                cy = a;
                P(cx, cy, 'L');
                break;
            case 'Q':
                if (!read_num(d, i, a) || !read_num(d, i, b) || !read_num(d, i, e) || !read_num(d, i, f)) return std::nullopt;
                cx = e, cy = f;
                P(e, f, 'Q', a, b);
                break;
            default: return std::nullopt;
        }
    }
    if (out.empty() || out.front().op != 'M') return std::nullopt;
    return out;
}

}  // namespace

R parse_svg(std::string_view text) {
    SvgDoc doc;
    struct Frame {
        Xform xf;
        std::optional<Color> fill;
        bool fill_set = false;
        bool evenodd = false;  // SVG varsayilani nonzero
    };
    // fill / fill-rule ozniteligi (g ve path ayni kural; path g'yi ezer). Hata: desteklenmeyen deger.
    const auto paint_attrs = [](std::string_view tag, Frame& f) -> bool {
        if (const auto fl = attr(tag, "fill")) {
            if (*fl == "currentColor") f.fill = std::nullopt;
            else {
                const auto c = Color::from_hex(*fl);
                if (!c) return false;
                f.fill = *c;
            }
            f.fill_set = true;
        }
        if (const auto fr = attr(tag, "fill-rule")) {
            if (*fr == "evenodd") f.evenodd = true;
            else if (*fr == "nonzero") f.evenodd = false;
            else return false;
        }
        return true;
    };
    std::vector<Frame> stack{{}};
    bool have_svg = false;
    std::size_t i = 0;
    while ((i = text.find('<', i)) != std::string_view::npos) {
        const std::size_t end = text.find('>', i);
        if (end == std::string_view::npos) return R::fail(ErrorCode::ParseError, "unterminated tag");
        const std::string_view tag = text.substr(i + 1, end - i - 1);
        i = end + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;
        if (tag.substr(0, 4) == "svg ") {
            const auto vb = attr(tag, "viewBox");
            if (!vb) return R::fail(ErrorCode::ParseError, "viewBox");
            std::size_t k = 0;
            float x0 = 0, y0 = 0, w = 0, h = 0;
            if (!read_num(*vb, k, x0) || !read_num(*vb, k, y0) || !read_num(*vb, k, w) || !read_num(*vb, k, h) || w <= 0 || h <= 0 ||
                x0 != 0 || y0 != 0)
                return R::fail(ErrorCode::ParseError, "viewBox");
            doc.view_w = w, doc.view_h = h;
            have_svg = true;
        } else if (tag.substr(0, 2) == "g " || tag == "g") {
            Frame f = stack.back();
            if (const auto t = attr(tag, "transform")) {
                const auto x = parse_transform(*t);
                if (!x) return R::fail(ErrorCode::Unsupported, "transform");
                f.xf = f.xf.then(*x);
            }
            if (!paint_attrs(tag, f)) return R::fail(ErrorCode::Unsupported, "fill");
            stack.push_back(f);
        } else if (tag == "/g") {
            if (stack.size() <= 1) return R::fail(ErrorCode::ParseError, "unbalanced g");
            stack.pop_back();
        } else if (tag.substr(0, 5) == "path ") {
            const auto d = attr(tag, "d");
            if (!d) return R::fail(ErrorCode::ParseError, "d");
            Xform xf = stack.back().xf;
            if (const auto t = attr(tag, "transform")) {
                const auto x = parse_transform(*t);
                if (!x) return R::fail(ErrorCode::Unsupported, "transform");
                xf = xf.then(*x);
            }
            Frame pf = stack.back();
            if (!paint_attrs(tag, pf)) return R::fail(ErrorCode::Unsupported, "fill");
            if (!pf.fill_set) return R::fail(ErrorCode::Unsupported, "fill missing");
            const auto cmds = parse_d(*d, xf);
            if (!cmds) return R::fail(ErrorCode::Unsupported, "path data");
            doc.shapes.push_back({pf.fill, *cmds, pf.evenodd});
        }
        // <title>, </svg>, </path> vb. yok sayilir.
    }
    if (!have_svg || doc.shapes.empty()) return R::fail(ErrorCode::ParseError, "no shapes");
    bool first = true;
    for (const auto& s : doc.shapes)
        for (const auto& c : s.cmds) {
            if (c.op == 'Z') continue;
            for (int k = 0; k < (c.op == 'Q' ? 2 : 1); ++k) {
                const float x = k ? c.cx : c.x, y = k ? c.cy : c.y;
                if (first) {
                    doc.min_x = doc.max_x = x, doc.min_y = doc.max_y = y;
                    first = false;
                }
                doc.min_x = std::min(doc.min_x, x), doc.max_x = std::max(doc.max_x, x);
                doc.min_y = std::min(doc.min_y, y), doc.max_y = std::max(doc.max_y, y);
            }
        }
    return doc;
}

}  // namespace ef::ui
