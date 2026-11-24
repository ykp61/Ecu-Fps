// F-54: yuklu modul adlarindan grafik API sezgisi (saf). Kesin render yolu iddiasi yoktur.
#include <string>
#include <string_view>
#include <vector>

#include "core/diagnosis/drops.h"
#include "core/diagnosis/graphics_api.h"
#include "core/i18n/catalog.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;

EF_TEST(graphics_api_unknown_when_no_known_module) {
    EF_CHECK(classify_graphics_api({}) == GraphicsApi::Unknown);
    EF_CHECK(classify_graphics_api({"kernel32.dll", "user32.dll"}) == GraphicsApi::Unknown);
    EF_CHECK(graphics_api_evidence(GraphicsApi::Unknown).empty());
}

EF_TEST(graphics_api_detects_each_module_case_insensitively) {
    EF_CHECK(classify_graphics_api({"d3d12.dll"}) == GraphicsApi::D3D12);
    EF_CHECK(classify_graphics_api({"D3D11.DLL"}) == GraphicsApi::D3D11);
    EF_CHECK(classify_graphics_api({"Vulkan-1.dll"}) == GraphicsApi::Vulkan);
    EF_CHECK(classify_graphics_api({"d3d9.dll"}) == GraphicsApi::D3D9);
    // Yol verilirse yalniz dosya adi eslesir.
    EF_CHECK(classify_graphics_api({R"(C:\Windows\System32\d3d12.dll)"}) == GraphicsApi::D3D12);
}

EF_TEST(graphics_api_priority_prefers_newer_api_when_multiple_modules_loaded) {
    // D3D12 oyunlari DXGI/D3D11 yardimci modullerini de yukleyebilir; en yeni API one alinir.
    EF_CHECK(classify_graphics_api({"d3d11.dll", "dxgi.dll", "d3d12.dll"}) == GraphicsApi::D3D12);
    EF_CHECK(classify_graphics_api({"d3d11.dll", "vulkan-1.dll"}) == GraphicsApi::Vulkan);
    EF_CHECK(classify_graphics_api({"d3d9.dll", "d3d11.dll"}) == GraphicsApi::D3D11);
}

EF_TEST(graphics_api_evidence_keys_are_localized) {
    for (const auto api : {GraphicsApi::D3D12, GraphicsApi::D3D11, GraphicsApi::Vulkan, GraphicsApi::D3D9}) {
        const std::string_view code = graphics_api_evidence(api);
        EF_CHECK(!code.empty());
        EF_CHECK(drop_evidence_key(std::string(code)) == "drop.evidence." + std::string(code));
        EF_CHECK(ef::core::i18n::id_of("drop.evidence." + std::string(code)).has_value());
    }
    // Yeni API kodlari bilinmeyen anahtara dusmez.
    EF_CHECK(drop_evidence_key("game_api_d3d12") != "drop.evidence.unknown");
}
