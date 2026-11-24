// CHK-01/02/05: ekran ve Windows oyun ayari kurallari (saf) + platform saf yardimcilari.
#include <algorithm>
#include <string>

#include "core/readiness/readiness.h"
#include "ef_test.h"
#include "platform/windows/readiness/display_reader.h"

using namespace ef::core::readiness;
using ef::platform::readiness::game_dvr_background_from;
using ef::platform::readiness::game_mode_from;
using ef::platform::readiness::power_mode_from;
using ef::platform::readiness::power_plan_kind_from;
using ef::platform::readiness::parse_dx_user_setting;

namespace {
const Check* find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}
DisplayInfo disp(std::uint32_t idx, std::optional<double> cur, std::optional<double> max) {
    DisplayInfo d;
    d.index = idx;
    d.name = "DELL S2721DGF";
    d.gdi_device = R"(\\.\DISPLAY)" + std::to_string(idx + 1);
    d.width = 2560;
    d.height = 1440;
    d.hz_current = cur;
    d.hz_max_same_res = max;
    return d;
}
WindowsGamingInfo all_good() {
    WindowsGamingInfo w;
    w.game_mode = true;
    w.game_dvr_background = false;
    w.windowed_optimizations = true;
    w.hags_supported = true;
    w.hags_enabled = true;
    w.fast_startup = false;
    w.vbs_running = false;
    w.hvci_enabled = false;
    w.power_mode = "best_performance";
    w.power_plan_kind = WindowsGamingInfo::PowerPlanKind::Builtin;
    return w;
}
}  // namespace

// ---------------- Ekran ----------------

EF_TEST(readiness_display_below_max_is_problem_with_autofix) {
    const auto v = display_checks({disp(0, 60.0, 144.0)});
    const auto* c = find(v, "display.0.refresh");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Problem);
    EF_CHECK(c->section == Section::Display);
    EF_CHECK(c->text_key == "chk.display.below_max");
    EF_CHECK(c->action == ActionKind::AutoFix);
    EF_CHECK(c->action_arg == "fix.refresh_rate");
    EF_CHECK(c->args.at("hz") == "60");
    EF_CHECK(c->args.at("max") == "144");
    EF_CHECK(c->args.at("name") == "DELL S2721DGF");
    EF_CHECK(c->args.at("res") == "2560x1440");
    EF_CHECK(c->args.at("index") == "0");
    EF_CHECK(c->args.at("device") == "DISPLAY1");
}

// Uyumluluk B6 (bosluk 10): GDI aygit adi okunamadiysa otomatik duzeltme sunulmaz (sira numarasi baska ekrani
// hedefleyebilir); Windows ekran ayarlari acilir.
EF_TEST(readiness_display_refresh_autofix_requires_gdi_device) {
    auto d = disp(1, 60.0, 144.0);
    d.gdi_device.clear();
    auto v = display_checks({d});
    auto* c = find(v, "display.1.refresh");
    EF_CHECK(c && c->level == Level::Problem && c->text_key == "chk.display.below_max");
    EF_CHECK(c && c->action == ActionKind::OpenSettings && c->action_arg == "ms-settings:display-advanced");
    EF_CHECK(c && c->args.count("device") == 0);
    d.gdi_device = "DISPLAY2";  // bicimsiz ad: kabul edilmez
    v = display_checks({d});
    c = find(v, "display.1.refresh");
    EF_CHECK(c && c->action == ActionKind::OpenSettings && c->args.count("device") == 0);
    d.gdi_device = R"(\\.\DISPLAY5)";
    v = display_checks({d});
    c = find(v, "display.1.refresh");
    EF_CHECK(c && c->action == ActionKind::AutoFix && c->args.at("device") == "DISPLAY5");
}

EF_TEST(readiness_display_below_max_with_hdr_is_advice) {
    auto d = disp(3, 60.0, 143.6);
    d.hdr_supported = true;
    d.hdr_enabled = true;
    const auto v = display_checks({d});
    const auto* c = find(v, "display.3.refresh");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Advice);
    EF_CHECK(c->text_key == "chk.display.below_max");
    EF_CHECK(c->action == ActionKind::AutoFix && c->action_arg == "fix.refresh_rate");
    EF_CHECK(c->args.at("index") == "3");
    EF_CHECK(c->args.at("max") == "144");  // tam sayi Hz (AutoFix bunu ayarlar)
    // HDR kapali (ya da bilinmiyor) -> Problem
    d.hdr_enabled = false;
    EF_CHECK(find(display_checks({d}), "display.3.refresh")->level == Level::Problem);
    d.hdr_enabled.reset();
    EF_CHECK(find(display_checks({d}), "display.3.refresh")->level == Level::Problem);
}

