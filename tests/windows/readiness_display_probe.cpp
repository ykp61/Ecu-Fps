// CHK-01/02/05 Windows probu: bu PC'deki gercek ekran ve Windows oyun ayari degerlerini okur, kurallarin
// urettigi bulgulari yazdirir. Salt okuma: ayar yazmaz, pencere acmaz, yonetici gerekmez.
#include <windows.h>

#include <cstdio>
#include <string>

#include "core/readiness/readiness.h"
#include "platform/windows/readiness/display_reader.h"

using namespace ef;
namespace cr = core::readiness;

namespace {
const char* level_text(cr::Level l) {
    switch (l) {
        case cr::Level::Good: return "Good";
        case cr::Level::Advice: return "Advice";
        case cr::Level::Problem: return "Problem";
        case cr::Level::Unknown: return "Unknown";
        case cr::Level::Info: return "Info";
    }
    return "?";
}
const char* action_text(cr::ActionKind a) {
    switch (a) {
        case cr::ActionKind::None: return "-";
        case cr::ActionKind::AutoFix: return "AutoFix";
        case cr::ActionKind::OpenSettings: return "OpenSettings";
        case cr::ActionKind::OpenUrl: return "OpenUrl";
        case cr::ActionKind::RunNetworkTest: return "RunNetworkTest";
    }
    return "?";
}
std::string opt(const std::optional<bool>& b) { return b ? (*b ? "true" : "false") : "null"; }
std::string opt(const std::optional<double>& d) {
    if (!d) return "null";
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", *d);
    return b;
}
void print_checks(const std::vector<cr::Check>& v) {
    for (const auto& c : v) {
        std::printf("  [%-7s] %-24s %-34s action=%s %s", level_text(c.level), c.id.c_str(), c.text_key.c_str(),
                    action_text(c.action), c.action_arg.c_str());
        for (const auto& [k, val] : c.args) std::printf(" %s=%s", k.c_str(), val.c_str());
        std::printf("\n");
    }
}
}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    platform::readiness::DisplayReadDiag diag;
    const auto displays = platform::readiness::read_displays(diag);
    std::printf("QueryDisplayConfig flags=0x%02x status=%ld virtual_refresh_aware=%d paths=%zu\n", diag.qdc_flags,
                diag.qdc_status, diag.virtual_refresh_aware ? 1 : 0, displays.size());
    for (const auto& d : displays) {
        std::printf("display[%u] name=\"%s\" primary=%d res=%ux%u hz_current=%s hz_max_same_res=%s dynamic=%d "
                    "hdr_supported=%s hdr_enabled=%s vrr_supported=%s\n",
                    d.index, d.name.c_str(), d.primary ? 1 : 0, d.width, d.height, opt(d.hz_current).c_str(),
                    opt(d.hz_max_same_res).c_str(), d.dynamic_refresh ? 1 : 0, opt(d.hdr_supported).c_str(),
                    opt(d.hdr_enabled).c_str(), opt(d.vrr_supported).c_str());
    }
    std::printf("display_checks:\n");
    print_checks(cr::display_checks(displays));

    const auto w = platform::readiness::read_windows_gaming();
    std::printf("windows: game_mode=%s game_dvr_background=%s windowed_optimizations=%s windows_vrr=%s "
                "hags_supported=%s hags_enabled=%s fast_startup=%s vbs_running=%s hvci_enabled=%s power_mode=%s\n",
                opt(w.game_mode).c_str(), opt(w.game_dvr_background).c_str(), opt(w.windowed_optimizations).c_str(),
                opt(w.windows_vrr).c_str(), opt(w.hags_supported).c_str(), opt(w.hags_enabled).c_str(),
                opt(w.fast_startup).c_str(), opt(w.vbs_running).c_str(), opt(w.hvci_enabled).c_str(),
                w.power_mode ? w.power_mode->c_str() : "null");
    std::printf("windows_checks:\n");
    print_checks(cr::windows_checks(w));
    return diag.qdc_status == 0 ? 0 : 1;
}
