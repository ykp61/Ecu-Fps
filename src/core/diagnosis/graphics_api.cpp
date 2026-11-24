#include "core/diagnosis/graphics_api.h"

#include <algorithm>
#include <cctype>

namespace ef::core::diagnosis {

namespace {

std::string base_name_lower(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

bool has_module(const std::vector<std::string>& names, std::string_view wanted) {
    for (const auto& n : names)
        if (base_name_lower(n) == wanted) return true;
    return false;
}

}  // namespace

GraphicsApi classify_graphics_api(const std::vector<std::string>& module_names) {
    if (has_module(module_names, "d3d12.dll")) return GraphicsApi::D3D12;
    if (has_module(module_names, "vulkan-1.dll")) return GraphicsApi::Vulkan;
    if (has_module(module_names, "d3d11.dll")) return GraphicsApi::D3D11;
    if (has_module(module_names, "d3d9.dll")) return GraphicsApi::D3D9;
    return GraphicsApi::Unknown;
}

std::string_view graphics_api_evidence(GraphicsApi api) noexcept {
    switch (api) {
        case GraphicsApi::D3D12: return "game_api_d3d12";
        case GraphicsApi::Vulkan: return "game_api_vulkan";
        case GraphicsApi::D3D11: return "game_api_d3d11";
        case GraphicsApi::D3D9: return "game_api_d3d9";
        case GraphicsApi::Unknown: break;
    }
    return {};
}

}  // namespace ef::core::diagnosis
