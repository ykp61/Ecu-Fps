#include "core/guided/undervolt_guide.h"

#include <algorithm>
#include <cctype>

namespace ef::core::guided {

namespace {

std::string lower(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// Harf/rakam/'-' dizileri; "(R)" "(TM)" "@" gibi isaretler ayirici sayilir.
std::vector<std::string> tokens(const std::string& l) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : l) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
            cur.push_back(c);
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    std::erase(out, std::string("r"));
    std::erase(out, std::string("tm"));
    return out;
}

bool all_digits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }); }
bool has_tok(const std::vector<std::string>& t, std::string_view w) { return std::find(t.begin(), t.end(), w) != t.end(); }

// "13700kf" -> sayi "13700", ek "kf". Sayi yoksa false.
bool split_num(std::string_view s, std::string& num, std::string& suffix) {
    std::size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    if (i == 0) return false;
    num = std::string(s.substr(0, i));
    suffix = std::string(s.substr(i));
    return true;
}

// Intel XTU listesi (K, KF, HK, X, XE) + UVP makalesindeki KS.
bool intel_unlocked(std::string_view sfx) { return sfx == "k" || sfx == "kf" || sfx == "ks" || sfx == "hk" || sfx == "x" || sfx == "xe"; }

struct IntelModel {
    bool ok = false;
    std::string model, suffix;
    int gen = 0;  // 0: bilinmiyor
};

IntelModel parse_intel(const std::vector<std::string>& t) {
    IntelModel m;
    for (std::size_t k = 0; k < t.size(); ++k) {
        const auto& s = t[k];
        // Core i3/i5/i7/i9-NNNN(N)<ek>
        if (s.size() > 3 && s[0] == 'i' && (s[1] == '3' || s[1] == '5' || s[1] == '7' || s[1] == '9') && s[2] == '-') {
            std::string num, sfx;
            if (!split_num(std::string_view(s).substr(3), num, sfx)) continue;
            if (num.size() == 5) m.gen = std::stoi(num.substr(0, 2));
            else if (num.size() == 4) m.gen = num[0] - '0';
            else continue;
            m.ok = true;
            m.suffix = sfx;
            m.model = "i" + std::string(1, s[1]) + "-" + num + upper(sfx);
            return m;
        }
        // Core Ultra 5/7/9 NNN<ek> (12. nesilden yeni aile)
        if (s == "ultra" && k + 2 < t.size() && all_digits(t[k + 1]) && t[k + 1].size() == 1) {
            std::string num, sfx;
            if (!split_num(t[k + 2], num, sfx) || num.size() != 3) continue;
            m.ok = true;
            m.gen = 99;
            m.suffix = sfx;
            m.model = "Ultra " + t[k + 1] + " " + num + upper(sfx);
            return m;
        }
    }
    return m;
}

struct AmdModel {
    bool ok = false, ai = false;
    std::string model, suffix;
};

AmdModel parse_amd(const std::vector<std::string>& t) {
    AmdModel m;
    for (std::size_t k = 0; k < t.size(); ++k) {
        if (t[k] != "ryzen") continue;
        std::size_t j = k + 1;
        if (j < t.size() && t[j] == "threadripper") return m;  // ayri aile; bu rehberde veri yok
        if (j < t.size() && t[j] == "ai") {
            m.ai = true;
            ++j;
        }
        if (j >= t.size() || !all_digits(t[j]) || t[j].size() != 1) return m;
        const std::string tier = t[j++];
        if (j < t.size() && (t[j] == "pro" || t[j] == "hx")) {
            if (t[j] == "hx") m.suffix = "hx";
            ++j;
        }
        if (j >= t.size()) return m;
        std::string num, sfx;
        if (!split_num(t[j], num, sfx) || num.size() < 3 || num.size() > 4) return m;
        if (m.suffix.empty()) m.suffix = sfx;
        m.ok = true;
        m.model = std::string("Ryzen ") + (m.ai ? "AI " : "") + tier + " " + num + upper(sfx);
        return m;
    }
    return m;
}

bool amd_mobile(const AmdModel& m) {
    const auto& s = m.suffix;
    if (s.rfind("hx", 0) == 0 || s.rfind("hs", 0) == 0 || s.rfind("h", 0) == 0 || s.rfind("u", 0) == 0) return true;
    return m.ai && s.find('g') == std::string::npos;  // Ryzen AI cogunlukla mobil; masaustu "G" modelleri haric
}