EF_TEST(readiness_display_fractional_rates_round_and_threshold) {
    // 143.98 fiziksel / 144 mod listesi: fark < 1.5 -> Good, metin "144".
    auto v = display_checks({disp(0, 143.981, 144.0)});
    auto* c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Good && c->text_key == "chk.display.at_max");
    EF_CHECK(c && c->args.at("hz") == "144");
    // 59.94 vs 60 -> Good
    v = display_checks({disp(0, 59.94, 60.0)});
    c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Good);
    // Esik: tam 1.5 -> Problem; 1.49 -> Good
    v = display_checks({disp(0, 142.5, 144.0)});
    c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Problem);
    EF_CHECK(c && c->args.at("hz") == "142.50");
    v = display_checks({disp(0, 142.51, 144.0)});
    c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Good);
    // 120 / 165 -> Problem
    v = display_checks({disp(0, 120.0, 165.0)});
    c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Problem && c->args.at("max") == "165");
}

EF_TEST(readiness_display_dynamic_refresh_is_info_not_problem) {
    auto d = disp(0, 60.0, 120.0);
    d.dynamic_refresh = true;
    const auto v = display_checks({d});
    const auto* c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Info);
    EF_CHECK(c && c->text_key == "chk.display.dynamic_refresh");
    EF_CHECK(c && c->action == ActionKind::OpenSettings && c->action_arg.rfind("ms-settings:", 0) == 0);
}

EF_TEST(readiness_display_unknown_cases) {
    auto v = display_checks({disp(0, std::nullopt, 144.0)});
    auto* c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.display.refresh_unknown");
    EF_CHECK(c && c->args.count("hz") == 0);
    v = display_checks({disp(0, 60.0, std::nullopt)});
    c = find(v, "display.0.refresh");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.display.max_unknown");
    EF_CHECK(c && c->args.at("hz") == "60");
    // Ekran yok
    v = display_checks({});
    EF_CHECK(v.size() == 1);
    EF_CHECK(v[0].id == "display.none" && v[0].level == Level::Unknown);
}

EF_TEST(readiness_display_hdr_rules) {
    auto d = disp(0, 144.0, 144.0);
    d.hdr_supported = true;
    d.hdr_enabled = true;
    auto v = display_checks({d});
    auto* c = find(v, "display.0.hdr");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.display.hdr_on");
    // Acik HDR asla Problem/Advice degil
    for (const auto& x : v) EF_CHECK(x.level != Level::Problem);
    d.hdr_enabled = false;
    v = display_checks({d});
    c = find(v, "display.0.hdr");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.display.hdr_available");
    d.hdr_supported = false;
    v = display_checks({d});
    EF_CHECK(find(v, "display.0.hdr") == nullptr);
    d.hdr_supported.reset();
    d.hdr_enabled.reset();
    v = display_checks({d});
    c = find(v, "display.0.hdr");
    EF_CHECK(c && c->level == Level::Unknown);
    // 24H2 oncesi yedek yol: HDR kapali, destek bilinmiyor -> bulgu yok (destek iddiasi yok)
    d.hdr_supported.reset();
    d.hdr_enabled = false;
    EF_CHECK(find(display_checks({d}), "display.0.hdr") == nullptr);
}

EF_TEST(readiness_display_vrr_single_unknown_and_known) {
    auto v = display_checks({disp(0, 144.0, 144.0), disp(1, 60.0, 60.0)});
    int vrr = 0;
    for (const auto& c : v)
        if (c.id.find("vrr") != std::string::npos) ++vrr;
    EF_CHECK(vrr == 1);
    const auto* c = find(v, "display.vrr");
    EF_CHECK(c && c->level == Level::Unknown);
    auto d = disp(0, 144.0, 144.0);
    d.vrr_supported = true;
    v = display_checks({d});
    EF_CHECK(find(v, "display.vrr") == nullptr);
    c = find(v, "display.0.vrr");
    EF_CHECK(c && c->level == Level::Good);
}

// ---------------- F-44 ekran yolu-GPU esleme ----------------

