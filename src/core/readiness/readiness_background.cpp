// F-79: overlay/yakalama, izleme ve RGB yazilimi denetimi (saf; Win32 yok, birim testlenir).
// Kaynak: platform/windows/readiness/background_reader (calisan surec listesi; salt okuma).
// Ilkeler:
//  - Bir surecin calismasi oyun karesine karistigi veya FPS dusurdugu KANITI degildir; metin
//    "aday olabilir" der ve olculmus sayi vermez.
//  - Fan/sogutma egrisi RGB/vendor yazilimina bagli olabileceginden kapatma degil, animasyonu
//    statik renge alma onerilir.
//  - Hicbir surec kapatilmaz/yazilmaz; AutoFix yoktur.
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::string lower_basename(std::string_view path) {
    const auto slash = path.find_last_of("\\/");
    std::string out(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

template <std::size_t N>
bool matches(std::string_view exe, const std::array<std::string_view, N>& patterns) {
    return std::any_of(patterns.begin(), patterns.end(), [&](std::string_view p) { return contains(exe, p); });
}

void add_unique(std::vector<std::string>& v, std::string name) {
    if (std::find(v.begin(), v.end(), name) == v.end()) v.push_back(std::move(name));
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        if (!out.empty()) out += ", ";
        out += s;
    }
    return out;
}

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Software;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

}  // namespace

BackgroundStack classify_background_stack(std::vector<std::string> exe_names) {
    static constexpr std::array<std::string_view, 13> kOverlay{
        "discord.exe", "gameoverlayui", "nvidia overlay", "nvidia share", "gamebar", "xboxgamebar",
        "radeonsoftware", "amdrsserv", "rtss", "obs64", "obs32", "overwolf", "medal.exe"};
    static constexpr std::array<std::string_view, 8> kMonitoring{
        "hwinfo", "aida64", "hwmonitor", "gpuz", "afterburner", "openhardwaremonitor",
        "librehardwaremonitor", "coretemp"};
    static constexpr std::array<std::string_view, 10> kRgb{
        "icue", "armoury", "mystic", "rgbfusion", "rgb fusion", "signalrgb", "openrgb", "synapse",
        "lghub", "razer"};

    BackgroundStack out;
    for (const auto& raw : exe_names) {
        const auto exe = lower_basename(raw);
        if (exe.empty()) continue;
        if (matches(exe, kOverlay)) add_unique(out.overlays, exe);
        if (matches(exe, kMonitoring)) add_unique(out.monitoring, exe);
        if (matches(exe, kRgb)) add_unique(out.rgb, exe);
    }
    return out;
}

std::vector<Check> background_stack_checks(const BackgroundStack& b) {
    std::vector<Check> out;
    if (!b.overlays.empty()) {
        if (b.overlays.size() == 1) {
            Check c = make("background.overlay", Level::Info, "chk.background.overlay");
            c.args["list"] = join(b.overlays);
            out.push_back(std::move(c));
        } else {
            Check c = make("background.overlay_many", Level::Advice, "chk.background.overlay_many");
            c.args["list"] = join(b.overlays);
            c.args["count"] = std::to_string(b.overlays.size());
            out.push_back(std::move(c));
        }
    }
    if (!b.monitoring.empty()) {
        Check c = make("background.monitoring", Level::Advice, "chk.background.monitoring");
        c.args["list"] = join(b.monitoring);
        out.push_back(std::move(c));
    }
    if (!b.rgb.empty()) {
        Check c = make("background.rgb", Level::Advice, "chk.background.rgb");
        c.args["list"] = join(b.rgb);
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace ef::core::readiness
