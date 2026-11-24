#pragma once
// F-57: upscaler varlık tespiti ve nominal render ölçeği tablosu — SALT OKUMA, saf.
//  - Varlık yalnız dosya adından sınıflanır; sürüm çağıran tarafta dosya sürüm kaynağından okunur.
//  - Render ölçekleri üretici belgelerindeki TİPİK değerlerdir; sürüme/oyuna göre değişebilir.
//    Bu tablo "garantili çözünürlük" değildir, yalnız danışmanlık metni içindir.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::games {

enum class UpscalerKind { Dlss, DlssFrameGeneration, DlssRayReconstruction, XeSs, Fsr };
std::string_view to_string(UpscalerKind k) noexcept;

// Küçük harfle karşılaştırır; bilinen DLL adı değilse nullopt.
std::optional<UpscalerKind> kind_from_filename(std::string_view file);

struct UpscalerAsset {
    UpscalerKind kind = UpscalerKind::Dlss;
    std::string file;     // göreli/kısa ad
    std::string version;  // dosya sürümü; okunamadıysa boş (uydurma yok)
};

struct RenderScale {
    std::string_view preset;  // "Kalite", "Dengeli", "Performans", "Ultra Performans"
    double scale = 1.0;       // nominal render ölçeği (0-1)
};
std::vector<RenderScale> nominal_presets(UpscalerKind k);

}  // namespace ef::core::games