EF_TEST(readiness_display_gpu_path_reports_known_adapter) {
    auto d = disp(0, 144.0, 144.0);
    d.gpu_known = true;
    d.gpu_name = "NVIDIA GeForce RTX 5080";
    d.gpu_vendor = "nvidia";
    d.gpu_high_perf = true;
    d.multi_gpu = true;
    auto v = display_checks({d});
    const auto* c = find(v, "display.0.gpu");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->section == Section::Display);
    EF_CHECK(c->text_key == "chk.display.gpu_path");
    EF_CHECK(c->args.at("name") == "DELL S2721DGF");
    EF_CHECK(c->args.at("gpu") == "NVIDIA GeForce RTX 5080");
    EF_CHECK(c->action == ActionKind::None);
    // Tek GPU da ayni yol metni
    d.multi_gpu = false;
    v = display_checks({d});
    EF_CHECK(find(v, "display.0.gpu") && find(v, "display.0.gpu")->text_key == "chk.display.gpu_path");
}

EF_TEST(readiness_display_gpu_hybrid_path_only_when_multi_gpu) {
    auto d = disp(0, 144.0, 144.0);
    d.gpu_known = true;
    d.gpu_name = "Intel(R) UHD Graphics";
    d.gpu_vendor = "intel";
    d.gpu_high_perf = false;
    d.multi_gpu = true;
    auto v = display_checks({d});
    const auto* c = find(v, "display.0.gpu");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->text_key == "chk.display.gpu_hybrid_path");
    EF_CHECK(c->args.at("gpu") == "Intel(R) UHD Graphics");
    EF_CHECK(c->action == ActionKind::None);
    // Tek adaptor varsa MUX/kopyalama anlatilmaz
    d.multi_gpu = false;
    v = display_checks({d});
    EF_CHECK(find(v, "display.0.gpu")->text_key == "chk.display.gpu_path");
}

EF_TEST(readiness_display_gpu_unknown_makes_no_claim) {
    auto d = disp(0, 144.0, 144.0);
    EF_CHECK(find(display_checks({d}), "display.0.gpu") == nullptr);
    // Esleme biliniyor ama adaptor adi bos: yol iddiasi uretilmez
    d.gpu_known = true;
    EF_CHECK(find(display_checks({d}), "display.0.gpu") == nullptr);
    // Bilinen ad ama esleme yok: yine bulgu yok
    d.gpu_known = false;
    d.gpu_name = "NVIDIA GeForce RTX 5080";
    EF_CHECK(find(display_checks({d}), "display.0.gpu") == nullptr);
}

// ---------------- F-05 kare senkronu danismani ----------------

EF_TEST(readiness_frame_sync_vrr_caps_three_below_refresh) {
    auto d = disp(0, 144.0, 144.0);
    d.vrr_supported = true;
    auto w = all_good();
    w.windows_vrr = true;
    const auto v = frame_sync_checks({d}, w);
    const auto* c = find(v, "display.vrr_sync");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->section == Section::Display);
    EF_CHECK(c->text_key == "chk.display.vrr_sync_advice");
    EF_CHECK(c->args.at("cap") == "141");
    EF_CHECK(c->args.at("hz") == "144");
    EF_CHECK(c->args.at("name") == "DELL S2721DGF");
    EF_CHECK(c->action == ActionKind::None && c->action_arg.empty());
}

EF_TEST(readiness_frame_sync_uses_vrr_display_then_rounds_cap) {
    auto a = disp(0, 60.0, 60.0);
    a.vrr_supported = true;
    auto b = disp(1, 143.981, 144.0);
    b.name = "SECOND";
    auto w = all_good();
    w.windows_vrr = true;
    // VRR bildiren ekran (60 Hz) referanstir; VRR'siz 144 Hz ekran secilmez.
    auto v = frame_sync_checks({a, b}, w);
    const auto* c = find(v, "display.vrr_sync");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->args.at("hz") == "60");
    EF_CHECK(c->args.at("cap") == "57");
    EF_CHECK(c->args.at("name") == "DELL S2721DGF");
    // Hicbir ekran VRR bildirmiyorsa (yalniz Windows anahtari) en yuksek Hz kullanilir.
    a.vrr_supported.reset();
    v = frame_sync_checks({a, b}, w);
    EF_CHECK(find(v, "display.vrr_sync")->args.at("hz") == "144");
    EF_CHECK(find(v, "display.vrr_sync")->args.at("cap") == "141");
    // 60 Hz -> 57; 24 Hz -> 21 (taban 1 FPS)
    auto d = disp(0, 59.94, 59.94);
    d.vrr_supported = true;
    auto v2 = frame_sync_checks({d}, w);
    EF_CHECK(find(v2, "display.vrr_sync")->args.at("cap") == "57");
    EF_CHECK(find(v2, "display.vrr_sync")->args.at("hz") == "60");
    d = disp(0, 24.0, 24.0);
    d.vrr_supported = true;
    v2 = frame_sync_checks({d}, w);
    EF_CHECK(find(v2, "display.vrr_sync")->args.at("cap") == "21");
    d = disp(0, 1.0, 1.0);
    d.vrr_supported = true;
    v2 = frame_sync_checks({d}, w);
    EF_CHECK(find(v2, "display.vrr_sync")->args.at("cap") == "1");
}

