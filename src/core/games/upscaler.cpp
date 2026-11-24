#include "core/games/upscaler.h"

#include <algorithm>
#include <cctype>

namespace ef::core::games {

namespace {
std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view basename(std::string_view p) {
    const auto slash = p.find_last_of("/\\");
    return slash == std::string_view::npos ? p : p.substr(slash + 1);
}
}  // namespace

std::string_view to_string(UpscalerKind k) noexcept {
    switch (k) {
        case UpscalerKind::Dlss: return "dlss";
        case UpscalerKind::DlssFrameGeneration: return "dlss_fg";
        case UpscalerKind::DlssRayReconstruction: return "dlss_rr";
        case UpscalerKind::XeSs: return "xess";
        case UpscalerKind::Fsr: return "fsr";
    }
    return "unknown";
}

std::optional<UpscalerKind> kind_from_filename(std::string_view file) {
    const std::string f = lower(basename(file));
    if (f == "nvngx_dlss.dll") return UpscalerKind::Dlss;
    if (f == "nvngx_dlssg.dll") return UpscalerKind::DlssFrameGeneration;
    if (f == "nvngx_dlssd.dll") return UpscalerKind::DlssRayReconstruction;
    if (f == "libxess.dll" || f == "libxess_dx11.dll" || f == "libxess_dx12.dll") return UpscalerKind::XeSs;
    if (f == "ffx_fsr2_api_x64.dll" || f == "ffx_fsr2_api_dx12_x64.dll" || f == "ffx_fsr2_api_vk_x64.dll" ||
        f == "ffx_fsr3_api_x64.dll" || f == "ffx_fsr3_api_dx12_x64.dll" || f == "ffx_fsr3_api_vk_x64.dll" ||
        f == "amd_fidelityfx_fsr2_dx12.dll" || f == "amd_fidelityfx_dx12.dll")
        return UpscalerKind::Fsr;
    return std::nullopt;
}

std::vector<RenderScale> nominal_presets(UpscalerKind k) {
    // Üretici belgelerindeki tipik ölçekler; sürüm/oyun farkı olabilir. Garanti değil.
    switch (k) {
        case UpscalerKind::Dlss:
        case UpscalerKind::DlssRayReconstruction:
            return {{"Kalite", 0.667}, {"Dengeli", 0.58}, {"Performans", 0.5}, {"Ultra Performans", 0.333}};
        case UpscalerKind::XeSs:
            return {{"Ultra Kalite", 0.77}, {"Kalite", 0.67}, {"Dengeli", 0.59}, {"Performans", 0.5}};
        case UpscalerKind::Fsr:
            return {{"Kalite", 0.667}, {"Dengeli", 0.588}, {"Performans", 0.5}, {"Ultra Performans", 0.333}};
        case UpscalerKind::DlssFrameGeneration:
            break;  // FG kare üretir; kendi render ölçeği yoktur.
    }
    return {};
}

}  // namespace ef::core::games
