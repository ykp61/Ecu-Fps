#include <cmath>

#include "core/measure/game_session.h"
#include "ef_test.h"

using namespace ef::core::measure;

namespace {
// 0-10 s menu (5 ms kare, cok hizli), 10-70 s oyun (10 ms), 70-75 s yukleme (50 ms).
std::vector<FrameEvent> trace() {
    std::vector<FrameEvent> v;
    double t = 0;
    auto push = [&](double until, double step) {
        for (; t < until; t += step) v.push_back({7, 1, t, true, t + 1, FrameType::Application, Runtime::Dxgi});
    };
    push(10000, 5);
    push(70000, 10);
    push(75000, 50);
    return v;
}
}  // namespace

EF_TEST(game_session_without_marks_is_uncertain) {
    const auto r = evaluate_session(trace(), 7, {0, 0, true, false}, {});
    EF_CHECK(r.uncertain_includes_menu && r.segmentation == SegmentationSource::None);
    EF_CHECK(r.gameplay.sufficient && *r.gameplay.avg_fps > 100.0);  // menu dahil: sisirilmis, etiketli
}

EF_TEST(game_session_user_marks_exclude_menu_and_loading) {
    const auto r = evaluate_session(trace(), 7, {0, 0, true, false},
                                    {{70000, SegmentKind::Loading, true}, {10000, SegmentKind::Gameplay, true}, {0, SegmentKind::Menu, true}});
    EF_CHECK(!r.uncertain_includes_menu && r.segmentation == SegmentationSource::UserMarks);
    EF_CHECK(r.gameplay.sufficient && std::fabs(*r.gameplay.avg_fps - 100.0) < 0.5);  // yalniz 10 ms bolum
    EF_CHECK(r.excluded_ms > 14000 && r.excluded_ms < 16000);                         // ~10 s menu + 5 s yukleme
    EF_CHECK(*r.gameplay.p99_ms < 11);
}
