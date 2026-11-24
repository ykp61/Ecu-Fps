#include <string>
#include <vector>

#include "core/games/upscaler.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(upscaler_kind_from_known_dll_names) {
    EF_CHECK(kind_from_filename("nvngx_dlss.dll") == UpscalerKind::Dlss);
    EF_CHECK(kind_from_filename("C:\\Game\\bin\\NVNGX_DLSSG.DLL") == UpscalerKind::DlssFrameGeneration);
    EF_CHECK(kind_from_filename("nvngx_dlssd.dll") == UpscalerKind::DlssRayReconstruction);
    EF_CHECK(kind_from_filename("libxess.dll") == UpscalerKind::XeSs);
    EF_CHECK(kind_from_filename("ffx_fsr3_api_dx12_x64.dll") == UpscalerKind::Fsr);
    EF_CHECK(!kind_from_filename("d3d12.dll").has_value());
    EF_CHECK(!kind_from_filename("").has_value());
}

EF_TEST(upscaler_kind_basename_only_not_directory) {
    // Klasör adı DLL adını içerse bile eşleşmemeli.
    EF_CHECK(!kind_from_filename("C:\\nvngx_dlss.dll\\readme.txt").has_value());
    EF_CHECK(kind_from_filename("C:\\mods\\x\\libxess.dll") == UpscalerKind::XeSs);
}

EF_TEST(upscaler_presets_nominal_and_frame_gen_empty) {
    const auto dlss = nominal_presets(UpscalerKind::Dlss);
    EF_CHECK(dlss.size() == 4);
    EF_CHECK(dlss[0].scale > dlss[3].scale);  // kalite > ultra performans
    const auto fsr = nominal_presets(UpscalerKind::Fsr);
    EF_CHECK(fsr.size() == 4);
    EF_CHECK(nominal_presets(UpscalerKind::DlssFrameGeneration).empty());
    EF_CHECK(nominal_presets(UpscalerKind::XeSs).size() == 4);
}
