// MEAS-02: sentetik kare izleri (frame-traces) ile hedef secimi ve gecerlilik.
#include <algorithm>

#include "core/measure/frames.h"
#include "ef_test.h"

using namespace ef::core::measure;

namespace {
// 60 Hz uygulama + ayni surecte ikinci (overlay) swapchain + baska PID'de Discord overlay.
std::vector<FrameEvent> game_trace(std::uint32_t pid, int frames, double period, bool with_generated = false) {
    std::vector<FrameEvent> v;
    for (int i = 0; i < frames; ++i) {
        FrameEvent e{pid, 0xA1, i * period, true, i * period + 8, FrameType::Application, Runtime::Dxgi};
        v.push_back(e);
        if (with_generated) v.push_back({pid, 0xA1, i * period + period / 2, true, i * period + period / 2 + 8, FrameType::Generated, Runtime::Dxgi});
        if (i % 10 == 0) v.push_back({pid, 0xB2, i * period + 1, false, 0, FrameType::Application, Runtime::Dxgi});  // ic overlay
    }
    for (int i = 0; i < frames * 2; ++i) v.push_back({999, 0xC3, i * 8.0, true, i * 8.0 + 1, FrameType::Application, Runtime::Dxgi});
    return v;
}
}  // namespace

EF_TEST(frames_wrong_process_or_overlay_not_measured) {
    const ProcessIdentity game{4242, 133000000000000000ull, "c:\\games\\cs2\\game\\bin\\win64\\cs2.exe"};
    EF_CHECK(check_target(game, game) == TargetError::None);
    auto reused = game;
    reused.create_time += 1;  // ayni PID, farkli surec
    EF_CHECK(check_target(game, reused) == TargetError::IdentityMismatch);
    const ProcessIdentity discord{4242, game.create_time, "c:\\users\\u\\appdata\\local\\discord\\app-1.0\\discord.exe"};
    EF_CHECK(check_target(game, discord) == TargetError::Overlay);
    const ProcessIdentity unknown{4242, 0, ""};
    EF_CHECK(check_target(game, unknown) == TargetError::UnknownProcess);  // yetkisiz oturum: <unknown>
    EF_CHECK(exe_name("c:\\a\\b\\obs64.exe") == "obs64.exe" && is_overlay_process("obs64.exe"));

    const auto s = summarize(game_trace(4242, 100, 1000.0 / 60), 4242, {0, 0, true, false});
    EF_CHECK(s.swapchain == 0xA1 && s.other_swapchains == 1);  // ic overlay swapchain dislandi
    EF_CHECK(s.render_frames == 100 && s.render_intervals_ms.size() == 99 && s.validity == Validity::Valid);
    EF_CHECK(std::all_of(s.render_intervals_ms.begin(), s.render_intervals_ms.end(), [](double d) { return d > 16 && d < 17; }));
}

EF_TEST(frames_lost_events_no_optimistic_fps) {
    auto s = summarize(game_trace(7, 100, 10), 7, {3, 0, true, false});
    EF_CHECK(s.validity == Validity::Invalid);
    EF_CHECK(std::find(s.reasons.begin(), s.reasons.end(), "etw_events_lost") != s.reasons.end());
    s = summarize(game_trace(7, 1, 10), 7, {0, 0, true, false});
    EF_CHECK(s.validity == Validity::Invalid);  // tek kare: aralik yok
    s = summarize({}, 7, {});
    EF_CHECK(s.validity == Validity::Invalid && s.render_frames == 0);
}

EF_TEST(frames_render_display_generated_separate) {
    const auto s = summarize(game_trace(5, 60, 20, true), 5, {0, 0, false, false});
    EF_CHECK(s.render_frames == 60 && s.generated_frames == 60 && s.displayed_frames == 120);
    EF_CHECK(s.render_intervals_ms.size() == 59 && s.display_intervals_ms.size() == 119);  // uretilen kare render FPS'e girmez
    EF_CHECK(s.validity == Validity::Valid);  // HAGS bilinmiyor FPS'i dusurmez
    EF_CHECK(std::find(s.reasons.begin(), s.reasons.end(), "hags_unknown") != s.reasons.end());
    std::vector<FrameEvent> gl;
    for (int i = 0; i < 10; ++i) gl.push_back({9, 1, i * 16.0, true, i * 16.0, FrameType::Application, Runtime::Other});
    EF_CHECK(summarize(gl, 9, {0, 0, true, false}).validity == Validity::LowConfidence);  // OpenGL/Vulkan
}