EF_TEST(readiness_frame_sync_vrr_without_hz_is_generic) {
    auto d = disp(0, std::nullopt, std::nullopt);
    d.vrr_supported = true;
    const auto v = frame_sync_checks({d}, all_good());  // windows_vrr bilinmiyor
    const auto* c = find(v, "display.vrr_sync");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->text_key == "chk.display.vrr_sync_advice_nohz");
    EF_CHECK(c->args.count("cap") == 0 && c->args.count("hz") == 0);
    EF_CHECK(c->action == ActionKind::None);
}

EF_TEST(readiness_frame_sync_windows_vrr_off_is_advice) {
    auto d = disp(0, 144.0, 144.0);
    d.vrr_supported = true;
    auto w = all_good();
    w.windows_vrr = false;
    auto v = frame_sync_checks({d}, w);
    const auto* c = find(v, "display.vrr_sync");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Advice);
    EF_CHECK(c->text_key == "chk.display.vrr_sync_win_off");
    EF_CHECK(c->args.at("cap") == "141");
    EF_CHECK(c->action == ActionKind::None);
    // Windows anahtari VRR destekli sistemde var; ekran durumu bilinmese de oneri verilir.
    d.vrr_supported.reset();
    v = frame_sync_checks({d}, w);
    EF_CHECK(find(v, "display.vrr_sync") != nullptr);
    // Hz yoksa genel metin, cap uydurulmaz
    d.hz_max_same_res.reset();
    d.hz_current.reset();
    v = frame_sync_checks({d}, w);
    c = find(v, "display.vrr_sync");
    EF_CHECK(c && c->text_key == "chk.display.vrr_sync_win_off_nohz");
    EF_CHECK(c && c->args.count("cap") == 0);
}

EF_TEST(readiness_frame_sync_unknown_makes_no_claim) {
    auto d = disp(0, 144.0, 144.0);
    EF_CHECK(frame_sync_checks({d}, all_good()).empty());
    EF_CHECK(frame_sync_checks({}, all_good()).empty());
    // Karisik: bir ekran VRR yok, digeri bilinmiyor -> "VRR yok" iddiasi yok
    auto a = disp(0, 144.0, 144.0);
    a.vrr_supported = false;
    auto b = disp(1, 60.0, 60.0);
    EF_CHECK(frame_sync_checks({a, b}, all_good()).empty());
}

EF_TEST(readiness_frame_sync_no_vrr_explains_options) {
    auto d = disp(0, 144.0, 144.0);
    d.vrr_supported = false;
    const auto v = frame_sync_checks({d}, all_good());
    const auto* c = find(v, "display.vrr_sync");
    EF_CHECK(c != nullptr);
    if (!c) return;
    EF_CHECK(c->level == Level::Info);
    EF_CHECK(c->text_key == "chk.display.no_vrr_vsync");
    EF_CHECK(c->action == ActionKind::None);
    for (const auto& x : v) EF_CHECK(x.action != ActionKind::AutoFix);
}

EF_TEST(readiness_display_multi_ids_and_name_fallback) {
    auto a = disp(0, 60.0, 144.0);
    auto b = disp(1, 60.0, 60.0);
    b.name.clear();
    const auto v = display_checks({a, b});
    const auto* c0 = find(v, "display.0.refresh");
    const auto* c1 = find(v, "display.1.refresh");
    EF_CHECK(c0 && c0->level == Level::Problem);
    EF_CHECK(c1 && c1->level == Level::Good);
    EF_CHECK(c1 && c1->args.at("name") == "#2");
    for (const auto& c : v) EF_CHECK(c.text_key.rfind("chk.", 0) == 0);
}

