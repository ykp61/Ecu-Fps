#include "core/games/quality.h"

namespace ef::core::games {

QualityState quality_from_cs2(const cs2::VideoSettings& v) {
    QualityState q;
    q.width = v.width;
    q.height = v.height;
    if (v.fsr_detail) {
        // CS2: videocfg_fsr_detail 0 = kapali; >0 FSR 1 kalite kademesi (gozlenen alan; kucuk deger = daha yuksek
        // kalite oldugu dogrulanmadi -> kalite sirasi bilinmiyor olarak birakilir).
        q.upscaler = *v.fsr_detail == 0 ? Upscaler::Off : Upscaler::Fsr1;
        q.render_scale = *v.fsr_detail == 0 ? std::optional<double>(1.0) : std::nullopt;
    }
    q.frame_generation = false;  // CS2'de kare uretimi ayari yok
    q.msaa_samples = v.msaa_samples;
    q.texture_detail = v.texture_detail;
    q.shadow_quality = v.shadow_quality;
    q.shader_quality = v.shader_quality;
    return q;
}

namespace {
template <class T>
void cmp(const char* name, const std::optional<T>& a, const std::optional<T>& b, QualityDelta& d) {
    if (!a && !b) return;
    if (!a || !b) {
        d.unknown.push_back(name);
        return;
    }
    if (*b < *a) d.lowered.push_back(name);
    else if (*a < *b) d.raised.push_back(name);
}
}  // namespace

QualityDelta compare_quality(const QualityState& a, const QualityState& b) {
    QualityDelta d;
    // Cozunurluk: piksel sayisi
    std::optional<long long> pa, pb;
    if (a.width && a.height) pa = static_cast<long long>(*a.width) * *a.height;
    if (b.width && b.height) pb = static_cast<long long>(*b.width) * *b.height;
    cmp("resolution", pa, pb, d);
    cmp("render_scale", a.render_scale, b.render_scale, d);
    if (a.upscaler && b.upscaler && *a.upscaler != *b.upscaler) {
        if (*a.upscaler == Upscaler::Off) d.lowered.push_back("upscaler_enabled");  // yerelden olceklemeye
        else if (*b.upscaler == Upscaler::Off) d.raised.push_back("upscaler_disabled");
        else d.unknown.push_back("upscaler_changed");
    } else if (a.upscaler.has_value() != b.upscaler.has_value()) {
        d.unknown.push_back("upscaler");
    }
    if (a.upscaler && b.upscaler && *a.upscaler == *b.upscaler && *a.upscaler != Upscaler::Off) cmp("upscaler_quality", a.upscaler_quality, b.upscaler_quality, d);
    cmp("msaa_samples", a.msaa_samples, b.msaa_samples, d);
    cmp("texture_detail", a.texture_detail, b.texture_detail, d);
    cmp("shadow_quality", a.shadow_quality, b.shadow_quality, d);
    cmp("shader_quality", a.shader_quality, b.shader_quality, d);
    d.frame_generation_added = b.frame_generation.value_or(false) && !a.frame_generation.value_or(false);
    if (!d.unknown.empty()) d.cls = ChangeClass::Unknown;
    else if (!d.lowered.empty() && !d.raised.empty()) d.cls = ChangeClass::Mixed;
    else if (!d.lowered.empty()) d.cls = ChangeClass::QualityLowered;
    else if (!d.raised.empty()) d.cls = ChangeClass::QualityRaised;
    return d;
}

GainLabel label_gain(const QualityDelta& d) noexcept {
    if (d.cls == ChangeClass::Unknown) return GainLabel::NotComparable;
    if (d.frame_generation_added) return GainLabel::GeneratedFrames;
    if (d.cls != ChangeClass::NoQualityChange) return GainLabel::QualityTradeoff;
    return GainLabel::NativeRender;
}

}  // namespace ef::core::games
