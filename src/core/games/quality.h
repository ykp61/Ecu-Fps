#pragma once
// GAME-04: oyun kalitesini etkileyen ayarlarin tipli modeli (saf; birim testlenir).
//  - Kalite dusuren degisiklik (cozunurluk/render olcegi/upscaler/MSAA/doku vb.) "optimizasyon" sayilmaz;
//    ayri kategori "kalite takasi" ve acik onay ister.
//  - Kare uretimi (DLSS FG / FSR FG / AFMF / XeFG) ile artan ekran FPS'i yerel render kazanci degildir;
//    kazanc yalniz render FPS ile ifade edilir, uretilen kare ayri etiketlenir.

#include <optional>
#include <string>
#include <vector>

#include "core/games/cs2.h"

namespace ef::core::games {

enum class Upscaler { Off, Fsr1, Fsr2Plus, Dlss, Xess, Unknown };

struct QualityState {
    std::optional<int> width, height;
    std::optional<double> render_scale;  // 1.0 = yerel
    std::optional<Upscaler> upscaler;
    std::optional<int> upscaler_quality;  // oyuna ozgu sira; buyuk = daha yuksek kalite
    std::optional<bool> frame_generation;
    std::optional<int> msaa_samples;
    std::optional<int> texture_detail, shadow_quality, shader_quality;
};
QualityState quality_from_cs2(const cs2::VideoSettings& v);

enum class ChangeClass { NoQualityChange, QualityLowered, QualityRaised, Mixed, Unknown };
struct QualityDelta {
    ChangeClass cls = ChangeClass::NoQualityChange;
    std::vector<std::string> lowered, raised, unknown;  // alan adlari
    bool frame_generation_added = false;
    bool requires_consent() const noexcept { return cls == ChangeClass::QualityLowered || cls == ChangeClass::Mixed || cls == ChangeClass::Unknown; }
};
QualityDelta compare_quality(const QualityState& before, const QualityState& after);

enum class GainLabel { NativeRender, QualityTradeoff, GeneratedFrames, NotComparable };
// FPS kazanci nasil etiketlenir: farkli kalite -> takas; kare uretimi eklendi -> uretilen; ikisi de yok -> yerel.
GainLabel label_gain(const QualityDelta& d) noexcept;

}  // namespace ef::core::games