// ---------------- Windows ----------------

EF_TEST(readiness_windows_all_good) {
    const auto v = windows_checks(all_good());
    for (const auto& c : v) {
        EF_CHECK(c.section == Section::Windows);
        EF_CHECK(c.level == Level::Good);
        EF_CHECK(c.text_key.rfind("chk.win.", 0) == 0);
    }
    EF_CHECK(find(v, "windows.game_mode") && find(v, "windows.game_dvr") && find(v, "windows.windowed_opt") &&
              find(v, "windows.hags") && find(v, "windows.fast_startup") && find(v, "windows.power"));
    EF_CHECK(find(v, "windows.fast_startup")->text_key == "chk.win.fast_startup_off");
    EF_CHECK(find(v, "windows.vbs") == nullptr);  // ikisi de kapali: bulgu yok
    EF_CHECK(find(v, "windows.vrr") == nullptr);  // bilinmiyor: gurultu yok
}

EF_TEST(readiness_windows_advice_autofix_keys) {
    auto w = all_good();
    w.game_mode = false;
    w.game_dvr_background = true;
    w.windowed_optimizations = false;
    w.hags_enabled = false;
    w.fast_startup = true;
    w.power_mode = "best_efficiency";
    const auto v = windows_checks(w);
    const struct {
        const char* id;
        const char* fix;
    } want[] = {{"windows.game_mode", "fix.game_mode"},
                {"windows.game_dvr", "fix.game_dvr"},
                {"windows.windowed_opt", "fix.windowed_opt"},
                {"windows.hags", "fix.hags"},
                {"windows.fast_startup", "fix.fast_startup"},
                {"windows.power", "fix.power"}};
    for (const auto& e : want) {
        const auto* c = find(v, e.id);
        EF_CHECK(c != nullptr);
        if (!c) continue;
        EF_CHECK(c->level == Level::Advice);
        EF_CHECK(c->action == ActionKind::AutoFix);
        EF_CHECK(c->action_arg == e.fix);
    }
    const auto* h = find(v, "windows.hags");
    EF_CHECK(h && h->args.count("restart") == 1);
    const auto* fs = find(v, "windows.fast_startup");
    EF_CHECK(fs && fs->args.count("restart") == 0);  // bir sonraki kapanista gecerli; yeniden baslatma istemez
    w.power_mode = "power_saver";
    const auto v2 = windows_checks(w);
    const auto* p2 = find(v2, "windows.power");
    EF_CHECK(p2 && p2->level == Level::Advice && p2->text_key == "chk.win.power_saver");
}

EF_TEST(readiness_windows_unknowns) {
    const WindowsGamingInfo w;  // hepsi nullopt
    const auto v = windows_checks(w);
    for (const char* id : {"windows.game_mode", "windows.game_dvr", "windows.windowed_opt", "windows.hags", "windows.vbs",
                           "windows.power"}) {
        const auto* c = find(v, id);
        EF_CHECK(c != nullptr);
        EF_CHECK(c && c->level == Level::Unknown);
        EF_CHECK(c && c->action != ActionKind::AutoFix);
    }
    EF_CHECK(find(v, "windows.vrr") == nullptr);
    EF_CHECK(find(v, "windows.fast_startup") == nullptr);  // bilinmiyor: bulgu yok, tahmin yok
    // HAGS destekli ama durum okunamadi -> Unknown; desteklenmiyor -> Info (oneri yok)
    auto g = all_good();
    g.hags_enabled.reset();
    const auto v2 = windows_checks(g);
    EF_CHECK(find(v2, "windows.hags")->level == Level::Unknown);
    g.hags_supported = false;
    const auto v3 = windows_checks(g);
    EF_CHECK(find(v3, "windows.hags")->level == Level::Info);
    EF_CHECK(find(v3, "windows.hags")->action == ActionKind::None);
}