void push(UndervoltGuide& g, const char* k) { g.step_keys.emplace_back(k); }

}  // namespace

std::string_view verdict_text_key(UndervoltVerdict v) noexcept {
    switch (v) {
        case UndervoltVerdict::MayBeAdjustable: return "undervolt.verdict.adjustable";
        case UndervoltVerdict::Locked: return "undervolt.verdict.locked";
        case UndervoltVerdict::NotSupported: return "undervolt.verdict.not_supported";
        case UndervoltVerdict::Unknown: return "undervolt.verdict.unknown";
    }
    return "undervolt.verdict.unknown";
}

UndervoltGuide undervolt_guide_for(std::string_view cpu_vendor, std::string_view cpu_brand, bool is_laptop) {
    UndervoltGuide g;
    const auto lv = lower(cpu_vendor), lb = lower(cpu_brand);
    const bool intel = lv.find("intel") != std::string::npos || (lv.empty() && lb.find("intel") != std::string::npos);
    const bool amd = !intel && (lv.find("amd") != std::string::npos || lv.find("advanced micro") != std::string::npos ||
                                (lv.empty() && lb.find("amd") != std::string::npos));
    const auto t = tokens(lb);
    push(g, "undervolt.step.no_auto");

    if (intel) {
        g.vendor_key = "intel";
        if (has_tok(t, "celeron") || has_tok(t, "atom") || has_tok(t, "xeon")) {
            g.verdict = UndervoltVerdict::NotSupported;  // XTU: sunucu, Celeron, Atom desteklenmez
            push(g, "undervolt.step.no_bypass");
        } else if (const auto m = parse_intel(t); m.ok) {
            g.model = m.model;
            g.uvp_applies = m.gen >= 12;
            if (intel_unlocked(m.suffix)) {
                g.verdict = UndervoltVerdict::MayBeAdjustable;
                g.tool_key = "undervolt.tool.xtu";
                if (g.uvp_applies) push(g, "undervolt.step.uvp");
                if (is_laptop) push(g, "undervolt.step.laptop_oem");
                push(g, "undervolt.step.note");
                push(g, "undervolt.step.small");
                push(g, "undervolt.step.stability");
                push(g, "undervolt.step.warranty");
            } else if (m.suffix == "hx") {
                g.verdict = UndervoltVerdict::Unknown;  // XTU listesinde yok; resmi kaynak bulunmadi
                push(g, "undervolt.step.laptop_oem");
            } else {
                g.verdict = UndervoltVerdict::Locked;
                push(g, "undervolt.step.no_bypass");
                if (g.uvp_applies) push(g, "undervolt.step.uvp");
            }
        } else {
            g.verdict = UndervoltVerdict::Unknown;
            if (is_laptop) push(g, "undervolt.step.laptop_oem");
        }
    } else if (amd) {
        g.vendor_key = "amd";
        const auto m = parse_amd(t);
        g.model = m.model;
        if (m.ok && !is_laptop && !amd_mobile(m)) {
            g.verdict = UndervoltVerdict::MayBeAdjustable;
            g.tool_key = "undervolt.tool.amd";
            push(g, "undervolt.step.amd_check");
            push(g, "undervolt.step.note");
            push(g, "undervolt.step.small");
            push(g, "undervolt.step.stability");
            push(g, "undervolt.step.amd_terms");
        } else {
            g.verdict = UndervoltVerdict::Unknown;  // mobil / taninmayan: destek dogrulanamaz
            if (is_laptop || (m.ok && amd_mobile(m))) push(g, "undervolt.step.laptop_oem");
        }
    }
    g.verdict_key = std::string(verdict_text_key(g.verdict));
    g.auto_apply = kAutoTuneAvailable;
    return g;
}

StabilityClaim evaluate_stability(const StabilityInput& in) noexcept {
    if (in.failures > 0 || in.whea_or_crash) return StabilityClaim::FailureSeen;
    if (in.passes <= 0) return StabilityClaim::NoData;
    return StabilityClaim::NoFailureSeen;
}

std::string_view stability_text_key(StabilityClaim c) noexcept {
    switch (c) {
        case StabilityClaim::NoData: return "undervolt.stability.no_data";
        case StabilityClaim::FailureSeen: return "undervolt.stability.failed";
        case StabilityClaim::NoFailureSeen: return "undervolt.stability.no_failure";
    }
    return "undervolt.stability.no_data";
}

}  // namespace ef::core::guided
