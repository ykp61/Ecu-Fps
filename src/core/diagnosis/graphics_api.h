#pragma once
// F-54: oyun surecinin yukledigi grafik API'sini modul adlarindan siniflandirir (saf; birim testlenir).
// Bu bir SEZGI'dir: modulun yuklu olmasi oyunun o API ile render ettigini tek basina kanitlamaz
// (overlay/kanca veya yardimci modul de yukleyebilir). Tam ekran/pencereli sunum modu disaridan
// dogrulanamaz; UI metni bunu acikca soyler ve kesin iddia uretmez.

#include <string>
#include <string_view>
#include <vector>

namespace ef::core::diagnosis {

enum class GraphicsApi { Unknown, D3D9, D3D11, D3D12, Vulkan };

// "d3d12.dll" -> D3D12; "vulkan-1.dll" -> Vulkan; "d3d11.dll" -> D3D11; "d3d9.dll" -> D3D9.
// Birden fazla modul varsa en yeni API one alinir (D3D12 > Vulkan > D3D11 > D3D9). Yol verilirse
// yalniz dosya adi eslesir; buyuk/kucuk harf duyarsizdir.
GraphicsApi classify_graphics_api(const std::vector<std::string>& module_names);

// Kanit kodu: "game_api_d3d12" | "game_api_d3d11" | "game_api_vulkan" | "game_api_d3d9";
// Unknown -> bos.
std::string_view graphics_api_evidence(GraphicsApi api) noexcept;

}  // namespace ef::core::diagnosis
