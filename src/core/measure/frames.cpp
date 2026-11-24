#include "core/measure/frames.h"

#include <algorithm>
#include <array>
#include <map>

namespace ef::core::measure {

std::string_view exe_name(std::string_view p) noexcept {
    const auto i = p.find_last_of("\\/");
    return i == std::string_view::npos ? p : p.substr(i + 1);
}

bool is_overlay_process(std::string_view exe) noexcept {
    // Kare sunan ama oyun olmayan bilinen surecler (overlay, yakalama, istatistik araclari).
    static constexpr std::array<std::string_view, 16> kOverlay{
        "discord.exe",        "gameoverlayui.exe", "gameoverlayui64.exe", "nvidia overlay.exe", "nvcontainer.exe",
        "obs64.exe",          "obs32.exe",         "rtss.exe",            "msiafterburner.exe", "gamebar.exe",
        "gamebarftserver.exe", "xboxgamebar.exe",  "radeonsoftware.exe",  "amdrsserv.exe",      "dwm.exe",
        "ecufps.exe"};
    return std::find(kOverlay.begin(), kOverlay.end(), exe) != kOverlay.end();
}

std::string_view to_string(TargetError e) noexcept {
    switch (e) {
        case TargetError::None: return "none";
        case TargetError::NoCandidate: return "no_candidate";
        case TargetError::IdentityMismatch: return "identity_mismatch";
        case TargetError::Overlay: return "overlay";
        case TargetError::UnknownProcess: return "unknown_process";
    }
    return "unknown";
}

std::string_view to_string(ApiConfidence c) noexcept {
    switch (c) {
        case ApiConfidence::RuntimeInstrumented: return "runtime_instrumented";
        case ApiConfidence::KernelOnly: return "kernel_only";
        case ApiConfidence::Unknown: return "unknown";
    }
    return "unknown";
}

ApiConfidence api_confidence(Runtime r) noexcept {
    switch (r) {
        case Runtime::Dxgi:
        case Runtime::D3d9: return ApiConfidence::RuntimeInstrumented;
        case Runtime::Other: return ApiConfidence::KernelOnly;
        case Runtime::Unknown: return ApiConfidence::Unknown;
    }
    return ApiConfidence::Unknown;
}

std::string_view to_string(Validity v) noexcept {
    switch (v) {
        case Validity::Valid: return "valid";
        case Validity::LowConfidence: return "low_confidence";
        case Validity::Invalid: return "invalid";
    }
    return "invalid";
}

TargetError check_target(const ProcessIdentity& expected, const ProcessIdentity& observed) noexcept {
    if (observed.pid == 0) return TargetError::NoCandidate;
    if (observed.image_path.empty() || observed.create_time == 0) return TargetError::UnknownProcess;  // yetkisiz/kisa omurlu
    if (is_overlay_process(exe_name(observed.image_path))) return TargetError::Overlay;
    if (observed.pid != expected.pid || observed.create_time != expected.create_time) return TargetError::IdentityMismatch;  // PID tekrar kullanimi
    if (!expected.image_path.empty() && observed.image_path != expected.image_path) return TargetError::IdentityMismatch;
    return TargetError::None;
}

void append_frame_events(const RawPresent& p, double ms_per_tick, std::uint64_t origin, std::vector<FrameEvent>& out) {
    auto ms = [&](std::uint64_t q) { return q >= origin ? static_cast<double>(q - origin) * ms_per_tick : -static_cast<double>(origin - q) * ms_per_tick; };
    const Runtime rt = p.runtime == 1 ? Runtime::Dxgi : p.runtime == 2 ? Runtime::D3d9 : p.runtime == 0 ? Runtime::Other : Runtime::Unknown;
    FrameEvent app{p.pid, p.swapchain, ms(p.start_qpc), false, 0, FrameType::Application, rt};
    if (p.gpu_tracked) app.gpu_busy_ms = static_cast<double>(p.gpu_busy_qpc) * ms_per_tick;  // sure: kokenden bagimsiz
    const bool presented = p.final_state == 1;
    std::vector<FrameEvent> gen;
    for (const auto& [ft, q] : p.displayed) {
        if (!presented) break;
        const bool generated = ft == 3 || ft == 50 || ft == 100;
        if (generated) {
            gen.push_back({p.pid, p.swapchain, ms(q), true, ms(q), FrameType::Generated, rt});
        } else if (!app.displayed) {
            app.displayed = true;
            app.display_ms = ms(q);
        }
    }
    out.push_back(app);
    for (auto& g : gen) out.push_back(g);
}

FrameSummary summarize(const std::vector<FrameEvent>& events, std::uint32_t target_pid, const StreamHealth& health) {
    FrameSummary s;
    std::map<std::uint64_t, std::size_t> per_chain;
    for (const auto& e : events)
        if (e.pid == target_pid) ++per_chain[e.swapchain];
    if (per_chain.empty()) {
        s.reasons.push_back("no_frames");
        return s;  // Invalid
    }
    const auto best = std::max_element(per_chain.begin(), per_chain.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    s.swapchain = best->first;
    s.other_swapchains = per_chain.size() - 1;

    std::vector<const FrameEvent*> chain;
    for (const auto& e : events)
        if (e.pid == target_pid && e.swapchain == s.swapchain) chain.push_back(&e);
    std::stable_sort(chain.begin(), chain.end(), [](const FrameEvent* a, const FrameEvent* b) { return a->present_ms < b->present_ms; });

    // PresentData GPU suresini SUREC bazinda biriktirir ve surecin tamamlanan ilk sunumuna yazar (hangi takas
    // zinciri olursa olsun). Secilen zincirin disindaki uygulama sunumlarinin aldigi pay kaybolursa GPU mesgul orani
    // oldugundan dusuk cikar ("GPU bekledi" yanilgisi): bu pay, izleyen zincir araligina eklenir.
    std::vector<const FrameEvent*> others;
    for (const auto& e : events)
        if (e.pid == target_pid && e.swapchain != s.swapchain && e.type != FrameType::Generated) others.push_back(&e);
    std::stable_sort(others.begin(), others.end(), [](const FrameEvent* a, const FrameEvent* b) { return a->present_ms < b->present_ms; });
    std::size_t next_other = 0;

    bool runtime_mixed = false;
    std::optional<double> last_render, last_display;
    bool gpu_busy_complete = true;  // her render araliginin GPU suresi var mi
    double gpu_carry_ms = 0;        // araliga henuz yazilmamis surec GPU suresi (baska zincir / atlanan duplike sunum)
    for (const auto* e : chain) {
        if (s.runtime == Runtime::Unknown) s.runtime = e->runtime;
        else if (e->runtime != Runtime::Unknown && e->runtime != s.runtime) runtime_mixed = true;
        if (e->type == FrameType::Generated) {
            ++s.generated_frames;
        } else {
            ++s.render_frames;
            for (; next_other < others.size() && others[next_other]->present_ms <= e->present_ms; ++next_other) {
                if (others[next_other]->gpu_busy_ms >= 0) gpu_carry_ms += others[next_other]->gpu_busy_ms;
                else gpu_busy_complete = false;
            }
            if (last_render) {
                const double d = e->present_ms - *last_render;
                if (d > 0) {  // duplike/ters zaman atlanir
                    s.render_intervals_ms.push_back(d);
                    if (e->gpu_busy_ms >= 0) s.render_gpu_busy_ms.push_back(e->gpu_busy_ms + gpu_carry_ms);
                    else gpu_busy_complete = false;
                    gpu_carry_ms = 0;
                } else if (e->gpu_busy_ms >= 0) {
                    gpu_carry_ms += e->gpu_busy_ms;  // aralik uretmeyen sunumun GPU suresi sonraki araliga gecer
                } else {
                    gpu_busy_complete = false;
                }
            } else {
                gpu_carry_ms = 0;  // ilk zincir sunumundan onceki GPU isi hicbir araliga ait degildir
            }
            last_render = e->present_ms;
        }
        if (e->displayed) {
            ++s.displayed_frames;
            if (last_display) {
                const double d = e->display_ms - *last_display;
                if (d > 0) s.display_intervals_ms.push_back(d);
            }
            last_display = e->display_ms;
        }
    }

    // Kismi GPU serisi kare araliklariyla hizalanamaz: tasinmaz (bos = olculmedi).
    if (!gpu_busy_complete || s.render_gpu_busy_ms.size() != s.render_intervals_ms.size()) s.render_gpu_busy_ms.clear();

    s.validity = Validity::Valid;
    auto lower = [&](Validity v, const char* why) {
        s.reasons.emplace_back(why);
        if (static_cast<int>(v) > static_cast<int>(s.validity)) s.validity = v;
    };
    if (health.events_lost > 0 || health.buffers_lost > 0) lower(Validity::Invalid, "etw_events_lost");  // iyimser FPS yok
    if (s.render_intervals_ms.size() < 2) lower(Validity::Invalid, "too_few_frames");
    if (runtime_mixed) lower(Validity::LowConfidence, "runtime_mixed");
    if (s.runtime == Runtime::Other || s.runtime == Runtime::Unknown) lower(Validity::LowConfidence, "runtime_other");  // OpenGL/Vulkan: daha az enstrumantasyon
    // HAGS yalniz GPU sure metriklerini etkiler (resmi not); FPS gecerliligini dusurmez, neden olarak kaydedilir.
    if (!health.hags_known) s.reasons.emplace_back("hags_unknown");
    else if (health.hags_on) s.reasons.emplace_back("hags_on_gpu_times_late");
    if (s.other_swapchains > 0) s.reasons.emplace_back("other_swapchains_excluded");
    return s;
}

}  // namespace ef::core::measure