EF_TEST(frames_raw_present_mapping) {
    std::vector<FrameEvent> out;
    RawPresent p{42, 7, 10'000, 1, 1, {{2, 12'000}, {100, 13'000}}};  // DXGI, gosterildi + AMD AFMF uretilen
    append_frame_events(p, 0.001, 10'000, out);
    EF_CHECK(out.size() == 2 && out[0].type == FrameType::Application && out[0].displayed && out[0].display_ms == 2.0);
    EF_CHECK(out[1].type == FrameType::Generated && out[1].display_ms == 3.0 && out[0].runtime == Runtime::Dxgi);
    out.clear();
    RawPresent d{42, 7, 11'000, 0, 2, {{2, 12'000}}};  // Discarded: ekranda yok sayilir
    append_frame_events(d, 0.001, 10'000, out);
    EF_CHECK(out.size() == 1 && !out[0].displayed && out[0].runtime == Runtime::Other && out[0].present_ms == 1.0);
}

// EF-X18: API basina guven seviyesi (PresentData runtime'indan; renderer sezgisinden bagimsiz).
EF_TEST(x18_api_confidence_per_runtime) {
    EF_CHECK(api_confidence(Runtime::Dxgi) == ApiConfidence::RuntimeInstrumented);
    EF_CHECK(api_confidence(Runtime::D3d9) == ApiConfidence::RuntimeInstrumented);
    EF_CHECK(api_confidence(Runtime::Other) == ApiConfidence::KernelOnly);
    EF_CHECK(api_confidence(Runtime::Unknown) == ApiConfidence::Unknown);
    EF_CHECK(to_string(ApiConfidence::KernelOnly) == "kernel_only");
    EF_CHECK(to_string(ApiConfidence::RuntimeInstrumented) == "runtime_instrumented");
}

// EF-X18-T02: yalniz kayip tampon da kosuyu gecersiz kilar (iyimser FPS yok).
EF_TEST(x18_t02_lost_buffers_only_invalidates_run) {
    StreamHealth h;
    h.buffers_lost = 1;
    h.hags_known = true;
    const auto s = summarize(game_trace(100, 300, 16.0), 100, h);
    EF_CHECK(s.validity == Validity::Invalid);
    EF_CHECK(std::find(s.reasons.begin(), s.reasons.end(), "etw_events_lost") != s.reasons.end());
    EF_CHECK(!s.render_intervals_ms.empty());  // veri var ama gecersiz: "sorun yok" sonucu degil
}

// Spec 2026-11-12 B (D5): PresentData GPUDuration kare olayina tasinir ve render araliklariyla bire bir hizalanir.
EF_TEST(frames_gpu_busy_is_carried_per_frame_and_stays_aligned) {
    // Sentetik olay izi: ms_per_tick = 0,001 (1 tik = 1 us). 10 ms aralikli 5 sunum; GPU suresi 9000..5000 tik.
    std::vector<FrameEvent> ev;
    const std::uint64_t busy_ticks[5] = {9000, 8000, 7000, 0, 5000};
    for (int i = 0; i < 5; ++i) {
        RawPresent p{42, 7, static_cast<std::uint64_t>(100'000 + i * 10'000), 1, 1, {{2, static_cast<std::uint64_t>(105'000 + i * 10'000)}}};
        p.gpu_busy_qpc = busy_ticks[i];
        p.gpu_tracked = true;
        append_frame_events(p, 0.001, 100'000, ev);
    }
    EF_CHECK(ev.size() == 5 && ev[0].gpu_busy_ms == 9.0 && ev[3].gpu_busy_ms == 0.0 && ev[4].gpu_busy_ms == 5.0);
    auto s = summarize(ev, 42, {0, 0, true, false});
    EF_CHECK(s.render_intervals_ms.size() == 4 && s.render_gpu_busy_ms.size() == 4);
    // i. deger, i. araligi BITIREN sunumun GPU suresidir (ilk sunumun araligi yoktur). 0 olculmus bir degerdir.
    EF_CHECK(s.render_gpu_busy_ms[0] == 8.0 && s.render_gpu_busy_ms[1] == 7.0 && s.render_gpu_busy_ms[2] == 0.0 && s.render_gpu_busy_ms[3] == 5.0);

    // Uretilen (kare uretimi) gosterimler GPU suresi tasimaz ve seriyi kaydirmaz.
    std::vector<FrameEvent> gen;
    for (int i = 0; i < 4; ++i) {
        RawPresent p{42, 7, static_cast<std::uint64_t>(100'000 + i * 10'000), 1, 1,
                     {{2, static_cast<std::uint64_t>(105'000 + i * 10'000)}, {100, static_cast<std::uint64_t>(108'000 + i * 10'000)}}};
        p.gpu_busy_qpc = 6000;
        p.gpu_tracked = true;
        append_frame_events(p, 0.001, 100'000, gen);
    }
    EF_CHECK(gen.size() == 8 && gen[1].type == FrameType::Generated && gen[1].gpu_busy_ms < 0);
    s = summarize(gen, 42, {0, 0, true, false});
    EF_CHECK(s.generated_frames == 4 && s.render_intervals_ms.size() == 3 && s.render_gpu_busy_ms.size() == 3 && s.render_gpu_busy_ms[0] == 6.0);

    // Ayni zamanli (duplike) sunum araligi atlanir; GPU serisi de ayni kareyi atlar (hiza bozulmaz).
    std::vector<FrameEvent> dup = ev;
    dup.push_back(dup.back());
    s = summarize(dup, 42, {0, 0, true, false});
    EF_CHECK(s.render_intervals_ms.size() == 4 && s.render_gpu_busy_ms.size() == 4);

    // GPU izleme kapali (eski davranis): seri bos; kare araliklari degismez.
    std::vector<FrameEvent> off;
    for (int i = 0; i < 5; ++i) append_frame_events(RawPresent{42, 7, static_cast<std::uint64_t>(100'000 + i * 10'000), 1, 1, {}}, 0.001, 100'000, off);
    EF_CHECK(off[0].gpu_busy_ms < 0);
    s = summarize(off, 42, {0, 0, true, false});
    EF_CHECK(s.render_intervals_ms.size() == 4 && s.render_gpu_busy_ms.empty());
    // Kismi seri (bir karede GPU suresi yok) hizalanamaz: tasinmaz.
    auto partial = ev;
    partial[2].gpu_busy_ms = -1;
    s = summarize(partial, 42, {0, 0, true, false});
    EF_CHECK(s.render_intervals_ms.size() == 4 && s.render_gpu_busy_ms.empty());
}

// Review D5: PresentData GPU suresini surec bazinda biriktirir. Ayni surecin baska takas zincirindeki sunumuna ya da
// aralik uretmeyen (ayni zamanli) sunuma yazilan pay kaybolmaz; izleyen zincir araligina eklenir.
EF_TEST(frames_gpu_busy_of_other_swapchain_and_duplicate_presents_carries_to_next_interval) {
    auto present = [](std::vector<FrameEvent>& out, std::uint64_t chain, std::uint64_t start_tick, std::uint64_t busy_tick, bool tracked = true) {
        RawPresent p{42, chain, start_tick, 1, 1, {}};
        p.gpu_busy_qpc = busy_tick;
        p.gpu_tracked = tracked;
        append_frame_events(p, 0.001, 100'000, out);
    };
    // Ana zincir 7: 0, 10, 20, 30 ms. Ikinci zincir 9: 4 ms (ilk araligin icinde) ve 25 ms (ucuncu araligin icinde).
    std::vector<FrameEvent> ev;
    present(ev, 7, 100'000, 9000);
    present(ev, 9, 104'000, 3000);
    present(ev, 7, 110'000, 4000);
    present(ev, 7, 120'000, 6000);
    present(ev, 9, 125'000, 2500);
    present(ev, 7, 130'000, 5000);
    auto s = summarize(ev, 42, {0, 0, true, false});
    EF_CHECK(s.swapchain == 7 && s.other_swapchains == 1 && s.render_intervals_ms.size() == 3 && s.render_gpu_busy_ms.size() == 3);
    EF_CHECK(s.render_gpu_busy_ms[0] == 7.0 && s.render_gpu_busy_ms[1] == 6.0 && s.render_gpu_busy_ms[2] == 7.5);
    // Seri toplami = ilk zincir sunumundan sonraki tum surec GPU isi (4 + 6 + 5 + 3 + 2,5).
    double total = 0;
    for (const double b : s.render_gpu_busy_ms) total += b;
    EF_CHECK(total == 20.5);

    // Ilk zincir sunumundan ONCEKI baska-zincir sunumu hicbir araliga ait degildir: eklenmez.
    std::vector<FrameEvent> before;
    present(before, 9, 95'000, 8000);
    present(before, 7, 100'000, 9000);
    present(before, 7, 110'000, 4000);
    present(before, 7, 120'000, 6000);
    s = summarize(before, 42, {0, 0, true, false});
    EF_CHECK(s.render_gpu_busy_ms.size() == 2 && s.render_gpu_busy_ms[0] == 4.0 && s.render_gpu_busy_ms[1] == 6.0);

    // Ayni zamanli (aralik uretmeyen) sunumun GPU suresi sonraki araliga gecer; hiza bozulmaz.
    std::vector<FrameEvent> dup;
    present(dup, 7, 100'000, 9000);
    present(dup, 7, 110'000, 4000);
    present(dup, 7, 110'000, 1500);
    present(dup, 7, 120'000, 6000);
    s = summarize(dup, 42, {0, 0, true, false});
    EF_CHECK(s.render_intervals_ms.size() == 2 && s.render_gpu_busy_ms.size() == 2);
    EF_CHECK(s.render_gpu_busy_ms[0] == 4.0 && s.render_gpu_busy_ms[1] == 7.5);

    // Baska zincirdeki sunumun GPU suresi bilinmiyorsa seri eksiktir: tasinmaz (dusuk deger uydurulmaz).
    std::vector<FrameEvent> unknown = ev;
    present(unknown, 9, 115'000, 0, false);
    s = summarize(unknown, 42, {0, 0, true, false});
    EF_CHECK(s.render_intervals_ms.size() == 3 && s.render_gpu_busy_ms.empty());
    // Baska surecin sunumlari hic sayilmaz.
    std::vector<FrameEvent> foreign = ev;
    {
        RawPresent p{77, 9, 112'000, 1, 1, {}};
        p.gpu_busy_qpc = 50'000;
        p.gpu_tracked = true;
        append_frame_events(p, 0.001, 100'000, foreign);
    }
    s = summarize(foreign, 42, {0, 0, true, false});
    EF_CHECK(s.render_gpu_busy_ms.size() == 3 && s.render_gpu_busy_ms[1] == 6.0);
}
