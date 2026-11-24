#include "core/measure/game_session.h"

#include <algorithm>

namespace ef::core::measure {

SessionResult evaluate_session(const std::vector<FrameEvent>& events, std::uint32_t target_pid, const StreamHealth& health,
                               std::vector<Marker> markers) {
    SessionResult r;
    r.frames = summarize(events, target_pid, health);
    std::sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.t_ms < b.t_ms; });
    if (markers.empty()) {
        // Isaret yok: tum oturum, belirsizlik etiketiyle (menu/yukleme ayrilmadi; tahmin yapilmaz).
        r.gameplay = compute_frame_stats(r.frames.render_intervals_ms);
        return r;
    }
    r.segmentation = std::all_of(markers.begin(), markers.end(), [](const Marker& m) { return !m.from_user; }) ? SegmentationSource::VerifiedAdapter
                                                                                                                : SegmentationSource::UserMarks;
    r.uncertain_includes_menu = false;
    // Isaretten once: bilinmiyor -> dislanir (kullanici "oyun basladi" demeden once).
    auto kind_at = [&](double t) -> int {
        int k = -1;
        for (const auto& m : markers) {
            if (m.t_ms <= t) k = static_cast<int>(m.kind);
            else break;
        }
        return k;
    };
    // Render araliklarini yeniden kur: yalniz iki ucu da Gameplay bolumunde olan aralik sayilir.
    std::vector<const FrameEvent*> chain;
    for (const auto& e : events)
        if (e.pid == target_pid && e.swapchain == r.frames.swapchain && e.type == FrameType::Application) chain.push_back(&e);
    std::stable_sort(chain.begin(), chain.end(), [](const FrameEvent* a, const FrameEvent* b) { return a->present_ms < b->present_ms; });
    std::vector<double> gp;
    for (std::size_t i = 1; i < chain.size(); ++i) {
        const double a = chain[i - 1]->present_ms, b = chain[i]->present_ms;
        const double d = b - a;
        if (d <= 0) continue;
        const bool both = kind_at(a) == static_cast<int>(SegmentKind::Gameplay) && kind_at(b) == static_cast<int>(SegmentKind::Gameplay);
        if (both) gp.push_back(d);
        else r.excluded_ms += d;
    }
    r.gameplay = compute_frame_stats(gp);
    return r;
}

}  // namespace ef::core::measure