EF_TEST(readiness_windows_vbs_hvci_info_only) {
    auto w = all_good();
    w.hvci_enabled = true;
    w.vbs_running = true;
    auto v = windows_checks(w);
    auto* c = find(v, "windows.vbs");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.win.hvci_on");
    EF_CHECK(c && c->action == ActionKind::None && c->action_arg.empty());
    w.hvci_enabled = false;
    v = windows_checks(w);
    c = find(v, "windows.vbs");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.win.vbs_on");
    // Hicbir durumda VBS/HVCI icin eylem yok (otomatik duzeltme de ayar baglantisi da yok)
    EF_CHECK(c && c->action == ActionKind::None && c->action_arg.empty());
    // Biri bilinmiyor, digeri kapali -> bulgu yok (tahmin yok, sorun yok)
    w.hvci_enabled.reset();
    w.vbs_running = false;
    EF_CHECK(find(windows_checks(w), "windows.vbs") == nullptr);
}

EF_TEST(readiness_windows_power_modes_and_vrr) {
    auto w = all_good();
    for (const char* m : {"best_performance", "better_performance", "high_performance", "ultimate"}) {
        w.power_mode = m;
        const auto v = windows_checks(w);
        EF_CHECK(find(v, "windows.power")->level == Level::Good);
    }
    w.power_mode = "balanced";
    auto v = windows_checks(w);
    EF_CHECK(find(v, "windows.power")->level == Level::Info);
    w.power_mode = "OEM Oyun Plani";
    v = windows_checks(w);
    const auto* c = find(v, "windows.power");
    EF_CHECK(c->level == Level::Info && c->args.at("plan") == "OEM Oyun Plani" && c->action == ActionKind::OpenSettings);
    w.windows_vrr = false;
    v = windows_checks(w);
    EF_CHECK(find(v, "windows.vrr") && find(v, "windows.vrr")->level == Level::Info);
    w.windows_vrr = true;
    v = windows_checks(w);
    EF_CHECK(find(v, "windows.vrr") && find(v, "windows.vrr")->level == Level::Good);
}

EF_TEST(readiness_windows_sorted_problem_first) {
    auto w = all_good();
    w.game_mode = false;
    auto v = display_checks({disp(0, 60.0, 144.0)});
    const auto wv = windows_checks(w);
    v.insert(v.end(), wv.begin(), wv.end());
    sort_checks(v);
    EF_CHECK(v.front().level == Level::Problem && v.front().section == Section::Display);
}

// ---------------- Platform saf yardimcilari ----------------

EF_TEST(readiness_dx_user_settings_parse) {
    const std::string s = "SwapEffectUpgradeEnable=1;VRROptimizeEnable=0;";
    EF_CHECK(parse_dx_user_setting(s, "SwapEffectUpgradeEnable") == std::optional<bool>(true));
    EF_CHECK(parse_dx_user_setting(s, "VRROptimizeEnable") == std::optional<bool>(false));
    EF_CHECK(!parse_dx_user_setting(s, "AutoHDREnable").has_value());
    EF_CHECK(parse_dx_user_setting(" swapeffectupgradeenable = 0 ", "SwapEffectUpgradeEnable") == std::optional<bool>(false));
    EF_CHECK(!parse_dx_user_setting("", "SwapEffectUpgradeEnable").has_value());
    EF_CHECK(!parse_dx_user_setting("SwapEffectUpgradeEnable=yes;", "SwapEffectUpgradeEnable").has_value());
    EF_CHECK(!parse_dx_user_setting("SwapEffectUpgradeEnable;", "SwapEffectUpgradeEnable").has_value());
    EF_CHECK(parse_dx_user_setting("A=1;;SwapEffectUpgradeEnable=1", "SwapEffectUpgradeEnable") == std::optional<bool>(true));
    // Ayni anahtar iki kez: sonuncusu gecerli
    EF_CHECK(parse_dx_user_setting("VRROptimizeEnable=1;VRROptimizeEnable=0;", "VRROptimizeEnable") == std::optional<bool>(false));
    // Ad oneki eslesmesi yanlis pozitif vermez
    EF_CHECK(!parse_dx_user_setting("SwapEffectUpgradeEnableX=1;", "SwapEffectUpgradeEnable").has_value());
}

