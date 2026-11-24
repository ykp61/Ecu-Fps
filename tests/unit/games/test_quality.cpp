#include "core/games/quality.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(quality_lowering_is_not_optimization) {
    QualityState a;
    a.width = 2560, a.height = 1440, a.upscaler = Upscaler::Off, a.render_scale = 1.0, a.msaa_samples = 4, a.frame_generation = false;
    auto b = a;
    EF_CHECK(compare_quality(a, b).cls == ChangeClass::NoQualityChange && label_gain(compare_quality(a, b)) == GainLabel::NativeRender);
    b.msaa_samples = 2;
    auto d = compare_quality(a, b);
    EF_CHECK(d.cls == ChangeClass::QualityLowered && d.requires_consent() && label_gain(d) == GainLabel::QualityTradeoff);
    b = a;
    b.upscaler = Upscaler::Dlss;
    b.render_scale = 0.67;
    d = compare_quality(a, b);
    EF_CHECK(d.cls == ChangeClass::QualityLowered && d.lowered.size() == 2);  // upscaler + render olcegi
    b = a;
    b.width = 1920, b.height = 1080;
    EF_CHECK(compare_quality(a, b).requires_consent());
    b = a;
    b.msaa_samples.reset();  // bilinmeyen -> karsilastirilamaz, onay gerekir
    EF_CHECK(compare_quality(a, b).cls == ChangeClass::Unknown && label_gain(compare_quality(a, b)) == GainLabel::NotComparable);
}

EF_TEST(quality_generated_frames_not_native_gain) {
    QualityState a;
    a.upscaler = Upscaler::Dlss, a.upscaler_quality = 2, a.frame_generation = false;
    auto b = a;
    b.frame_generation = true;
    const auto d = compare_quality(a, b);
    EF_CHECK(d.frame_generation_added && label_gain(d) == GainLabel::GeneratedFrames);
}

EF_TEST(quality_from_cs2_observed_fields) {
    cs2::VideoSettings v;
    v.width = 2560, v.height = 1440, v.fsr_detail = 0, v.msaa_samples = 4;
    const auto q = quality_from_cs2(v);
    EF_CHECK(q.upscaler == Upscaler::Off && q.render_scale == 1.0 && q.frame_generation == false && q.msaa_samples == 4);
    v.fsr_detail = 2;
    const auto f = quality_from_cs2(v);
    EF_CHECK(f.upscaler == Upscaler::Fsr1 && !f.render_scale);  // kademe->olcek dogrulanmadi: bilinmiyor
    EF_CHECK(compare_quality(q, f).requires_consent());
}
