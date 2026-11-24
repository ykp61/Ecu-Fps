#include "core/measure/game_limit_view.h"

namespace ef::core::measure {

namespace dg = diagnosis;

GameLimitTexts game_limit_texts(const GameLimitView& v) {
    const auto& r = v.result;
    GameLimitTexts t;
    bool show_full = r.full_pct.has_value(), show_waited = r.waited_pct.has_value();
    const auto undetermined = [&t](std::string reason_key) {
        t.headline = {"game_limit.headline.undetermined", {}, {}, {}};
        t.lines.push_back({std::move(reason_key), {}, {}, {}});
    };
    if (r.verdict == dg::GameLimit::GpuFull && r.full_pct) {
        t.headline = {"game_limit.headline.gpu_full", r.full_pct, {}, {}};
        show_full = false;  // baslikta yazildi
    } else if (r.verdict == dg::GameLimit::GpuWaited && r.waited_pct) {
        t.headline = {"game_limit.headline.gpu_waited", r.waited_pct, {}, {}};
        show_waited = false;
    } else if (r.verdict == dg::GameLimit::FrameCapSuspected) {
        t.headline = {"game_limit.headline.frame_cap", {}, {}, {}};
        if (r.avg_fps && r.cap_fps)
            t.lines.push_back({r.cap_is_refresh ? "game_limit.line.cap_refresh"
                               : r.cap_is_vrr   ? "game_limit.line.cap_vrr"
                                                : "game_limit.line.cap_common",
                               {}, r.avg_fps, r.cap_fps});
        t.note_keys.push_back("game_limit.note.cap");
    } else if (r.verdict == dg::GameLimit::Undetermined && r.reason != dg::GameLimitReason::None) {
        undetermined("game_limit.reason." + std::string(dg::to_string(r.reason)));
    } else {
        // Olculen payi olmayan hukum ya da nedensiz "belirlenemedi" (olmamasi gereken): hukum yazilmaz.
        undetermined("game_limit.reason.no_gpu_timing");
    }
    if (show_full) t.lines.push_back({"game_limit.line.gpu_full", r.full_pct, {}, {}});
    if (show_waited) t.lines.push_back({"game_limit.line.gpu_waited", r.waited_pct, {}, {}});
    if (v.game_gpu_max_pct) t.lines.push_back({"game_limit.line.game_gpu_max", v.game_gpu_max_pct, {}, {}});
    if (v.game_gpu_pct) t.lines.push_back({"game_limit.line.game_gpu", v.game_gpu_pct, {}, {}});  // ikincil: 3D
    if (r.gpu_time_approximate) t.note_keys.push_back("game_limit.note.hags");
    if (v.adapter_level == dg::GameAdapterLevel::Advice) t.note_keys.push_back("game_limit.note.adapter_advice");
    else if (v.adapter_level == dg::GameAdapterLevel::Info) t.note_keys.push_back("game_limit.note.adapter_info");
    t.note_keys.push_back("game_limit.note.scope");
    return t;
}

std::map<std::string, double> game_limit_metrics(const GameLimitView& v) {
    std::map<std::string, double> m;
    if (v.result.full_pct) m["gpu_full_frame_pct"] = *v.result.full_pct;
    if (v.result.waited_pct) m["gpu_waited_frame_pct"] = *v.result.waited_pct;
    if (v.result.busy_ratio) m["gpu_busy_ratio"] = *v.result.busy_ratio;
    if (v.game_gpu_max_pct) m["game_gpu_max_pct"] = *v.game_gpu_max_pct;
    if (v.game_gpu_pct) m["game_gpu_3d_pct"] = *v.game_gpu_pct;
    if (v.adapter_level == dg::GameAdapterLevel::Info) m["game_adapter_other"] = 1.0;
    else if (v.adapter_level == dg::GameAdapterLevel::Advice) m["game_adapter_other"] = 2.0;
    return m;
}

}  // namespace ef::core::measure