EF_TEST(readiness_game_dvr_background_derivation) {
    using U = std::optional<std::uint32_t>;
    const U none;
    EF_CHECK(game_dvr_background_from(none, none, none, none) == std::optional<bool>(false));  // varsayilan kapali
    EF_CHECK(game_dvr_background_from(none, U(1), U(1), U(1)) == std::optional<bool>(true));
    EF_CHECK(game_dvr_background_from(none, none, none, U(1)) == std::optional<bool>(true));
    EF_CHECK(game_dvr_background_from(U(0), U(1), U(1), U(1)) == std::optional<bool>(false));  // politika
    EF_CHECK(game_dvr_background_from(U(1), U(1), U(1), U(1)) == std::optional<bool>(true));
    EF_CHECK(game_dvr_background_from(none, U(0), U(0), U(1)) == std::optional<bool>(false));  // ana anahtar kapali
    EF_CHECK(game_dvr_background_from(none, U(0), none, U(1)) == std::optional<bool>(false));
    // Iki ana anahtar celisiyor -> bilinmiyor (tahmin yok)
    EF_CHECK(!game_dvr_background_from(none, U(0), U(1), U(1)).has_value());
    EF_CHECK(!game_dvr_background_from(none, U(1), U(0), U(1)).has_value());
    // Celiski olsa da politika ya da "Olanlari kaydet" kapaliysa sonuc kesin: kapali
    EF_CHECK(game_dvr_background_from(U(0), U(0), U(1), U(1)) == std::optional<bool>(false));
    EF_CHECK(game_dvr_background_from(none, U(0), U(1), U(0)) == std::optional<bool>(false));
    // Yalniz biri kayitli ve kapali -> kapali; yalniz biri kayitli ve acik -> acik
    EF_CHECK(game_dvr_background_from(none, none, U(0), U(1)) == std::optional<bool>(false));
    EF_CHECK(game_dvr_background_from(none, U(1), none, U(1)) == std::optional<bool>(true));
    EF_CHECK(game_dvr_background_from(none, U(1), U(1), U(0)) == std::optional<bool>(false));
}

EF_TEST(readiness_game_mode_derivation) {
    using U = std::optional<std::uint32_t>;
    const U none;
    EF_CHECK(game_mode_from(U(1), none) == std::optional<bool>(true));
    EF_CHECK(game_mode_from(U(0), none) == std::optional<bool>(false));
    EF_CHECK(game_mode_from(U(1), U(0)) == std::optional<bool>(true));   // yeni ad onceliklidir
    EF_CHECK(game_mode_from(none, U(0)) == std::optional<bool>(false));  // AutoGameModeEnabled yok -> AllowAutoGameMode
    EF_CHECK(game_mode_from(none, U(1)) == std::optional<bool>(true));
    EF_CHECK(game_mode_from(none, none) == std::optional<bool>(true));   // varsayilan acik
}

