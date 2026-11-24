// P6: dizustu hibrit GPU (tumlesik + ayrik) icin oyun basina Windows Grafik tercihi bulgulari. Saf: Win32 yok.
//  - Yalniz laptop == true VE hybrid == true iken bulgu uretilir; bilinmiyorsa/degilse susulur (masaustu gurultusu yok).
//  - Bu okuma salt okunurdur; Performans Acik plani (plan.gpu_route, T01) tercihi journal'li adaptorla yazar. Elle yol: Ayarlar > Ekran > Grafik.
//  - Pilde hibrit dizustu: Windows oyunu tumlesik GPU'ya yonlendirebilir; yalniz "pil acikca okunduysa" soylenir.
#include <cctype>
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {
constexpr const char* kGraphicsSettings = "ms-settings:display-advancedgraphics";
constexpr std::size_t kMaxGames = 3;

Check make(std::string id, Level l, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Graphics;
    c.level = l;
    c.text_key = std::move(key);
    return c;
}
}  // namespace

GpuPrefKind gpu_pref_from_setting(const std::optional<std::string>& value, bool read_error) {
    if (read_error) return GpuPrefKind::Unknown;
    if (!value) return GpuPrefKind::SystemDefault;
    // "GpuPreference=2;" (baska ogeler olabilir). Yalniz 0/1/2 taninir; ozel adaptor secimi gibi digerleri tahmin edilmez.
    std::size_t pos = 0;
    std::optional<GpuPrefKind> out;
    while (pos <= value->size()) {
        std::size_t end = value->find(';', pos);
        if (end == std::string::npos) end = value->size();
        const std::string item = value->substr(pos, end - pos);
        const auto eq = item.find('=');
        if (eq != std::string::npos) {
            std::string k = item.substr(0, eq), v = item.substr(eq + 1);
            for (auto* x : {&k, &v}) {
                while (!x->empty() && std::isspace(static_cast<unsigned char>(x->front()))) x->erase(x->begin());
                while (!x->empty() && std::isspace(static_cast<unsigned char>(x->back()))) x->pop_back();
                for (auto& ch : *x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (k == "gpupreference") {
                if (v == "0") out = GpuPrefKind::SystemDefault;
                else if (v == "1") out = GpuPrefKind::PowerSaving;
                else if (v == "2") out = GpuPrefKind::HighPerformance;
                else out = GpuPrefKind::Unknown;
            }
        }
        if (end == value->size()) break;
        pos = end + 1;
    }
    return out.value_or(GpuPrefKind::Unknown);
}

std::vector<Check> gpu_preference_checks(const GpuPrefInfo& g) {
    std::vector<Check> out;
    if (!g.laptop.value_or(false) || !g.hybrid.value_or(false)) return out;

    if (g.on_battery.value_or(false)) {
        auto c = make("graphics.gpu_pref.battery", Level::Advice, "chk.gpu.battery_hybrid");
        c.action = ActionKind::OpenSettings;
        c.action_arg = "ms-settings:powersleep";
        out.push_back(std::move(c));
    }
    std::size_t n = 0;
    for (const auto& game : g.games) {
        if (n >= kMaxGames) break;
        const std::string id = "graphics.gpu_pref." + std::to_string(n);
        ++n;
        const char* key = nullptr;
        Level lvl = Level::Info;
        switch (game.pref) {
            case GpuPrefKind::HighPerformance: lvl = Level::Good; key = "chk.gpu.pref_high"; break;
            case GpuPrefKind::PowerSaving: lvl = Level::Advice; key = "chk.gpu.pref_power_saving"; break;
            case GpuPrefKind::SystemDefault: lvl = Level::Advice; key = "chk.gpu.pref_default"; break;
            case GpuPrefKind::Unknown: lvl = Level::Unknown; key = "chk.gpu.pref_unknown"; break;
        }
        auto c = make(id, lvl, key);
        c.args["game"] = game.game;
        if (game.pref != GpuPrefKind::HighPerformance) {
            c.action = ActionKind::OpenSettings;
            c.action_arg = kGraphicsSettings;
        }
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace ef::core::readiness
