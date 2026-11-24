#include "core/readiness/readiness.h"

#include "core/adapters/windows/display_process.h"
#include "core/adapters/windows/dns.h"

#include <algorithm>

namespace ef::core::readiness {

namespace {
int level_rank(Level l) {
    switch (l) {
        case Level::Problem: return 0;
        case Level::Advice: return 1;
        case Level::Unknown: return 2;
        case Level::Info: return 3;
        case Level::Good: return 4;
    }
    return 5;
}
}  // namespace

bool action_uri_allowed(std::string_view u) {
    if (u.size() > 512) return false;
    auto charset_ok = [](std::string_view s, bool path) {
        for (char c : s) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                            (path && (c == '/' || c == '_' || c == '%'));
            if (!ok) return false;
        }
        return true;
    };
    if (u.rfind("ms-settings:", 0) == 0) {
        const auto rest = u.substr(12);
        return !rest.empty() && charset_ok(rest, false);
    }
    if (u == "windowsdefender://coreisolation") return true;
    if (u == "devmgmt.msc") return true;  // T17: Aygit Yoneticisi (yalniz acar; surucu islemi yok)
    if (u.rfind("https://", 0) != 0) return false;
    const auto rest = u.substr(8);
    const auto slash = rest.find('/');
    const auto host = rest.substr(0, slash);
    const auto path = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
    if (!charset_ok(path, true)) return false;  // sorgu dizesi/parca (? # &) yok
    static constexpr std::string_view kHosts[] = {"nvidia.com", "amd.com", "intel.com", "microsoft.com", "aka.ms", "waveform.com",
                                                  "asus.com", "msi.com", "gigabyte.com", "asrock.com", "prosettings.net",
                                                  "ecufast.asilpolat.com"};  // T20: yasal sayfalar (kullanici tiklayinca)
    for (const auto h : kHosts) {
        if (host == h) return true;
        if (host.size() > h.size() + 1 && host.substr(host.size() - h.size()) == h && host[host.size() - h.size() - 1] == '.' &&
            charset_ok(host, false))
            return true;
    }
    return false;
}

std::string fallback_settings_uri(std::string_view k) {
    if (k == "fix.refresh_rate") return "ms-settings:display-advanced";
    if (k == "fix.game_mode") return "ms-settings:gaming-gamemode";
    if (k == "fix.game_dvr") return "ms-settings:gaming-gamedvr";
    if (k == "fix.windowed_opt" || k == "fix.hags") return "ms-settings:display-advancedgraphics";
    if (k == "fix.power") return "ms-settings:powersleep";
    return {};
}

std::string fix_target_for_check(std::string_view id) {
    // App::run_auto_fix / run_admin_fix ile ayni hedefler (tek islem, "fixes" kapsami).
    if (id == "windows.game_mode") return "gaming:game_mode";
    if (id == "windows.game_dvr") return "gaming:dvr_background";
    if (id == "windows.windowed_opt") return "gaming:windowed_opt";
    if (id == "windows.vrr") return "gaming:windows_vrr";
    if (id == "windows.hags") return "hags:hwschmode";
    if (id == "windows.fast_startup") return "faststartup:hiberboot";
    if (id.rfind("network.dns.", 0) == 0) {  // EF-X14: satir kimligi arayuz GUID'ini tasir
        const std::string t = "dns:" + std::string(id.substr(12));
        return adapters::windows::parse_dns_target(t) ? t : std::string();
    }
    if (id.rfind("network.dns_bench.fix.", 0) == 0) {  // T10: karsilastirma sonucu satiri ayni arayuz hedefine gider
        const std::string t = "dns:" + std::string(id.substr(22));
        return adapters::windows::parse_dns_target(t) ? t : std::string();
    }
    constexpr std::string_view pre = "display.", suf = ".refresh";
    if (id.size() > pre.size() + suf.size() && id.substr(0, pre.size()) == pre && id.substr(id.size() - suf.size()) == suf) {
        const auto idx = id.substr(pre.size(), id.size() - pre.size() - suf.size());
        for (const char ch : idx)
            if (ch < '0' || ch > '9') return {};
        return "display:" + std::string(idx) + ":refresh";
    }
    return {};
}

std::vector<std::string> fix_targets_for_check(std::string_view id) {
    // F-73: arka plan kaydi tek "Duzelt"te uc anahtari kapatir; ucunun kaydi ayni transaction'da tutulur.
    if (id == "windows.game_dvr") return {"gaming:dvr_background", "gaming:app_capture", "gaming:dvr_enabled"};
    auto t = fix_target_for_check(id);
    if (t.empty()) return {};
    return {std::move(t)};
}

std::vector<std::string> fix_targets_for_check(const Check& c) {
    auto ts = fix_targets_for_check(c.id);
    // B6 inceleme: yenileme hizi satiri kendi GDI aygit adini tasir; hedef tam ekli bicim olur (konum numarasi
    // QueryDisplayConfig sirasindan gelir ve ekran cikarilinca/klon degisince kayabilir).
    if (ts.size() == 1 && ts[0].rfind("display:", 0) == 0) {
        const auto dev = c.args.find("device");
        if (dev != c.args.end()) {
            const auto ref = adapters::windows::parse_display_target(ts[0]);
            if (ref.ok()) {
                auto suffixed = adapters::windows::format_display_target(ref.value().index, dev->second);
                if (!suffixed.empty()) ts[0] = std::move(suffixed);
            }
        }
    }
    return ts;
}

