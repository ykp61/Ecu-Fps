#pragma once
#include <optional>
// UI-01/UI-08: hareket. cubic-bezier egrisi (tokens.easing) ve sure tabanli gecis. Azaltilmis
// harekette sure 0 olur ve deger hedefe aninda atlar. Surekli bos animasyon yoktur: gecis bitince
// zamanlayici durur (active() == false).

#include <array>
#include <cstdint>

namespace ef::ui {

// CSS cubic-bezier(x1, y1, x2, y2); t in [0,1] -> ilerleme.
float cubic_bezier(const std::array<float, 4>& p, float t);

class Tween {
public:
    Tween() = default;
    explicit Tween(float value) : from_(value), to_(value), last_(value) {}

    // now_ms: monoton milisaniye. duration_ms <= 0 ise aninda hedefe gecer.
    void retarget(float to, std::int64_t now_ms, int duration_ms);
    float value(std::int64_t now_ms, const std::array<float, 4>& easing) const;
    bool active(std::int64_t now_ms) const { return duration_ > 0 && now_ms < start_ + duration_; }
    float target() const { return to_; }

private:
    float from_ = 0, to_ = 0;
    std::int64_t start_ = 0;
    int duration_ = 0;
    mutable float last_ = 0;
};

// Sistem tercihine gore efektif sure.
inline int effective_ms(int ms, bool reduced_motion) { return reduced_motion ? 0 : ms; }
// UI-11: Windows animasyonlari kapali VEYA kullanici uygulama animasyonlarini kapattiysa hareket yok.
inline bool reduced_motion_for(bool system_animations, bool app_animations) { return !system_animations || !app_animations; }
// UI-13: uygulama hareketi acik mi? Performans Windows animasyonlarini kapattiysa (journal) ozgun deger gecerlidir.
inline bool app_motion_enabled(bool system_animations_now, std::optional<bool> original_before_performance) {
    return original_before_performance ? *original_before_performance : system_animations_now;
}

}  // namespace ef::ui
