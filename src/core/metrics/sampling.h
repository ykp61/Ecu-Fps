#pragma once
// UI-09: canli ornekleme kapisi (saf). Ornek yalniz pencere gorunurken ve en fazla 1 Hz alinir;
// simge durumunda/gizliyken/kapaliyken hic alinmaz. Toplayici is parcacigi bu karari kullanir.

#include <cstdint>

namespace ef::core::metrics {

class SamplingGate {
public:
    explicit SamplingGate(std::int64_t interval_ms = 1000) : interval_(interval_ms) {}
    // Gorunurluk degisti. Gorunur olunca ilk ornek hemen alinabilir.
    void set_visible(bool v) {
        if (v && !visible_) next_ = 0;
        visible_ = v;
    }
    bool visible() const { return visible_; }
    // now_ms: monoton. true donerse ornek alinmali ve sonraki zaman kurulur.
    bool should_sample(std::int64_t now_ms) {
        if (!visible_ || now_ms < next_) return false;
        next_ = now_ms + interval_;
        ++taken_;
        return true;
    }
    // Bir sonraki ornege kalan sure (gorunmezken -1: suresiz bekle).
    std::int64_t wait_ms(std::int64_t now_ms) const { return visible_ ? (next_ > now_ms ? next_ - now_ms : 0) : -1; }
    std::uint64_t taken() const { return taken_; }

private:
    std::int64_t interval_;
    std::int64_t next_ = 0;
    bool visible_ = false;
    std::uint64_t taken_ = 0;
};

}  // namespace ef::core::metrics
