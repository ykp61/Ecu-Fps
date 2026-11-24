#include "ui/motion/motion.h"

#include <cmath>

namespace ef::ui {

namespace {
float bez(float a, float b, float t) {
    // 0, a, b, 1 kontrol noktali tek eksen Bezier
    const float u = 1 - t;
    return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
}
float bez_d(float a, float b, float t) {
    const float u = 1 - t;
    return 3 * u * u * a + 6 * u * t * (b - a) + 3 * t * t * (1 - b);
}
}  // namespace

float cubic_bezier(const std::array<float, 4>& p, float x) {
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    // x(t) = x icin t'yi Newton + ikiye bolme ile coz.
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float err = bez(p[0], p[2], t) - x;
        if (std::fabs(err) < 1e-5f) return bez(p[1], p[3], t);
        const float d = bez_d(p[0], p[2], t);
        if (std::fabs(d) < 1e-6f) break;
        t -= err / d;
        if (t < 0 || t > 1) break;
    }
    float lo = 0, hi = 1;
    t = x;
    for (int i = 0; i < 32; ++i) {
        const float v = bez(p[0], p[2], t);
        if (std::fabs(v - x) < 1e-5f) break;
        if (v < x) lo = t; else hi = t;
        t = (lo + hi) / 2;
    }
    return bez(p[1], p[3], t);
}

void Tween::retarget(float to, std::int64_t now_ms, int duration_ms) {
    // Hareket ortasinda yeniden hedef: son cizilen konumdan devam eder (sicrama yok).
    const bool mid = duration_ > 0 && now_ms < start_ + duration_;
    from_ = mid ? last_ : to_;
    to_ = to;
    start_ = now_ms;
    duration_ = duration_ms > 0 ? duration_ms : 0;
    last_ = duration_ == 0 ? to : from_;
}

float Tween::value(std::int64_t now_ms, const std::array<float, 4>& easing) const {
    if (duration_ <= 0 || now_ms >= start_ + duration_) return last_ = to_;
    const float x = static_cast<float>(now_ms - start_) / static_cast<float>(duration_);
    const float k = cubic_bezier(easing, x < 0 ? 0 : x);
    return last_ = from_ + (to_ - from_) * k;
}

}  // namespace ef::ui
