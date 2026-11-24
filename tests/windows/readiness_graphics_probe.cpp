// CHK-03/04 Windows probu: bu PC'de okunan gercek grafik/yazilim/BIOS degerleri ve uretilen bulgular.
// SALT OKUMA: ayar yazmaz, pencere acmaz, yonetici istemez. ctest'e eklenmez (ortama bagli cikti).
#include <cstdio>
#include <string>

#include "core/readiness/readiness.h"
#include "platform/windows/readiness/graphics_reader.h"
#include "platform/windows/vendor_runtime.h"

using namespace ef;
namespace cr = core::readiness;

namespace {

const char* level_name(cr::Level l) {
    switch (l) {
        case cr::Level::Good: return "Good";
        case cr::Level::Advice: return "Advice";
        case cr::Level::Problem: return "Problem";
        case cr::Level::Unknown: return "Unknown";
        case cr::Level::Info: return "Info";
    }
    return "?";
}
const char* action_name(cr::ActionKind a) {
    switch (a) {
        case cr::ActionKind::None: return "None";
        case cr::ActionKind::AutoFix: return "AutoFix";
        case cr::ActionKind::OpenSettings: return "OpenSettings";
        case cr::ActionKind::OpenUrl: return "OpenUrl";
        case cr::ActionKind::RunNetworkTest: return "RunNetworkTest";
    }
    return "?";
}
std::string ob(const std::optional<bool>& b) { return b ? (*b ? "true" : "false") : "null"; }
template <typename T>
std::string ou(const std::optional<T>& v, bool hex = false) {
    if (!v) return "null";
    char buf[32];
    std::snprintf(buf, sizeof buf, hex ? "0x%llx" : "%lld", static_cast<long long>(*v));
    return buf;
}

void print_checks(const std::vector<cr::Check>& v) {
    for (const auto& c : v) {
        std::printf("  [%-7s] %-36s %s", level_name(c.level), c.id.c_str(), c.text_key.c_str());
        for (const auto& [k, val] : c.args) std::printf(" %s=%s", k.c_str(), val.c_str());
        if (c.action != cr::ActionKind::None) std::printf(" -> %s %s", action_name(c.action), c.action_arg.c_str());
        std::printf("\n");
    }
}

}  // namespace

int main() {
    platform::harden_dll_search();
    std::optional<platform::readiness::NvVrrRaw> raw;
    const auto g = platform::readiness::read_graphics(&raw);
    std::printf("GRAPHICS\n");
    std::printf("  gpu_name=%s vendor=%s driver_version=%s driver_date_unix=%s\n", g.gpu_name.c_str(), g.vendor.c_str(),
                g.driver_version.c_str(), ou(g.driver_date_unix).c_str());
    std::printf("  d3d12_max_fl=%s shader_model=%s raytracing_tier=%s mesh=%s vrs2=%s sampler_feedback=%s\n",
                ou(g.d3d12_max_feature_level, true).c_str(), ou(g.shader_model, true).c_str(),
                ou(g.raytracing_tier).c_str(), ob(g.mesh_shaders).c_str(), ob(g.vrs_tier2).c_str(),
                ob(g.sampler_feedback).c_str());
    std::printf("  d3d11_fallback_fl=%s\n", ou(g.d3d11_feature_level, true).c_str());
    std::printf("  dx12_ultimate=%s dx9_legacy_runtime=%s gsync_enabled=%s gsync_possible=%s\n",
                cr::dx12_ultimate(g) ? "true" : "false", ob(g.dx9_legacy_runtime).c_str(), ob(g.gsync_enabled).c_str(),
                ob(g.gsync_possible).c_str());
    if (raw)
        std::printf("  nvapi_vrr display=%s enabled=%d possible=%d requested=%d indicator=%d in_vrr_mode=%d\n",
                    raw->display.c_str(), raw->enabled, raw->possible, raw->requested, raw->indicator,
                    raw->in_vrr_mode);
    else
        std::printf("  nvapi_vrr=unavailable\n");
    print_checks(cr::graphics_checks(g));

    const auto s = platform::readiness::read_software();
    std::printf("SOFTWARE\n");
    for (const auto& it : s.items)
        std::printf("  item name=\"%s\" version=%s date_unix=%s\n", it.name.c_str(), it.version.c_str(),
                    ou(it.date_unix).c_str());
    std::printf("  bios_date_unix=%s board_vendor=\"%s\" board_model=\"%s\"\n", ou(s.bios_date_unix).c_str(),
                s.board_vendor.c_str(), s.board_model.c_str());
    print_checks(cr::software_checks(s));
    return 0;
}