std::vector<bool> undo_rows(const std::vector<Check>& checks, const std::vector<std::string>& active) {
    const auto st = undo_states(checks, FixTargets{active, {}});
    std::vector<bool> out(st.size(), false);
    for (std::size_t i = 0; i < st.size(); ++i) out[i] = st[i] != UndoState::None;
    return out;
}

bool fix_target_matches(std::string_view check_target, std::string_view op_target) {
    if (check_target == op_target) return true;
    if (check_target.rfind("display:", 0) != 0) return false;
    // B6 inceleme: satir aygit ekli ise aygit kimligi esas alinir: ayni GDI adli kayit (konum numarasi kaymis
    // olsa da; geri alma GDI adiyla dogru ekrana gider) veya aygit bilgisi olmayan eski kayit (ayni konum).
    if (const auto cref = adapters::windows::parse_display_target(check_target); cref.ok() && !cref.value().gdi.empty()) {
        const auto oref = adapters::windows::parse_display_target(op_target);
        if (!oref.ok()) return false;
        if (!oref.value().gdi.empty()) return oref.value().gdi == cref.value().gdi;
        return oref.value().index == cref.value().index;
    }
    // Eski (eksiz) satir hedefi: yalniz konum numarasi eslesir.
    if (op_target.size() <= check_target.size() + 1) return false;
    if (op_target.substr(0, check_target.size()) != check_target || op_target[check_target.size()] != ':') return false;
    const auto ref = adapters::windows::parse_display_target(op_target);
    return ref.ok() && !ref.value().gdi.empty();
}

std::vector<UndoState> undo_states(const std::vector<Check>& checks, const FixTargets& targets) {
    std::vector<UndoState> out(checks.size(), UndoState::None);
    for (std::size_t i = 0; i < checks.size(); ++i) {
        const auto ts = fix_targets_for_check(checks[i]);
        if (ts.empty()) continue;
        auto in = [&ts](const std::vector<std::string>& v) {
            return std::any_of(ts.begin(), ts.end(), [&v](const std::string& t) {
                return std::any_of(v.begin(), v.end(), [&t](const std::string& o) { return fix_target_matches(t, o); });
            });
        };
        if (in(targets.conflict)) out[i] = UndoState::Conflict;
        else if (in(targets.recovery)) out[i] = UndoState::Retry;
        else if (in(targets.interrupted)) out[i] = UndoState::Interrupted;
        else if (in(targets.active)) out[i] = UndoState::Undo;
    }
    return out;
}

std::string pro_settings_slug(std::string_view launcher, std::string_view app_id, std::string_view name) {
    if (launcher == "steam") {
        static constexpr std::pair<std::string_view, std::string_view> kSteam[] = {
            {"730", "cs2"},           {"570", "dota-2"},       {"1172470", "apex-legends"}, {"578080", "pubg"},
            {"359550", "rainbow-six"}, {"2767030", "marvel-rivals"}, {"1422450", "deadlock"}, {"2357570", "overwatch"},
            {"1938090", "call-of-duty-warzone"},
        };
        for (const auto& [id, slug] : kSteam)
            if (app_id == id) return std::string(slug);
    }
    if (launcher == "epic" && (app_id == "Fortnite" || name == "Fortnite")) return "fortnite";
    return {};
}

ReadinessSummary summarize_checks(const std::vector<Check>& v) noexcept {
    ReadinessSummary s;
    for (const auto& c : v) {
        switch (c.level) {
            case Level::Good: ++s.verified; break;
            case Level::Problem: s.has_problem = true; [[fallthrough]];
            case Level::Advice: ++s.action; break;
            case Level::Unknown: ++s.unknown; break;
            case Level::Info: continue;
        }
        ++s.total;
    }
    return s;
}

void group_latency_checks(std::vector<Check>& v) {
    static constexpr std::string_view kIds[] = {"display.vrr", "display.vrr_sync", "windows.vrr", "windows.hags", "windows.windowed_opt", "graphics.gsync", "graphics.vrr_vendor"};
    bool any = false;
    const auto per_display_vrr = [](const std::string& id) {
        return id.rfind("display.", 0) == 0 && id.size() > 12 && id.compare(id.size() - 4, 4, ".vrr") == 0;  // display.<n>.vrr
    };
    for (auto& c : v)
        if (per_display_vrr(c.id) || std::find(std::begin(kIds), std::end(kIds), c.id) != std::end(kIds)) {
            c.section = Section::Latency;
            any = true;
        }
    if (!any) return;
    Check g;
    g.id = "latency.guide";
    g.section = Section::Latency;
    g.level = Level::Info;
    g.text_key = "chk.latency.guide";
    v.push_back(std::move(g));
}

void sort_checks(std::vector<Check>& v) {
    std::stable_sort(v.begin(), v.end(), [](const Check& a, const Check& b) {
        if (a.section != b.section) return static_cast<int>(a.section) < static_cast<int>(b.section);
        return level_rank(a.level) < level_rank(b.level);
    });
}

}  // namespace ef::core::readiness