EF_TEST(readiness_power_mode_derivation) {
    using S = std::optional<std::string>;
    const S none;
    const S balanced("381b4222-f694-41f0-9685-ff5bb260df2e");
    const S high("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    const S saver("a1841308-3541-4fab-bc81-f71556f20b4a");
    const S ultimate("e9a42b02-d5df-448d-aa00-03f14749eb61");
    const S custom("0b8f1a2c-1111-2222-3333-444455556666");
    const S ov_eff("961cc777-2547-4f9d-8174-7d86181b8a7a");
    const S ov_zero("00000000-0000-0000-0000-000000000000");
    const S ov_best("ded574b5-45a0-4f42-8737-46345c09c238");
    const S ov_odd("12345678-0000-0000-0000-000000000000");
    // Balanced, no overlay
    EF_CHECK(power_mode_from(balanced, S("Dengeli"), none) == S("balanced"));
    EF_CHECK(power_mode_from(balanced, S("Dengeli"), ov_zero) == S("balanced"));
    EF_CHECK(power_mode_from(balanced, S("Dengeli"), ov_odd) == S("balanced"));
    EF_CHECK(power_mode_from(balanced, S("Dengeli"), ov_eff) == S("best_efficiency"));
    EF_CHECK(power_mode_from(balanced, S("Dengeli"), ov_best) == S("best_performance"));
    // Custom plan, overlay present: katman yok sayilir, plan adi raporlanir (asla "balanced" + AutoFix)
    EF_CHECK(power_mode_from(custom, S("OEM Oyun Plani"), ov_zero) == S("OEM Oyun Plani"));
    EF_CHECK(power_mode_from(custom, S("OEM Oyun Plani"), ov_eff) == S("OEM Oyun Plani"));
    EF_CHECK(!power_mode_from(custom, none, ov_zero).has_value());
    WindowsGamingInfo w = all_good();
    w.power_mode = power_mode_from(custom, S("OEM Oyun Plani"), ov_zero);
    const auto v = windows_checks(w);
    const auto* c = find(v, "windows.power");
    EF_CHECK(c && c->text_key == "chk.win.power_custom" && c->action != ActionKind::AutoFix);
    // Sabit planlar: katmandan bagimsiz
    EF_CHECK(power_mode_from(high, none, ov_eff) == S("high_performance"));
    EF_CHECK(power_mode_from(saver, none, ov_best) == S("power_saver"));
    EF_CHECK(power_mode_from(ultimate, none, none) == S("ultimate"));
    // Plan bilinmiyor -> bilinmiyor (katmandan tahmin yok)
    EF_CHECK(!power_mode_from(none, S("Dengeli"), ov_best).has_value());
}

EF_TEST(readiness_power_custom_plan_named_like_key_gets_no_autofix) {
    using S = std::optional<std::string>;
    using K = WindowsGamingInfo::PowerPlanKind;
    const S balanced("381b4222-f694-41f0-9685-ff5bb260df2e");
    const S custom("0b8f1a2c-1111-2222-3333-444455556666");
    EF_CHECK(power_plan_kind_from(balanced) == K::Builtin);
    EF_CHECK(power_plan_kind_from(S("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c")) == K::Builtin);
    EF_CHECK(power_plan_kind_from(custom) == K::Custom);
    EF_CHECK(power_plan_kind_from(std::nullopt) == K::Unknown);

    // Kullanici ozel planini dahili anahtarlardan biriyle adlandirmis: daima ozel plan, AutoFix yok.
    for (const char* name : {"balanced", "power_saver", "best_efficiency", "best_performance"}) {
        WindowsGamingInfo w = all_good();
        w.power_mode = power_mode_from(custom, S(name), std::nullopt);
        w.power_plan_kind = power_plan_kind_from(custom);
        const auto v = windows_checks(w);
        const auto* c = find(v, "windows.power");
        EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.win.power_custom");
        EF_CHECK(c && c->args.at("plan") == name);
        EF_CHECK(c && c->action == ActionKind::OpenSettings && c->action_arg == "ms-settings:powersleep");
    }

    // Gercek Dengeli plan: AutoFix korunur.
    WindowsGamingInfo w = all_good();
    w.power_mode = power_mode_from(balanced, S("Dengeli"), std::nullopt);
    w.power_plan_kind = power_plan_kind_from(balanced);
    const auto v1 = windows_checks(w);
    const auto* c = find(v1, "windows.power");
    EF_CHECK(c && c->text_key == "chk.win.power_balanced" && c->action == ActionKind::AutoFix &&
              c->action_arg == "fix.power");

    // Plan turu bilinmiyor: ayni metin ama otomatik duzeltme yerine Ayarlar.
    w.power_plan_kind = K::Unknown;
    w.power_mode = "best_efficiency";
    const auto v2 = windows_checks(w);
    c = find(v2, "windows.power");
    EF_CHECK(c && c->text_key == "chk.win.power_efficiency" && c->action == ActionKind::OpenSettings);
}

// Panel Sistem karti "Ekran" hucresi: birincil ekranin okunan alanlari; okunamayan parca yazilmaz.
EF_TEST(display_summary_primary_and_missing_parts) {
    EF_CHECK(display_summary({}).empty());
    auto a = disp(0, 59.95, 165.0);
    a.name = "Yan Ekran";
    a.width = 1920, a.height = 1080;
    auto b = disp(1, 164.98, 165.0);
    b.primary = true;
    // Birincil ekran secilir; Hz tam sayiya yakinsa tam sayi.
    EF_CHECK(display_summary({a, b}) == "DELL S2721DGF · 2560×1440 · 165 Hz");
    // Birincil isaretli yoksa ilk etkin yol.
    EF_CHECK(display_summary({a}) == "Yan Ekran · 1920×1080 · 60 Hz");
    // Ad yok / Hz bilinmiyor / cozunurluk 0: o parca yazilmaz, sayi uydurulmaz.
    auto c = disp(0, std::nullopt, std::nullopt);
    c.name.clear();
    EF_CHECK(display_summary({c}) == "2560×1440");
    c.width = 0;
    EF_CHECK(display_summary({c}).empty());
    auto d = disp(0, 143.5, 144.0);
    d.width = 0, d.height = 0;
    EF_CHECK(display_summary({d}) == "DELL S2721DGF · 143.50 Hz");
}
