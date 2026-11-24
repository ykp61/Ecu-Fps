// UI-01 kabul araci: kabugu ekran disi (WIC) cizer; 100/125/150/200 DPI, en kucuk ve varsayilan
// pencere, tum sayfalar, tr-TR ve en-US icin metin tasmasini denetler. --out <klasor> ile PNG yazar.
// Cikis: 0 tasma yok, 1 tasma veya buyuk dugme bulundu, 2 altyapi hatasi.

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/diagnostics/health_test.h"
#include "core/i18n/catalog.h"
#include "core/service/uac_copy.h"
#include "core/insights/history_compare.h"
#include "core/insights/peer.h"
#include "core/measure/bench_text.h"
#include "ui/brand/brand.h"
#include "core/drivers/driver_install.h"
#include "core/drivers/vendor_install.h"
#include "ui/consent/consent_dialog.h"
#include "ui/consent/fix_dialogs.h"
#include "core/setup/setup_flow.h"
#include "ui/render/graphics.h"
#include "ui/setup/setup_view.h"
#include "ui/shell/screen.h"
#include "ui/theme/system_palette.h"
#include "ui/theme/theme.h"

using Microsoft::WRL::ComPtr;
using namespace ef::ui;
namespace core = ef::core;
namespace i18n = ef::core::i18n;

namespace {

// Spec 2026-11-12 §4: ikonlu durum icin KODLA uretilmis pikseller (dosya, kurulu oyun veya gercek logo yok).
// Bicim yukleyicinin ciktisiyla ayni: 64x64 on-carpilmis BGRA. Merkezde duz renkli bir bolge vardir (denetim icin).
//   0: opak, dikey gecis + duz daire (Steam kitaplik JPEG'i gibi kare ve opak)
//   1: saydam zeminde kenari yumusatilmis eskenar dortgen + ic cekirdek (EXE ikon kaynagi gibi alfa kanalli)
//   2: opak, kosegen iki ton + duz kare
struct IconRgb {
    float r, g, b;
};
constexpr IconRgb kIconCentre[3] = {{240, 150, 40}, {70, 200, 255}, {235, 235, 245}};
ImageRef generated_icon(int style) {
    constexpr std::uint32_t n = 64;
    auto img = std::make_shared<ImagePixels>();
    img->w = img->h = n;
    img->bgra.resize(static_cast<std::size_t>(n) * n * 4);
    const auto cover = [](float d) { return std::clamp(0.5f - d, 0.f, 1.f); };  // d: kenara isaretli uzaklik (piksel)
    for (std::uint32_t y = 0; y < n; ++y)
        for (std::uint32_t x = 0; x < n; ++x) {
            const float fx = static_cast<float>(x) + 0.5f - 32.f, fy = static_cast<float>(y) + 0.5f - 32.f;
            const float t = static_cast<float>(y) / static_cast<float>(n - 1);
            IconRgb c{0, 0, 0};
            float a = 1;
            const auto over = [&c](const IconRgb& top, float k) {
                c = {c.r + (top.r - c.r) * k, c.g + (top.g - c.g) * k, c.b + (top.b - c.b) * k};
            };
            if (style == 0) {
                c = {40 + 30 * t, 50 + 40 * t, 110 + 90 * t};
                over(kIconCentre[0], cover(std::sqrt(fx * fx + fy * fy) - 18.f));
            } else if (style == 1) {
                const float d = (std::fabs(fx) + std::fabs(fy)) * 0.70710678f;  // eskenar dortgen kenarina uzaklik
                a = cover(d - 20.f);
                c = {30, 90, 200};
                over(kIconCentre[1], cover(d - 10.f));
            } else {
                c = fx + fy < 0 ? IconRgb{150, 40, 60} : IconRgb{90, 30, 110};
                over(kIconCentre[2], cover(std::max(std::fabs(fx), std::fabs(fy)) - 12.f));
            }
            std::uint8_t* px = &img->bgra[(static_cast<std::size_t>(y) * n + x) * 4];
            px[0] = static_cast<std::uint8_t>(c.b * a + 0.5f);
            px[1] = static_cast<std::uint8_t>(c.g * a + 0.5f);
            px[2] = static_cast<std::uint8_t>(c.r * a + 0.5f);
            px[3] = static_cast<std::uint8_t>(255.f * a + 0.5f);
        }
    return img;
}

ViewModel fixture(bool loading) {
    ViewModel vm;
    vm.version = "0.1.0";
    vm.system_loading = loading;
    vm.findings_loading = loading;
    if (loading) return vm;
    vm.os = "Windows 11 Pro 26H2 (26300.9711)";
    vm.cpu = "AMD Ryzen 7 9800X3D 8-Core Processor · 8C/8T";
    vm.gpu = "NVIDIA GeForce RTX 5080 · 16 GB\nAMD Radeon(TM) Graphics";
    vm.ram = "64 GB · 2×32 GB DDR5 6000 MT/s";
    vm.board = "Gigabyte Technology Co., Ltd. X870E AORUS ELITE WIFI7 ICE · BIOS F11b";
    vm.display = "DELL S2721DGF · 2560×1440 · 165 Hz";  // Sistem karti "Ekran" hucresi (core::readiness::display_summary bicimi)
    // Spec 2026-11-12 H: Guncellemeler uretici satirlari (iki ekran karti ureticisi, yonga seti, BIOS). Arama varsayilan
    // olarak "Denetlenmedi" (sayfanin ilk durumu); diger durumlar L4 Guncellemeler vakalarinda.
    vm.updates.gpus = {{"NVIDIA GeForce RTX 5080", 0x10DE, "32.0.15.6094"}, {"AMD Radeon(TM) Graphics", 0x1002, "32.0.21025.10016"}};
    vm.updates.cpu_vendor = "AuthenticAMD";
    vm.updates.board_vendor = "Gigabyte Technology Co., Ltd.";
    vm.updates.board_model = "X870E AORUS ELITE WIFI7 ICE";
    vm.updates.bios_version = "F11b";
    vm.updates.bios_date = "05/20/2025";
    // UI-09: canli veri (biri olculemez: GPU sayaci yok); 4 birim, biri kritik dolu.
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;
    vm.live.cpu_pct = Measurement::of(23.4, Unit::Percent);
    vm.live.cpu_max_core_pct = Measurement::of(41.2, Unit::Percent);
    vm.live.gpu_pct = Measurement::missing(Unit::Percent, MissingReason::NotSupported);
    vm.live.ram_pct = Measurement::of(31.6, Unit::Percent);
    vm.live.disk_pct = Measurement::of(96.0, Unit::Percent);
    vm.live.ram_used_mib = 20000;
    vm.live.ram_total_mib = 63123;
    // LED tepe izi: CPU'da onceki tepe (%78 > %23,4: ust parca soluk yanar); GPU olculemezken iz cizilmez.
    vm.live_peak_pct[0] = 78.0;
    vm.live_peak_pct[2] = 90.0;
    // Kutucuk ince grafigi: son canli orneklerin yuzdesi (GPU olculemedi: NaN -> grafik yok; DISK'te bir eksik ornek
    // bosluk birakir). Degerler kutucuk yuzdesinin gercek bicimindedir (ag: indirme / baglanti).
    for (int i = 0; i < 40; ++i) {
        const float k = static_cast<float>(i);
        vm.live_history[0].push_back(23.4f + 12.f * std::sin(k * 0.35f) + (i % 7 == 0 ? 18.f : 0.f));
        vm.live_history[1].push_back(31.0f + 0.6f * std::sin(k * 0.2f));
        vm.live_history[2].push_back(std::numeric_limits<float>::quiet_NaN());
        vm.live_history[3].push_back(i == 20 ? std::numeric_limits<float>::quiet_NaN() : 60.f + 36.f * std::fabs(std::sin(k * 0.15f)));
        vm.live_history[4].push_back(91.2f * (0.4f + 0.6f * std::fabs(std::cos(k * 0.25f))));
    }
    // Task 10: disk akisi ve ag (en genis alt satirlar; ag baglanti bilinmiyor -> kelime/cubuk yok degil, sayi var).
    vm.live.disk_read_mbs = Measurement::of(1234.5, Unit::MBps);
    vm.live.disk_write_mbs = Measurement::of(987.6, Unit::MBps);
    vm.live.net_down_mbps = Measurement::of(912.4, Unit::Mbps);
    vm.live.net_up_mbps = Measurement::of(48.25, Unit::Mbps);
    vm.live.net_link_mbps = Measurement::of(1000.0, Unit::Mbps);
    vm.live.volumes = {{"C:\\", 952, 643}, {"D:\\", 931, 797}, {"E:\\", 931, 60}, {"F:\\", 1862, 1662}};
    // DIAG-08: depolama (uzun model, HDD'de oyun, kritik birim)
    {
        namespace ci = ef::core::inventory;
        ci::VolumeAdvice c;
        c.root = "C:\\";
        c.total_gib = 952, c.free_gib = 643, c.free_pct = 67.5, c.status = ci::SpaceStatus::Ok;
        c.bus = ci::BusKind::Nvme, c.media = ci::MediaKind::Ssd, c.disk_model = "Samsung SSD 990 PRO with Heatsink 1TB (uzun model adi testi)";
        ci::VolumeAdvice e = c;
        e.root = "E:\\";
        e.total_gib = 931, e.free_gib = 55, e.free_pct = 5.9, e.status = ci::SpaceStatus::Critical;
        e.bus = ci::BusKind::Sata, e.media = ci::MediaKind::Hdd, e.disk_model = "WDC WD10EZEX";
        e.games = 7, e.largest_game = "Microsoft Flight Simulator 2024", e.largest_game_gib = 180, e.recommended_free_gib = 45, e.games_on_hdd = true;
        vm.storage = {c, e};
        vm.storage_loading = false;
    }
    // Oyunlar sayfasi (handoff EXT-AC): calisan oyun + uzun ad + bilinmeyen surum
    vm.games_loading = loading;
    vm.games = {{"Counter-Strike 2", "steam", "21093427", "c2", true, true, "gate.capture_unverified", "cs2"},
                {"Microsoft Flight Simulator 2024 — Standard Edition Uzun Ad Denemesi", "steam", "", "m", false, false, "gate.capture_unknown"},
                {"Fortnite", "epic", "++Fortnite+Release-33.10", "f", false, false, "gate.capture_unknown"}};
    // F-47: uc durum da cizilir — uygulanmis ("Geri al"), acik ("Kapat") ve EXE yolu bilinmiyor (dugme yok).
    vm.games[0].fs_known = true, vm.games[0].fs_disabled = true, vm.games[0].fs_applied = true;
    vm.games[0].fs_target = "appcompat:633a5c67616d65735c6373325c6373322e657865";
    vm.games[1].fs_known = true, vm.games[1].fs_disabled = false;
    vm.games[1].fs_target = "appcompat:633a5c67616d65735c6d7366735c666c6967687473696d756c61746f722e657865";
    vm.games_notice_key = "games.fs.notice_applied";
    // EF-X11: kaynak karti + elle eklenen oyun satiri ("Listeden cikar")
    vm.library_providers = {{"steam", "games.source.found", 2}, {"epic", "games.source.found", 1}, {"gog", "games.source.disabled", 0},
                            {"xbox", "games.source.disabled", 0}, {"ea", "games.source.unsupported", 0}, {"manual", "games.source.found", 1}};
    vm.games.push_back({"Contoso Racing", "manual", "", "r", false, false, "gate.capture_unknown"});
    vm.games.back().manual_exe = "D:\\Games\\Racing\\racing.exe";
    // Islem Gecmisi: gercek journal bicimindeki degerler (GUID, enabled/disabled, hex ad)
    {
        namespace cr = ef::core::recovery;
        using ef::contracts::OperationId;
        vm.history.loading = loading;
        cr::HistoryTxRow a{"user", "a1", 1762300000000, cr::HistoryStatus::Active, {}};
        a.steps.push_back({OperationId::PowerSchemeSet, "power:scheme:active", true, "a1841308-3541-4fab-bc81-f71556f20b4a",
                           "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", cr::StepState::Verified, false});
        a.steps.push_back({OperationId::VisualEffectsSet, "vfx:animations", true, "1", "0", cr::StepState::Verified, false});
        cr::HistoryTxRow b{"startup", "b2", 1762200000000, cr::HistoryStatus::PartlyRestored, {}};
        b.steps.push_back({OperationId::StartupRunEntry, "startup:user_run:537465616d", true, "enabled", "disabled", cr::StepState::KeptExternal, false});
        cr::HistoryTxRow c{"user", "c3", 1762100000000, cr::HistoryStatus::RecoveryRequired, {}};
        c.steps.push_back({OperationId::PowerModeSet, "power:mode", false, "", "", cr::StepState::Failed, true});
        vm.history.view.rows = {a, b, c};
        vm.history.view.total = 3;
        vm.history.view.corrupt = 1;
        // R16: yerel test gecmisi (gercek metrik anahtarlari; biri eksik, biri 0 taban, kosul izi farkli).
        namespace ci = ef::core::insights;
        ci::HistoryEntry t1;
        t1.when_unix_ms = 1762300000000;
        t1.source = "in_game";
        t1.title = "Counter-Strike 2";
        t1.fingerprint = "fp-cs2-a";
        t1.app_version = "0.1.0";
        t1.scene_version = "game_capture";
        t1.protocol_version = "capture_v3";
        t1.run_valid = true;
        t1.metrics = {{"avg_fps", 241.6}, {"low1_fps", 180.2}, {"p99_ms", 8.4}, {"drop_windows", 2.0}};
        ci::HistoryEntry t2 = t1;
        t2.when_unix_ms = 1762200000000;
        t2.metrics = {{"avg_fps", 236.1}, {"p99_ms", 8.6}, {"drop_windows", 0.0}};  // low1_fps OLCULMEDI
        t2.exclusions = {"run.too_short"};
        ci::HistoryEntry t3 = t1;
        t3.when_unix_ms = 1762100000000;
        t3.source = "builtin";
        t3.title = "scene_v1 high";
        t3.fingerprint = "fp-builtin-b";
        t3.scene_version = "scene_v3";
        t3.protocol_version = "bench_v1";
        t3.metrics = {{"scene_avg_fps", 144.0}, {"scene_low1_fps", 121.0}, {"scene_p99_ms", 9.8}};
        t3.exclusions.clear();
        ci::HistoryEntry t4 = t3;
        t4.when_unix_ms = 1762000000000;
        t4.fingerprint = "fp-builtin-b";
        vm.history.tests = {t1, t2, t3, t4};
        vm.history.tests_total = 27;  // "en yeni N (toplam M)" satiri da cizilir
        vm.history.selected = {0, 1};
        vm.history.comparison = ci::compare_history(t2, t1);  // eski -> yeni; ayni kaynak + iz
        vm.history.peer_query.source = t1.source;
        vm.history.peer_query.fingerprint = t1.fingerprint;
        vm.history.peer = ci::compare_peers(vm.history.peer_query, {});  // yerel havuz yok: N=0 Insufficient
        vm.history.now_unix_ms = 1762300000000 + 86400000;  // B13 gun basliklari sabit: ilk kayit "Dun"
    }
    // REV-2026-11-09: Genel Bakis "Son olcum" karti (gercek ResultView; halkalardan once cizilir).
    {
        namespace cm = core::measure;
        cm::ResultView last;
        last.source = cm::TestSource::InGame;
        last.title = "Counter-Strike 2";
        last.when_unix_ms = 1762300000000;
        last.headline_key = "measure.result.insufficient";
        last.show_confidence = true;
        last.avg_fps = Measurement::of(241.6, Unit::Fps);
        last.low1_fps = Measurement::of(180.2, Unit::Fps);
        last.p99_ms = Measurement::of(8.4, Unit::Milliseconds);
        last.run_valid = true;
        // Kayitli saniye basina FPS (Panel ritim seridi); bir saniye ortalamanin %90'inin altinda.
        last.fps_series = {238.0, 244.5, 240.1, 246.9, 239.4, 247.2, 241.8, 205.3, 243.6, 245.0, 240.7, 242.2};
        vm.measure.history = {last};
    }
    // Oyun Hazirligi (CHK): her seviye ve eylem turu, uzun metin + parametre
    {
        namespace rd = ef::core::readiness;
        vm.readiness.loading = loading;
        vm.readiness.net_available = true;
        vm.readiness.checks = {
            // Gate P3b: gercek bulgu anahtarlari (yer tutucu seviye kelimeleri satir basligi gibi gorunuyordu).
            {"display.0.refresh", rd::Section::Display, rd::Level::Problem, "chk.display.below_max", {{"name", "DELL S2721DGF"}, {"hz", "60"}, {"max", "165"}, {"index", "0"}, {"device", "DISPLAY1"}},  // gercek tarama aygit adini da tasir (P12-B1)
             rd::ActionKind::AutoFix, "fix.refresh_rate"},
            {"display.0.hdr", rd::Section::Display, rd::Level::Info, "chk.display.hdr_on", {{"name", "DELL S2721DGF"}}, rd::ActionKind::None, ""},
            {"gfx.dx", rd::Section::Graphics, rd::Level::Good, "chk.graphics.dx12u", {}, rd::ActionKind::None, ""},
            {"gfx.driver", rd::Section::Graphics, rd::Level::Advice, "chk.graphics.driver_old", {{"months", "9"}, {"version", "566.36"}},
             rd::ActionKind::OpenUrl, "https://www.nvidia.com/Download/index.aspx"},
            {"win.vbs", rd::Section::Windows, rd::Level::Info, "chk.win.vbs_on", {}, rd::ActionKind::OpenSettings, "windowsdefender://coreisolation"},
            {"net.none", rd::Section::Network, rd::Level::Unknown, "chk.network.not_measured", {}, rd::ActionKind::RunNetworkTest, ""},
        };
        rd::sort_checks(vm.readiness.checks);
    }
    // En uzun metinler: tum saglik/guncelleme/tani anahtarlari ayni anda.
    for (std::size_t i = 0; i < i18n::kStringCount; ++i) {
        const auto k = i18n::key_of(static_cast<i18n::StringId>(i));
        if (k.starts_with("health.") || k.starts_with("update.") || k.starts_with("diag."))
            vm.findings.push_back({std::string(k), true});
    }
    return vm;
}

struct Variant {
    const wchar_t* name;
    PerfView view;
    ViewModel::BeforeAfterView ba{};  // OPT-08
    ViewModel::CheckView check{};      // OPT-07
    bool admin_without_uac = false;    // sahip raporu 2026-11-13: yardimci hizmet yolu ("UAC sorulmaz" metinleri)
};

std::vector<Variant> perf_variants() {
    using core::perf::Notice;
    using core::perf::Phase;
    auto v = [](Phase ph, Notice n = Notice::None) {
        PerfView p;
        p.phase = ph;
        p.notice = n;
        p.switch_on = ph == Phase::On || ph == Phase::ConfirmRestore || ph == Phase::Restoring;
        // UI-10 gruplari: biri degisecek, biri zaten uygun, biri kullanicinin "Hayir"i
        p.groups = {{"plan.power_mode", "plan.power_mode.desc", true, true, true},
                    {"plan.power_scheme", "plan.power_scheme.desc", true, false, true},
                    {"plan.vfx", "plan.vfx.desc", true, true, false}};
        return p;
    };
    std::vector<Variant> out;
    out.push_back({L"off", v(Phase::Off)});
    {
        using core::perf::CheckPhase;
        ViewModel::CheckView rv;
        rv.phase = CheckPhase::Review;
        rv.rows = {{"plan.power_mode", true, true}, {"plan.vfx", true, false}, {"health.xmp_off", false, false},
                   {"health.pcie_width", false, false}, {"health.bios_old", false, false}};
        out.push_back({L"check_review", v(Phase::Off), {}, rv});
        ViewModel::CheckView rp;
        rp.phase = CheckPhase::Report;
        core::perf::CheckReport rep;
        rep.outcome = core::perf::OutcomeKind::Active;
        rep.auto_applied = 1;
        rep.guided.resize(3);
        rp.report = rep;
        out.push_back({L"check_report", v(Phase::Off), {}, rp});
        ViewModel::CheckView ap;
        ap.phase = CheckPhase::Applying;
        ap.done = 1, ap.total = 2;
        out.push_back({L"check_applying", v(Phase::Off), {}, ap});
    }
    {
        // Koruma Modu (T07): Windows yazdigimiz degerleri geri cevirdi; "Yeniden uygula" / "Boyle kalsin" (ve guncelleme sonrasi metni)
        auto d = v(Phase::On, Notice::SomeOff);
        d.active = 4;
        d.drift_n = 3;
        out.push_back({L"drift_prompt", d});
        auto du = d;
        du.drift_update = true;
        out.push_back({L"drift_prompt_update", du});
        auto dc = v(Phase::Confirming);
        dc.item_keys = {"plan.power_mode", "plan.vfx"};
        dc.drift_confirm = true;
        out.push_back({L"drift_confirm", dc});
    }
    out.push_back({L"no_changes", v(Phase::Off, Notice::NoChanges)});
    out.push_back({L"denied", v(Phase::Off, Notice::Denied)});
    out.push_back({L"failed_restored", v(Phase::Off, Notice::FailedRestored)});
    out.push_back({L"plan_failed", v(Phase::Off, Notice::PlanFailed)});
    out.push_back({L"planning", v(Phase::Planning)});
    auto c = v(Phase::Confirming);
    c.item_keys = {"plan.power_mode", "plan.power_scheme"};
    out.push_back({L"confirm_simple", c});
    {
        auto cn = c;
        cn.groups.push_back({"plan.nvidia", "plan.nvidia.desc", true, true, true, {"plan.field.nv_power", "plan.field.nv_queue"}});
        cn.item_keys.push_back("plan.nvidia");
        out.push_back({L"confirm_nvidia_fields", cn});
        // P2: arka plan programlari grubu (olculmus satirlar; varsayilan kapali, onayda yalniz secildiyse)
        auto cbg = c;
        GroupVM bg{"plan.background", "plan.background.desc", true, true, true, {}, {{"Discord", 2.4, 412ull << 20}, {"OneDrive", 1.1, 160ull << 20}}};
        cbg.groups.push_back(bg);
        cbg.item_keys.push_back("plan.background");
        out.push_back({L"confirm_background", cbg});
        // T04: varsayilani Hayir grup "Gelismis (1)" altinda katli (ve acik varyanti); kanit/risk cipleri
        auto cadv = c;
        cadv.groups[0].evidence = core::perf::Evidence::Measured;  // cip tonlari: Olculdu (iyi) / Orta risk (uyari) / Yuksek risk (tehlike)
        cadv.groups[1].risk = core::perf::Risk::Medium;
        cadv.groups[2].risk = core::perf::Risk::High;
        GroupVM adv_bg{"plan.background", "plan.background.desc", true, true, false, {}, {{"Discord", 2.4, 412ull << 20}}};
        adv_bg.evidence = core::perf::Evidence::Disputed;
        adv_bg.advanced = true;
        cadv.groups.push_back(adv_bg);
        cadv.item_keys.push_back("plan.background");
        out.push_back({L"confirm_advanced", cadv});
        out.push_back({L"confirm_advanced_open", cadv});
        // P4: bu PC'de "Fark yok" olculen secili grup (uygulanir) "Ayrintilar (N)" altinda, varsayilani Hayir grupla birlikte
        auto cne = cadv;
        cne.groups[0].no_effect = true;
        out.push_back({L"confirm_no_effect", cne});
        out.push_back({L"confirm_no_effect_open", cne});        auto obg = v(Phase::Off);
        bg.selected = false;
        obg.groups.push_back(bg);
        out.push_back({L"off_background", obg});
        // Gap 11 / B7 inceleme: pilli cihazda guc modu grubunun alan satirlari (AC+DC, yalniz AC, DC okunamadi)
        auto cb = c;
        cb.groups[0].field_keys = {"plan.field.power_mode_ac", "plan.field.power_mode_dc"};
        out.push_back({L"confirm_power_battery", cb});
        cb.groups[0].field_keys = {"plan.field.power_mode_ac_only"};
        out.push_back({L"confirm_power_ac_only", cb});
        cb.groups[0].field_keys = {"plan.field.power_mode_ac_dc_unknown"};
        out.push_back({L"confirm_power_dc_unknown", cb});
    }
    c.quality = c.reboot = c.admin = true;
    c.startup = 3;
    out.push_back({L"confirming", c});
    out.push_back({L"confirming_svc", c, {}, {}, true});  // ayni onay; hizmet yolu: "UAC sorulmaz"
    auto a = v(Phase::Applying);
    a.done = 2;
    a.total = 5;
    out.push_back({L"applying", a});
    {
        // T18: adim sayaci + grup + asama (Uygulama / Geri yukleme noktasi / Son dogrulama); sure tahmini yok
        auto as = v(Phase::Applying);
        as.done = 3, as.total = 9;
        as.progress_has_step = true, as.progress_step = 2, as.progress_steps = 3;
        as.progress_group = "plan.power_mode";
        as.progress_phase = core::perf::ApplyPhase::Apply;
        out.push_back({L"applying_step", as});
        as.progress_group = "plan.restore_point";
        as.progress_step = 1;
        as.progress_phase = core::perf::ApplyPhase::RestorePoint;
        out.push_back({L"applying_restore_point", as});
        as.progress_has_step = false;
        as.progress_phase = core::perf::ApplyPhase::Verify;
        as.done = 9;
        out.push_back({L"applying_verify", as});
        auto ct = c;
        ct.tailored = true;
        out.push_back({L"confirm_tailored", ct});
    }
    auto on = v(Phase::On);
    on.active = 2;
    out.push_back({L"on", on});
    {
        auto os = on;
        os.verified = 2;
        out.push_back({L"on_summary", os});
    }
    {
        // OPT-08: sonrasi olculuyor + rapor (anlamli fark, fark yok, veri yetersiz, desteklenmiyor, disk)
        using core::perf::ReportCollector;
        ViewModel::BeforeAfterView col;
        col.stage = ReportCollector::Stage::CollectingAfter;
        col.after_n = 4;
        out.push_back({L"on_ba_collecting", on, col});
        std::vector<core::perf::ResourceSample> sb, sa2;
        for (int i = 0; i < 10; ++i) {
            core::perf::ResourceSample x, y;
            x.cpu_pct = 38.0 + (i % 3), y.cpu_pct = 21.0 + (i % 3);
            x.ram_used_pct = 61.0 + (i % 2), y.ram_used_pct = 61.0 + ((i + 1) % 2);
            x.ram_free_bytes = 6ull << 30, y.ram_free_bytes = i % 2 ? (6ull << 30) + (40ull << 20) : (6ull << 30) - (40ull << 20);
            x.volumes = {{"C:\\", 689ull << 30}, {"D:\\", 1200ull << 30}};
            y.volumes = x.volumes;
            if (i < 2) y.gpu_pct = 12.0;  // yetersiz
            sb.push_back(x), sa2.push_back(y);
        }
        ViewModel::BeforeAfterView done;
        done.stage = ReportCollector::Stage::Done;
        done.after_n = 10;
        done.report = core::perf::compare_before_after(sb, sa2);
        out.push_back({L"on_ba_report", on, done});
    }
    out.push_back({L"confirm_restore", v(Phase::ConfirmRestore)});
    auto r = v(Phase::Restoring);
    r.done = 4;
    r.total = 5;
    out.push_back({L"restoring", r});
    out.push_back({L"recovery", v(Phase::RecoveryNeeded, Notice::RestoreFailed)});
    out.push_back({L"off_details", v(Phase::Off)});           // panel acik (UiState.details_open)
    out.push_back({L"on_tips_open", v(Phase::On)});           // T20: Acik + Ayrinti acik + "Oyun ici oneriler" acik
    out.push_back({L"none_selected", v(Phase::Off, Notice::NoneSelected)});
    return out;
}

std::vector<std::pair<const wchar_t*, core::measure::MeasureState>> measure_variants() {
    namespace cm = core::measure;
    namespace dg = ef::core::diagnosis;
    using ef::contracts::Measurement;
    using ef::contracts::MissingReason;
    using ef::contracts::Unit;
    std::vector<std::pair<const wchar_t*, cm::MeasureState>> out;
    cm::MeasureState idle;
    out.push_back({L"idle_unavailable", idle});
    cm::MeasureState game_off = idle;
    game_off.source = cm::TestSource::InGame;
    game_off.ingame_available = false;
    game_off.ingame_reason_key = "gate.capture_no_game";
    out.push_back({L"game_unavailable_reason", game_off});
    cm::MeasureState run;
    run.builtin_available = true;
    run.phase = cm::TestPhase::Running;
    run.stage_key = "measure.stage_measure";
    run.elapsed_s = 24;
    run.total_s = 60;
    out.push_back({L"running", run});
    cm::MeasureState game = run;
    game.source = cm::TestSource::InGame;
    game.ingame_available = true;
    game.total_s = 0;  // oyunda test: sure acik uclu, sahte yuzde yok
    out.push_back({L"running_game_open", game});
    cm::MeasureState done;
    done.builtin_available = done.ingame_available = true;
    done.phase = cm::TestPhase::Done;
    cm::ResultView r;
    r.title = "Counter-Strike 2";
    r.source = cm::TestSource::InGame;
    r.when_unix_ms = 1793800000000;
    r.diagnosis = cm::Diagnosis::Limited;
    r.confidence = ef::contracts::Confidence::Medium;
    r.evidence_keys = {"health.xmp_off", "diag.efficiency_overlay"};
    r.avg_fps = Measurement::of(412.37, Unit::Fps);
    r.low1_fps = Measurement::missing(Unit::Fps, MissingReason::NotSampled);
    r.p99_ms = Measurement::of(3.94, Unit::Milliseconds);
    done.result = r;
    done.comparison = {true, cm::Verdict::Improved, 7.4};
    cm::ResultView h1 = r, h2 = r, h3;
    h2.avg_fps = Measurement::missing(Unit::Fps, MissingReason::SourceError);
    h2.diagnosis = cm::Diagnosis::Insufficient;
    h3.source = cm::TestSource::Builtin;
    h3.when_unix_ms = 1793700000000;
    h3.diagnosis = cm::Diagnosis::None;
    h3.avg_fps = Measurement::of(12345.5, Unit::Fps);
    done.history = {h1, h2, h3};
    out.push_back({L"result", done});
    // DIAG-08: takilma raporu karti (bulunan pencereler + yetersiz veri). Kanit kodlari yerellestirilir.
    cm::MeasureState drop_found = done;
    {
        dg::DropReport rep;
        rep.run_id = "snapshot";
        rep.outcome = dg::DropOutcome::DropsFound;
        rep.valid_frames = 3600;
        rep.measured_ms = 60000;
        rep.median_interval_ms = 16.7;
        rep.p95_interval_ms = 17.2;
        rep.threshold_ms = 33.4;
        rep.total_windows = 2;
        rep.analyzed_windows = 2;
        rep.drop_ms = 420;
        rep.drop_time_ratio = 0.007;
        dg::DropWindow w;
        w.first_frame = 1200;
        w.last_frame = 1215;
        w.start_ms = 19840;
        w.end_ms = 20120;
        w.frame_count = 16;
        w.slow_frame_count = 16;
        w.worst_ms = 48.2;
        w.avg_fps = 62.3;
        w.estimated_lost_frames = 1;
        w.source = dg::DropSource::GpuBound;
        w.confidence = dg::DropConfidence::Corroborated;
        w.supporting = {"gpu_util_high"};
        w.counter = {"cpu_core_not_saturated"};
        rep.worst = {w};
        rep.dominant = dg::DropSource::GpuBound;
        rep.confidence = dg::DropConfidence::Corroborated;
        rep.supporting = {"gpu_util_high", "gpu_busy_near_frame_time"};
        rep.counter = {"cpu_core_not_saturated"};
        rep.notes = {"refresh_interval_match"};
        drop_found.result->drops = std::move(rep);
    }
    out.push_back({L"drop_found", drop_found});
    cm::MeasureState drop_missing = done;
    {
        dg::DropReport rep;
        rep.run_id = "snapshot";
        rep.outcome = dg::DropOutcome::Insufficient;
        rep.valid_frames = 2;
        rep.excluded_frames = 1;
        rep.measured_ms = 12;
        rep.total_windows = 0;
        rep.notes = {"frames_insufficient", "no_aligned_metrics", "unknown_evidence_xyz"};
        rep.unaligned_sources = {"pdh"};
        drop_missing.result->drops = std::move(rep);
    }
    out.push_back({L"drop_insufficient", drop_missing});
    // Spec 2026-11-12 B: "bu oyunda ne sinirladi" karti (Oyunda Test). Bes durum: GPU tam mesgul (+HAGS notu),
    // GPU bekledi, kare siniri suphesi, belirlenemedi, celiski (GPU suresi "bekledi" derken oyunun kendi GPU kullanimi
    // yuksek: hukum yok, en uzun kart). Sayilar goruntu denetimi icin sabit ornek degerlerdir.
    {
        auto limit_state = [&](const wchar_t* name, const cm::MeasureState& base, dg::GameLimit verdict, dg::GameLimitReason reason,
                               std::optional<double> full, std::optional<double> waited, bool hags) {
            cm::MeasureState st = base;
            cm::GameLimitView v;
            v.result.verdict = verdict;
            v.result.reason = reason;
            v.result.frames = 8640;
            v.result.windows = 60;
            v.result.full_pct = full;
            v.result.waited_pct = waited;
            if (full) v.result.busy_ratio = 0.5;
            v.result.avg_fps = 143.6;
            v.result.gpu_time_approximate = hags;
            if (verdict == dg::GameLimit::FrameCapSuspected) {
                v.result.cap_fps = 144.0;
                v.result.cap_is_refresh = true;
            }
            if (full) v.game_gpu_pct = verdict == dg::GameLimit::GpuFull || reason == dg::GameLimitReason::Conflict ? 97.0 : 54.0;
            // Spec 2026-11-12 C3: oyun yuksek performansli kartta degil -> oneri (masaustu) / bilgi (dizustu) notu.
            if (verdict == dg::GameLimit::GpuWaited) v.adapter_level = dg::GameAdapterLevel::Advice;
            if (verdict == dg::GameLimit::FrameCapSuspected) v.adapter_level = dg::GameAdapterLevel::Info;
            st.result->game_limit = v;
            out.push_back({name, st});
        };
        limit_state(L"limit_gpu_full", drop_found, dg::GameLimit::GpuFull, dg::GameLimitReason::None, 92.0, 3.0, true);
        limit_state(L"limit_gpu_waited", drop_found, dg::GameLimit::GpuWaited, dg::GameLimitReason::None, 6.0, 81.0, false);
        limit_state(L"limit_frame_cap", done, dg::GameLimit::FrameCapSuspected, dg::GameLimitReason::None, 12.0, 70.0, false);
        limit_state(L"limit_undetermined", drop_missing, dg::GameLimit::Undetermined, dg::GameLimitReason::NoGpuTiming, std::nullopt,
                    std::nullopt, false);
        limit_state(L"limit_conflict", drop_found, dg::GameLimit::Undetermined, dg::GameLimitReason::Conflict, 4.0, 88.0, true);
    }
    cm::MeasureState missing = done;
    missing.result->avg_fps = Measurement::missing(Unit::Fps, MissingReason::NotSupported);
    missing.result->p99_ms = Measurement::missing(Unit::Milliseconds, MissingReason::PermissionDenied);
    missing.result->diagnosis = cm::Diagnosis::Insufficient;
    missing.result->evidence_keys.clear();
    missing.comparison = {true, cm::Verdict::Uncertain, std::nullopt};
    out.push_back({L"result_missing", missing});
    cm::MeasureState canc;
    canc.builtin_available = true;
    canc.phase = cm::TestPhase::Cancelled;
    out.push_back({L"cancelled", canc});
    cm::MeasureState fail = canc;
    fail.phase = cm::TestPhase::Failed;
    out.push_back({L"failed", fail});

    // --- UI denetimi B: gercek uygulama durumlari (total_s = 0, Hazirlaniyor, Durduruluyor, gercek neden kodlu sonuc) ---
    // Uygulama Testi sonucu: koşucunun gerçekte ürettiği durum/neden kodlari (ham kod ekranda gorunmemeli).
    const std::vector<cm::SubtestOutcome> outcomes{{"scene", "completed", ""},
                                                   {"cpu", "completed", ""},
                                                   {"memory", "skipped", "free_memory_below_reserve"},
                                                   {"storage", "partial", "io_error_1117"},
                                                   {"combined", "partial", "no_overlap_storage_"}};
    const auto sum = cm::builtin_summary(outcomes);
    cm::ResultView br;
    br.source = cm::TestSource::Builtin;
    br.tier = "medium";
    br.mode = cm::TestMode::Full;
    br.when_unix_ms = 1793900000000;
    br.headline_key = sum.headline_key;
    br.headline_args = sum.args;
    br.show_confidence = false;
    br.avg_fps = Measurement::of(93.66, Unit::Fps);
    br.low1_fps = Measurement::of(88.2, Unit::Fps);
    br.p99_ms = Measurement::of(11.37, Unit::Milliseconds);
    std::map<std::string, cm::SubtestInfo> si;
    for (const auto& o : outcomes) {
        br.evidence_keys.push_back("bench." + o.id + "." + o.status);
        si[o.id] = {o.status, o.reason};
    }
    br.components = cm::component_cards(
        {{"scene_avg_fps", 93.66}, {"scene_low1_fps", 88.2}, {"scene_p99_ms", 11.37}, {"cpu_st_units_per_s", 861.7},
         {"cpu_mt_units_per_s", 5235.6}, {"cpu_st_memory_limited", 1}, {"cpu_mt_memory_limited", 1}, {"cpu_bigdata_budget_known", 1},
         {"storage_seq_1m_qd8_read_mbps", 4544}, {"combined_avg_fps", 71.2}, {"combined_io_ops", 0},
         // EF-X22 dogrulama karti: GPU gecti, CPU gecti (AVX-512 desteklenmiyor), disk dogrulamasi calismadi.
         {"scene_gpu_verify_state", 1}, {"scene_gpu_verify_repeats", 32}, {"scene_gpu_verify_mismatch_words", 0},
         {"cpu_verify_state", 1}, {"cpu_verify_blocks", 18432}, {"cpu_verify_mismatch_blocks", 0}, {"cpu_verify_sse2_supported", 1},
         {"cpu_verify_avx2_supported", 1}, {"cpu_verify_avx512_supported", 0}},
        si, {{"tier", "medium"}, {"res", "1920x1080"}, {"volume", "D:\\"}});
    cm::ResultView legacy;  // eski gecmis kaydi: ic kimlik basligi ("scene_v3 medium") -> kademe metni
    legacy.source = cm::TestSource::Builtin;
    legacy.title = "scene_v3 medium";
    legacy.when_unix_ms = 1793600000000;
    legacy.headline_key = cm::builtin_history_headline({"memory.skipped"});
    legacy.show_confidence = false;
    legacy.avg_fps = Measurement::of(90.1, Unit::Fps);

    cm::MeasureState bres;
    bres.builtin_available = true;
    bres.phase = cm::TestPhase::Done;
    bres.mode = cm::TestMode::Full;
    bres.result = br;
    bres.history = {br, legacy};
    out.push_back({L"builtin_result", bres});
    // B15: karsilastirilabilir onceki test (dusus cipi) + saniye basina gercek FPS serisi (kare suresi grafigi), tum olceklerde.
    cm::MeasureState bres_cmp = bres;
    for (int k = 0; k < 30; ++k) bres_cmp.result->fps_series.push_back(92.0 + 6.0 * std::sin(k * 0.9));
    bres_cmp.comparison = {true, cm::Verdict::Uncertain, -12.35};
    out.push_back({L"builtin_result_compare", bres_cmp});

    cm::MeasureState prep = bres;  // onceki sonuc gorunur kalir ("Onceki sonuc")
    prep.phase = cm::TestPhase::Preparing;
    prep.stage_key = "measure.stage_prepare";
    prep.elapsed_s = 2;
    out.push_back({L"preparing", prep});

    cm::MeasureState brun = bres;  // gercek uygulama: total_s = 0, asama 3/5, gecen sure acik uclu
    brun.phase = cm::TestPhase::Running;
    brun.stage_key = "measure.stage_memory";
    brun.elapsed_s = 73;
    brun.total_s = 0;
    out.push_back({L"running_builtin_open", brun});

    cm::MeasureState stopping = brun;
    stopping.phase = cm::TestPhase::Stopping;
    stopping.elapsed_s = 81;
    out.push_back({L"stopping", stopping});

    cm::MeasureState fcause = bres;  // M11: neden + sonraki adim, onceki sonuc gorunur
    fcause.phase = cm::TestPhase::Failed;
    fcause.fail_reason = "device_removed";
    out.push_back({L"failed_cause", fcause});

    // --- R13/INS-03: uzun test karti. Kendi is parcaciginda calisir; olcum fazini degistirmez. ---
    cm::MeasureState eidle;
    eidle.builtin_available = true;
    out.push_back({L"endurance_idle", eidle});

    cm::MeasureState erun = eidle;  // gercek gecen sure tabani; sahte yuzde yok
    erun.endurance.running = true;
    erun.endurance.started_ms = 100000;
    erun.endurance.budget_s = 600;
    out.push_back({L"endurance_running", erun});

    // B14: isinma asamasi + anlik satir; GPU telemetrisi desteklenmiyor (NVIDIA disi) -> "desteklenmiyor (yalniz NVIDIA)".
    cm::MeasureState ewarm = eidle;
    ewarm.endurance.running = true;
    ewarm.endurance.budget_s = 600;
    ewarm.endurance.elapsed_s = 312;  // started_ms = 0: kosucu ornegi; goruntu saniyeyle degismez
    ewarm.endurance.has_live = true;
    ewarm.endurance.live_fps = 143.0;
    ewarm.endurance.telemetry_unsupported = true;
    out.push_back({L"endurance_warming_live", ewarm});

    // --- P1: Performans etkisini olc karti. Sayilar SENTETIK fikstur degerleridir (urun olcumu degil). ---
    {
        namespace cp = core::perf;
        const auto synth = [](double off, double on, double noise, double low_off, double low_on) {
            std::vector<cp::ProofRun> v;
            for (int i = 0; i < 5; ++i) {
                const double e = ((i % 3) - 1) * noise;
                for (const auto arm : {cp::ProofArm::Off, cp::ProofArm::On}) {
                    cp::ProofRun r;
                    r.arm = arm;
                    r.valid = true;
                    r.conditions = {{"arena", "arena_v1"}, {"driver", "1.0"}};
                    r.avg_fps = (arm == cp::ProofArm::Off ? off + e : on - e);
                    r.low1_fps = (arm == cp::ProofArm::Off ? low_off + e : low_on - e);
                    r.p99_ms = 1000.0 / *r.low1_fps;
                    v.push_back(r);
                }
            }
            return v;
        };
        cm::MeasureState pidle = eidle;
        pidle.proof.estimate_min = 4;
        out.push_back({L"proof_idle", pidle});

        cm::MeasureState prun = eidle;
        prun.phase = cm::TestPhase::Running;
        prun.proof.phase = cp::ProofPhase::Running;
        prun.proof.estimate_min = 4;
        prun.proof.stage = "run_on";
        prun.proof.progress.scored_total = 10;
        prun.proof.progress.scored_done = 4;
        prun.proof.progress.arm = cp::ProofArm::On;
        out.push_back({L"proof_running", prun});

        cm::MeasureState pbetter = pidle;
        pbetter.proof.phase = cp::ProofPhase::Done;
        pbetter.proof.result = cp::evaluate_proof(synth(142.0, 151.0, 0.8, 118.0, 124.0), true);
        out.push_back({L"proof_result_better", pbetter});

        cm::MeasureState pnone = pidle;
        pnone.proof.phase = cp::ProofPhase::Done;
        pnone.proof.result = cp::evaluate_proof(synth(142.0, 142.4, 2.0, 118.0, 118.2));
        out.push_back({L"proof_result_no_difference", pnone});

        cm::MeasureState pinvalid = pidle;
        pinvalid.proof.phase = cp::ProofPhase::Aborted;
        auto bad = synth(142.0, 151.0, 0.8, 118.0, 124.0);
        bad[6].valid = false;
        bad[6].invalid_reason = "unstable";
        pinvalid.proof.result = cp::evaluate_proof(bad);
        out.push_back({L"proof_result_invalid_run", pinvalid});

        cm::MeasureState prefused = pidle;
        prefused.proof.refusal = cp::ProofRefusal::PlanNeedsAdmin;
        out.push_back({L"proof_refused", prefused});

        // Karisik plan: yonetici gruplari disarida, olculen alt kume acikca yazilir (baslamadan once, calisirken, sonucta).
        cm::MeasureState psub = pidle;
        psub.proof.pre_groups = 3;
        psub.proof.pre_partial = true;
        out.push_back({L"proof_idle_subset", psub});

        cm::MeasureState pwhy = pidle;  // Baslat dugmesi gorunur ama neden dugmenin yaninda yazili
        pwhy.proof.precheck = cp::ProofRefusal::PerfNotOff;
        out.push_back({L"proof_idle_precheck", pwhy});

        cm::MeasureState prunsub = prun;
        prunsub.proof.scope_groups = 3;
        prunsub.proof.scope_partial = true;
        out.push_back({L"proof_running_subset", prunsub});

        cm::MeasureState pbsub = pbetter;
        pbsub.proof.scope_groups = 3;
        pbsub.proof.scope_partial = true;
        out.push_back({L"proof_result_better_subset", pbsub});
    }

    cm::MeasureState ewarm_unread = ewarm;  // destekli sensor bu ornekte bos dondu -> "okunamiyor"
    ewarm_unread.endurance.telemetry_unsupported = false;
    out.push_back({L"endurance_warming_live_unreadable", ewarm_unread});

    cm::MeasureState ewarm_nv = ewarm;  // son 60 sn, NVIDIA: saat ve yuk okunuyor
    ewarm_nv.endurance.telemetry_unsupported = false;
    ewarm_nv.endurance.elapsed_s = 571;
    ewarm_nv.endurance.live_gpu_mhz = 2655.0;
    ewarm_nv.endurance.live_gpu_load_pct = 98.0;
    out.push_back({L"endurance_warm_live_nvidia", ewarm_nv});

    cm::MeasureState eprep = eidle;  // kosucu saati baslamadi: "Hazirlaniyor"
    eprep.endurance.running = true;
    eprep.endurance.budget_s = 600;
    out.push_back({L"endurance_preparing", eprep});

    cm::MeasureState epart = eidle;  // 4 dk sonra kullanici durdurdu: kismi sonuc
    epart.endurance.has_result = true;
    epart.endurance.elapsed_s = 241;
    epart.endurance.result.stop = cm::EnduranceStopKind::Cancelled;
    epart.endurance.result.duration_s = 241;
    epart.endurance.result.cold_fps = 151.2;
    epart.endurance.result.warm_fps = 146.9;
    epart.endurance.result.fps_change_pct = -2.8;
    out.push_back({L"endurance_partial", epart});

    cm::MeasureState eshort = eidle;  // 2 dk dolmadan durduruldu
    eshort.endurance.has_result = true;
    eshort.endurance.elapsed_s = 75;
    eshort.endurance.result.stop = cm::EnduranceStopKind::Cancelled;
    eshort.endurance.result.duration_s = 75;
    out.push_back({L"endurance_too_short", eshort});

    cm::MeasureState estable = eidle;  // tamamlandi, NVIDIA, son iki pencere %2 icinde
    estable.endurance.has_result = true;
    estable.endurance.elapsed_s = 600;
    estable.endurance.result.stop = cm::EnduranceStopKind::Completed;
    estable.endurance.result.duration_s = 600;
    estable.endurance.result.cold_fps = 152.0;
    estable.endurance.result.warm_fps = 147.5;
    estable.endurance.result.fps_change_pct = -3.0;
    estable.endurance.result.sensors_available = true;
    estable.endurance.result.cold_gpu_mhz = 2700.0;
    estable.endurance.result.warm_gpu_mhz = 2640.0;
    estable.endurance.result.stability_checked = true;
    estable.endurance.result.stable = true;
    out.push_back({L"endurance_completed_stable", estable});

    cm::MeasureState eunstable = estable;  // tamamlandi, NVIDIA disi (saat desteklenmiyor), sabitlenmedi
    eunstable.endurance.result.sensors_available = false;
    eunstable.endurance.result.telemetry_unsupported = true;
    eunstable.endurance.result.cold_gpu_mhz = eunstable.endurance.result.warm_gpu_mhz = std::nullopt;
    eunstable.endurance.result.stable = false;
    eunstable.endurance.result.stability_fps_only = true;
    out.push_back({L"endurance_completed_not_stable", eunstable});

    cm::MeasureState eres = eidle;  // sensor yok: soguk/isinmis FPS var, GPU saati yok; saglik raporu degil
    eres.endurance.has_result = true;
    eres.endurance.elapsed_s = 600;
    eres.endurance.result.stop = cm::EnduranceStopKind::TimeBudget;
    eres.endurance.result.cold_fps = 121.4;
    eres.endurance.result.warm_fps = 109.8;
    eres.endurance.result.fps_change_pct = -9.6;
    eres.endurance.result.sensors_available = false;
    out.push_back({L"endurance_result_sensors_missing", eres});

    // --- 0.2.1 geri bildirimi (Olcumler): Sonuc karti hic tumuyle "—" olmaz; baslatma oncesi uyari. ---
    cm::MeasureState olc_none;  // kayit yok: tek soluk satir + ipucu; Uygulama Testi baslatilabilir (uyari gorunur)
    olc_none.builtin_available = true;
    out.push_back({L"olc_no_record", olc_none});
    {
        using BV = dg::BalanceVerdict;
        cm::BalanceView b;  // en yeni kayit denge testi: 1440p ekran -> "1440p · Darbogaz yok"
        b.cpu_fps = 212.4;
        b.rows[0] = {BV::CpuLimited, "", 388.1, true, 208.0};
        b.rows[1] = {BV::GpuLimited, "", 150.2, false, 148.0};
        b.rows[2] = {BV::Undetermined, "on_battery", std::nullopt, false, std::nullopt};
        b.adapter = "NVIDIA GeForce RTX 4070";
        b.quick = true;
        cm::ResultView rb;
        rb.source = cm::TestSource::Builtin;
        rb.mode = cm::TestMode::Quick;
        rb.when_unix_ms = 1794000000000;
        rb.show_confidence = false;
        rb.balance = b;
        cm::MeasureState olc_bal;
        olc_bal.builtin_available = true;
        olc_bal.phase = cm::TestPhase::Done;
        olc_bal.scope = cm::BuiltinScope::Balance;
        olc_bal.result = rb;
        olc_bal.balance_latest = b;
        olc_bal.history = {rb, br};
        out.push_back({L"olc_balance_latest", olc_bal});
    }
    cm::MeasureState olc_app = bres;  // en yeni kayit Genel test: dort sayi kayittan
    olc_app.history = {br, legacy};
    out.push_back({L"olc_app_latest", olc_app});
    cm::MeasureState olc_run = olc_none;  // test suruyor: uyari gizli, canli sayilar yoksa "—"
    olc_run.phase = cm::TestPhase::Running;
    olc_run.stage_key = "measure.stage_measure";
    olc_run.elapsed_s = 12;
    olc_run.total_s = 60;
    out.push_back({L"olc_running", olc_run});
    // Bosluk 7 (B5 inceleme): bos alan yuzunden atlanan disk karti, doldurulmus {need}/{free} (3 haneli GiB) ile en uzun
    // "free_space_short" metni; arglar kosucu eslemesinden (storage_free_space_args) gelir.
    {
        constexpr std::uint64_t kGiB = 1ull << 30;
        std::map<std::string, cm::SubtestInfo> fsi = si;
        cm::SubtestInfo st{"skipped", "free_space_floor"};
        for (const auto& [k, v] : cm::storage_free_space_args(true, "free_space_floor", 2000 * kGiB, 312 * kGiB - 1, 287 * kGiB + 3))
            st.args[k] = v;
        fsi["storage"] = st;
        cm::MeasureState fs = bres;
        fs.result->components = cm::component_cards({{"scene_avg_fps", 93.66}, {"cpu_st_units_per_s", 861.7}}, fsi,
                                                    {{"tier", "medium"}, {"res", "1920x1080"}, {"volume", "D:\\"}});
        fs.history = {*fs.result, legacy};
        out.push_back({L"builtin_storage_free_space", fs});
    }
    return out;
}

// Boyut denetimi yalniz etkilesimli denetimler icindir (salt okunur ilerleme/metin ogeleri kart genisliginde olabilir).
bool is_control(const HitMap& hits, Hit id) {
    const auto it = hits.meta.find(id);
    return it == hits.meta.end() || interactive(it->second.role);
}

// Denetim boyutu: tek bir denetim control_max_h'den yuksek / kenar cubugundan genis olamaz. Bilincli istisna (spec
// 2026-11-12 B.1): Panel'in yuvarlak Performans dugmesi (Hit::Toggle) power_button_d capinda tek buyuk ana eylemdir;
// o capi asamaz.
// Ikinci bilincli istisna (spec 2026-11-12 B2): oyun kartinin tamami secim ogesidir (karta basinca oyun secilir); kart
// ince kart boyunu (game_card_h) ve govde genisligini asamaz.
// Ucuncu ve dorduncu istisna (spec 2026-11-12 C): Olcumler Test kartinin yuvarlak baslat / durdur dugmesi
// test_button_d capindadir (canli gorunumdeki "Testi durdur" normal dugmedir; o da bu siniri asmaz); "Son 3 test"
// satirinin tamami ayrinti ac/kapa ogesidir (iki satirli satir boyunu ve govde genisligini asamaz).
// Besinci istisna (spec 2026-11-12 B6): Gecmis zaman cizgisi satiri (satira basinca acilir) ve "Son 3 test" satiri
// (satirin tamami secim ogesidir) ayni sinirla.
bool control_too_big(const Theme& theme, Hit id, const RectF& r) {
    if (id == Hit::Toggle) return r.w > theme.power_button_d + 0.5f || r.h > theme.power_button_d + 0.5f;
    if (id == Hit::TestStart || id == Hit::TestStop) return r.w > std::max(theme.test_button_d, theme.sidebar_max) + 0.5f || r.h > theme.test_button_d + 0.5f;
    // Altinci istisna (BOOST maketi M1): kaynak dugmeleri kartin yarisini doldurur (flex-1); yukseklik siniri ayni kalir.
    if (id == Hit::SourceBuiltin || id == Hit::SourceGame) return r.h > theme.control_max_h + 0.5f || r.w > theme.content_max_w + 0.5f;
    if (classify_hit(id).kind == HitKind::MeasureRecent || classify_hit(id).kind == HitKind::HistoryRow || classify_hit(id).kind == HitKind::HistorySelect)
        return r.h > theme.row2_h + 0.5f || r.w > theme.content_max_w + 0.5f;
    if (classify_hit(id).kind == HitKind::GameCard) return r.h > game_card_h(theme) + 0.5f || r.w > theme.content_max_w + 0.5f;
    // Denetim 5.1: genislik siniri metin oraniyla buyur (Windows "Metin boyutu"nda etiket buyur, dugme genisler; bu
    // dogru davranis). Oran, govde yazisinin varsayilana orani (with_text_scale'in uyguladigi oran). P9-B6: eskiden dugme
    // yuksekliginin orani kullaniliyordu; o, satir yuksekliginin cift sayiya yuvarlanmasina bagliydi (denetim, olcekli
    // etiket genisliginden bagimsiz bir degere baglanmis oluyordu).
    const float text_k = theme.role(Role::Body).size / default_theme().role(Role::Body).size;
    return r.h > theme.control_max_h + 0.5f || r.w > theme.sidebar_max * text_k + 0.5f;
}

// UI-04: yalniz fareyle kullanilabilen kontrol yok; her ogenin ekran okuyucu adi var; klavye odagi gorunur.
// DS-05/TH-01: etkin ve etkilesimli her oge gercek Tab sirasinda; devre disi/salt okunur oge Tab sirasinda degil
// ama UIA agacinda (adi var). Niyet sirasindaki her durak icin cizimde oge var (olu Tab duragi yok).
// EF-A11Y: cizilen salt okunur metin UIA agacinda Text ogesi olarak var mi (ad tam/onek/icerik eslesmesi).
bool has_text_item(const HitMap& hits, const std::wstring& needle, int mode /*0 tam,1 onek,2 icerir*/) {
    if (needle.empty()) return false;
    for (const auto& [id, meta] : hits.meta) {
        if (meta.role != AccRole::Text) continue;
        const auto& n = meta.name;
        if (mode == 0 ? n == needle : mode == 1 ? n.starts_with(needle) : n.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

int a11y_issues(const UiState& st, const HitMap& hits, const wchar_t* where) {
    int n = 0;
    const auto order = focus_order(st, hits);
    const auto intent = focus_order(st);
    for (const auto& [id, r] : hits.items) {
        const auto it = hits.meta.find(id);
        const bool in_order = std::find(order.begin(), order.end(), id) != order.end();
        // TH-03: roving radyo grubunda secili olmayan secenek etkin ama Tab duragi degildir (ok tuslari/UIA).
        const bool tab_stop = it != hits.meta.end() && it->second.tab_stop;
        if (tab_stop && hits.activatable(id) && !in_order) {
            ++n;
            std::wprintf(L"A11Y not keyboard reachable id=%d (%ls)\n", static_cast<int>(id), where);
        }
        if (!hits.activatable(id) && in_order) {
            ++n;
            std::wprintf(L"A11Y disabled item in tab order id=%d (%ls)\n", static_cast<int>(id), where);
        }
        if (it == hits.meta.end() || it->second.name.empty()) {
            ++n;
            std::wprintf(L"A11Y missing name id=%d (%ls)\n", static_cast<int>(id), where);
        }
        // Spec 2026-11-11 §2: dokunma hedefi asgari 24x24 DIP (WCAG 2.5.8) korunur. Etkilesimli her ogenin isabet
        // kutusu iki yonde de en az touch_min olmalidir (salt okunur metin/ilerleme ogeleri haric).
        const float min_side = default_theme().touch_min;
        if (it != hits.meta.end() && interactive(it->second.role) && r.w > 0 && (r.w < min_side - 0.01f || r.h < min_side - 0.01f)) {
            ++n;
            std::wprintf(L"A11Y target below %.0fx%.0f id=%d %.1fx%.1f (%ls)\n", min_side, min_side, static_cast<int>(id), r.w, r.h, where);
        }
    }
    for (Hit h : intent)
        if (!hits.meta.count(h)) {
            ++n;
            std::wprintf(L"A11Y tab stop without element id=%d (%ls)\n", static_cast<int>(h), where);
        }
    bool focused_visible_item = false;
    for (const auto& [id, r] : hits.items) {
        const auto it = hits.meta.find(id);
        focused_visible_item |= id == st.focus && it != hits.meta.end() && !it->second.offscreen;
    }
    if (st.focus_visible && focused_visible_item && !hits.focus_drawn) {
        ++n;
        std::wprintf(L"A11Y focus ring not drawn focus=%d (%ls)\n", static_cast<int>(st.focus), where);
    }
    // RDY-01: her oge bilinen ve karede tekil bir UIA AutomationId tasir (ayni ad iki farkli ogeye gitmez).
    std::map<std::wstring, Hit> ids;
    for (const auto& [id, r] : hits.items) {
        const std::wstring aid = hit_automation_id(id);
        if (aid == L"unknown") {
            ++n;
            std::wprintf(L"A11Y unknown automation id id=%d (%ls)\n", static_cast<int>(id), where);
        }
        const auto [pos, inserted] = ids.emplace(aid, id);
        if (!inserted && pos->second != id) {
            ++n;
            std::wprintf(L"A11Y duplicate automation id %ls ids=%d,%d (%ls)\n", aid.c_str(), static_cast<int>(pos->second), static_cast<int>(id), where);
        }
    }
    return n;
}

// TH-05/TH-02/DS-10: renk token'lari yuzeylerde WCAG 1.4.11 (metin disi 3:1) esigini gecer.
int palette_contrast_issues(const Palette& p, const wchar_t* where) {
    int n = 0;
    const std::pair<const Color*, const wchar_t*> surfaces[] = {
        {&p.background, L"background"}, {&p.surface, L"surface"}, {&p.surface_raised, L"surface_raised"}};
    for (const auto& [bg, name] : surfaces) {
        const double border = contrast_ratio(p.border, *bg);
        const double ring = contrast_ratio(p.focus_ring, *bg);
        if (border < 3.0) {
            ++n;
            std::wprintf(L"A11Y border contrast %.2f < 3 on %ls (%ls)\n", border, name, where);
        }
        if (ring < 3.0) {
            ++n;
            std::wprintf(L"A11Y focus ring contrast %.2f < 3 on %ls (%ls)\n", ring, name, where);
        }
    }
    // TH-02: odak halkasi secim/vurgu rengiyle ayni olmamali (secim kenari accent kullanir).
    if (p.focus_ring == p.accent) {
        ++n;
        std::wprintf(L"A11Y focus ring equals accent (%ls)\n", where);
    }
    return n;
}

const wchar_t* page_slug(Page p) {
    static const wchar_t* slug[] = {L"overview", L"readiness", L"games", L"measure", L"cleanup", L"startup", L"history", L"settings", L"system", L"updates"};
    const auto i = static_cast<std::size_t>(p);
    return i < std::size(slug) ? slug[i] : L"unknown";
}

// ---- Simetri denetimi (spec 2026-11-12 §5): butun sayfa x boyut x dil x palet matrisinde her cizimde calisir ----
// ROW_UNEQUAL: kayitli esit satirda (HitMap::rows) hucrelerin ust kenari / yuksekligi 0,5 DIP'ten fazla ayrisiyor ya da
// hucrenin ust ve alt kenarina oturan kart cercevesi (HitMap::cards) yok — kart satir yuksekligini kullanmamis.
// ORPHAN_CARD: ayni ust kenarda, yatayda ayrik, farkli boyda iki kart cercevesi (ayni kayitli satirin farkli hucrelerinde
// degillerse) — esit satir mekanizmasinin disinda yan yana cizilmis kartlar (eski bagimsiz sutun yigini).
struct SymmetryIssues {
    int row_unequal = 0, orphan_card = 0;
};
SymmetryIssues symmetry_issues(const HitMap& hits, const wchar_t* where, bool print) {
    constexpr float kTol = 0.5f;
    SymmetryIssues n;
    const auto in_cell_x = [](const RectF& c, const RectF& cell) { return c.x >= cell.x - kTol && c.right() <= cell.right() + kTol; };
    for (std::size_t ri = 0; ri < hits.rows.size(); ++ri) {
        const auto& row = hits.rows[ri];
        if (row.size() < 2) continue;
        for (std::size_t ci = 0; ci < row.size(); ++ci) {
            const RectF& cell = row[ci];
            bool top = false, bottom = false;
            for (const auto& c : hits.cards) {
                if (!in_cell_x(c, cell)) continue;
                top = top || std::fabs(c.y - cell.y) <= kTol;
                bottom = bottom || std::fabs(c.bottom() - cell.bottom()) <= kTol;
            }
            if (std::fabs(cell.y - row[0].y) <= kTol && std::fabs(cell.h - row[0].h) <= kTol && top && bottom) continue;
            ++n.row_unequal;
            if (print)
                std::wprintf(L"ROW_UNEQUAL %ls row=%zu cell=%zu x=%.1f y=%.1f h=%.1f (first y=%.1f h=%.1f) card_top=%d card_bottom=%d\n", where, ri, ci,
                             cell.x, cell.y, cell.h, row[0].y, row[0].h, top ? 1 : 0, bottom ? 1 : 0);
        }
    }
    // Kartin icinde durdugu kayitli satir ve hucre (yoksa {-1, -1}).
    const auto slot_of = [&](const RectF& c) {
        for (std::size_t ri = 0; ri < hits.rows.size(); ++ri)
            for (std::size_t ci = 0; ci < hits.rows[ri].size(); ++ci) {
                const RectF& cell = hits.rows[ri][ci];
                if (in_cell_x(c, cell) && c.y >= cell.y - kTol && c.bottom() <= cell.bottom() + kTol)
                    return std::pair<int, int>{static_cast<int>(ri), static_cast<int>(ci)};
            }
        return std::pair<int, int>{-1, -1};
    };
    for (std::size_t i = 0; i < hits.cards.size(); ++i)
        for (std::size_t j = i + 1; j < hits.cards.size(); ++j) {
            const RectF& a = hits.cards[i];
            const RectF& b = hits.cards[j];
            if (std::fabs(a.y - b.y) > kTol || std::fabs(a.h - b.h) <= kTol) continue;
            if (!(a.right() <= b.x + kTol || b.right() <= a.x + kTol)) continue;  // ust uste / ic ice: kardes kart degil
            const auto sa = slot_of(a), sb = slot_of(b);
            if (sa.first >= 0 && sa.first == sb.first && sa.second != sb.second) continue;  // esit satirin alt yigini
            ++n.orphan_card;
            if (print)
                std::wprintf(L"ORPHAN_CARD %ls y=%.1f a: x=%.1f w=%.1f h=%.1f b: x=%.1f w=%.1f h=%.1f\n", where, a.y, a.x, a.w, a.h, b.x, b.w, b.h);
        }
    return n;
}
int g_row_unequal = 0, g_orphan_card = 0;
// Her cizim simetri denetiminden gecer (sayfa durumu ne olursa olsun); ilk 60 bulgu yazilir.
HitMap paint_audited(Screen& screen, Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now) {
    HitMap hits = screen.paint(g, w, h, s, vm, now);
    wchar_t where[96];
    swprintf_s(where, L"page=%ls size=%.0fx%.0f scroll=%.0f", page_slug(s.page), w, h, s.scroll);
    const auto n = symmetry_issues(hits, where, g_row_unequal + g_orphan_card < 60);
    g_row_unequal += n.row_unequal;
    g_orphan_card += n.orphan_card;
    return hits;
}
// Oz-denetim: denetim gercekten yakaliyor mu (bos gecen denetim kanit degildir).
bool symmetry_self_test() {
    HitMap ok;
    ok.rows = {{{0, 0, 100, 50}, {110, 0, 100, 50}}};
    ok.cards = {{0, 0, 100, 50}, {110, 0, 100, 20}, {110, 30, 100, 20}, {0, 60, 210, 30}};  // sag hucre alt yigin + tam genislik kart
    const auto a = symmetry_issues(ok, L"self", false);
    HitMap unequal = ok;
    unequal.rows[0][1].h = 40;  // hucre yuksekligi farkli
    const auto b = symmetry_issues(unequal, L"self", false);
    HitMap short_card = ok;
    short_card.cards[2].h = 10;  // alt yigin satirin altina ulasmiyor
    const auto c = symmetry_issues(short_card, L"self", false);
    HitMap orphan;
    orphan.cards = {{0, 0, 100, 50}, {110, 0, 100, 40}};  // kayitli satir yok: bagimsiz sutunlar
    const auto d = symmetry_issues(orphan, L"self", false);
    HitMap nested;
    nested.cards = {{0, 0, 210, 80}, {0, 0, 100, 40}};  // ic ice cerceve kardes kart degildir
    const auto e = symmetry_issues(nested, L"self", false);
    return a.row_unequal == 0 && a.orphan_card == 0 && b.row_unequal == 1 && c.row_unequal == 1 && d.orphan_card == 1 && d.row_unequal == 0 &&
           e.orphan_card == 0;
}

std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// UI denetimi Track C (RDY-02): Oyun Hazirligi geri alma durumlari. Gercek kimlikler (fix_target_for_check ile eslesen),
// gercek metin anahtarlari; Undo / Retry / Interrupted / Conflict (+ ayar ac eylemli Conflict: 3 dugme) + tek dugmeli satir.
void readiness_undo_fixture(ViewModel& vm) {
    namespace rd = ef::core::readiness;
    auto& r = vm.readiness;
    r.loading = false;
    r.net_available = true;
    r.checks = {
        {"display.0.refresh", rd::Section::Display, rd::Level::Problem, "chk.display.below_max",
         {{"name", "DELL S2721DGF"}, {"hz", "60"}, {"max", "165"}, {"index", "0"}, {"device", "DISPLAY1"}}, rd::ActionKind::AutoFix, "fix.refresh_rate"},
        {"windows.game_dvr", rd::Section::Windows, rd::Level::Advice, "chk.win.game_dvr_on", {}, rd::ActionKind::AutoFix, "fix.game_dvr"},
        {"windows.game_mode", rd::Section::Windows, rd::Level::Advice, "chk.win.game_mode_off", {}, rd::ActionKind::AutoFix, "fix.game_mode"},
        {"windows.hags", rd::Section::Windows, rd::Level::Advice, "chk.win.hags_off", {{"restart", "1"}}, rd::ActionKind::AutoFix, "fix.hags"},
        {"windows.vrr", rd::Section::Windows, rd::Level::Info, "chk.win.vrr_off", {}, rd::ActionKind::OpenSettings, "ms-settings:display-advancedgraphics"},
        // Spec 2026-11-12 C1: CPU hiz siniri satiri (salt okuma; eylemsiz).
        {"cpu.speed_limit", rd::Section::Hardware, rd::Level::Advice, "chk.cpu.speed_limit_capped", {{"limit", "62"}}, rd::ActionKind::None, ""},
        {"net.none", rd::Section::Network, rd::Level::Unknown, "chk.network.not_measured", {}, rd::ActionKind::RunNetworkTest, ""},
    };
    r.checks.push_back(rd::mouse_polling_check(false, std::nullopt));  // T06: olculmemis fare satiri (Olc dugmesi)
    {   // T16: saglik testi satiri + ornek sonuc (CPU yavaslama, sicaklik sensoru yok, RAM temiz, GPU yuk uygulanmadi)
        ef::core::diagnostics::HealthReport hr;
        for (int i = 0; i < 60; ++i) {
            ef::core::diagnostics::CpuSample cs;
            cs.pct_of_max_freq = i < 10 ? 100.0 : 78.0;
            hr.cpu.push_back(cs);
        }
        hr.ram = {1ull << 30, 0, true};
        hr.ram_run = true;
        for (auto& c : ef::core::diagnostics::health_test_checks(false, 0, 0, hr)) r.checks.push_back(std::move(c));
    }
    {   // T10: DNS satiri + karsilastirma sonucu (belirgin fark: hazir liste secili Duzelt eylemi; AdGuard notu)
        rd::Check dns{"network.dns.{0A1B2C3D-0000-1111-2222-333344445555}", rd::Section::Network, rd::Level::Info, "chk.network.dns_auto",
                      {{"adapter", "Ethernet"}}, rd::ActionKind::AutoFix, "fix.dns"};
        r.checks.push_back(dns);
        auto sample = [](std::string id, double ms) {
            ef::core::net::ResolverSamples s{std::move(id), {}};
            for (int i = 0; i < 15; ++i) s.ms.push_back(ms);
            return s;
        };
        ef::core::net::DnsBenchReport rep;
        rep.current_modem = true;
        rep.current_name = "192.168.1.1";
        rep.stats = ef::core::net::summarize({sample("current", 38.0), sample("cloudflare", 14.0), sample("google", 19.0),
                                               sample("quad9", 23.0), sample("adguard", 21.0)});
        rep.verdict = ef::core::net::verdict(rep.stats);
        for (auto& c : rd::dns_bench_checks(false, rep, r.checks)) r.checks.push_back(std::move(c));
    }
    rd::sort_checks(r.checks);
    r.undo.assign(r.checks.size(), rd::UndoState::None);
    for (std::size_t k = 0; k < r.checks.size(); ++k) {
        const auto& id = r.checks[k].id;
        if (id == "display.0.refresh") r.undo[k] = rd::UndoState::Undo;
        else if (id == "windows.game_dvr") r.undo[k] = rd::UndoState::Retry;
        else if (id == "windows.game_mode") r.undo[k] = rd::UndoState::Interrupted;
        else if (id == "windows.hags" || id == "windows.vrr") r.undo[k] = rd::UndoState::Conflict;
    }
}

std::size_t check_index(const ViewModel& vm, std::string_view id) {
    for (std::size_t k = 0; k < vm.readiness.checks.size(); ++k)
        if (vm.readiness.checks[k].id == id) return k;
    return 0;
}

// ---- Canli test ekrani (spec 2026-11-12 A4) fiksturleri ----
// Kodla uretilmis onizleme pikselleri: tunel halkalari + dilimler + birkac parlak nesne (marka paleti). Gercek sahne
// goruntusu DEGILDIR; canli gorunumun yerlesimini ve onizleme ustundeki gostergelerin okunurlugunu denetlemek icindir.
// variant: adim basina farkli desen / renk. Pikseller on-carpilmis BGRA, alfa 255 (urun onizlemesiyle ayni bicim).
core::games::ImageRef live_fixture_image(std::uint32_t w, std::uint32_t h, int variant, const Palette& p) {
    auto img = std::make_shared<core::games::ImagePixels>();
    img->w = w;
    img->h = h;
    img->bgra.resize(static_cast<std::size_t>(w) * h * 4);
    const Color glow = variant % 3 == 0 ? p.accent : variant % 3 == 1 ? p.cyan : p.violet;
    const Color hot = variant % 2 == 0 ? p.warning : p.orange;
    const float cx = static_cast<float>(w) * (0.5f + 0.05f * static_cast<float>(variant % 3 - 1)), cy = static_cast<float>(h) * 0.52f;
    const float fh = static_cast<float>(h);
    const float sectors = static_cast<float>(6 + 2 * (variant % 4));
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const float dx = (static_cast<float>(x) - cx) / fh, dy = (static_cast<float>(y) - cy) / fh;
            const float d = std::sqrt(dx * dx + dy * dy) + 1e-3f;
            const float depth = 0.16f / d;  // tunel derinligi: merkeze dogru sikisan halkalar
            const float ang = std::atan2(dy, dx);
            const float ring = 0.5f + 0.5f * std::sin(depth * 9.4f + static_cast<float>(variant) * 1.3f);
            const float sect = 0.5f + 0.5f * std::sin(ang * sectors + depth * 2.f);
            const float fog = std::clamp(d * 2.4f, 0.f, 1.f);  // uzak (merkez) karanlik
            Color c = mix(p.background, p.surface_raised, ring);
            c = mix(c, glow, std::pow(ring * sect, 3.f) * 0.9f);
            // Parlak nesneler: tunel duvarinda ucan birkac "isik" (konum variant ile degisir).
            for (int k = 0; k < 5; ++k) {
                const float a = static_cast<float>(k) * 1.2566f + static_cast<float>(variant) * 0.7f;
                const float ox = std::cos(a) * (0.18f + 0.07f * static_cast<float>(k)), oy = std::sin(a) * (0.12f + 0.05f * static_cast<float>(k));
                const float od = std::sqrt((dx - ox) * (dx - ox) + (dy - oy) * (dy - oy));
                c = mix(c, hot, std::clamp(1.f - od * 22.f, 0.f, 1.f));
            }
            const auto to8 = [fog](float v) { return static_cast<std::uint8_t>(std::clamp(v * (0.25f + 0.75f * fog), 0.f, 1.f) * 255.f + 0.5f); };
            std::uint8_t* px = img->bgra.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            px[0] = to8(c.b), px[1] = to8(c.g), px[2] = to8(c.r), px[3] = 255;
        }
    return img;
}

// Iki bitmap ayni piksel icerigine sahip mi (hizli yol denetimi). Boyut farkli / kilit alinamadi -> false.
bool bitmaps_equal(IWICBitmap* a, IWICBitmap* b) {
    UINT aw = 0, ah = 0, bw = 0, bh = 0;
    if (!a || !b || FAILED(a->GetSize(&aw, &ah)) || FAILED(b->GetSize(&bw, &bh)) || aw != bw || ah != bh) return false;
    WICRect rc{0, 0, static_cast<INT>(aw), static_cast<INT>(ah)};
    ComPtr<IWICBitmapLock> la, lb;
    if (FAILED(a->Lock(&rc, WICBitmapLockRead, &la)) || FAILED(b->Lock(&rc, WICBitmapLockRead, &lb))) return false;
    UINT sa = 0, sb = 0, na = 0, nb = 0;
    BYTE *pa = nullptr, *pb = nullptr;
    if (FAILED(la->GetStride(&sa)) || FAILED(lb->GetStride(&sb)) || FAILED(la->GetDataPointer(&na, &pa)) || FAILED(lb->GetDataPointer(&nb, &pb))) return false;
    for (UINT y = 0; y < ah; ++y)
        if (std::memcmp(pa + static_cast<std::size_t>(sa) * y, pb + static_cast<std::size_t>(sb) * y, static_cast<std::size_t>(aw) * 4) != 0) return false;
    return true;
}

// Iki bitmap, verilen piksel dikdortgeninin DISINDA ayni mi (bolgesel hizli yol yalniz kendi bolgesine dokunur).
bool bitmaps_equal_outside(IWICBitmap* a, IWICBitmap* b, const RECT& skip) {
    UINT aw = 0, ah = 0, bw = 0, bh = 0;
    if (!a || !b || FAILED(a->GetSize(&aw, &ah)) || FAILED(b->GetSize(&bw, &bh)) || aw != bw || ah != bh) return false;
    WICRect rc{0, 0, static_cast<INT>(aw), static_cast<INT>(ah)};
    ComPtr<IWICBitmapLock> la, lb;
    if (FAILED(a->Lock(&rc, WICBitmapLockRead, &la)) || FAILED(b->Lock(&rc, WICBitmapLockRead, &lb))) return false;
    UINT sa = 0, sb = 0, na = 0, nb = 0;
    BYTE *pa = nullptr, *pb = nullptr;
    if (FAILED(la->GetStride(&sa)) || FAILED(lb->GetStride(&sb)) || FAILED(la->GetDataPointer(&na, &pa)) || FAILED(lb->GetDataPointer(&nb, &pb))) return false;
    for (UINT y = 0; y < ah; ++y) {
        const BYTE* ra = pa + static_cast<std::size_t>(sa) * y;
        const BYTE* rb = pb + static_cast<std::size_t>(sb) * y;
        const bool in_rows = static_cast<LONG>(y) >= skip.top && static_cast<LONG>(y) < skip.bottom;
        if (!in_rows) {
            if (std::memcmp(ra, rb, static_cast<std::size_t>(aw) * 4) != 0) return false;
            continue;
        }
        const std::size_t left = static_cast<std::size_t>(std::clamp<LONG>(skip.left, 0, static_cast<LONG>(aw)));
        const std::size_t right = static_cast<std::size_t>(std::clamp<LONG>(skip.right, 0, static_cast<LONG>(aw)));
        if (std::memcmp(ra, rb, left * 4) != 0) return false;
        if (right < aw && std::memcmp(ra + right * 4, rb + right * 4, (aw - right) * 4) != 0) return false;
    }
    return true;
}

// Panel durumlari (wmain disinda kurulur: buyuk gecici ViewModel nesneleri wmain yigin cercevesine girmez).
// Onay acik: degisiklik listesi + Evet/Hayir bolumun altinda (uyarilarin hepsiyle: en uzun durum).
ViewModel panel_confirm_vm(const std::vector<Variant>& variants) {
    auto vm = fixture(false);
    for (const auto& v : variants)
        if (std::wstring(v.name) == L"confirming") vm.perf = v.view;
    return vm;
}

// Kurtarma gerekli + dis degisiklik: "Tekrar dene" ve "Mevcut degeri koru" ayni satirda (B7: es genislik / yukseklik).
ViewModel panel_recovery_conflict_vm(const std::vector<Variant>& variants) {
    auto vm = fixture(false);
    for (const auto& v : variants)
        if (std::wstring(v.name) == L"recovery") vm.perf = v.view;
    vm.perf.external_conflict = true;
    return vm;
}

// Uzun parca adlari (kutucukta "…" ile kisalir, tam ad UIA adinda), 2,5 Gbps baglanti, sekiz birim (doluluk satiri
// "+N" ile kisalir), denge sonucu (darbogaz karti: islemci darbogazi + "en az" pay, darbogaz yok + pay, belirlenemedi).
ViewModel panel_long_names_vm() {
    using ef::contracts::Measurement;
    using ef::contracts::Unit;
    using BV = core::diagnosis::BalanceVerdict;
    auto vm = fixture(false);
    vm.cpu = "Intel(R) Core(TM) Ultra 9 285K Processor with Performance Hybrid Architecture (uzun ad testi) · 24C/24T";
    vm.gpu = "NVIDIA GeForce RTX 5090 Founders Edition Liquid Cooled (uzun ad testi) · 32 GB\nIntel(R) Arc(TM) Graphics";
    vm.ram = "192 GB · 4×48 GB DDR5 6400 MT/s";
    vm.display = "Samsung Odyssey Neo G9 57 inch Dual UHD · 7680×2160 · 240 Hz";
    vm.live.cpu_pct = Measurement::of(12.0, Unit::Percent);
    vm.live.cpu_max_core_pct = Measurement::of(100.0, Unit::Percent);
    vm.live.ram_pct = Measurement::of(55.5, Unit::Percent);
    vm.live.ram_used_mib = 108800;
    vm.live.ram_total_mib = 196000;
    vm.live.gpu_pct = Measurement::of(37.0, Unit::Percent);
    vm.live.gpu_temp_c = Measurement::of(58.0, Unit::Celsius);
    vm.live.disk_pct = Measurement::of(3.0, Unit::Percent);
    vm.live.net_down_mbps = Measurement::of(1800.0, Unit::Mbps);
    vm.live.net_up_mbps = Measurement::of(940.0, Unit::Mbps);
    vm.live.net_link_mbps = Measurement::of(2500.0, Unit::Mbps);
    vm.live.volumes = {{"C:\\", 1862, 596}, {"D:\\", 3725, 521}, {"E:\\", 3725, 3501}, {"F:\\", 931, 100},
                       {"G:\\", 7451, 7000}, {"H:\\", 465, 5},   {"I:\\", 931, 931},   {"J:\\", 14902, 9000}};
    vm.perf.phase = core::perf::Phase::On;
    vm.perf.switch_on = true;
    vm.perf.active = 12;
    core::measure::BalanceView b;
    b.cpu_fps = 212.4;
    b.rows[0] = {BV::CpuLimited, "", 388.1, true, 208.0};   // ekran karti alt sinir: "en az %46'si bosta kaliyor"
    b.rows[1] = {BV::GpuLimited, "", 150.2, false, 148.0};  // "ekran karti tam yukte · islemcide %30 pay"
    b.rows[2] = {BV::Undetermined, "on_battery", 52.9, false, 52.1};
    b.adapter = "NVIDIA GeForce RTX 5090";
    vm.measure.balance_latest = b;
    vm.components_loading = false;
    return vm;
}

bool rects_overlap(const RectF& a, const RectF& b);
bool save_png(IWICImagingFactory* wic, IWICBitmap* bmp, const std::wstring& path);
// Maket uyum denetimi: EF_DUMP_RECTS ayarliysa <png>.txt'ye kart / isabet kutularini (DIP, pencere koordinati) yazar.
// Yalniz gelistirici olcumu; urun davranisi ve test sonucu degismez.
void dump_rects(const std::wstring& png_path, const HitMap& hits) {
    wchar_t env[8] = {};
    if (GetEnvironmentVariableW(L"EF_DUMP_RECTS", env, 8) == 0) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (png_path + L".txt").c_str(), L"wb") != 0 || !f) return;
    const auto utf8 = [](const std::wstring& w) {
        std::string o(w.size() * 4 + 1, 'x');
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), o.data(), static_cast<int>(o.size()), nullptr, nullptr);
        o.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
        return o;
    };
    for (const auto& r : hits.cards) std::fprintf(f, "card %.1f %.1f %.1f %.1f\n", r.x, r.y, r.w, r.h);
    for (const auto& [id, r] : hits.items) {
        const auto it = hits.meta.find(id);
        std::fprintf(f, "hit %.1f %.1f %.1f %.1f %s\n", r.x, r.y, r.w, r.h, it == hits.meta.end() ? "" : utf8(it->second.name).c_str());
    }
    std::fclose(f);
}
HitMap paint_audited(Screen& screen, Graphics& g, float w, float h, UiState& s, const ViewModel& vm, std::int64_t now);

// ---- Piksel denetimi (spec 2026-11-12 B5 / B7): cizilen karede murekkebin (zeminden farkli piksellerin) yeri. ----
struct Bgra {
    int b = 0, g = 0, r = 0;
};
int color_dist(const Bgra& a, const Bgra& b) { return std::abs(a.b - b.b) + std::abs(a.g - b.g) + std::abs(a.r - b.r); }
Bgra to_bgra(const Color& c) {
    const auto ch = [](float v) { return static_cast<int>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return {ch(c.b), ch(c.g), ch(c.r)};
}
// Kilitli bitmap gorunumu (salt okuma). Kilit, nesne yok olunca birakilir: yeniden cizimden once kapsam kapanmali.
struct PixelView {
    ComPtr<IWICBitmapLock> lock;
    BYTE* data = nullptr;
    UINT stride = 0, w = 0, h = 0;
    bool open(IWICBitmap* bmp) {
        if (!bmp || FAILED(bmp->GetSize(&w, &h))) return false;
        WICRect rc{0, 0, static_cast<INT>(w), static_cast<INT>(h)};
        UINT n = 0;
        return SUCCEEDED(bmp->Lock(&rc, WICBitmapLockRead, &lock)) && SUCCEEDED(lock->GetStride(&stride)) &&
               SUCCEEDED(lock->GetDataPointer(&n, &data)) && data;
    }
    Bgra at(int x, int y) const {
        if (!data || x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h)) return {};
        const BYTE* p = data + static_cast<std::size_t>(stride) * static_cast<std::size_t>(y) + static_cast<std::size_t>(x) * 4;
        return {p[0], p[1], p[2]};
    }
};
// Bolgede (piksel; sag / alt haric) zemin renginden belirgin farkli piksellerin sinir kutusu (sag / alt haric).
// Murekkep yoksa right <= left.
RECT ink_bounds(const PixelView& px, const RECT& region, const Bgra& bg, int tol = 48) {
    RECT out{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    for (LONG y = std::max<LONG>(region.top, 0); y < std::min<LONG>(region.bottom, static_cast<LONG>(px.h)); ++y)
        for (LONG x = std::max<LONG>(region.left, 0); x < std::min<LONG>(region.right, static_cast<LONG>(px.w)); ++x)
            if (color_dist(px.at(static_cast<int>(x), static_cast<int>(y)), bg) > tol) {
                out.left = std::min(out.left, x);
                out.top = std::min(out.top, y);
                out.right = std::max(out.right, x + 1);
                out.bottom = std::max(out.bottom, y + 1);
            }
    if (out.right <= out.left) return {0, 0, 0, 0};
    return out;
}
Palette snapshot_hc_palette() {
    const auto hex = [](const char* hx) { return *Color::from_hex(hx); };
    return high_contrast_palette_from(
        SystemColorTable{hex("#000000"), hex("#000000"), hex("#FFFFFF"), hex("#1AEBFF"), hex("#000000"), hex("#FFFF00"), hex("#3FF23F")});
}

// Spec 2026-11-12 B5 (ust menu; butun sayfalar). Gercek yazi olculeri ve cizilen piksellerle:
//  - logonun MUREKKEBI icerik sutununun sol kenarinda baslar (SVG'nin ic boslugu kirpilmis);
//  - logo ile ilk sekme arasinda sabit bosluk (kTopbarBrandGap);
//  - sekmeler bitisik, hepsinde ayni ic bosluk, etiket kutuda ortali; baska sekme secilince kutular yer degistirmez;
//  - Performans durumu en sagda, sutunun sag kenarinda (sol ve sag kenar payi esit);
//  - logo dikeyde sekme yazilarinin ortasinda;
//  - menudeki kisa ad "Sistem", UIA adi ve sayfa basligi "Sistem araclari";
//  - sekiz sekme (sonraki gorevde gelen "Guncellemeler" ile) asgari pencerede kisa etikete dusmeden sigar.
// Koyu + yuksek kontrast, 960x540 ve 1180x720. Donus: bulgu sayisi; < 0: cizim hatasi.
int topbar_b5_issues(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, const i18n::Catalog& cat, const std::wstring& out,
                     int& renders, int& overflow_total) {
    using i18n::StringId;
    int bad = 0;
    const bool tr_loc = cat.locale() == i18n::Locale::TrTR;
    const wchar_t* loc_name = tr_loc ? L"tr" : L"en";
    constexpr float kUnbounded = 10000.f;
    // Menude gorunen etiket (Screen ile ayni kural; yeni sekme eklenince buraya da eklenir).
    const auto label_id = [](Page pg) {
        switch (pg) {
            case Page::Overview: return StringId::NavOverview;
            case Page::Games: return StringId::NavGames;
            case Page::Readiness: return StringId::NavReadiness;
            case Page::Measurements: return StringId::NavMeasurements;
            case Page::Cleanup: return StringId::NavSystem;
            case Page::History: return StringId::NavHistory;
            case Page::Settings: return StringId::NavSettings;
            case Page::Updates: return StringId::NavUpdates;
            default: return StringId::AppTitle;
        }
    };
    const auto label_w = [&](StringId id) { return std::ceil(g.measure(Role::Body, wide(cat.get(id)), kUnbounded, false).w); };
    const Palette hc = snapshot_hc_palette();
    const std::pair<float, float> sizes[] = {{theme.min_window_w, theme.min_window_h}, {1180.f, 720.f}};
    float status_w = 0;
    for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc}) {
        screen.set_palette(pal);
        for (auto [w, h] : sizes) {
            const auto fail = [&](const wchar_t* what, float a = 0, float b = 0) {
                ++bad;
                std::wprintf(L"TOPBAR_B5 %ls loc=%ls hc=%d size=%.0fx%.0f (%.2f / %.2f)\n", what, loc_name, pal ? 1 : 0, w, h, a, b);
            };
            const auto vm = fixture(false);
            const auto render = [&](Page page, ComPtr<IWICBitmap>& bmp, HitMap& hits) {
                if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                    return false;
                if (!g.bind_wic(bmp.Get(), 96.f) || !g.begin()) return false;
                UiState st;
                st.reduced_motion = true;
                st.page = page;
                g.clear_overflows();
                hits = paint_audited(screen, g, w, h, st, vm, 0);
                if (!g.end()) return false;
                ++renders;
                overflow_total += static_cast<int>(g.overflows().size());
                return true;
            };
            ComPtr<IWICBitmap> bmp_games, bmp;
            HitMap hits_games, hits;
            if (!render(Page::Games, bmp_games, hits_games) || !render(Page::Overview, bmp, hits)) {
                screen.set_palette(nullptr);
                return -1;
            }
            const float col_w = std::min(w - 2 * theme.page_margin, theme.content_max_w), col_x = (w - col_w) / 2;
            const RectF* first = hits.rect_of(nav_hit(static_cast<std::size_t>(kNavOrder.front())));
            const RectF* status = hits.rect_of(Hit::TopbarStatus);
            if (!first || !status) {
                fail(L"tab / status missing");
                continue;
            }
            status_w = std::max(status_w, status->w);
            // Sekmeler: aralarinda es bosluk — maket bicimi kTopbarTabGap (gap-0.5), sikisik bicimde 0 —, es ic bosluk,
            // secimden bagimsiz kutular.
            const RectF* second = hits.rect_of(nav_hit(static_cast<std::size_t>(kNavOrder[1])));
            const float tab_gap = second ? second->x - first->right() : 0.f;
            if (std::fabs(tab_gap - kTopbarTabGap) > 0.01f && std::fabs(tab_gap) > 0.01f) fail(L"tab gap is neither kTopbarTabGap nor 0", tab_gap, kTopbarTabGap);
            float pad0 = -1, prev_right = first->x - tab_gap;
            for (const Page pg : kNavOrder) {
                const Hit id = nav_hit(static_cast<std::size_t>(pg));
                const RectF* r = hits.rect_of(id);
                const RectF* rg = hits_games.rect_of(id);
                if (!r || !rg) {
                    fail(L"tab missing");
                    continue;
                }
                if (std::fabs(r->x - rg->x) > 0.01f || std::fabs(r->w - rg->w) > 0.01f || std::fabs(r->y - rg->y) > 0.01f)
                    fail(L"tab moved when another tab was selected", r->x, rg->x);
                if (std::fabs(r->x - prev_right - tab_gap) > 0.01f) fail(L"tabs are not evenly spaced", r->x, prev_right);
                prev_right = r->right();
                // Sahip secimi 2026-11-14: sekme ikonu varsa icerik = ikon + bosluk + etiket. Ikonsuz ic bosluk hicbir zaman
                // kTopbarTabPad'i asmaz; asiyorsa sekme ikonludur ve ic bosluk ikon payi dusulerek olculur.
                float pad = (r->w - label_w(label_id(pg))) / 2;
                if (pad > kTopbarTabPad + 0.01f) pad -= (kNavIconSize + kNavIconGap) / 2;
                if (pad0 < 0) pad0 = pad;
                if (std::fabs(pad - pad0) > 0.01f || pad < kTopbarTabPadMin - 0.01f || pad > kTopbarTabPad + 0.01f)
                    fail(L"tab inner padding differs", pad, pad0);
            }
            if (prev_right > status->x - theme.page_margin + 0.5f) fail(L"last tab too close to the status", prev_right, status->x);
            // UIA: kisa ad yalniz gorseldir; sekmenin adi sayfanin tam adidir.
            const auto sys = hits.meta.find(nav_hit(static_cast<std::size_t>(Page::Cleanup)));
            if (sys == hits.meta.end() || sys->second.name != wide(cat.get(StringId::NavSystemTools))) fail(L"system tab UIA name is not the full page name");
            if (std::fabs(status->right() - (col_x + col_w)) > 0.5f) fail(L"status is not at the column's right edge", status->right(), col_x + col_w);
            {
                PixelView px;
                if (!px.open(bmp.Get())) {
                    screen.set_palette(nullptr);
                    return -1;
                }
                const float tab_cy = first->y + first->h / 2;
                const Bgra bg = px.at(static_cast<int>(col_x / 2), static_cast<int>(tab_cy));
                const LONG top = static_cast<LONG>(theme.strip_h) + 1, bottom = static_cast<LONG>(theme.strip_h + theme.topbar_h) - 2;
                // Logo murekkebi: sol kenar = sutun, dikey orta = sekme ortasi, sag kenar + sabit bosluk = ilk sekme.
                const RECT logo = ink_bounds(px, {0, top, static_cast<LONG>(first->x) - 1, bottom}, bg);
                if (logo.right <= logo.left) fail(L"logo not drawn");
                else {
                    if (std::fabs(static_cast<float>(logo.left) - col_x) > 1.f) fail(L"logo ink does not start at the content edge", static_cast<float>(logo.left), col_x);
                    const float logo_cy = static_cast<float>(logo.top + logo.bottom) / 2;
                    if (std::fabs(logo_cy - tab_cy) > 1.f) fail(L"logo is not centred on the tab labels", logo_cy, tab_cy);
                    // BOOST maketi (justify-between): sekme grubu logo ile durum gostergesinin arasinda ortali; logoya en az
                    // kTopbarBrandGap (yer yoksa tam bu bosluk).
                    const RectF* last = hits.rect_of(nav_hit(static_cast<std::size_t>(kNavOrder.back())));
                    const float gap_l = first->x - static_cast<float>(logo.right), gap_r = last ? status->x - last->right() : 0.f;
                    if (gap_l < kTopbarBrandGap - 2.f || (std::fabs(gap_l - gap_r) > 2.f && std::fabs(gap_l - kTopbarBrandGap) > 2.f))
                        fail(L"tab group is not centred between the logo and the status", gap_l, gap_r);
                }
                // Sag kenar payi = sol kenar payi (durum metninin murekkebi sutunun sag kenarinda biter).
                const RECT st_ink = ink_bounds(px, {static_cast<LONG>(status->x) - 1, top, static_cast<LONG>(w), bottom}, bg);
                if (st_ink.right <= st_ink.left) fail(L"status not drawn");
                else if (const float gap_r = col_x + col_w - static_cast<float>(st_ink.right); gap_r < -0.5f || gap_r > 2.5f)
                    fail(L"status ink does not end at the content edge", static_cast<float>(st_ink.right), col_x + col_w);
                // Etiket sekme kutusunda yatay ortali (iki yanda ayni ic bosluk).
                for (const Page pg : kNavOrder) {
                    const RectF* r = hits.rect_of(nav_hit(static_cast<std::size_t>(pg)));
                    if (!r) continue;
                    const Bgra tab_bg = px.at(static_cast<int>(r->x) + 3, static_cast<int>(r->y + r->h / 2));
                    const RECT ink = ink_bounds(px, {static_cast<LONG>(r->x) + 3, static_cast<LONG>(r->y) + 6, static_cast<LONG>(r->right()) - 3,
                                                     static_cast<LONG>(r->bottom()) - 6},
                                                tab_bg);
                    const float left = static_cast<float>(ink.left) - r->x, right = r->right() - static_cast<float>(ink.right);
                    if (ink.right <= ink.left || std::fabs(left - right) > 2.f) fail(L"tab label is not centred in its tab", left, right);
                }
            }
            if (!out.empty() && (tr_loc || (!pal && w < 1180.f))) {
                wchar_t name[160];
                swprintf_s(name, L"\\b5_%ls_topbar_%.0fx%.0f%ls.png", loc_name, w, h, pal ? L"_hc" : L"");
                if (!save_png(wic, bmp.Get(), out + name)) {
                    screen.set_palette(nullptr);
                    return -1;
                }
            }
        }
    }
    screen.set_palette(nullptr);
    // Sekiz sekme: gercek etiket genislikleriyle sigdirma kurali. "Guncellemeler" sekmesi henuz yoksa etiketi burada
    // olculur (dizgi sonraki gorevde eklenir); sekme eklendiginde ayni denetim gercek sekmelerle calisir.
    {
        TopbarWidths tw;
        tw.tabs = kNavTabCount;
        for (const Page pg : kNavOrder) tw.labels += label_w(label_id(pg));
        if constexpr (kNavTabCount < 8) {
            ++tw.tabs;
            tw.labels += std::ceil(g.measure(Role::Body, tr_loc ? L"Güncellemeler" : L"Updates", kUnbounded, false).w);
        }
        tw.short_saving = label_w(StringId::NavReadiness) - label_w(StringId::NavReadinessShort);
        const auto& lk = ef::ui::brand::logo(ef::ui::brand::Logo::LockupDark);
        const auto& mk = ef::ui::brand::logo(ef::ui::brand::Logo::MarkCurrent);
        tw.lockup = lk.view_w > 0 ? std::ceil((lk.max_x - lk.min_x) * theme.logo_lockup_w / lk.view_w) : 0.f;
        tw.mark = mk.max_y > mk.min_y ? std::ceil((mk.max_x - mk.min_x) * theme.logo_mark_h / (mk.max_y - mk.min_y)) : 0.f;
        tw.status = status_w;
        // BOOST: sigmayan pencerede durum gostergesi hapsiz (nokta + 12px metin; Screen::topbar_metrics ile ayni olcu).
        tw.status_compact = 8.f + theme.space(1) + std::max(g.measure(Role::Secondary, wide(cat.get(StringId::TopbarPerfOn)), kUnbounded, false).w,
                                                             g.measure(Role::Secondary, wide(cat.get(StringId::TopbarPerfOff)), kUnbounded, false).w);
        tw.status_dot = 8.f + theme.space(1);
        const float avail = theme.min_window_w - 2 * theme.page_margin;
        const TopbarFit fit = fit_topbar(avail, theme.page_margin, tw);
        std::wprintf(L"topbar_fit: loc=%ls tabs=%zu avail=%.0f labels=%.0f lockup=%.0f mark=%.0f status=%.0f -> mode=%d pad=%.0f fits=%d\n", loc_name,
                     tw.tabs, avail, tw.labels, tw.lockup, tw.mark, tw.status, static_cast<int>(fit.mode), fit.tab_pad, fit.fits ? 1 : 0);
        if (!fit.fits || topbar_short_labels(fit.mode) || tw.lockup <= 0 || tw.mark <= 0 || tw.status <= 0) {
            ++bad;
            std::wprintf(L"TOPBAR_B5 eight tabs do not fit the minimum window with full labels loc=%ls\n", loc_name);
        }
    }
    return bad;
}

// Spec 2026-11-12 B7 dugme kurali (butun uygulama; ortak cizim yardimcisi Screen::button_face) + Panel'deki "Testi
// baslat" ana dugmesi (spec B.1; Performans kapali ve acik durumda vurgu rengi dolgulu):
//  - etiketin murekkebi dugme kutusunun yatay ortasinda; inen / cikan harfi olmayan etikette ("Evet" / "Yes") dikey
//    ortasinda (buyuk harf yuksekligi kutunun ortasinda);
//  - ayni satirdaki Evet / Hayir es genislik ve es yukseklikte;
//  - "Testi baslat" etkin ve dolgusu vurgu rengi (koyu + yuksek kontrast; kapali + acik).
// %100 ve %150 olcek. Donus: bulgu sayisi; < 0: cizim hatasi.
int button_rule_issues(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, const std::vector<Variant>& variants, bool tr_loc,
                       int& renders, int& overflow_total) {
    int bad = 0;
    const Palette hc = snapshot_hc_palette();
    auto vm_off = fixture(false);
    vm_off.measure.builtin_available = true;
    auto vm_on = vm_off;
    vm_on.perf.phase = core::perf::Phase::On;
    vm_on.perf.switch_on = true;
    vm_on.perf.active = 3;
    const ViewModel vm_confirm = panel_confirm_vm(variants);
    const ViewModel vm_recovery = panel_recovery_conflict_vm(variants);
    struct Case {
        const wchar_t* name;
        const ViewModel* vm;
    };
    const Case cases[] = {{L"off", &vm_off}, {L"on", &vm_on}, {L"confirm", &vm_confirm}, {L"recovery_conflict", &vm_recovery}};
    for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc}) {
        screen.set_palette(pal);
        const Palette& p = pal ? *pal : theme.dark;
        for (float sc : {1.f, 1.5f})
            for (const auto& c : cases) {
                const float w = 1180.f, h = 720.f;
                const auto fail = [&](const wchar_t* what, float a = 0, float b = 0) {
                    ++bad;
                    std::wprintf(L"BUTTON_RULE %ls case=%ls loc=%ls hc=%d scale=%.2f (%.2f / %.2f)\n", what, c.name, tr_loc ? L"tr" : L"en", pal ? 1 : 0, sc,
                                 a, b);
                };
                ComPtr<IWICBitmap> bmp;
                if (FAILED(wic->CreateBitmap(static_cast<UINT>(w * sc + 0.5f), static_cast<UINT>(h * sc + 0.5f), GUID_WICPixelFormat32bppPBGRA,
                                             WICBitmapCacheOnLoad, &bmp)) ||
                    !g.bind_wic(bmp.Get(), 96.f * sc) || !g.begin()) {
                    screen.set_palette(nullptr);
                    return -1;
                }
                UiState st;
                st.reduced_motion = true;
                st.page = Page::Overview;
                sync_perf_animations(st, PerfView{}, c.vm->perf, theme, 0);
                st.toggle_knob = Tween(c.vm->perf.switch_on ? 1.f : 0.f);
                g.clear_overflows();
                const auto hits = paint_audited(screen, g, w, h, st, *c.vm, 0);
                if (!g.end()) {
                    screen.set_palette(nullptr);
                    return -1;
                }
                ++renders;
                overflow_total += static_cast<int>(g.overflows().size());
                PixelView px;
                if (!px.open(bmp.Get())) {
                    screen.set_palette(nullptr);
                    return -1;
                }
                // Dolu (ana) dugme: dolgu rengi vurgu; etiket murekkebinin kutudaki yeri (piksel -> DIP).
                const auto primary = [&](Hit id, bool vertical) {
                    const RectF* r = hits.rect_of(id);
                    if (!r || !hits.activatable(id)) {
                        fail(L"primary button missing or disabled", static_cast<float>(static_cast<int>(id)));
                        return;
                    }
                    const int cy = static_cast<int>((r->y + r->h / 2) * sc);
                    const Bgra fill = px.at(static_cast<int>((r->x + 4) * sc), cy);
                    // Koyu tema: BOOST .btn-primary dikey gradyani (#52FF8B -> #2EE66B; vurgu ailesi); beklenen renk
                    // ornek satirinin gradyandaki konumundan. Yuksek kontrast: duz vurgu dolgusu.
                    const float fy = std::clamp(((static_cast<float>(cy) + 0.5f) / sc - r->y) / r->h, 0.f, 1.f);
                    const Color want = pal ? p.accent : mix(depth::kBtnTop, depth::kBtnBottom, fy);
                    if (color_dist(fill, to_bgra(want)) > 12) fail(L"primary button is not accent-filled", static_cast<float>(fill.g), want.g * 255.f);
                    const RECT ink = ink_bounds(px, {static_cast<LONG>((r->x + 3) * sc), static_cast<LONG>((r->y + 3) * sc),
                                                     static_cast<LONG>((r->right() - 3) * sc), static_cast<LONG>((r->bottom() - 3) * sc)},
                                                fill, 96);
                    if (ink.right <= ink.left) {
                        fail(L"button label not drawn");
                        return;
                    }
                    const float left = static_cast<float>(ink.left) / sc - r->x, right = r->right() - static_cast<float>(ink.right) / sc;
                    const float top = static_cast<float>(ink.top) / sc - r->y, bottom = r->bottom() - static_cast<float>(ink.bottom) / sc;
                    if (std::fabs(left - right) > 1.5f) fail(L"button label is not centred horizontally", left, right);
                    if (vertical && std::fabs(top - bottom) > 1.5f) fail(L"button label is not centred vertically", top, bottom);
                };
                if (c.vm == &vm_confirm) {
                    primary(Hit::Yes, true);
                    const RectF* yes = hits.rect_of(Hit::Yes);
                    const RectF* no = hits.rect_of(Hit::No);
                    if (!yes || !no || std::fabs(yes->w - no->w) > 0.01f || std::fabs(yes->h - no->h) > 0.01f || std::fabs(yes->y - no->y) > 0.01f)
                        fail(L"Yes / No are not the same size", yes ? yes->w : 0.f, no ? no->w : 0.f);
                } else if (c.vm == &vm_recovery) {
                    // Kurtarma satiri: "Tekrar dene" (ana) + "Mevcut degeri koru" yan yana, es boyda, cakismadan.
                    primary(Hit::Retry, false);
                    const RectF* again = hits.rect_of(Hit::Retry);
                    const RectF* keep = hits.rect_of(Hit::PerfKeep);
                    if (!again || !keep || std::fabs(again->w - keep->w) > 0.01f || std::fabs(again->h - keep->h) > 0.01f ||
                        std::fabs(again->y - keep->y) > 0.01f || keep->x < again->right())
                        fail(L"Retry / Keep are not the same size", again ? again->w : 0.f, keep ? keep->w : 0.f);
                } else {
                    primary(Hit::TestStart, false);
                }
            }
    }
    screen.set_palette(nullptr);
    return bad;
}

// Spec 2026-11-12 F: Panel hizli yollari. 1 Hz canli ornek ve deger gecisi yalniz kutucuk bolgesini, nefes halkasi
// yalniz Performans dugmesi bolgesini yeniden cizer. Denetim (ekran disi; pencere yolu ayni Screen islevlerini cagirir):
//  - bolgeler kaydedilir, birbirine ve baska hicbir ogeye / karta degmez;
//  - onceki ornekle cizilmis kareye yeni ornekle bolgesel cizim = yeni ornekle tam kare (piksel piksel);
//  - kutucuklarin UIA adlari yenilenir, bilinmeyen oge kaydedilmez;
//  - nefes karesi yalniz dugme bolgesinin icini degistirir; hareket durunca kare yine tam kareyle aynidir.
// %100 ve %150 (kesirli olcek), iki pencere boyutu. Donus: bulgu sayisi; < 0: cizim hatasi.
// Spec 2026-11-12 B2: Oyunlar "Calisiyor" noktasinin nabzi ve hizli yolu (Screen::paint_pulse_region).
//  - hareket kapaliyken (pulse_live == false) nokta sabittir: zaman degisse de kare ayni;
//  - hareket acikken dongu basinda (opaklik 1) kare sabit kareyle aynidir, dongu ortasinda yalniz nokta kutulari degisir;
//  - bolgesel cizim (yalniz nokta kutulari) o andaki tam kareyle bayt bayt aynidir;
//  - nokta kutusu calisan oyunun kartinin icindedir ve baska hicbir ogeye degmez.
int games_pulse_issues(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, bool tr_loc, int& renders) {
    int bad = 0;
    auto vm = fixture(false);  // Counter-Strike 2 calisiyor
    vm.games_notice_key.clear();
    const auto state = [](bool live) {
        UiState st;
        st.page = Page::Games;
        st.games_guides_open = false;
        st.pulse_live = live;
        return st;
    };
    const int period = theme.motion.pulse_period;
    const std::pair<float, float> sizes[] = {{theme.min_window_w, theme.min_window_h}, {1180.f, 720.f}};
    for (float sc : {1.f, 1.5f})
        for (auto [w, h] : sizes) {
            const auto fail = [&](const wchar_t* what) {
                ++bad;
                std::wprintf(L"GAMES pulse: %ls tr=%d scale=%.2f size=%.0fx%.0f\n", what, tr_loc ? 1 : 0, sc, w, h);
            };
            const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
            ComPtr<IWICBitmap> still, still_late, start, mid, fast;
            for (IWICBitmap** slot : {still.GetAddressOf(), still_late.GetAddressOf(), start.GetAddressOf(), mid.GetAddressOf(), fast.GetAddressOf()})
                if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, slot))) return -1;
            const auto full = [&](IWICBitmap* b, UiState& st, std::int64_t now, HitMap* hits = nullptr) {
                if (!g.bind_wic(b, 96.f * sc) || !g.begin()) return false;
                HitMap hm = paint_audited(screen, g, w, h, st, vm, now);
                if (hits) *hits = std::move(hm);
                ++renders;
                return g.end();
            };
            UiState s0 = state(false), s1 = state(false), s2 = state(true), s3 = state(true), s4 = state(true);
            HitMap hits;
            if (!full(still.Get(), s0, 0, &hits) || !full(still_late.Get(), s1, period / 2) || !full(start.Get(), s2, 0) ||
                !full(mid.Get(), s3, period / 2) || !full(fast.Get(), s4, 0))
                return -1;
            if (s0.pulse_regions.size() != 1 || s3.pulse_regions.size() != 1) {
                fail(L"pulse region count (one running game expected)");
                continue;
            }
            const RectF dot = s0.pulse_regions[0];
            const RectF* card = hits.rect_of(hit_of(HitKind::GameCard, 0));
            if (!card || dot.x < card->x || dot.right() > card->right() || dot.y < card->y || dot.bottom() > card->bottom())
                fail(L"pulse dot outside the running game's card");
            for (const auto& [id, r] : hits.items)
                if (id != hit_of(HitKind::GameCard, 0) && rects_overlap(r, dot)) fail(L"another element under the pulse dot");
            if (!bitmaps_equal(still.Get(), still_late.Get())) fail(L"dot changes over time while motion is off");
            if (!bitmaps_equal(still.Get(), start.Get())) fail(L"cycle start differs from the static frame");
            if (bitmaps_equal(still.Get(), mid.Get())) fail(L"dot does not dim at mid cycle");
            // Bolgesel cizim: dongu basindaki karenin ustune dongu ortasinin noktasi -> dongu ortasinin tam karesi.
            if (!g.bind_wic(fast.Get(), 96.f * sc) || !g.begin()) return -1;
            screen.paint_pulse_region(g, s4, period / 2);
            if (!g.end()) return -1;
            if (!bitmaps_equal(mid.Get(), fast.Get())) fail(L"region repaint differs from the full paint");
            if (std::fabs(pulse_alpha(true, period / 2, period) - (1.f - alpha_scale::kPulseDim)) > 0.01f || pulse_alpha(true, 0, period) != 1.f ||
                pulse_alpha(false, period / 2, period) != 1.f)
                fail(L"pulse_alpha curve");
        }
    return bad;
}

int panel_fast_path_issues(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, bool tr_loc, const std::wstring& out,
                           int& renders, int& overflow_total) {
    using ef::contracts::Measurement;
    using ef::contracts::Unit;
    int bad = 0;
    auto vm_old = fixture(false);
    vm_old.perf.phase = core::perf::Phase::On;
    vm_old.perf.switch_on = true;
    vm_old.perf.active = 3;
    vm_old.live.gpu_pct = Measurement::of(20.0, Unit::Percent);  // iki ornekte de deger var: kutucuk satirinin boyu ayni
    auto vm_new = vm_old;
    vm_new.live.cpu_pct = Measurement::of(71.0, Unit::Percent);
    vm_new.live.cpu_max_core_pct = Measurement::of(96.0, Unit::Percent);
    vm_new.live.ram_pct = Measurement::of(44.4, Unit::Percent);
    vm_new.live.ram_used_mib = 28000;
    vm_new.live.gpu_pct = Measurement::of(88.0, Unit::Percent);
    vm_new.live.gpu_temp_c = Measurement::of(64.0, Unit::Celsius);
    vm_new.live.disk_pct = Measurement::of(12.0, Unit::Percent);
    vm_new.live.net_down_mbps = Measurement::of(120.0, Unit::Mbps);
    vm_new.live.net_up_mbps = Measurement::of(12.0, Unit::Mbps);
    vm_new.live.volumes = {{"C:\\", 952, 100}, {"D:\\", 931, 797}};
    const auto state = [&](const ViewModel& vm) {
        UiState st;
        st.reduced_motion = true;
        st.page = Page::Overview;
        sync_perf_animations(st, PerfView{}, vm.perf, theme, 0);
        st.toggle_knob = Tween(1.f);
        return st;
    };
    const std::pair<float, float> sizes[] = {{theme.min_window_w, theme.min_window_h}, {1180.f, 720.f}};
    for (float sc : {1.f, 1.5f})
        for (auto [w, h] : sizes) {
            const auto fail = [&](const wchar_t* what) {
                ++bad;
                std::wprintf(L"PANEL fast path: %ls tr=%d scale=%.2f size=%.0fx%.0f\n", what, tr_loc ? 1 : 0, sc, w, h);
            };
            const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
            ComPtr<IWICBitmap> full, fast;
            if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &full)) ||
                FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &fast)))
                return -1;
            UiState sa = state(vm_new);
            if (!g.bind_wic(full.Get(), 96.f * sc) || !g.begin()) return -1;
            const HitMap full_hits = paint_audited(screen, g, w, h, sa, vm_new, 0);
            if (!g.end()) return -1;
            UiState sb = state(vm_old);
            if (!g.bind_wic(fast.Get(), 96.f * sc) || !g.begin()) return -1;
            (void)paint_audited(screen, g, w, h, sb, vm_old, 0);  // onceki ornekle tam kare
            if (!g.end() || !g.begin()) return -1;
            if (!screen.tiles_layout_unchanged(g, sb, vm_new)) fail(L"tile row height changed between two samples with values");
            HitMap fresh;
            screen.paint_tiles_region(g, sb, vm_new, 0, fresh);
            screen.paint_power_region(g, sb, vm_new, 0);
            if (!g.end()) return -1;
            renders += 2;
            const RectF tiles = sa.tiles_region, power = sa.power_region;
            if (tiles.w <= 0 || power.w <= 0 || sb.tiles_region.w <= 0 || sb.power_region.w <= 0) fail(L"region not recorded");
            if (tiles.x < 0 || tiles.right() > w || tiles.bottom() > h || power.x < 0 || power.right() > w || power.bottom() > h || rects_overlap(tiles, power))
                fail(L"regions outside the window or overlapping");
            // Bolgeye yalniz kendi ogeleri duser: kutucuk bolgesinde kutucuklar (Status 3..7), dugme bolgesinde dugme.
            for (const auto& [id, r] : full_hits.items) {
                const HitSlot slot = classify_hit(id);
                const bool tile = slot.kind == HitKind::Status && slot.index >= 3 && slot.index < 3 + kLiveTileCount;
                if (!tile && rects_overlap(r, tiles)) fail(L"another element inside the tile region");
                if (tile && (r.x < tiles.x || r.right() > tiles.right() || r.y < tiles.y || r.bottom() > tiles.bottom())) fail(L"tile outside its region");
                if (id != Hit::Toggle && rects_overlap(r, power)) fail(L"another element inside the power button region");
            }
            for (const auto& cr : full_hits.cards) {
                const bool in_tiles = cr.x >= tiles.x && cr.right() <= tiles.right() && cr.y >= tiles.y && cr.bottom() <= tiles.bottom();
                if (!in_tiles && rects_overlap(cr, tiles)) fail(L"a card crosses the tile region");
                if (rects_overlap(cr, power)) fail(L"a card crosses the power button region");
            }
            if (!bitmaps_equal(full.Get(), fast.Get())) fail(L"region repaint differs from the full paint");
            if (fresh.meta.size() != kLiveTileCount) fail(L"region repaint did not register the five tiles");
            for (const auto& [id, meta] : fresh.meta) {
                const auto it = full_hits.meta.find(id);
                if (it == full_hits.meta.end()) fail(L"region repaint registered an unknown element");
                else if (it->second.name != meta.name) fail(L"UIA text not refreshed");
            }
            // Nefes: halka hareket eder (kare degisir), ama yalniz dugme bolgesinin icinde; durunca tam kareyle ayni.
            RECT ring{};
            ring.left = static_cast<LONG>(std::floor(power.x * sc));
            ring.top = static_cast<LONG>(std::floor(power.y * sc));
            ring.right = static_cast<LONG>(std::ceil(power.right() * sc));
            ring.bottom = static_cast<LONG>(std::ceil(power.bottom() * sc));
            sb.breathe_live = true;
            if (!g.begin()) return -1;
            screen.paint_power_region(g, sb, vm_new, theme.motion.breathe_period / 2);
            if (!g.end()) return -1;
            ++renders;
            if (bitmaps_equal(full.Get(), fast.Get())) fail(L"breathing ring did not move");
            if (!bitmaps_equal_outside(full.Get(), fast.Get(), ring)) fail(L"breathing frame changed pixels outside the button region");
            if (!out.empty() && tr_loc && sc == 1.f && w == 1180.f && !save_png(wic, fast.Get(), out + L"\\panel_tr_breathe_frame_1180x720_100.png")) return -1;
            sb.breathe_live = false;
            if (!g.begin()) return -1;
            screen.paint_power_region(g, sb, vm_new, theme.motion.breathe_period / 2);
            if (!g.end()) return -1;
            ++renders;
            if (!bitmaps_equal(full.Get(), fast.Get())) fail(L"settled ring differs from the full paint");
            // Kalp atisi (spec B.1): Performans kapaliyken dugme cift vurus yapar; kare yalniz dugme bolgesinin icinde
            // degisir (olcek 1,10 + disa acilan halka bolgeye sigar), dinlenmede ve hareket kapaliyken tam kareyle aynidir.
            {
                auto vm_off = vm_new;
                vm_off.perf = PerfView{};  // Kapali
                const auto off_state = [&] {
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Overview;
                    sync_perf_animations(st, PerfView{}, vm_off.perf, theme, 0);
                    st.toggle_knob = Tween(0.f);
                    return st;
                };
                ComPtr<IWICBitmap> off_full, off_fast;
                if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &off_full)) ||
                    FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &off_fast)))
                    return -1;
                UiState so = off_state();
                if (!g.bind_wic(off_full.Get(), 96.f * sc) || !g.begin()) return -1;
                (void)paint_audited(screen, g, w, h, so, vm_off, 0);
                if (!g.end()) return -1;
                UiState sh = off_state();
                if (!g.bind_wic(off_fast.Get(), 96.f * sc) || !g.begin()) return -1;
                (void)paint_audited(screen, g, w, h, sh, vm_off, 0);
                if (!g.end()) return -1;
                renders += 2;
                const RectF pr = sh.power_region;
                if (pr.w <= 0) fail(L"power button region not recorded while off");
                RECT beat{};
                beat.left = static_cast<LONG>(std::floor(pr.x * sc));
                beat.top = static_cast<LONG>(std::floor(pr.y * sc));
                beat.right = static_cast<LONG>(std::ceil(pr.right() * sc));
                beat.bottom = static_cast<LONG>(std::ceil(pr.bottom() * sc));
                const int period = theme.motion.heartbeat_period;
                const auto beat_at = [&](float phase) { return static_cast<std::int64_t>(phase * static_cast<float>(period)); };
                sh.breathe_live = true;
                // Birinci vurus tepesi (olcek 1,10), ikinci vurusun sonu (halka disa acilmis), dinlenme.
                const struct {
                    float phase;
                    bool moves;
                    const wchar_t* png;
                } frames[] = {{kHeartbeatBeat / 2, true, L"\\panel_tr_heartbeat_peak_1180x720_100.png"},
                              {kHeartbeatSecond + kHeartbeatBeat, true, L"\\panel_tr_heartbeat_ring_1180x720_100.png"},
                              {0.8f, false, nullptr}};
                for (const auto& f : frames) {
                    if (!g.begin()) return -1;
                    screen.paint_power_region(g, sh, vm_off, beat_at(f.phase));
                    if (!g.end()) return -1;
                    ++renders;
                    if (f.moves == bitmaps_equal(off_full.Get(), off_fast.Get()))
                        fail(f.moves ? L"heartbeat did not move" : L"resting heartbeat frame differs from the full paint");
                    if (!bitmaps_equal_outside(off_full.Get(), off_fast.Get(), beat)) fail(L"heartbeat frame changed pixels outside the button region");
                    if (f.png && !out.empty() && tr_loc && sc == 1.f && w == 1180.f && !save_png(wic, off_fast.Get(), out + f.png)) return -1;
                }
                // Tam kare de ayni vurus karesini cizer (hover gibi tam kare isteyen bir gecis surerken): bolgesel = tam.
                {
                    UiState sf = off_state();
                    sf.breathe_live = true;
                    if (!g.bind_wic(off_full.Get(), 96.f * sc) || !g.begin()) return -1;
                    (void)paint_audited(screen, g, w, h, sf, vm_off, beat_at(kHeartbeatBeat / 2));
                    if (!g.end() || !g.bind_wic(off_fast.Get(), 96.f * sc) || !g.begin()) return -1;
                    screen.paint_power_region(g, sh, vm_off, beat_at(kHeartbeatBeat / 2));
                    if (!g.end()) return -1;
                    renders += 2;
                    if (!bitmaps_equal(off_full.Get(), off_fast.Get())) fail(L"heartbeat region repaint differs from the full paint of the same frame");
                }
                // Hareket kapali (ayar / Windows tercihi / oyun / pil): sabit kirmizi halka = duragan tam kare.
                sh.breathe_live = false;
                if (!g.bind_wic(off_full.Get(), 96.f * sc) || !g.begin()) return -1;
                (void)paint_audited(screen, g, w, h, so, vm_off, 0);
                if (!g.end() || !g.bind_wic(off_fast.Get(), 96.f * sc) || !g.begin()) return -1;
                screen.paint_power_region(g, sh, vm_off, beat_at(kHeartbeatBeat / 2));
                if (!g.end()) return -1;
                renders += 2;
                if (!bitmaps_equal(off_full.Get(), off_fast.Get())) fail(L"heartbeat drawn while motion is off");
                if (!g.bind_wic(fast.Get(), 96.f * sc)) return -1;  // sonraki denetimler onceki hedefle surer
            }
            // Satir boyu degisimi: olculemeyen deger neden metni tasir; metin sarinca kutucuk satiri uzar ve alttaki kartlar
            // kayar. Bolgesel cizim bunu kapsamaz: denetim, tam karenin gercek yerlesimiyle ayni karari vermeli.
            {
                auto vm_wrap = vm_new;
                vm_wrap.live.gpu_pct = Measurement::missing(Unit::Percent, ef::contracts::MissingReason::NotSupported);
                vm_wrap.live.disk_pct = Measurement::missing(Unit::Percent, ef::contracts::MissingReason::SourceError);
                UiState sw = state(vm_wrap);
                if (!g.begin()) return -1;
                (void)paint_audited(screen, g, w, h, sw, vm_wrap, 0);
                if (!g.end()) return -1;
                ++renders;
                const bool same_height = std::fabs(sw.tiles_h - sb.tiles_h) < 0.5f;
                if (screen.tiles_layout_unchanged(g, sb, vm_wrap) != same_height) fail(L"tile row height change not detected");
            }
            overflow_total += static_cast<int>(g.overflows().size());
            g.clear_overflows();
            // Maliyet (ekran disi, yazilim cizimi): bolgesel cizim tam kareye gore ne kadar ucuz (spec F: bos yuk butcesi).
            if (tr_loc && sc == 1.f && w == 1180.f) {
                g.set_audit(false);
                LARGE_INTEGER f, t0, t1, t2, t3;
                QueryPerformanceFrequency(&f);
                constexpr int kRuns = 100;
                sb.breathe_live = true;
                QueryPerformanceCounter(&t0);
                for (int i = 0; i < kRuns; ++i) {
                    g.begin();
                    screen.paint_power_region(g, sb, vm_new, i * 50);
                    g.end();
                }
                QueryPerformanceCounter(&t1);
                for (int i = 0; i < kRuns; ++i) {
                    g.begin();
                    HitMap scratch;
                    screen.paint_tiles_region(g, sb, vm_new, 0, scratch);
                    g.end();
                }
                QueryPerformanceCounter(&t2);
                for (int i = 0; i < 20; ++i) {
                    g.begin();
                    (void)screen.paint(g, w, h, sb, vm_new, 0);
                    g.end();
                }
                QueryPerformanceCounter(&t3);
                g.set_audit(true);
                g.clear_overflows();
                const auto ms = [&](const LARGE_INTEGER& a, const LARGE_INTEGER& b, int n) {
                    return 1000.0 * static_cast<double>(b.QuadPart - a.QuadPart) / static_cast<double>(f.QuadPart) / n;
                };
                std::wprintf(L"panel_fast_cost: power_region_ms=%.3f tiles_region_ms=%.3f full_paint_ms=%.3f (offscreen %ux%u px)\n", ms(t0, t1, kRuns),
                             ms(t1, t2, kRuns), ms(t2, t3, 20), pw, ph);
            }
        }
    g.release_target();
    return bad;
}

bool rects_overlap(const RectF& a, const RectF& b) { return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f; }

// Canli test ekrani durumlari (wmain disinda kurulur: buyuk gecici ViewModel nesneleri wmain yigin cercevesine girmez).
struct LiveCase {
    const wchar_t* name;
    ViewModel vm;
    core::measure::LiveSample sample;
    bool en_png = false, big_png = false;
};
std::vector<LiveCase> live_cases(const Theme& theme) {
    namespace cm = core::measure;
    std::vector<LiveCase> lc;
    const auto add = [&](const wchar_t* name, cm::TestPhase phase, const char* stage, double elapsed, const cm::LiveSample& sample) -> LiveCase& {
        lc.emplace_back();
        LiveCase& c = lc.back();
        c.name = name;
        c.vm = fixture(false);
        for (const auto& [vname, state] : measure_variants())
            if (std::wstring(vname) == L"builtin_result") c.vm.measure = state;
        auto& ms = c.vm.measure;
        ms.scope = cm::BuiltinScope::Balance;
        ms.mode = cm::TestMode::Quick;
        ms.phase = phase;
        ms.stage_key = stage;
        ms.elapsed_s = elapsed;
        ms.total_s = 0;
        ms.live_view.active = true;
        c.sample = sample;
        return c;
    };
    const auto ring = [](cm::LiveSample& s, std::size_t n, double fps, double wobble, int spike_at) {
        s.ring_count = std::min(n, s.fps_ring.size());
        for (std::size_t i = 0; i < s.ring_count; ++i)
            s.fps_ring[i] = static_cast<float>(fps * (1.0 + wobble * std::sin(static_cast<double>(i) * 1.7)) * (static_cast<int>(i) == spike_at ? 0.62 : 1.0));
    };
    {  // Hazirlik: adim baslamadi, onizleme ve sayi yok
        cm::LiveSample s;
        s.phase = cm::LivePhase::Idle;
        add(L"prepare", cm::TestPhase::Preparing, "measure.stage_balance_prepare", 1, s).en_png = true;
    }
    {  // S1 CPU kapasitesi: 320x180 onizleme (buyutulur), isinma, CPU tam / GPU dusuk, grafik yeni doluyor
        cm::LiveSample s;
        s.preview = live_fixture_image(320, 180, 0, theme.dark);
        s.phase = cm::LivePhase::Warmup;
        s.step_index = 1, s.step_total = 9, s.step_code = 1, s.width = 320, s.height = 180;
        s.has_numbers = true, s.fps = 143.2, s.cpu_ms = 6.79, s.gpu_ms = 2.05;
        ring(s, 5, 143.2, 0.02, -1);
        add(L"s1_cpu", cm::TestPhase::Running, "measure.stage_balance_s1", 3, s);
    }
    {  // S3 GPU kapasitesi 1440p: olcum, GPU tam / CPU dusuk, grafik dolu + bir yavas kova
        cm::LiveSample s;
        s.preview = live_fixture_image(960, 540, 1, theme.dark);
        s.phase = cm::LivePhase::Measure;
        s.step_index = 3, s.step_total = 9, s.step_code = 3, s.width = 2560, s.height = 1440;
        s.has_numbers = true, s.fps = 151.9, s.cpu_ms = 1.12, s.gpu_ms = 6.52;
        ring(s, 32, 151.9, 0.03, 21);
        add(L"s3_gpu", cm::TestPhase::Running, "measure.stage_balance_s3", 14, s).en_png = true;
    }
    {  // K3 birlikte 4K: ikisi de tam yukte, dusuk FPS (ondalikli)
        cm::LiveSample s;
        s.preview = live_fixture_image(960, 540, 2, theme.dark);
        s.phase = cm::LivePhase::Measure;
        s.step_index = 7, s.step_total = 9, s.step_code = 7, s.width = 3840, s.height = 2160;
        s.has_numbers = true, s.fps = 67.1, s.cpu_ms = 13.86, s.gpu_ms = 14.61;
        ring(s, 32, 67.1, 0.05, 9);
        add(L"k3_both", cm::TestPhase::Running, "measure.stage_balance_k3", 33, s).big_png = true;
    }
    {  // T2 tekrar, durduruluyor: dugme devre disi; GPU zaman damgasi yok (GPU yuku "—"), cok yuksek FPS
        cm::LiveSample s;
        s.preview = live_fixture_image(960, 540, 3, theme.dark);
        s.phase = cm::LivePhase::Measure;
        s.step_index = 9, s.step_total = 9, s.step_code = 9, s.width = 1920, s.height = 1080;
        s.has_numbers = true, s.fps = 1012.4, s.cpu_ms = 0.41;
        ring(s, 18, 1012.4, 0.01, -1);
        add(L"t2_stopping", cm::TestPhase::Stopping, "measure.stage_balance_t2", 44, s);
    }
    return lc;
}

bool save_png(IWICImagingFactory* wic, IWICBitmap* bmp, const std::wstring& path) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)))
        return false;
    UINT w = 0, h = 0;
    bmp->GetSize(&w, &h);
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&fmt);
    return SUCCEEDED(frame->WriteSource(bmp, nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}

// Spec 2026-11-11 §3: vektor ikon seti. Her ikon her olcekte gercekten cizilir (kutu icinde murekkep var) ve
// kutusunun disina tasmaz (kenar yumusatma icin 1 piksel pay). Sorun sayisini dondurur; altyapi hatasi -1.
constexpr Icon kAllIcons[] = {Icon::Cpu, Icon::Ram, Icon::Gpu, Icon::Disk, Icon::Net, Icon::Display, Icon::Windows,
                              Icon::NavOverview, Icon::NavGames, Icon::NavReadiness, Icon::NavMeasurements, Icon::NavUpdates, Icon::NavSystem, Icon::NavHistory, Icon::NavSettings};
constexpr const wchar_t* kIconNames[] = {L"Cpu", L"Ram", L"Gpu", L"Disk", L"Net", L"Display", L"Windows", L"NavOverview", L"NavGames",
                                         L"NavReadiness", L"NavMeasurements", L"NavUpdates", L"NavSystem", L"NavHistory", L"NavSettings"};
static_assert(std::size(kAllIcons) == kIconCount && std::size(kIconNames) == kIconCount);

int icon_render_issues(IWICImagingFactory* wic, Graphics& g, const Theme& theme) {
    int issues = 0;
    const Color bg = theme.dark.surface;
    constexpr float kMargin = 8.f;
    for (std::size_t k = 0; k < kIconCount; ++k)
        for (const float sc : {1.f, 1.25f, 1.5f, 2.f})
            for (const float side : {16.f, 20.f, 32.f}) {
                const UINT px = static_cast<UINT>(std::ceil((side + 2 * kMargin) * sc));
                ComPtr<IWICBitmap> bmp;
                if (FAILED(wic->CreateBitmap(px, px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return -1;
                if (!g.bind_wic(bmp.Get(), 96.f * sc) || !g.begin()) return -1;
                g.clear(bg);
                g.draw_icon(kAllIcons[k], {kMargin, kMargin, side, side}, theme.dark.accent);
                if (!g.end()) return -1;
                std::vector<std::uint32_t> pix(static_cast<std::size_t>(px) * px);
                if (FAILED(bmp->CopyPixels(nullptr, px * 4, static_cast<UINT>(pix.size() * 4), reinterpret_cast<BYTE*>(pix.data()))))
                    return -1;
                const std::uint32_t empty = pix[0];  // sol ust kose: kutunun disinda, yalniz zemin
                const float lo = kMargin * sc - 1.f, hi = (kMargin + side) * sc + 1.f;
                int inside = 0, outside = 0;
                for (UINT y = 0; y < px; ++y)
                    for (UINT x = 0; x < px; ++x) {
                        if (pix[static_cast<std::size_t>(y) * px + x] == empty) continue;
                        const float fx = static_cast<float>(x), fy = static_cast<float>(y);
                        const bool in_box = fx + 1 > lo && fx < hi && fy + 1 > lo && fy < hi;
                        ++(in_box ? inside : outside);
                    }
                // En az bir kenar boyu kadar murekkep (bos/eksik cizim yakalanir); kutu disinda hic piksel yok.
                if (inside < static_cast<int>(side * sc) || outside != 0) {
                    ++issues;
                    std::wprintf(L"ICON %ls scale=%.2f side=%.0f inside=%d outside=%d\n", kIconNames[k], sc, side, inside, outside);
                }
            }
    return issues;
}

// Kanit: ikon seti tek sayfada (boyutlar 16/20/24/32/48 + kutucuk bicimi + yuksek kontrast satiri).
bool save_icon_sheet(IWICImagingFactory* wic, Graphics& g, const Theme& theme, float sc, const std::wstring& path) {
    const auto& p = theme.dark;
    constexpr float kW = 760.f, kH = 400.f, kCol = 96.f, kLeft = 72.f;
    const UINT pw = static_cast<UINT>(kW * sc + 0.5f), ph = static_cast<UINT>(kH * sc + 0.5f);
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
    if (!g.bind_wic(bmp.Get(), 96.f * sc) || !g.begin()) return false;
    g.clear(p.background);
    for (std::size_t k = 0; k < kIconCount; ++k)
        g.text(Role::Secondary, kIconNames[k], {kLeft + kCol * static_cast<float>(k), 8, kCol, 18}, p.text_secondary,
               {Align::Center, false, true});
    float y = 36;
    for (const float side : {16.f, 20.f, 24.f, 32.f, 48.f}) {
        wchar_t label[16];
        swprintf_s(label, L"%.0f", side);
        g.text(Role::Secondary, label, {8, y, kLeft - 16, side}, p.text_secondary, {Align::Leading, false, true});
        for (std::size_t k = 0; k < kIconCount; ++k)
            g.draw_icon(kAllIcons[k], {kLeft + kCol * static_cast<float>(k) + (kCol - side) / 2, y, side, side},
                        k % 2 ? p.text_primary : p.accent);
        y += side + 16;
    }
    // Kutucuk bicimi (spec §4): 32x32 ikon kutusu, zemin ikon renginin %12'si, ikon 20 DIP.
    g.text(Role::Secondary, L"tile", {8, y, kLeft - 16, 32}, p.text_secondary, {Align::Leading, false, true});
    const Color tones[] = {p.accent, p.warning, p.orange, p.error, p.text_secondary, p.cyan, p.accent};
    for (std::size_t k = 0; k < kIconCount; ++k) {
        const RectF tile{kLeft + kCol * static_cast<float>(k) + (kCol - 32) / 2, y, 32, 32};
        g.fill_round(tile, theme.radius_control, mix(p.surface, tones[k], alpha_scale::kChipTint));
        g.draw_icon(kAllIcons[k], tile.inset(6), tones[k]);
    }
    y += 32 + 16;
    // Yuksek kontrast: siyah zemin, sistem metin rengi (beyaz).
    g.fill({0, y, kW, 56}, {0, 0, 0, 1});
    g.text(Role::Secondary, L"HC", {8, y, kLeft - 16, 56}, {1, 1, 1, 1}, {Align::Leading, false, true});
    for (std::size_t k = 0; k < kIconCount; ++k)
        g.draw_icon(kAllIcons[k], {kLeft + kCol * static_cast<float>(k) + (kCol - 24) / 2, y + 16, 24, 24}, {1, 1, 1, 1});
    if (!g.end()) return false;
    return save_png(wic, bmp.Get(), path);
}

}  // namespace

// Spec 2026-11-12 E: uygulama ici kip penceresi (Screen::modal_layer). Durumlar: olagan onay, yikici islem (ana dugme
// hata renginde), uyari (geri donussuz), madde listesi, isaret kutusu (kapali / isaretli), tek secimli liste (secimsiz:
// Evet devre disi / secili), geri sayim, oyun onayi, en uzun metinler, odak Evet'te, zorunlu guncelleme kartinin ustunde.
// Koyu + yuksek kontrast, dort olcek (%100..%200), 960x540 ve 1180x720, iki dil. Denetimler:
//  - isabet haritasinda yalniz kip ogeleri (sayfa ve ust menu ogeleri yok); pencere UIA Window, ad = baslik, govde okunur;
//  - kart pencerede ortali ve pencerenin icinde; butun ogeler kartin icinde; Hayir solda, Evet sagda, es genislik / hiza;
//  - ilk odak Hayir ve odak halkasi cizilmis; Evet yalniz secim varsa etkin; isaret kutusu durumu modelle ayni;
//  - ana dugme dolgusu: yikici islemde hata rengi, aksi halde vurgu rengi (piksel);
//  - icerik karartilmis (kipsiz kareden farkli) ve kartin disinda hicbir oge fareyle isabet almiyor;
//  - acilis gecisi: hareket acikken kare zamanla degisir ve biter; hareket kapaliyken ilk kare son karedir (gecis yok).
// Donus: false = cizim hatasi.
struct ModalAudit {
    int renders = 0, overflow = 0, big = 0, missing = 0, a11y = 0;
};
bool modal_snapshot_cases(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, const i18n::Catalog& cat, const std::wstring& out,
                          ModalAudit& n) {
    using i18n::StringId;
    const bool tr_loc = cat.locale() == i18n::Locale::TrTR;
    const wchar_t* loc_name = tr_loc ? L"tr" : L"en";
    const auto t = [&](StringId id) { return wide(cat.get(id)); };
    const auto rep = [](std::wstring s, std::wstring_view key, std::wstring_view value) {
        if (const auto p = s.find(key); p != std::wstring::npos) s.replace(p, key.size(), value);
        return s;
    };
    const auto base_vm = fixture(false);
    struct Case {
        const wchar_t* name;
        ModalSpec spec;
        Page page = Page::Overview;
        std::vector<ModalKey> keys;  // acilistan sonra uygulanan tuslar (odak / secim durumu)
        int ticks = 0;               // geri sayim tiklari
        bool gate = false;           // zorunlu guncelleme kartinin ustunde
        bool png_200 = false;        // %200 PNG'si de kaydedilir
    };
    std::vector<Case> cases;
    std::wstring finding = tr_loc ? L"Ekran 60 Hz'de" : L"Display at 60 Hz";
    if (!base_vm.readiness.checks.empty()) finding = screen.check_text(base_vm.readiness.checks.front());
    const std::wstring fix_q = rep(t(StringId::ChkFixQuestion), L"{finding}", finding);
    {
        // Spec 2026-11-12 B3: Oyun hazirligi "Duzelt" onayi — "Uygulansin mi?" + "eski → yeni" + "Geri alinabilir." + ayrinti.
        namespace rd = ef::core::readiness;
        const rd::Check refresh{"display.0.refresh", rd::Section::Display, rd::Level::Problem, "chk.display.below_max",
                                {{"name", "DELL S2721DGF"}, {"hz", "60"}, {"max", "165"}}, rd::ActionKind::AutoFix, "fix.refresh_rate"};
        Case c{L"fix_apply", fix_apply_modal_spec(cat, fix_change_line(cat, refresh), screen.check_text(refresh), t(StringId::ChkFixDetailDisplay)),
               Page::Readiness};
        cases.push_back(c);
        const rd::Check hags{"windows.hags", rd::Section::Windows, rd::Level::Advice, "chk.win.hags_off", {}, rd::ActionKind::AutoFix, "fix.hags"};
        Case a{L"fix_apply_admin", fix_apply_modal_spec(cat, fix_change_line(cat, hags), screen.check_text(hags), t(StringId::ChkFixDetailAdmin)),
               Page::Readiness};
        cases.push_back(a);
        // Sahip raporu 2026-11-13: ayni duzeltme, yardimci hizmet yolu (UAC sorulmaz) — App::ac ile ayni secim.
        Case as{L"fix_apply_admin_svc",
                fix_apply_modal_spec(cat, fix_change_line(cat, hags), screen.check_text(hags),
                                     t(core::service::admin_copy(StringId::ChkFixDetailAdmin, true))),
                Page::Readiness};
        cases.push_back(as);
    }
    {
        Case c{L"normal", fix_modal_spec(cat, fix_q, t(StringId::ChkFixDetail)), Page::Readiness};
        c.png_200 = true;
        cases.push_back(c);
        c.name = L"focus_yes";
        c.keys = {ModalKey::Tab};
        c.png_200 = false;
        cases.push_back(c);
    }
    {
        // Yikici islem (spec B4 temizlik onayi; App'in sordugu tanimin aynisi): ana dugme hata renginde "Evet, temizle".
        Case c{L"destructive", cleanup_modal_spec(cat, 3, tr_loc ? L"1,2 GB" : L"1.2 GB", L"C:\\"), Page::Cleanup};
        c.png_200 = true;
        cases.push_back(c);
    }
    {
        // Baslangic degisiklikleri onayi (spec B4 / E): Sistem araclari sayfasinin ustunde.
        Case c{L"startup", startup_modal_spec(cat), Page::Startup};
        c.png_200 = true;
        cases.push_back(c);
    }
    {
        // Spec B7 / E: Uygulamayi kaldir — yikici onay ("Evet, kaldir" hata renginde, "Veri silme geri alinamaz").
        Case c{L"uninstall", uninstall_modal_spec(cat), Page::Settings};
        c.png_200 = true;
        cases.push_back(c);
    }
    cases.push_back({L"warning", fix_modal_spec(cat, t(StringId::PerfKeepQuestion), t(StringId::PerfKeepDetail), FixDialogIcon::Warning), Page::Overview});
    {
        ModalSpec s = fix_modal_spec(cat, t(StringId::ActionApplyQuestion), t(StringId::ChkFixDetail));
        for (std::size_t i = 0; i < base_vm.readiness.checks.size() && s.list.size() < 4; ++i) s.list.push_back(screen.check_text(base_vm.readiness.checks[i]));
        if (s.list.empty()) s.list = {finding};
        Case c{L"list", s, Page::Readiness};
        c.png_200 = true;
        cases.push_back(c);
    }
    {
        // Isaret kutusu (spec B7 destek paketi kip penceresi): varsayilan kapali; ikinci durumda klavyeyle isaretlenmis.
        Case c{L"checkbox", support_modal_spec(cat), Page::Settings};
        c.png_200 = true;
        cases.push_back(c);
        c.name = L"checkbox_on";
        c.keys = {ModalKey::ShiftTab, ModalKey::Space};
        c.png_200 = false;
        cases.push_back(c);
    }
    {
        Case c{L"choice", dns_modal_spec(cat, finding, t(StringId::ChkFixDetailDns)), Page::Readiness};
        cases.push_back(c);
        c.name = L"choice_picked";
        c.keys = {ModalKey::Tab, ModalKey::Down};
        cases.push_back(c);
    }
    {
        // R-C4: oyun sunucusu adresi — tek satir yazi alani (bos + ornek metni; yazilmis metin + Evet etkin).
        Case c{L"server_input", server_ping_modal_spec(cat, {}), Page::Measurements};
        c.png_200 = true;
        cases.push_back(c);
        Case f{L"server_input_filled", server_ping_modal_spec(cat, "203.0.113.10"), Page::Measurements};
        cases.push_back(f);
    }
    {
        Case c{L"countdown", display_keep_modal_spec(cat, 15), Page::Readiness};
        c.ticks = 3;
        cases.push_back(c);
    }
    cases.push_back({L"consent", game_consent_modal_spec(cat, L"Counter-Strike 2"), Page::Games});
    cases.push_back({L"long_recovery", fix_modal_spec(cat, t(StringId::RecMachineQuestion), t(StringId::RecMachineDetail)), Page::Overview});
    cases.push_back({L"long_repair", fix_modal_spec(cat, t(StringId::ChkMaintRepairQuestion), t(StringId::ChkMaintRepairDetail)), Page::Readiness});
    cases.push_back({L"speed", fix_modal_spec(cat, t(StringId::SpeedQuestion), t(StringId::SpeedDetail)), Page::Measurements});
    {
        // Spec H2: surucu kurulumu onayi (kurulacak oge + "Yonetici izni istenir"); Guncellemeler sayfasinin ustunde.
        Case c{L"driver_install",
               driver_install_modal_spec(cat, tr_loc ? L"Realtek Semiconductor Corp. - Net · Ağ · Realtek · 10.72.1203.2025 · 03.12.2025 · 4,0 MB"
                                                     : L"Realtek Semiconductor Corp. - Net · Network · Realtek · 10.72.1203.2025 · 12/03/2025 · 4.0 MB"),
               Page::Updates};
        c.png_200 = true;
        cases.push_back(c);
    }
    {
        Case c{L"gate_restore", fix_modal_spec(cat, t(StringId::UpdateRestoreQuestion), t(StringId::UpdateRestoreDetail)), Page::Overview};
        c.gate = true;
        cases.push_back(c);
    }

    const float scales[] = {1.f, 1.25f, 1.5f, 2.f};
    const std::pair<float, float> sizes[] = {{theme.min_window_w, theme.min_window_h}, {1180.f, 720.f}};
    const Palette hc = snapshot_hc_palette();
    const auto modal_id = [](Hit id) {
        if (id == Hit::Modal || id == Hit::ModalTitle || id == Hit::ModalBody0 || id == Hit::ModalBody1 || id == Hit::ModalCheck ||
            id == Hit::ModalNo || id == Hit::ModalYes || id == Hit::ModalInput)
            return true;
        const HitKind k = classify_hit(id).kind;
        return k == HitKind::ModalList || k == HitKind::ModalChoice;
    };
    for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc}) {
        screen.set_palette(pal);
        const Palette& p = screen.pal();
        for (float sc : scales)
            for (auto [w, h] : sizes)
                for (const auto& c : cases) {
                    const auto fail = [&](const wchar_t* what, float a = 0, float b = 0) {
                        ++n.missing;
                        std::wprintf(L"MODAL %ls case=%ls loc=%ls hc=%d scale=%.2f size=%.0fx%.0f (%.2f / %.2f)\n", what, c.name, loc_name, pal ? 1 : 0, sc, w,
                                     h, a, b);
                    };
                    auto vm = base_vm;
                    if (c.gate) {
                        vm.update.outcome = core::update::UpdateOutcome::Mandatory;
                        vm.update.new_version = "0.2.1";
                        vm.perf.phase = core::perf::Phase::On;
                        vm.perf.switch_on = true;
                        vm.perf.active = 3;
                    }
                    ModalState m = modal_open(c.spec);
                    // R-C4: yazi alanli pencerede ilk odak alandadir (yazmaya hemen baslanir; Enter alanda hicbir sey calistirmaz).
                    const ModalItem first_item = modal_has_input(m) ? ModalItem::Input : ModalItem::No;
                    if (m.focus != ModalFocus{first_item, 0}) fail(L"first focus is not No");
                    for (const ModalKey k : c.keys) modal_key(m, k);
                    for (int i = 0; i < c.ticks; ++i) modal_tick(m);
                    if (m.done) fail(L"fixture closed the modal");
                    const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                    ComPtr<IWICBitmap> bmp;
                    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
                    if (!g.bind_wic(bmp.Get(), 96.f * sc) || !g.begin()) return false;
                    UiState st;
                    st.reduced_motion = true;
                    st.page = c.page;
                    st.modal = &m;
                    st.focus = modal_hit(m.focus);
                    st.focus_visible = true;
                    g.clear_overflows();
                    const HitMap hits = paint_audited(screen, g, w, h, st, vm, 0);
                    if (!g.end()) return false;
                    ++n.renders;
                    for (const auto& o : g.overflows()) {
                        ++n.overflow;
                        std::wprintf(L"OVERFLOW modal=%ls loc=%ls hc=%d scale=%.2f size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", c.name, loc_name,
                                     pal ? 1 : 0, sc, w, h, o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                    }
                    g.clear_overflows();
                    wchar_t where[96];
                    swprintf_s(where, L"modal_%ls", c.name);
                    n.a11y += a11y_issues(st, hits, where);
                    for (const auto& [id, r] : hits.items)
                        if (is_control(hits, id) && control_too_big(theme, id, r)) {
                            ++n.big;
                            std::wprintf(L"BIG_CONTROL modal=%ls id=%d %.1fx%.1f\n", c.name, static_cast<int>(id), r.w, r.h);
                        }
                    // ---- Yapi: yalniz kip ogeleri; pencere + baslik + govde + liste + secim + kutu + dugmeler ----
                    for (const auto& [id, r] : hits.items)
                        if (!modal_id(id)) fail(L"page element reachable under the modal", static_cast<float>(static_cast<int>(id)));
                    const RectF* card = hits.rect_of(Hit::Modal);
                    const RectF* no = hits.rect_of(Hit::ModalNo);
                    const RectF* yes = hits.rect_of(Hit::ModalYes);
                    if (!card || !no || !yes || !hits.rect_of(Hit::ModalTitle)) {
                        fail(L"window / title / buttons missing");
                        continue;
                    }
                    const auto& wm = hits.meta.at(Hit::Modal);
                    if (wm.role != AccRole::Window || wm.name != m.spec.title) fail(L"window role / name");
                    if (hits.meta.at(Hit::ModalTitle).role != AccRole::Text || hits.meta.at(Hit::ModalTitle).name != m.spec.title) fail(L"title text");
                    for (std::size_t i = 0; i < m.spec.body.size(); ++i) {
                        const auto it = hits.meta.find(i == 0 ? Hit::ModalBody0 : Hit::ModalBody1);
                        if (it == hits.meta.end() || it->second.role != AccRole::Text || it->second.name != modal_body_text(m, i)) fail(L"body text not readable");
                        else if (wm.help.find(it->second.name) == std::wstring::npos) fail(L"window help text lacks the body");
                    }
                    if ((hits.meta.count(Hit::ModalBody0) > 0) != (m.spec.body.size() > 0) || (hits.meta.count(Hit::ModalBody1) > 0) != (m.spec.body.size() > 1))
                        fail(L"body line count");
                    if (!m.spec.caption.empty() && wm.help.find(m.spec.caption) == std::wstring::npos) fail(L"caption not in window help text");
                    if (m.spec.countdown_s > 0 && wm.help.find(std::to_wstring(m.seconds_left)) == std::wstring::npos) fail(L"countdown not in help text");
                    for (std::size_t i = 0; i < kModalMaxList; ++i) {
                        const auto it = hits.meta.find(hit_of(HitKind::ModalList, i));
                        if ((it != hits.meta.end()) != (i < m.spec.list.size())) fail(L"list rows");
                        else if (it != hits.meta.end() && (it->second.role != AccRole::Text || it->second.name != m.spec.list[i])) fail(L"list row text");
                    }
                    int tab_stops = 0;
                    for (std::size_t i = 0; i < kModalMaxChoices; ++i) {
                        const auto it = hits.meta.find(hit_of(HitKind::ModalChoice, i));
                        if ((it != hits.meta.end()) != (i < m.spec.choices.size())) {
                            fail(L"choice rows");
                            continue;
                        }
                        if (it == hits.meta.end()) continue;
                        if (it->second.role != AccRole::RadioButton || it->second.name != m.spec.choices[i] ||
                            it->second.selected != (m.choice == static_cast<int>(i)))
                            fail(L"choice row role / name / selection");
                        tab_stops += it->second.tab_stop ? 1 : 0;
                    }
                    if (!m.spec.choices.empty() && tab_stops != 1) fail(L"choice list is not one roving tab stop", static_cast<float>(tab_stops));
                    {
                        const auto it = hits.meta.find(Hit::ModalCheck);
                        if ((it != hits.meta.end()) != !m.spec.checkbox.empty()) fail(L"checkbox presence");
                        else if (it != hits.meta.end() &&
                                 (it->second.role != AccRole::CheckBox || it->second.toggle != (m.checked ? 1 : 0) || it->second.name != m.spec.checkbox))
                            fail(L"checkbox role / state / name");
                    }
                    if (hits.meta.at(Hit::ModalNo).name != m.spec.no || hits.meta.at(Hit::ModalYes).name != m.spec.yes) fail(L"button labels");
                    if (!hits.activatable(Hit::ModalNo) || hits.activatable(Hit::ModalYes) != modal_yes_enabled(m)) fail(L"button enabled state");
                    if (!hits.focus_drawn) fail(L"focus ring not drawn");
                    if (c.keys.empty() && st.focus != (modal_has_input(m) ? Hit::ModalInput : Hit::ModalNo)) fail(L"initial focus is not on No");
                    // ---- Geometri ----
                    constexpr float kTol = 0.5f;
                    if (card->x < -kTol || card->y < -kTol || card->right() > w + kTol || card->bottom() > h + kTol)
                        fail(L"card outside the window", card->bottom(), h);
                    if (std::fabs(card->x + card->w / 2 - w / 2) > kTol) fail(L"card not centred horizontally", card->x + card->w / 2, w / 2);
                    if (std::fabs(card->y + card->h / 2 - h / 2) > kTol) fail(L"card not centred vertically", card->y + card->h / 2, h / 2);
                    for (const auto& [id, r] : hits.items)
                        if (r.x < card->x - kTol || r.y < card->y - kTol || r.right() > card->right() + kTol || r.bottom() > card->bottom() + kTol)
                            fail(L"element outside the card", static_cast<float>(static_cast<int>(id)));
                    if (std::fabs(no->w - yes->w) > kTol || std::fabs(no->h - yes->h) > kTol || std::fabs(no->y - yes->y) > kTol)
                        fail(L"buttons differ in size / row", no->w, yes->w);
                    if (no->right() > yes->x + kTol) fail(L"No is not left of Yes", no->right(), yes->x);
                    const float pad = theme.space(4);
                    if (std::fabs(card->right() - pad - yes->right()) > kTol || std::fabs(card->bottom() - pad - yes->bottom()) > kTol)
                        fail(L"buttons not at the bottom right of the card", yes->right(), yes->bottom());
                    for (std::size_t i = 0; i < hits.items.size(); ++i)
                        for (std::size_t j = i + 1; j < hits.items.size(); ++j) {
                            if (hits.items[i].first == Hit::Modal || hits.items[j].first == Hit::Modal) continue;
                            if (rects_overlap(hits.items[i].second, hits.items[j].second))
                                fail(L"elements overlap", static_cast<float>(static_cast<int>(hits.items[i].first)), static_cast<float>(static_cast<int>(hits.items[j].first)));
                        }
                    // Fare: kartin disinda (karartilmis sayfada) hicbir sey isabet almaz.
                    for (const auto& pt : {Point{8, 8}, Point{w / 2, 8}, Point{w - 8, h / 2}, Point{8, h - 8}, Point{card->x - 4, card->y + card->h / 2}})
                        if (hits.at(pt.x, pt.y) != Hit::None) fail(L"hit outside the card", pt.x, pt.y);
                    // UIA noktadan oge (ElementProviderFromPoint): dugmenin / basligin ustunde pencere degil o oge doner;
                    // kartin bos kenar payinda pencerenin kendisi, kartin disinda hicbir sey.
                    {
                        const RectF* title = hits.rect_of(Hit::ModalTitle);
                        if (hits.element_at(no->x + no->w / 2, no->y + no->h / 2) != Hit::ModalNo ||
                            hits.element_at(yes->x + yes->w / 2, yes->y + yes->h / 2) != Hit::ModalYes ||
                            hits.element_at(title->x + 2, title->y + 2) != Hit::ModalTitle)
                            fail(L"UIA point lookup returns the window instead of the element");
                        if (hits.element_at(card->x + 2, card->y + 2) != Hit::Modal) fail(L"UIA point lookup misses the window on the card padding");
                        if (hits.element_at(card->x - 4, card->y + card->h / 2) != Hit::None) fail(L"UIA point lookup hits outside the card");
                    }
                    // ---- Piksel: ana dugme dolgusu (yikici: hata rengi) ve karartma ----
                    if (modal_yes_enabled(m)) {
                        PixelView px;
                        if (!px.open(bmp.Get())) return false;
                        // Dugmenin sol ic kenari (etiketin disinda): dolgu rengi. Odak Evet'teyse halka dugmenin disindadir.
                        const Bgra fill = px.at(static_cast<int>((yes->x + 5.f) * sc), static_cast<int>((yes->y + yes->h / 2) * sc));
                        const Color want = m.spec.destructive ? p.error : p.accent;
                        if (color_dist(fill, to_bgra(want)) > 24) fail(L"primary button fill colour", static_cast<float>(fill.r), static_cast<float>(fill.g));
                    }
                    if (sc == 1.f) {
                        ComPtr<IWICBitmap> plain;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &plain))) return false;
                        if (!g.bind_wic(plain.Get(), 96.f * sc) || !g.begin()) return false;
                        UiState sp;
                        sp.reduced_motion = true;
                        sp.page = c.page;
                        const HitMap page_hits = paint_audited(screen, g, w, h, sp, vm, 0);
                        if (!g.end()) return false;
                        ++n.renders;
                        g.clear_overflows();
                        if (page_hits.meta.count(Hit::Modal)) fail(L"modal element without a modal");
                        // Kartin disindaki bolge karartilmis: kipsiz kareyle ayni olamaz (koyu temada; HC zemini zaten duz).
                        RECT card_px{static_cast<LONG>(card->x), static_cast<LONG>(card->y), static_cast<LONG>(std::ceil(card->right())),
                                     static_cast<LONG>(std::ceil(card->bottom()))};
                        if (bitmaps_equal_outside(bmp.Get(), plain.Get(), card_px)) fail(L"content not dimmed");
                    }
                    if (!out.empty() && w == 1180.f && (sc == 1.f || (sc == 2.f && c.png_200))) {
                        wchar_t name[200];
                        swprintf_s(name, L"\\p1_%ls_modal_%ls%ls_1180x720_%d.png", loc_name, c.name, pal ? L"_hc" : L"", static_cast<int>(sc * 100 + 0.5f));
                        if (!save_png(wic, bmp.Get(), out + name)) return false;
                    }
                    if (!out.empty() && w == theme.min_window_w && sc == 1.f && !pal && (c.png_200 || std::wcsncmp(c.name, L"long", 4) == 0 || std::wcsncmp(c.name, L"choice", 6) == 0)) {
                        wchar_t name[200];
                        swprintf_s(name, L"\\p1_%ls_modal_%ls_960x540_100.png", loc_name, c.name);
                        if (!save_png(wic, bmp.Get(), out + name)) return false;
                    }
                }
        // ---- Acilis gecisi (spec E / F): hareket acikken belirir ve biter; hareket kapaliyken gecis yok ----
        {
            const float w = 1180.f, h = 720.f;
            const ModalState m = modal_open(cases.front().spec);
            const auto vm = base_vm;
            const auto frame = [&](UiState& st, std::int64_t now, ComPtr<IWICBitmap>& bmp) {
                if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
                if (!g.bind_wic(bmp.Get(), 96.f) || !g.begin()) return false;
                (void)paint_audited(screen, g, w, h, st, vm, now);
                ++n.renders;
                const bool ok = g.end();
                g.clear_overflows();
                return ok;
            };
            const auto state = [&](bool reduced, bool paused = false) {
                UiState st;
                st.reduced_motion = reduced;
                st.motion_paused = paused;  // oyun acik / test suruyor / pilde (spec F)
                st.page = Page::Settings;
                st.page_fade = Tween(1);
                st.brand_reveal = Tween(1);
                st.modal = &m;
                st.focus = Hit::ModalNo;
                st.focus_visible = true;
                screen.open_modal_motion(st, 1000);
                return st;
            };
            const auto fail = [&](const wchar_t* what) {
                ++n.missing;
                std::wprintf(L"MODAL motion: %ls loc=%ls hc=%d\n", what, loc_name, pal ? 1 : 0);
            };
            const int open_ms = theme.motion.dialog_open;
            UiState on = state(false);
            ComPtr<IWICBitmap> first, mid, last, later;
            if (!frame(on, 1000, first)) return false;
            const bool anim_first = screen.animating(on, 1000);
            if (!frame(on, 1000 + open_ms / 2, mid) || !frame(on, 1000 + open_ms, last)) return false;
            const bool anim_last = screen.animating(on, 1000 + open_ms);
            if (!frame(on, 1000 + open_ms + 500, later)) return false;
            if (open_ms > 0 && (!anim_first || anim_last)) fail(L"open transition does not start / stop");
            if (open_ms > 0 && (bitmaps_equal(first.Get(), last.Get()) || bitmaps_equal(mid.Get(), last.Get()))) fail(L"open transition draws no intermediate frame");
            if (!bitmaps_equal(last.Get(), later.Get())) fail(L"frame keeps changing after the transition");
            UiState off = state(true);
            ComPtr<IWICBitmap> off_first, off_later;
            if (!frame(off, 1000, off_first)) return false;
            if (screen.animating(off, 1000)) fail(L"animations off: transition timer requested");
            if (!frame(off, 1000 + open_ms + 500, off_later)) return false;
            if (!bitmaps_equal(off_first.Get(), off_later.Get())) fail(L"animations off: first frame is not the final frame");
            // Spec F: oyun acik / test suruyor / pilde de gecis yoktur (animasyon zamanlayicisi istenmez).
            UiState paused = state(false, true);
            ComPtr<IWICBitmap> paused_first, paused_later;
            if (!frame(paused, 1000, paused_first)) return false;
            if (screen.animating(paused, 1000)) fail(L"motion paused: transition timer requested");
            if (!frame(paused, 1000 + open_ms + 500, paused_later)) return false;
            if (!bitmaps_equal(paused_first.Get(), paused_later.Get())) fail(L"motion paused: first frame is not the final frame");
        }
    }
    screen.set_palette(nullptr);
    return true;
}

// ADR-033 / spec 3: kurulum penceresi (480x560 DIP) her evrede, 4 olcekte, koyu ve yuksek kontrast paletle cizilir.
// Denetim: metin tasmasi, buyuk denetim, simetri; her oge adli, etkilesimli ogeler >= 24 DIP ve gercek Tab sirasinda,
// niyet sirasindaki her durak cizimde var, odak halkasi cizilmis, UIA kimlikleri bilinen ve tekil; setup_buttons'in
// gerektirdigi her dugme (etkin/devre disi durumu dahil) var; etkilesimli ogeler pencere icinde ve ust uste binmiyor;
// Confirm'de yalniz kip ogeleri. Spec 8: Ui'de "Kurulum gerekli" seridi (Kur dugmesi Tab sirasinda, metin UIA'da).
// Spec 8: ana pencere "Kurulum gerekli" seridi. Ana pencerenin temasiyla cizilir (EF_TEXT_SCALE kosusunda olcekli),
// kurulum penceresi ise uretimdeki gibi varsayilan temayla (setup_snapshot_cases).
bool install_offer_snapshot_cases(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, const i18n::Catalog& cat,
                                  const std::wstring& out, ModalAudit& n) {
    const bool tr_loc = cat.locale() == i18n::Locale::TrTR;
    const wchar_t* loc_name = tr_loc ? L"tr" : L"en";
    // ---- Spec 8: Ui "Kurulum gerekli" seridi (engellemez; Kur Tab sirasinda, ust menuden hemen sonra) ----
    const std::pair<float, float> sizes[] = {{theme.min_window_w, theme.min_window_h}, {1180.f, 720.f}};
    for (const float sc : {1.f, 1.5f})
        for (auto [w, h] : sizes)
            for (const Page pg : {Page::Overview, Page::Settings}) {
                auto vm = fixture(false);
                vm.install_offer = true;
                vm.settings.autostart_notice = pg == Page::Settings;  // iki serit birlikte de sigmali
                const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                ComPtr<IWICBitmap> bmp;
                if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
                if (!g.bind_wic(bmp.Get(), 96.f * sc) || !g.begin()) return false;
                UiState st;
                st.reduced_motion = true;
                st.page = pg;
                st.focus = Hit::InstallOfferInstall;
                st.focus_visible = true;
                g.clear_overflows();
                const HitMap hits = paint_audited(screen, g, w, h, st, vm, 0);
                if (!g.end()) return false;
                ++n.renders;
                for (const auto& o : g.overflows()) {
                    ++n.overflow;
                    std::wprintf(L"OVERFLOW install_offer loc=%ls scale=%.2f size=%.0fx%.0f \"%.60ls\"\n", loc_name, sc, w, h, o.text.c_str());
                }
                g.clear_overflows();
                wchar_t where[96];
                swprintf_s(where, L"install_offer_%ls_%.0fx%.0f_%.0f", loc_name, w, h, sc * 100);
                n.a11y += a11y_issues(st, hits, where);
                const auto order = focus_order(st, hits);
                const auto pos = std::find(order.begin(), order.end(), Hit::InstallOfferInstall);
                const auto nav_end = std::find(order.begin(), order.end(), nav_hit(static_cast<std::size_t>(kNavOrder[kNavTabCount - 1])));
                if (!st.install_offer || !hits.activatable(Hit::InstallOfferInstall) || pos == order.end() || nav_end == order.end() || pos != nav_end + 1) {
                    ++n.missing;
                    std::wprintf(L"SETUP install offer button / tab order (%ls)\n", where);
                }
                const auto it = hits.meta.find(Hit::InstallOfferText);
                if (it == hits.meta.end() || it->second.role != AccRole::Text || it->second.name != wide(cat.get(i18n::StringId::InstallOfferText))) {
                    ++n.missing;
                    std::wprintf(L"SETUP install offer text (%ls)\n", where);
                }
                if (pg == Page::Settings && !hits.meta.count(Hit::AutostartNoticeOff)) {
                    ++n.missing;
                    std::wprintf(L"SETUP autostart notice hidden by install offer (%ls)\n", where);
                }
                if (!out.empty() && sc == 1.f && (tr_loc || pg == Page::Overview)) {
                    wchar_t name[160];
                    swprintf_s(name, L"\\install_offer_%ls_%ls_%.0fx%.0f_100.png", loc_name, pg == Page::Overview ? L"overview" : L"settings", w, h);
                    if (!save_png(wic, bmp.Get(), out + name)) return false;
                }
            }
    // Serit yokken (kurulu surum) Kur kimligi cizilmez ve Tab sirasinda degildir.
    {
        auto vm = fixture(false);
        ComPtr<IWICBitmap> bmp;
        if (FAILED(wic->CreateBitmap(1180, 720, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
        if (!g.bind_wic(bmp.Get(), 96.f) || !g.begin()) return false;
        UiState st;
        st.reduced_motion = true;
        const HitMap hits = paint_audited(screen, g, 1180.f, 720.f, st, vm, 0);
        if (!g.end()) return false;
        ++n.renders;
        g.clear_overflows();
        const auto order = focus_order(st, hits);
        if (st.install_offer || hits.meta.count(Hit::InstallOfferInstall) ||
            std::find(order.begin(), order.end(), Hit::InstallOfferInstall) != order.end()) {
            ++n.missing;
            std::wprintf(L"SETUP install offer drawn without vm.install_offer\n");
        }
    }
    return true;
}

bool setup_snapshot_cases(IWICImagingFactory* wic, Graphics& g, Screen& screen, const Theme& theme, const i18n::Catalog& cat, const std::wstring& out,
                          ModalAudit& n) {
    (void)screen;  // ana pencere seridi install_offer_snapshot_cases'te (ana tema); kurulum penceresi kendi SetupView'unu kullanir
    namespace cs = core::setup;
    namespace sev = core::setup::ev;
    using core::update::UpdateOutcome;
    const bool tr_loc = cat.locale() == i18n::Locale::TrTR;
    const wchar_t* loc_name = tr_loc ? L"tr" : L"en";
    SetupView view(theme, cat);
    const std::wstring dir = L"C:\\Program Files\\Ecu Fps";
    struct Case {
        const wchar_t* name;
        cs::SetupVm vm;
        bool png_200 = false;
    };
    const auto R = [](cs::SetupVm v, const cs::SetupEvent& e) { return cs::reduce(std::move(v), e); };
    const auto prog = [&](cs::SetupVm v, cs::SetupStep s, cs::StepRecord r) { return R(std::move(v), sev::WorkerProgress{cs::SetupProgress{s, r}}); };
    const cs::SetupVm base = cs::initial_vm("0.3.0", false);
    const cs::SetupVm ready = R(base, sev::CheckDone{UpdateOutcome::UpToDate, {}});
    cs::SetupVm dl = R(base, sev::CheckDone{UpdateOutcome::Mandatory, "0.3.1"});
    dl = R(dl, sev::DownloadProgress{0.42});
    const cs::SetupVm inst = R(R(ready, sev::InstallClicked{false}), sev::ConfirmAnswer{true});
    cs::SetupVm mid = prog(inst, cs::SetupStep::CreateDir, cs::StepRecord::Started);
    mid = prog(mid, cs::SetupStep::CreateDir, cs::StepRecord::Done);
    mid = prog(mid, cs::SetupStep::CopyExe, cs::StepRecord::Started);
    mid = prog(mid, cs::SetupStep::CopyExe, cs::StepRecord::Done);
    mid = prog(mid, cs::SetupStep::Shortcut, cs::StepRecord::Started);
    cs::SetupReply ok;
    ok.result.ok = true;
    const cs::SetupVm done = R(mid, sev::WorkerFinished{core::ElevationOutcome::Started, ok});
    cs::SetupVm done_portable = done;
    done_portable.from_portable = true;
    cs::SetupVm rolled = prog(mid, cs::SetupStep::Shortcut, cs::StepRecord::Undone);
    rolled = prog(rolled, cs::SetupStep::CopyExe, cs::StepRecord::Undone);
    rolled = prog(rolled, cs::SetupStep::CreateDir, cs::StepRecord::Undone);
    cs::SetupReply clean;
    clean.result.ok = false;
    clean.result.failed_at = cs::SetupStep::Shortcut;
    cs::SetupReply dirty = clean;
    dirty.result.rollback_clean = false;
    dirty.result.undo_failed = {cs::SetupStep::Shortcut, cs::SetupStep::CopyExe};
    std::vector<Case> cases;
    cases.push_back({L"checking", base});
    cases.push_back({L"ready", ready, true});
    cases.push_back({L"ready_license", R(ready, sev::ToggleLicense{}), true});
    cases.push_back({L"downloading", dl, true});
    cases.push_back({L"offline", R(base, sev::CheckDone{UpdateOutcome::CheckFailed, {}}), true});
    cases.push_back({L"verify_failed", R(dl, sev::DownloadDone{sev::DownloadResult::VerifyFailed})});
    cases.push_back({L"confirm", R(ready, sev::InstallClicked{false}), true});
    cases.push_back({L"installing", mid, true});
    cases.push_back({L"done", done, true});
    cases.push_back({L"done_portable", R(done_portable, sev::AutostartResult{false})});
    cases.push_back({L"failed_clean", R(rolled, sev::WorkerFinished{core::ElevationOutcome::Started, clean}), true});
    cases.push_back({L"failed_undo", R(prog(mid, cs::SetupStep::Shortcut, cs::StepRecord::UndoFailed), sev::WorkerFinished{core::ElevationOutcome::Started, dirty})});
    cases.push_back({L"uac_denied", R(inst, sev::WorkerFinished{core::ElevationOutcome::Denied, std::nullopt})});
    cases.push_back({L"app_running", R(ready, sev::InstallClicked{true})});
    // Istek gitmeden biten isci: imza reddi (kExitUntrusted) ve diger erken hata + teknik ayrinti satiri; yanitsiz sonuc.
    cases.push_back({L"failed_untrusted",
                     R(inst, sev::WorkerFinished{core::ElevationOutcome::Started, std::nullopt, false,
                                                 static_cast<std::uint32_t>(core::kExitUntrusted), "setup worker exited early (14)"}),
                     true});
    cases.push_back({L"failed_not_started_detail",
                     R(inst, sev::WorkerFinished{core::ElevationOutcome::Started, std::nullopt, false, std::nullopt,
                                                 "setup pipe is not the worker process"})});
    cases.push_back({L"failed_unknown_detail", R(mid, sev::WorkerFinished{core::ElevationOutcome::Started, std::nullopt, true, std::nullopt,
                                                                          "setup result unknown: read timeout"})});
    // Sabitlerin istenen evrede oldugu (reducer degisirse denetim sessizce baska evreyi cizmesin).
    {
        const cs::SetupPhase want[] = {cs::SetupPhase::Checking,  cs::SetupPhase::Ready,     cs::SetupPhase::Ready,      cs::SetupPhase::Downloading,
                                       cs::SetupPhase::Offline,   cs::SetupPhase::VerifyFailed, cs::SetupPhase::Confirm, cs::SetupPhase::Installing,
                                       cs::SetupPhase::Done,      cs::SetupPhase::Done,      cs::SetupPhase::Failed,     cs::SetupPhase::Failed,
                                       cs::SetupPhase::UacDenied, cs::SetupPhase::AppRunning, cs::SetupPhase::Failed,    cs::SetupPhase::Failed,
                                       cs::SetupPhase::Failed};
        static_assert(sizeof(want) / sizeof(want[0]) == 17);
        if (cases.size() != 17) {
            ++n.missing;
            std::wprintf(L"SETUP fixture case count=%zu\n", cases.size());
        }
        for (std::size_t i = 0; i < cases.size() && i < 17; ++i)
            if (cases[i].vm.phase != want[i]) {
                ++n.missing;
                std::wprintf(L"SETUP fixture phase case=%ls\n", cases[i].name);
            }
    }
    const float W = theme.setup.window_w, H = theme.setup.window_h;
    const float scales[] = {1.f, 1.25f, 1.5f, 2.f};
    const Palette hc = snapshot_hc_palette();
    for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc}) {
        view.screen().set_palette(pal);
        for (float sc : scales)
            for (const auto& c : cases) {
                const auto fail = [&](const wchar_t* what, int id = 0) {
                    ++n.missing;
                    std::wprintf(L"SETUP %ls case=%ls loc=%ls hc=%d scale=%.2f id=%d\n", what, c.name, loc_name, pal ? 1 : 0, sc, id);
                };
                const auto& vm = c.vm;
                ModalState m;
                UiState st;
                st.reduced_motion = true;
                st.focus_visible = true;
                if (vm.phase == cs::SetupPhase::Confirm) {
                    m = modal_open(view.confirm_spec());
                    st.modal = &m;
                    st.focus = modal_hit(m.focus);
                } else {
                    const auto intent = setup_focus_order(vm, st);
                    st.focus = intent.empty() ? Hit::None : intent.back();  // ana eylem (en sagdaki dugme) odakta
                }
                const UINT pw = static_cast<UINT>(W * sc + 0.5f), ph = static_cast<UINT>(H * sc + 0.5f);
                ComPtr<IWICBitmap> bmp;
                if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
                if (!g.bind_wic(bmp.Get(), 96.f * sc) || !g.begin()) return false;
                g.clear_overflows();
                const HitMap hits = view.paint(g, W, H, st, vm, dir, 0);
                if (!g.end()) return false;
                ++n.renders;
                for (const auto& o : g.overflows()) {
                    ++n.overflow;
                    std::wprintf(L"OVERFLOW setup=%ls loc=%ls hc=%d scale=%.2f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", c.name, loc_name, pal ? 1 : 0, sc,
                                 o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                }
                g.clear_overflows();
                wchar_t where[96];
                swprintf_s(where, L"setup_%ls_%ls_hc%d_%.0f", c.name, loc_name, pal ? 1 : 0, sc * 100);
                {
                    const auto sym = symmetry_issues(hits, where, g_row_unequal + g_orphan_card < 60);
                    g_row_unequal += sym.row_unequal;
                    g_orphan_card += sym.orphan_card;
                }
                for (const auto& [id, r] : hits.items)
                    if (is_control(hits, id) && control_too_big(theme, id, r)) {
                        ++n.big;
                        std::wprintf(L"BIG_CONTROL setup=%ls id=%d %.1fx%.1f\n", c.name, static_cast<int>(id), r.w, r.h);
                    }
                // ---- Erisilebilirlik ----
                {
                    const auto order = setup_focus_order(vm, st, hits);
                    const auto intent = setup_focus_order(vm, st);
                    std::map<std::wstring, Hit> ids;
                    for (const auto& [id, r] : hits.items) {
                        const auto it = hits.meta.find(id);
                        if (it == hits.meta.end() || it->second.name.empty()) {
                            ++n.a11y;
                            std::wprintf(L"A11Y missing name id=%d (%ls)\n", static_cast<int>(id), where);
                            continue;
                        }
                        const bool in_order = std::find(order.begin(), order.end(), id) != order.end();
                        if (it->second.tab_stop && hits.activatable(id) && !in_order) {
                            ++n.a11y;
                            std::wprintf(L"A11Y not keyboard reachable id=%d (%ls)\n", static_cast<int>(id), where);
                        }
                        if (!hits.activatable(id) && in_order) {
                            ++n.a11y;
                            std::wprintf(L"A11Y disabled item in tab order id=%d (%ls)\n", static_cast<int>(id), where);
                        }
                        if (interactive(it->second.role) && (r.w < theme.touch_min - 0.01f || r.h < theme.touch_min - 0.01f)) {
                            ++n.a11y;
                            std::wprintf(L"A11Y target below 24x24 id=%d %.1fx%.1f (%ls)\n", static_cast<int>(id), r.w, r.h, where);
                        }
                        const std::wstring aid = hit_automation_id(id);
                        if (aid == L"unknown") {
                            ++n.a11y;
                            std::wprintf(L"A11Y unknown automation id id=%d (%ls)\n", static_cast<int>(id), where);
                        }
                        const auto [pos, inserted] = ids.emplace(aid, id);
                        if (!inserted && pos->second != id) {
                            ++n.a11y;
                            std::wprintf(L"A11Y duplicate automation id %ls (%ls)\n", aid.c_str(), where);
                        }
                    }
                    for (const Hit hh : intent)
                        if (!hits.meta.count(hh)) {
                            ++n.a11y;
                            std::wprintf(L"A11Y tab stop without element id=%d (%ls)\n", static_cast<int>(hh), where);
                        }
                    if (st.focus != Hit::None && hits.meta.count(st.focus) && !hits.focus_drawn) {
                        ++n.a11y;
                        std::wprintf(L"A11Y focus ring not drawn focus=%d (%ls)\n", static_cast<int>(st.focus), where);
                    }
                }
                // ---- Beklenen denetimler ----
                const auto present = [&](Hit h) { return hits.meta.count(h) > 0; };
                const auto b = cs::setup_buttons(vm);
                if (st.modal) {
                    for (const auto& [id, r] : hits.items) {
                        const bool modal_item = id == Hit::Modal || id == Hit::ModalTitle || id == Hit::ModalBody0 || id == Hit::ModalBody1 ||
                                                id == Hit::ModalNo || id == Hit::ModalYes;
                        if (!modal_item) fail(L"element reachable under the modal", static_cast<int>(id));
                    }
                    if (!present(Hit::ModalYes) || !present(Hit::ModalNo) || hits.meta.at(Hit::Modal).name != view.confirm_spec().title)
                        fail(L"Evet/Hayir modal missing");
                } else if (vm.license_open) {
                    if (!hits.activatable(Hit::SetupLicenseClose)) fail(L"license close missing");
                    if (present(Hit::SetupAutostart) || present(Hit::SetupInstall)) fail(L"rows / footer drawn under the license panel");
                    int paras = 0;
                    for (const auto& [id, meta] : hits.meta) paras += meta.role == AccRole::Text ? 1 : 0;
                    if (paras < 7) fail(L"license / privacy text not in UIA", paras);
                } else {
                    const std::pair<bool, Hit> want[] = {{b.cancel, Hit::SetupCancel}, {b.install, Hit::SetupInstall},
                                                         {b.retry, Hit::SetupRetry},   {b.install_anyway, Hit::SetupInstallAnyway},
                                                         {b.stop, Hit::SetupStop},     {b.close, Hit::SetupClose},
                                                         {b.open, Hit::SetupOpen},     {b.rows, Hit::SetupAutostart},
                                                         {b.rows, Hit::SetupLicense}};
                    for (const auto& [shown, id] : want)
                        if (shown != present(id)) fail(shown ? L"control missing" : L"unexpected control", static_cast<int>(id));
                    if (b.install && present(Hit::SetupInstall) && hits.activatable(Hit::SetupInstall) != b.install_enabled) fail(L"Kur enabled state");
                    if (b.stop && present(Hit::SetupStop) && hits.activatable(Hit::SetupStop) != b.stop_enabled) fail(L"Durdur enabled state");
                    if (b.rows && present(Hit::SetupAutostart) &&
                        (hits.meta.at(Hit::SetupAutostart).role != AccRole::ToggleSwitch || hits.meta.at(Hit::SetupAutostart).toggle != (vm.autostart ? 1 : 0)))
                        fail(L"autostart toggle role / state");
                    if (vm.phase == cs::SetupPhase::Downloading) {
                        const auto it = hits.meta.find(hit_of(HitKind::Progress, 0));
                        if (it == hits.meta.end() || it->second.role != AccRole::ProgressBar || std::fabs(it->second.range_value - 42.0) > 0.5)
                            fail(L"download progress bar");
                    }
                    if (vm.phase == cs::SetupPhase::Installing || vm.phase == cs::SetupPhase::Done || vm.phase == cs::SetupPhase::Failed) {
                        int rows = 0;
                        for (const auto& [id, meta] : hits.meta) rows += meta.role == AccRole::Text && meta.name.find(L": ") != std::wstring::npos ? 1 : 0;
                        if (rows < 4) fail(L"step rows not in UIA", rows);
                    }
                    if (vm.phase == cs::SetupPhase::Installing && (present(Hit::SetupCancel) || present(Hit::SetupClose))) fail(L"cancel during install");
                }
                // ---- Geometri: etkilesimli ogeler pencere icinde ve ust uste binmiyor ----
                {
                    constexpr float kTol = 0.5f;
                    std::vector<std::pair<Hit, RectF>> ctl;
                    for (const auto& [id, r] : hits.items) {
                        const auto it = hits.meta.find(id);
                        if (it != hits.meta.end() && it->second.offscreen) continue;
                        if (r.x < -kTol || r.y < -kTol || r.right() > W + kTol || r.bottom() > H + kTol) fail(L"element outside the window", static_cast<int>(id));
                        if (it != hits.meta.end() && interactive(it->second.role)) ctl.push_back({id, r});
                    }
                    for (std::size_t i = 0; i < ctl.size(); ++i)
                        for (std::size_t j = i + 1; j < ctl.size(); ++j)
                            if (rects_overlap(ctl[i].second, ctl[j].second)) fail(L"controls overlap", static_cast<int>(ctl[i].first));
                }
                const bool png = !out.empty() && ((tr_loc && !pal && (sc == 1.f || (sc == 2.f && c.png_200))) || (!tr_loc && !pal && sc == 1.f) ||
                                                  (tr_loc && pal && sc == 1.f && std::wstring_view(c.name) == L"ready"));
                if (png) {
                    wchar_t name[160];
                    swprintf_s(name, L"\\setup_%ls_%ls_%03d%ls.png", loc_name, c.name, static_cast<int>(sc * 100 + 0.5f), pal ? L"_hc" : L"");
                    if (!save_png(wic, bmp.Get(), out + name)) return false;
                }
            }
    }
    view.screen().set_palette(nullptr);

    return true;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring out;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring(argv[i]) == L"--out") out = argv[i + 1];
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return 2;
    // Denetim 5.1: EF_TEXT_SCALE=1.25 (ust sinir kMaxTextScale; daha buyuk deger kirpilir) tum vakalari Windows "Metin boyutu" buyutmesiyle cizer (with_text_scale).
    const Theme scaled_theme = [] {
        wchar_t v[16] = {};
        const DWORD n = GetEnvironmentVariableW(L"EF_TEXT_SCALE", v, 16);
        return with_text_scale(default_theme(), n > 0 && n < 16 ? static_cast<float>(_wtof(v)) : 1.f);
    }();
    const auto& theme = scaled_theme;

    const float scales[] = {1.f, 1.25f, 1.5f, 2.f};
    const std::pair<float, float> sizes[] = {{theme.min_window_w, theme.min_window_h}, {1180.f, 720.f}};
    int overflow_total = 0, big_buttons = 0, renders = 0, missing_controls = 0, a11y_total = 0;
    std::wstring family;
    if (!symmetry_self_test()) {
        std::wprintf(L"symmetry audit self-test failed\n");
        return 2;
    }
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto cat = i18n::Catalog::embedded(loc);
        if (!cat.ok()) return 2;
        auto g = Graphics::create(theme, loc == i18n::Locale::TrTR ? L"tr-TR" : L"en-US");
        if (!g) return 2;
        family = g->font_family();
        g->set_audit(true);
        {
            // Oz-denetim: dar kutuya tek satir uzun metin tasma olarak yakalanmali.
            ComPtr<IWICBitmap> probe;
            if (FAILED(wic->CreateBitmap(64, 64, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &probe))) return 2;
            if (!g->bind_wic(probe.Get(), 96.f) || !g->begin()) return 2;
            g->text(Role::Body, L"Çok uzun Türkçe başlık metni", {0, 0, 40, 20}, theme.dark.text_primary, {Align::Leading, false});
            g->end();
            if (g->overflows().size() != 1) {
                std::wprintf(L"audit self-test failed\n");
                return 2;
            }
            g->clear_overflows();
        }
        Screen screen(theme, cat.value());
        if (loc == i18n::Locale::TrTR) a11y_total += palette_contrast_issues(theme.dark, L"dark");
        const auto variants = perf_variants();
        const auto mvariants = measure_variants();
        // UI-03: Olcumler sayfasi durumlari (4 olcek x 2 boyut)
        for (float sc : scales)
            for (auto [w, h] : sizes)
                for (const auto& [mname, mstate] : mvariants) {
                    const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                    ComPtr<IWICBitmap> bmp;
                    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                    if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Measurements;
                    auto vm = fixture(false);
                    auto ms = mstate;
                    ms.storage_roots = {"C:\\", "D:\\", "E:\\", "F:\\"};  // BENCH-05 birim secimi (E: secili)
                    ms.storage_choice = 3;
                    if (ms.result && ms.result->source == core::measure::TestSource::Builtin && ms.result->components.empty() && !ms.result->balance) {
                        // BENCH v2 kartlari: gercekci metrikler + atlanan disk + burst notu + kucuk tampon notu
                        ms.result->components = core::measure::component_cards(
                            {{"scene_avg_fps", 93.66}, {"scene_low1_fps", 88.2}, {"scene_p99_ms", 11.37}, {"cpu_st_units_per_s", 861.7},
                             {"cpu_mt_units_per_s", 5235.6}, {"mem_mt_read_gbps", 61.4}, {"mem_mt_write_gbps", 35.7}, {"mem_mt_copy_gbps", 58.1},
                             {"mem_latency_ns", 92.1}, {"mem_mt_buffer_l3_ratio", 4}, {"storage_seq_1m_qd8_read_mbps", 4544},
                             {"storage_seq_1m_qd8_write_mbps", 2564}, {"storage_seq_1m_qd8_write_burst", 1}, {"storage_rnd_4k_qd1_read_iops", 10770},
                             // EF-X22: GPU desteklenmiyor (neden notu), CPU tamamlanmadi, disk gecti (gercek sayac bicimi)
                             {"scene_gpu_verify_state", 3}, {"cpu_verify_state", 8}, {"cpu_verify_blocks", 7}, {"cpu_verify_mismatch_blocks", 0},
                             {"storage_verify_state", 1}, {"storage_verify_blocks", 64}, {"storage_verify_mismatch_blocks", 0}},
                            {{"scene", {"completed", ""}}, {"cpu", {"completed", ""}}, {"memory", {"completed", ""}}, {"storage", {"partial", "free_share_limit"}}},
                            {{"tier", "high"}, {"res", "1920x1080"}, {"volume", "C:\\"}});
                    }
                    // DIAG-05 + BM-02: bilesen raporu kapi kapali + CPU/GPU olculmedi (UIA metin denetimi icin).
                    vm.components = {{core::diagnosis::Component::Cpu, core::diagnosis::Verdict::Abstain, {}, "no_runtime_evidence"},
                                     {core::diagnosis::Component::Gpu, core::diagnosis::Verdict::Abstain, {}, "no_runtime_evidence"}};
                    vm.diagnosis_gate_key = "diag.gate.no_real_holdout";
                    vm.components_loading = false;
                    vm.measure = ms;
                    st.measure = ms;
                    const auto order = focus_order(st);
                    st.focus = order.back();
                    st.focus_visible = true;
                    g->clear_overflows();
                    // Uzun icerik: once yuksekligi olc, sonra en alta kaydirip tekrar ciz (alt kartlar da denetlenir).
                    const auto hits0 = paint_audited(screen, *g, w, h, st, vm, 0);  // ust gorunum (dugmeler burada)
                    st.scroll = 1e6f;
                    const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                    if (!g->end()) return 2;
                    ++renders;
                    for (const auto& o : g->overflows()) {
                        ++overflow_total;
                        std::wprintf(L"OVERFLOW measure=%ls loc=%d scale=%.2f size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", mname,
                                     static_cast<int>(loc), sc, w, h, o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                    }
                    for (const auto& [id, r] : hits.items)
                        if (is_control(hits, id) && control_too_big(theme, id, r)) {
                            ++big_buttons;
                            std::wprintf(L"BIG_CONTROL measure=%ls id=%d %.1fx%.1f\n", mname, static_cast<int>(id), r.w, r.h);
                        }
                    a11y_total += a11y_issues(st, hits0, mname);
                    // EF-A11Y: teshis karti (kapi metni, CPU/GPU satiri) ve dogrulama karti UIA'da okunur metin.
                    {
                        const auto& cv = cat.value();
                        const auto need_text = [&](const std::wstring& needle, int mode, const wchar_t* what) {
                            if (!has_text_item(hits, needle, mode)) {
                                ++a11y_total;
                                std::wprintf(L"A11Y_TEXT missing measure=%ls %ls \"%.60ls\"\n", mname, what, needle.c_str());
                            }
                        };
                        // Kart kisa durum cumlesi gosterir; uzun gerekce (kapi nedeni) UIA HelpText'te okunur.
                        need_text(wide(cv.get(i18n::StringId::DiagGateClosedShort)), 0, L"diag_gate");
                        {
                            const auto full = wide(cv.get(i18n::StringId::DiagGateNoRealHoldout));
                            bool help_ok = false;
                            for (const auto& [id, m] : hits.meta)
                                if (m.role == AccRole::Text && m.help == full) help_ok = true;
                            if (!help_ok) {
                                ++a11y_total;
                                std::wprintf(L"A11Y_TEXT missing measure=%ls diag_gate_help\n", mname);
                            }
                        }
                        need_text(wide(cv.get(i18n::StringId::DiagCompCpu)) + L": ", 1, L"diag_cpu");
                        need_text(wide(cv.get(i18n::StringId::DiagCompGpu)) + L": ", 1, L"diag_gpu");
                        // Bosluk 7: bos alan atlama metni doldurulmus sayilarla cizilir (yer tutucu kalmaz).
                        if (std::wstring(mname) == L"builtin_storage_free_space" && ms.result) {
                            for (const auto& c : ms.result->components)
                                if (c.id == "storage") {
                                    const auto s = screen.component_status_text(c);
                                    if (c.reason != "free_space_short" || s.find(L"312") == std::wstring::npos ||
                                        s.find(L"287") == std::wstring::npos || s.find(L'{') != std::wstring::npos) {
                                        ++a11y_total;
                                        std::wprintf(L"A11Y_TEXT storage_free_space bad \"%.80ls\"\n", s.c_str());
                                    }
                                    need_text(s, 2, L"storage_free_space");
                                }
                        }
                        // B14 incelemesi: uzun test kartinin asama / anlik / sonuc notlari UIA'da salt okunur metin.
                        if (std::wcsncmp(mname, L"endurance_", 10) == 0 && (ms.endurance.running || ms.endurance.has_result || ms.endurance.failed)) {
                            const auto title = wide(cv.get(i18n::StringId::MeasureEnduranceTitle));
                            if (!has_text_item(hits0, title, 1) && !has_text_item(hits, title, 1)) {
                                ++a11y_total;
                                std::wprintf(L"A11Y_TEXT missing measure=%ls endurance_status\n", mname);
                            }
                            if (ms.endurance.running && ms.endurance.has_live) {
                                const auto live = wide(cv.get(ms.endurance.telemetry_unsupported ? i18n::StringId::MeasureEnduranceLiveUnsupported
                                                                                                 : i18n::StringId::MeasureEnduranceUnreadable));
                                const auto probe = ms.endurance.telemetry_unsupported ? live.substr(live.find(L"·")) : live;
                                const bool want = ms.endurance.telemetry_unsupported || !ms.endurance.live_gpu_mhz;
                                if (want && !has_text_item(hits0, probe, 2) && !has_text_item(hits, probe, 2)) {
                                    ++a11y_total;
                                    std::wprintf(L"A11Y_TEXT endurance live line missing measure=%ls\n", mname);
                                }
                                // Desteklenmeyen sensor "okunamiyor" diye yazilmaz (yalniz uzun test kartinin ogesi denetlenir).
                                const auto unread = wide(cv.get(i18n::StringId::MeasureEnduranceUnreadable));
                                const auto card_says_unread = [&](const HitMap& hm) {
                                    for (const auto& [id, meta] : hm.meta)
                                        if (meta.role == AccRole::Text && meta.name.starts_with(title) && meta.name.find(unread) != std::wstring::npos) return true;
                                    return false;
                                };
                                if (ms.endurance.telemetry_unsupported && (card_says_unread(hits0) || card_says_unread(hits))) {
                                    ++a11y_total;
                                    std::wprintf(L"A11Y_TEXT unsupported telemetry shown as unreadable measure=%ls\n", mname);
                                }
                            }
                        }
                        if (ms.result) {
                            for (const auto& c : ms.result->components)
                                if (c.id == "verify") {
                                    need_text(wide(cv.get(i18n::StringId::BenchCardVerify)), 1, L"verify_card");
                                    need_text(wide(cv.get(i18n::StringId::BenchVerifyGpu)) + L": ", 2, L"verify_gpu_row");
                                }
                        }
                    }
                    // M01/M03: ekranda/UIA'da ham neden kodu veya ic kimlik yok (bilesen durum metni, gecmis basligi, UIA adlari).
                    {
                        auto raw = [](const std::wstring& s) {
                            return s.find(L'_') != std::wstring::npos || s.find(L"scene_v") != std::wstring::npos ||
                                   s.find(L"bench.") != std::wstring::npos;
                        };
                        std::vector<std::wstring> texts;
                        if (ms.result) {
                            for (const auto& c : ms.result->components) texts.push_back(screen.component_status_text(c));
                            texts.push_back(screen.result_headline(*ms.result));
                            texts.push_back(screen.result_title(*ms.result));
                            if (ms.result->drops) {
                                const auto dt = screen.drop_report_texts(*ms.result->drops);
                                texts.push_back(dt.headline);
                                for (const auto& f : dt.fields) texts.push_back(f);
                                for (const auto& e : dt.evidence) texts.push_back(e);
                            }
                            if (ms.result->game_limit) {
                                const auto gt = screen.game_limit_ui_texts(*ms.result->game_limit);
                                texts.push_back(gt.headline);
                                for (const auto& l : gt.lines) texts.push_back(l);
                                for (const auto& n : gt.notes) texts.push_back(n);
                            }
                        }
                        for (const auto& hh : ms.history) texts.push_back(screen.result_title(hh) + L" " + screen.result_headline(hh));
                        if (ms.phase == core::measure::TestPhase::Failed) texts.push_back(screen.measure_failure_text(ms));
                        for (const auto* hm : {&hits0, &hits})
                            for (const auto& [id, meta] : hm->meta)
                                if (!interactive(meta.role)) texts.push_back(meta.name);
                        for (const auto& tx : texts)
                            if (raw(tx)) {
                                ++a11y_total;
                                std::wprintf(L"RAW_KEY measure=%ls \"%.80ls\"\n", mname, tx.c_str());
                            }
                    }
                    // M05: Durduruluyor'da Durdur dugmesi devre disi gorunur (Baslat yok); calisirken etkin. Spec 2026-11-12 C:
                    // yuvarlak dugme her zaman Test kartinin ortasindadir — baslatilamazken Baslat devre disi (UIA'da kalir,
                    // nedeni HelpText'te); etkin Baslat yalniz can_start iken.
                    bool start = false, stop = false, stop_enabled = false, start_drawn = false;
                    for (const auto& [id, r] : hits0.items) {
                        start_drawn |= id == Hit::TestStart;
                        start |= id == Hit::TestStart && hits0.activatable(id);
                        stop |= id == Hit::TestStop;
                        stop_enabled |= id == Hit::TestStop && hits0.activatable(id);
                    }
                    if (!core::measure::busy(mstate) && !start_drawn) {
                        ++missing_controls;
                        std::wprintf(L"MEASURE round start button missing %ls\n", mname);
                    }
                    // M07: test islerken kaynak/sure/birim UIA'da var ama devre disi.
                    if (core::measure::busy(mstate))
                        for (Hit sel : {Hit::SourceBuiltin, Hit::SourceGame, Hit::ModeQuick, Hit::StorageChoice0})
                            if (hits0.meta.count(sel) && hits0.activatable(sel)) {
                                ++missing_controls;
                                std::wprintf(L"MEASURE selection enabled while busy %ls id=%d\n", mname, static_cast<int>(sel));
                            }
                    if (core::measure::busy(mstate) != stop || core::measure::can_stop(mstate) != stop_enabled ||
                        core::measure::can_start(mstate) != start) {
                        ++missing_controls;
                        std::wprintf(L"MEASURE controls mismatch %ls\n", mname);
                    }
                    // 0.2.1 Olcumler duzeltmeleri: 960x540 (%100) ve 1180x720 (%200) ust gorunumleri de kaydedilir.
                    const bool olc_extra = std::wcsncmp(mname, L"olc_", 4) == 0 && ((sc == 1.f && w == theme.min_window_w) || (sc == 2.f && w == 1180.f));
                    if (!out.empty() && loc == i18n::Locale::TrTR && ((sc == 1.f && w == 1180.f) || olc_extra)) {
                        // Kanit: kaydirmadan (ust) gorunum ayri cizilir
                        ComPtr<IWICBitmap> top;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &top))) return 2;
                        if (!g->bind_wic(top.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st2 = st;
                        st2.scroll = 0;
                        paint_audited(screen, *g, w, h, st2, vm, 0);
                        if (!g->end()) return 2;
                        wchar_t name[200];
                        swprintf_s(name, L"\\ui03_tr_%ls_%.0fx%.0f%ls.png", mname, w, h, sc == 2.f ? L"_200" : L"");
                        if (!save_png(wic.Get(), top.Get(), out + name)) return 2;
                        // B14 incelemesi: uzun test kartinin notlari ve dugmesi 1180x720'de katlamanin altinda kalir; kart
                        // tamami gorunsun diye uzun pencerede (1180x1100) ayrica cizilir. Tasma denetimi bu cizimde de sayilir.
                        const bool is_proof = std::wcsncmp(mname, L"proof_", 6) == 0;  // P1: kart katlamanin altinda
                        if ((std::wcsncmp(mname, L"endurance_", 10) == 0 || is_proof) && w == 1180.f && sc == 1.f) {
                            const float kTallH = is_proof ? 1400.f : 1100.f;
                            ComPtr<IWICBitmap> tall;
                            if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(kTallH), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &tall)))
                                return 2;
                            if (!g->bind_wic(tall.Get(), 96.f) || !g->begin()) return 2;
                            g->clear_overflows();
                            UiState st3 = st;
                            st3.scroll = 0;
                            paint_audited(screen, *g, w, kTallH, st3, vm, 0);
                            if (!g->end()) return 2;
                            for (const auto& o : g->overflows()) {
                                ++overflow_total;
                                std::wprintf(L"OVERFLOW measure=%ls tall size=%.0fx%.0f \"%.60ls\"\n", mname, w, kTallH, o.text.c_str());
                            }
                            swprintf_s(name, L"\\ui03_tr_%ls_%.0fx%.0f.png", mname, w, kTallH);
                            if (!save_png(wic.Get(), tall.Get(), out + name)) return 2;
                        }
                    }
                }
        // Task 12: internet hiz testi karti durumlari (Olcumler sayfasinin ust satiri; tasma/dugme/a11y denetimi).
        {
            using SV = ViewModel::SpeedView;
            namespace cn = core::net;
            std::vector<std::pair<const wchar_t*, SV>> sv;
            sv.push_back({L"speed_idle", SV{}});
            {
                SV v;
                v.running = true;
                v.phase = cn::SpeedPhase::Download;
                v.live_mbps = 312.4;
                v.bytes = 41'200'000;
                sv.push_back({L"speed_down", v});
                v.phase = cn::SpeedPhase::Upload;
                v.live_mbps = 48.7;
                sv.push_back({L"speed_up", v});
            }
            {
                SV v;
                v.phase = cn::SpeedPhase::Done;
                v.down = 945.6;
                v.up = 51.3;
                v.latency = 12.4;
                v.bytes = 268'000'000;
                sv.push_back({L"speed_done", v});
            }
            {
                SV v;
                v.phase = cn::SpeedPhase::Cancelled;
                v.bytes = 1'100'000;
                sv.push_back({L"speed_cancel", v});
                v.fail = cn::SpeedFail::Gated;
                sv.push_back({L"speed_gated", v});
                v.phase = cn::SpeedPhase::Failed;
                v.fail = cn::SpeedFail::Tls;
                sv.push_back({L"speed_tls", v});
            }
            for (float sc : scales)
                for (auto [w, h] : sizes)
                    for (bool blocked : {false, true})
                        for (const auto& [sname, sstate] : sv) {
                            if (blocked && sstate.running) continue;
                            const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                            ComPtr<IWICBitmap> bmp;
                            if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                            if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Measurements;
                            auto vm = fixture(false);
                            vm.speed = sstate;
                            if (blocked) {
                                vm.game_gate_blocked = true;
                                vm.game_block_reason_key = "gate.game_running";
                                vm.game_block_title_key = "gate.game_running_title";
                            }
                            g->clear_overflows();
                            paint_audited(screen, *g, w, h, st, vm, 0);  // icerik yuksekligi
                            st.scroll = 1e6f;                    // en alt: alttaki kartlar da tasma denetiminden gecer
                            paint_audited(screen, *g, w, h, st, vm, 0);
                            st.scroll = 0;                       // hiz testi karti ust satirda: kanit goruntusu ve isabetler ustten
                            const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                            if (!g->end()) return 2;
                            ++renders;
                            for (const auto& o : g->overflows()) {
                                ++overflow_total;
                                std::wprintf(L"OVERFLOW speed=%ls loc=%d scale=%.2f size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", sname,
                                             static_cast<int>(loc), sc, w, h, o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                            }
                            // Calisirken yalniz Durdur (etkin); degilken Baslat (oyun kapisi kapaliyken devre disi).
                            const bool has_start = hits.meta.count(Hit::SpeedStart) > 0, has_stop = hits.meta.count(Hit::SpeedStop) > 0;
                            const bool ok = sstate.running ? (has_stop && !has_start && hits.activatable(Hit::SpeedStop))
                                                           : (has_start && !has_stop && hits.activatable(Hit::SpeedStart) == !blocked);
                            if (!ok) {
                                ++missing_controls;
                                std::wprintf(L"SPEED controls mismatch %ls blocked=%d\n", sname, blocked ? 1 : 0);
                            }
                            for (const auto& [id, r] : hits.items)
                                if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                    ++big_buttons;
                                    std::wprintf(L"BIG_CONTROL speed=%ls id=%d %.1fx%.1f\n", sname, static_cast<int>(id), r.w, r.h);
                                }
                            a11y_total += a11y_issues(st, hits, sname);
                            if (!out.empty() && loc == i18n::Locale::TrTR && sc == 1.f && w == 1180.f && !blocked) {
                                wchar_t name[200];
                                swprintf_s(name, L"\\task12_tr_%ls_1180x720.png", sname);
                                if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                            }
                        }
        }
        // Zorunlu guncelleme: ust serit durumlari (Genel Bakis ustu) ve tam ekran guncelleme karti (indirme, kurulum,
        // hata, klasor yazilamiyor; Performans acikken "Onceki ayarlara don"). Koyu ve yuksek kontrast paletleri.
        {
            using UV = ViewModel::UpdateView;
            using core::update::UpdateOutcome;
            struct UpdCase {
                const wchar_t* name;
                UV v;
                bool perf_on;
            };
            std::vector<UpdCase> uc;
            {
                UV v;
                v.outcome = UpdateOutcome::Checking;
                uc.push_back({L"upd_checking", v, false});
                v.outcome = UpdateOutcome::CheckFailed;
                uc.push_back({L"upd_failed", v, false});
                v.outcome = UpdateOutcome::VerifyFailed;
                uc.push_back({L"upd_verify_failed", v, false});
                v.outcome = UpdateOutcome::CheckFailed;
                v.notice_key = "update.rolled_back";
                uc.push_back({L"upd_rolled_back", v, false});
                v.outcome = UpdateOutcome::UpToDate;
                v.notice_key = "update.updated";
                v.notice_version = "0.2.0";
                uc.push_back({L"upd_updated", v, false});
            }
            {
                UV v;
                v.outcome = UpdateOutcome::Mandatory;
                v.new_version = "0.2.0";
                uc.push_back({L"gate_idle", v, false});
                uc.push_back({L"gate_restore", v, true});
                v.step = UV::Step::Downloading;
                v.bytes = 12'400'000;
                v.total = 38'900'000;
                v.progress01 = 12.4 / 38.9;
                uc.push_back({L"gate_downloading", v, true});
                v.step = UV::Step::Installing;
                v.progress01 = 1;
                uc.push_back({L"gate_installing", v, false});
                v.step = UV::Step::Idle;
                v.notice_key = "update.download_failed";
                uc.push_back({L"gate_download_failed", v, true});
                v.notice_key = "update.verify_failed";
                uc.push_back({L"gate_verify_failed", v, false});
                // ADR-033 / spec 7: kurulu surumde hizmet yok/bozuk -> "Yardimci hizmeti onar"; onarim suruyor.
                v.notice_key = "update.service_unavailable";
                v.service_unavailable = true;
                uc.push_back({L"gate_repair_service", v, true});
                v.notice_key.clear();
                v.step = UV::Step::Repairing;
                uc.push_back({L"gate_repairing", v, false});
                v.step = UV::Step::Idle;
                v.notice_key = "update.service_failed";
                v.service_unavailable = false;
                uc.push_back({L"gate_service_failed", v, false});
                v.notice_key.clear();
                v.outcome = UpdateOutcome::NotWritable;
                uc.push_back({L"gate_not_writable", v, true});
            }
            auto hex = [](const char* hx) { return *Color::from_hex(hx); };
            const Palette hc_upd = high_contrast_palette_from(
                SystemColorTable{hex("#000000"), hex("#000000"), hex("#FFFFFF"), hex("#1AEBFF"), hex("#000000"), hex("#FFFF00"),
                                 hex("#3FF23F")});
            for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc_upd}) {
                screen.set_palette(pal);
                for (float sc : scales)
                    for (auto [w, h] : sizes)
                        for (const auto& c : uc) {
                            const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                            ComPtr<IWICBitmap> bmp;
                            if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                            if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Overview;
                            auto vm = fixture(false);
                            vm.update = c.v;
                            if (c.perf_on) {
                                vm.perf.phase = core::perf::Phase::On;
                                vm.perf.switch_on = true;
                                vm.perf.active = 3;
                            }
                            const bool gate = update_gate_shown(vm);
                            g->clear_overflows();
                            (void)paint_audited(screen, *g, w, h, st, vm, 0);  // kart durumu (odak sirasi) ogrenilir
                            if (!g->end() || !g->begin()) return 2;
                            st.focus = focus_order(st).back();
                            st.focus_visible = true;
                            g->clear_overflows();
                            const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                            if (!g->end()) return 2;
                            ++renders;
                            for (const auto& o : g->overflows()) {
                                ++overflow_total;
                                std::wprintf(L"OVERFLOW update=%ls loc=%d scale=%.2f size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", c.name,
                                             static_cast<int>(loc), sc, w, h, o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                            }
                            bool ok = true;
                            if (gate) {
                                const bool nw = c.v.outcome == UpdateOutcome::NotWritable;
                                const Hit main = nw ? Hit::UpdateOpenPage : (c.v.service_unavailable ? Hit::UpdateRepairService : Hit::UpdateNow);
                                const bool busy = c.v.step != UV::Step::Idle;
                                ok = hits.meta.count(main) && hits.activatable(main) == !busy && hits.activatable(Hit::UpdateQuit) &&
                                     (hits.meta.count(Hit::UpdateRestore) > 0) == c.perf_on && !hits.meta.count(nav_hit(0)) &&
                                     !hits.meta.count(Hit::Toggle);
                            } else {
                                ok = hits.meta.count(Hit::UpdateStatus) && hits.meta.count(nav_hit(0)) && hits.meta.count(Hit::Toggle);
                                // Serit metni icerik sutununda: logo, sayfa basligi ve kartlarla ayni sol kenar.
                                const RectF* us = hits.rect_of(Hit::UpdateStatus);
                                const float col_w = std::min(w - 2 * theme.page_margin, theme.content_max_w), col_x = (w - col_w) / 2;
                                ok = ok && us && std::fabs(us->x - col_x) < 0.5f && us->right() <= col_x + col_w + 0.5f;
                            }
                            if (!ok) {
                                ++missing_controls;
                                std::wprintf(L"UPDATE controls mismatch %ls\n", c.name);
                            }
                            for (const auto& [id, r] : hits.items)
                                if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                    ++big_buttons;
                                    std::wprintf(L"BIG_CONTROL update=%ls id=%d %.1fx%.1f\n", c.name, static_cast<int>(id), r.w, r.h);
                                }
                            a11y_total += a11y_issues(st, hits, c.name);
                            if (!out.empty() && sc == 1.f && w == 1180.f) {
                                wchar_t name[200];
                                swprintf_s(name, L"\\task15_%ls_%ls%ls_1180x720.png", loc == i18n::Locale::TrTR ? L"tr" : L"en", c.name,
                                           pal ? L"_hc" : L"");
                                if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                            }
                        }
            }
            screen.set_palette(nullptr);
        }
        // Spec 2026-11-12 E: uygulama ici kip penceresi durumlari.
        {
            ModalAudit ma;
            if (!modal_snapshot_cases(wic.Get(), *g, screen, theme, cat.value(), out, ma)) return 2;
            renders += ma.renders;
            overflow_total += ma.overflow;
            big_buttons += ma.big;
            missing_controls += ma.missing;
            a11y_total += ma.a11y;
        }
        // ADR-033 / spec 3 ve 8: kurulum penceresi evreleri ve Ui "Kurulum gerekli" seridi.
        {
            ModalAudit sa;
            // Kurulum penceresi uretimde varsayilan temayla cizilir (setup_app: default_theme; metin boyutu uygulanmaz).
            // Ana pencere seridi ana temayla (olcekli kosuda olcekli).
            if (theme.button_h == default_theme().button_h) {
                if (!setup_snapshot_cases(wic.Get(), *g, screen, theme, cat.value(), out, sa)) return 2;
            } else {
                auto sg = Graphics::create(default_theme(), loc == i18n::Locale::TrTR ? L"tr-TR" : L"en-US");
                Screen sscreen(default_theme(), cat.value());
                if (!sg || !setup_snapshot_cases(wic.Get(), *sg, sscreen, default_theme(), cat.value(), out, sa)) return 2;
            }
            if (!install_offer_snapshot_cases(wic.Get(), *g, screen, theme, cat.value(), out, sa)) return 2;
            renders += sa.renders;
            overflow_total += sa.overflow;
            big_buttons += sa.big;
            missing_controls += sa.missing;
            a11y_total += sa.a11y;
        }
        for (float sc : scales)
            for (auto [w, h] : sizes)
                for (std::size_t pg = 0; pg < kPageCount; ++pg)
                    for (bool loading : {false, true})
                        for (std::size_t vi = 0; vi < variants.size(); ++vi) {
                            if (loading && pg != 0) continue;
                            // Anahtar asamalari Panel'de; tasinan kartlarin durumlari kendi sayfalarinda da cizilir (spec
                            // 2026-11-12 B.4): tek tik kontrol -> Oyun hazirligi, Once/Sonra -> Olcumler.
                            const bool check_variant = variants[vi].check.phase != core::perf::CheckPhase::Idle;
                            const bool ba_variant = variants[vi].ba.stage != core::perf::ReportCollector::Stage::Idle;
                            const bool card_page = (check_variant && pg == static_cast<std::size_t>(Page::Readiness)) ||
                                                   (ba_variant && pg == static_cast<std::size_t>(Page::Measurements));
                            if (vi > 0 && ((pg != 0 && !card_page) || loading)) continue;
                            const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                            ComPtr<IWICBitmap> bmp;
                            if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                                return 2;
                            if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                            UiState st;
                            st.reduced_motion = true;
                            st.page = static_cast<Page>(pg);
                            auto vm = fixture(loading);
                            vm.perf = variants[vi].view;
                            vm.before_after = variants[vi].ba;
                            vm.check = variants[vi].check;
                            vm.admin_without_uac = variants[vi].admin_without_uac;
                            vm.show_welcome = vi == 1;  // UI-08: ilk acilis karsilamasi (sifir is varyantiyla birlikte)
                            sync_perf_animations(st, PerfView{}, vm.perf, theme, 0);
                            st.details_open = std::wstring(variants[vi].name) == L"off_details" || std::wstring(variants[vi].name) == L"on_tips_open";
                            st.perf_tips_open = std::wstring(variants[vi].name) == L"on_tips_open";
                            st.advanced_open = std::wstring(variants[vi].name) == L"confirm_advanced_open" || std::wstring(variants[vi].name) == L"confirm_no_effect_open";
                            // Grup anahtarlari odak sirasina girsin diye bir kez cizilip etkin gruplar ogrenilir.
                            if ((pg == 0 || card_page) && !loading) {
                                paint_audited(screen, *g, w, h, st, vm, 0);
                                g->end();
                                g->begin();
                                g->clear_overflows();
                            }
                            const auto order = focus_order(st);
                            st.focus = vi > 0 && !order.empty() ? order.back() : nav_hit(pg);
                            st.focus_visible = true;
                            g->clear_overflows();
                            const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                            if (!g->end()) return 2;
                            ++renders;
                            for (const auto& o : g->overflows()) {
                                ++overflow_total;
                                std::wprintf(L"OVERFLOW loc=%d scale=%.2f size=%.0fx%.0f page=%zu variant=%ls box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n",
                                             static_cast<int>(loc), sc, w, h, pg, variants[vi].name, o.box.w, o.box.h, o.need_w,
                                             o.need_h, o.text.c_str());
                            }
                            for (const auto& [id, r] : hits.items)
                                if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                    ++big_buttons;
                                    std::wprintf(L"BIG_CONTROL id=%d %.1fx%.1f\n", static_cast<int>(id), r.w, r.h);
                                }
                            a11y_total += a11y_issues(st, hits, variants[vi].name);
                            // Spec 2026-11-12 B.4: tek tik kontrol Panel'de degil, Oyun hazirligi sayfasinda — ayni kimliklerle
                            // (Baslat / Evet / Hayir asamaya gore).
                            if (pg == 0 && !loading && (hits.meta.count(Hit::CheckStart) || hits.meta.count(Hit::CheckYes) || hits.meta.count(Hit::CheckNo))) {
                                ++missing_controls;
                                std::wprintf(L"PANEL still shows the one-click check variant=%ls\n", variants[vi].name);
                            }
                            if (pg == static_cast<std::size_t>(Page::Readiness)) {
                                using core::perf::CheckPhase;
                                const auto cph = vm.check.phase;
                                const bool want_start = cph == CheckPhase::Idle || cph == CheckPhase::Report || cph == CheckPhase::Failed;
                                const bool want_answer = cph == CheckPhase::Review;
                                if ((hits.meta.count(Hit::CheckStart) > 0) != want_start || (hits.meta.count(Hit::CheckYes) > 0) != want_answer ||
                                    (hits.meta.count(Hit::CheckNo) > 0) != want_answer) {
                                    ++missing_controls;
                                    std::wprintf(L"READINESS one-click check controls mismatch variant=%ls\n", variants[vi].name);
                                }
                            }
                            // Onay/kurtarma asamasinda Evet/Hayir veya Tekrar dene erisilebilir olmali; anahtar konumu dogru.
                            if (pg == 0 && !loading) {
                                const auto ph2 = vm.perf.phase;
                                const bool need_yes = ph2 == core::perf::Phase::Confirming || ph2 == core::perf::Phase::ConfirmRestore;
                                bool yes = false, no = false, retry = false;
                                for (const auto& [id, r] : hits.items) {
                                    yes |= id == Hit::Yes;
                                    no |= id == Hit::No;
                                    retry |= id == Hit::Retry;
                                }
                                if (need_yes && !(yes && no)) {
                                    ++missing_controls;
                                    std::wprintf(L"MISSING yes/no variant=%ls\n", variants[vi].name);
                                }
                                if (ph2 == core::perf::Phase::RecoveryNeeded && !retry) {
                                    ++missing_controls;
                                    std::wprintf(L"MISSING retry\n");
                                }
                                if (st.toggle_knob.target() != (vm.perf.switch_on ? 1.f : 0.f)) {
                                    ++missing_controls;
                                    std::wprintf(L"SWITCH mismatch variant=%ls\n", variants[vi].name);
                                }
                            }
                            // Kanit seti (tr-TR): UI-01 genel bakis 4 olcek + yukleniyor/oyunlar/en kucuk; UI-02 asamalari %100.
                            bool want_png = false;
                            wchar_t name[200] = {};
                            if (!out.empty() && loc == i18n::Locale::TrTR) {
                                if (vi == 0 && ((w == 1180.f && pg == 0 && !loading) ||
                                                (sc == 1.f && pg <= 2 && (w == 1180.f || !loading)))) {
                                    want_png = true;
                                    swprintf_s(name, L"\\ui01_tr_%ls_%.0fx%.0f_%03d%ls.png", pg == 0 ? L"overview" : pg == 1 ? L"readiness" : L"games", w, h,
                                               static_cast<int>(sc * 100 + 0.5f), loading ? L"_loading" : L"");
                                } else if (vi == 1 && pg == 0 && sc == 1.f && w == 1180.f) {
                                    want_png = true;
                                    swprintf_s(name, L"\\ui08_tr_welcome_%.0fx%.0f.png", w, h);
                                } else if (vi > 0 && pg != 0) {  // tasinan kartin durumu kendi sayfasinda
                                    want_png = sc == 1.f && w == 1180.f;
                                    swprintf_s(name, L"\\ui02_tr_%ls_%ls_%.0fx%.0f.png", variants[vi].name, page_slug(static_cast<Page>(pg)), w, h);
                                } else if (vi > 0 && sc == 1.f &&
                                           (w == 1180.f || variants[vi].view.phase == core::perf::Phase::Confirming)) {
                                    want_png = true;
                                    swprintf_s(name, L"\\ui02_tr_%ls_%.0fx%.0f.png", variants[vi].name, w, h);
                                } else if (vi == 0 && sc == 1.f && w == 1180.f && !loading && pg == static_cast<std::size_t>(Page::History)) {
                                    want_png = true;  // REC-01/02 kanit
                                    swprintf_s(name, L"\\rec_tr_history_%.0fx%.0f.png", w, h);
                                }
                            }
                            if (want_png && !save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
        // Spec 2026-11-12 §5 simetri kaniti + spec 2026-11-12 B.4: Panel'den tasinan kartlar kendi sayfalarinda (Once/sonra
        // -> Olcumler, bulgu listesi + tek tik kontrol -> Oyun hazirligi) ekranin altinda kalir; uzun pencerede
        // (kaydirmasiz) tek goruntude cizilir. Koyu tema, %100.
        if (!out.empty() && loc == i18n::Locale::TrTR)
            for (const auto& v : variants) {
                const std::wstring vn = v.name;
                if (vn != L"on_ba_report" && vn != L"on_ba_collecting" && vn != L"check_report" && vn != L"on") continue;
                for (float w : {1180.f, 960.f}) {
                    UiState st;
                    st.reduced_motion = true;
                    st.page = v.ba.stage != core::perf::ReportCollector::Stage::Idle ? Page::Measurements
                              : v.check.phase != core::perf::CheckPhase::Idle        ? Page::Readiness
                                                                                     : Page::Overview;
                    auto vm = fixture(false);
                    vm.perf = v.view;
                    vm.before_after = v.ba;
                    vm.check = v.check;
                    sync_perf_animations(st, PerfView{}, vm.perf, theme, 0);
                    float h = 720.f;
                    ComPtr<IWICBitmap> bmp;
                    for (int pass = 0; pass < 2; ++pass) {  // 1: icerik yuksekligi olculur; 2: o yukseklikte cizilir
                        if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                            return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
                        g->clear_overflows();
                        (void)paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        overflow_total += static_cast<int>(g->overflows().size());
                        h = std::clamp(st.content_height + 150.f, 720.f, 4000.f);
                    }
                    wchar_t name[200];
                    swprintf_s(name, L"\\sym_tr_%ls_%ls_%.0f.png", page_slug(st.page), v.name, w);
                    if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                }
            }
        // R15/R16: Islem Gecmisi test karsilastirma + disa aktarma durumlari — 4 olcek x 2 boyut
        {
            namespace ci = ef::core::insights;
            const auto base = fixture(false).history;
            ci::PeerQuery pq;
            pq.source = "in_game";
            pq.fingerprint = "fp-cs2-a";
            pq.metric = "avg_fps";
            pq.value = 241.6;
            std::vector<ci::PeerSample> pool20;
            for (int i = 0; i < 20; ++i) {
                ci::PeerSample s;
                s.source = pq.source;
                s.fingerprint = pq.fingerprint;
                s.conditions = pq.conditions;
                s.metrics["avg_fps"] = 190.0 + i * 5.0;  // gercek ornekler: yuzdelik 55.0
                pool20.push_back(s);
            }
            const std::vector<ci::PeerSample> pool7(pool20.begin(), pool20.begin() + 7);
            std::vector<std::pair<const wchar_t*, ViewModel::HistoryPageView>> hstates;
            hstates.push_back({L"hist_peer_no_pool", base});  // varsayilan: yerel havuz yok, N=0
            {
                auto hv = base;
                hv.peer_query = pq;
                hv.peer = ci::compare_peers(pq, pool20);
                hstates.push_back({L"hist_peer_comparable", hv});
            }
            {
                auto hv = base;
                hv.peer_query = pq;
                hv.peer = ci::compare_peers(pq, pool7);
                hstates.push_back({L"hist_peer_below_min", hv});
            }
            {
                auto hv = base;  // ayni kaynak + iz: uyumlu; eksik metrik + 0 taban gorunur
                hv.selected = {0, 1};
                hv.comparison = ci::compare_history(base.tests[1], base.tests[0]);
                hstates.push_back({L"hist_compare_compatible", hv});
            }
            {
                auto hv = base;  // farkli kaynak + iz: "kosullar uyusmuyor"
                hv.selected = {0, 2};
                hv.comparison = ci::compare_history(base.tests[2], base.tests[0]);
                hstates.push_back({L"hist_compare_incompatible", hv});
            }
            for (const bool up : {true, false}) {
                // Spec 2026-11-12 B6: karsilastirilabilir cift (ayni kaynak + kosul izi) -> karsilastirma seridi: isaretli FPS
                // farki (iyilesme yesil / dusus kirmizi) ve iki cubuk.
                auto hv = base;
                hv.tests = {base.tests[0], base.tests[2], base.tests[3]};
                hv.tests[2].metrics["scene_avg_fps"] = up ? 131.5 : 158.25;
                hv.selected = {1, 2};
                hv.comparison = ci::compare_history(hv.tests[2], hv.tests[1]);  // eski (t4) -> yeni (t3)
                hstates.push_back({up ? L"hist_compare_strip_up" : L"hist_compare_strip_down", hv});
            }
            {
                auto hv = base;
                hv.selected.clear();
                hv.comparison.reset();
                hstates.push_back({L"hist_compare_none", hv});
            }
            {
                auto hv = base;
                hv.export_done = true;
                hv.export_path = L"C:\\Users\\kullanici\\Belgeler\\EcuFps-history.json";
                hv.export_bytes = 18422;
                hstates.push_back({L"hist_export_done", hv});
            }
            {
                auto hv = base;
                hv.export_failed = true;
                hv.export_error_key = "history.export.failed_write";
                hstates.push_back({L"hist_export_failed", hv});
            }
            {
                auto hv = base;
                hv.export_enabled = false;  // ornek: kaldirma suruyor
                hstates.push_back({L"hist_export_disabled", hv});
            }
            for (float sc : scales)
                for (auto [w, h] : sizes)
                    for (const auto& [hname, hv] : hstates) {
                        const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                        ComPtr<IWICBitmap> bmp;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::History;
                        auto vm = fixture(false);
                        vm.history = hv;
                        paint_audited(screen, *g, w, h, st, vm, 0);  // odak sirasi icin satir/dugme sayisi ogrenilir
                        g->end();
                        g->begin();
                        g->clear_overflows();
                        if (std::wstring(hname).starts_with(L"hist_export")) st.scroll = 1e6f;  // alt kart gorunur (kaydirma sinirlanir)
                        st.focus = focus_order(st).back();
                        st.focus_visible = true;
                        const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        for (const auto& o : g->overflows()) {
                            ++overflow_total;
                            std::wprintf(L"OVERFLOW history=%ls scale=%.2f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", hname, sc, o.box.w,
                                         o.box.h, o.need_w, o.need_h, o.text.c_str());
                        }
                        for (const auto& [id, r] : hits.items)
                            if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                ++big_buttons;
                                std::wprintf(L"BIG_CONTROL history=%ls id=%d %.1fx%.1f\n", hname, static_cast<int>(id), r.w, r.h);
                            }
                        a11y_total += a11y_issues(st, hits, hname);
                        // EF-A11Y: karsilastirma satirlari (kosul uyusmazligi, metrik once/sonra) UIA'da okunur metin.
                        if (hv.selected.size() == 2 && hv.comparison) {
                            const auto& cmp = *hv.comparison;
                            // Spec 2026-11-12 B6: uymayan cift tek satir "Farkli test turu · karsilastirilamaz" (sayi yok); kosul
                            // listesi ve metrik once/sonra satirlari seridin HelpText'inde okunur.
                            if (!cmp.compatibility.compatible) {
                                const auto line = wide(cat.value().get(i18n::StringId::HistoryCompareIncomparable));
                                bool help_has_fields = false;
                                auto pre = wide(cat.value().get(i18n::StringId::HistoryCompareIncompatible));
                                pre = pre.substr(0, pre.find(L'{'));
                                for (const auto& [mid, mm] : hits.meta) help_has_fields = help_has_fields || mm.help.find(pre) != std::wstring::npos;
                                // Kosullar uyusuyor, yalniz kosu gecerliligi eksik: sayilar + uyumsuzluk cumlesi uyari satirinda.
                                const bool ok_text = cmp.compatibility.conditions_match ? has_text_item(hits, pre, 1) && !has_text_item(hits, line, 0)
                                                                                         : has_text_item(hits, line, 0) && help_has_fields;
                                if (!ok_text) {
                                    ++a11y_total;
                                    std::wprintf(L"A11Y_TEXT missing history=%ls incompatible\n", hname);
                                }
                            }
                            bool help_arrow = false;
                            for (const auto& [mid, mm] : hits.meta) help_arrow = help_arrow || mm.help.find(L" → ") != std::wstring::npos;
                            if ((cmp.compatibility.compatible || cmp.compatibility.conditions_match) && !cmp.metrics.empty() && !help_arrow) {
                                ++a11y_total;
                                std::wprintf(L"A11Y_TEXT missing history=%ls metric before/after\n", hname);
                            }
                        }
                        bool export_hit = false;
                        std::size_t select_hits = 0;
                        for (const auto& [id, r] : hits.items) {
                            if (id == Hit::HistoryExport) {
                                export_hit = true;
                                const auto it = hits.meta.find(id);
                                const bool want = hv.export_enabled && !hv.tests.empty();
                                if (it == hits.meta.end() || it->second.enabled != want) ++missing_controls;
                            } else if (classify_hit(id).kind == HitKind::HistorySelect) {
                                ++select_hits;
                            }
                        }
                        if (!export_hit || select_hits != std::min<std::size_t>(hv.tests.size(), static_cast<std::size_t>(kMaxMeasureRecent))) {
                            ++missing_controls;
                            std::wprintf(L"MISSING history controls state=%ls export=%d rows=%zu\n", hname, export_hit ? 1 : 0,
                                         select_hits);
                        }
                        if (!out.empty() && loc == i18n::Locale::TrTR && sc == 1.f && w == 1180.f) {
                            wchar_t name[160];
                            swprintf_s(name, L"\\r16_tr_%ls_1180x720.png", hname);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
        }
        // WIN-08: Temizlik sayfasi durumlari — 4 olcek x 2 boyut
        {
            namespace cc = ef::core::cleanup;
            using CV = ViewModel::CleanupView;
            using CR = ViewModel::CleanupRow;
            CV ready;
            ready.phase = cc::SessionPhase::Ready;
            ready.rows = {CR{cc::CleanupCategory::UserTemp, cc::PreviewStatus::Ready, 1347, 0, 984844284ULL, false, true, true},
                          CR{cc::CleanupCategory::WindowsTemp, cc::PreviewStatus::NeedsAdmin, 0, 0, 0, true, false, false},
                          CR{cc::CleanupCategory::ThumbnailCache, cc::PreviewStatus::Ready, 30, 0, 176276384ULL, false, true, true},
                          CR{cc::CleanupCategory::WerReportsUser, cc::PreviewStatus::NotPresent, 0, 0, 0, false, false, false},
                          CR{cc::CleanupCategory::WerReportsSystem, cc::PreviewStatus::NeedsAdmin, 0, 0, 0, true, false, false},
                          CR{cc::CleanupCategory::DeliveryOptimization, cc::PreviewStatus::NeedsAdmin, 0, 0, 0, true, false, false},
                          CR{cc::CleanupCategory::RecycleBin, cc::PreviewStatus::Ready, 1, 0, 7483040ULL, false, true, false}};
            // EXT-CLN: uygulamanin kendi test dosyalari kategorisi de urun listesindedir.
            ready.rows.push_back(CR{cc::CleanupCategory::AppTestFiles, cc::PreviewStatus::Ready, 3, 0, 5120000ULL, false, true, false});
            // T15: bekleme bellegi satiri (olculen boyut; varsayilan secili degil).
            ready.rows.push_back(CR{cc::CleanupCategory::StandbyMemory, cc::PreviewStatus::Ready, 0, 0, 3328000000ULL, false, true, false});
            ready.any_selected = true;
            ready.selected_bytes = 984844284ULL + 78917632ULL + 176276384ULL;
            ready.selected_allocated = 1012000000ULL + 176500000ULL;
            // EXT-CLN-01: 4 birim (uzun etiketli), secilen C:\ kimligi
            ready.volumes = {L"C:\\", L"D:\\", L"E:\\", L"F:\\"};
            ready.volume_free = {689000000000ULL, 1200000000000ULL, std::nullopt, 52000000000ULL};
            ready.volume = L"C:\\";
            ready.volume_id.root = L"C:\\", ready.volume_id.serial = 0x1A2B3C4Du, ready.volume_id.cluster = 4096;
            ready.volume_id.fs = L"NTFS", ready.volume_id.label = L"Windows", ready.volume_id.known = true;
            // EXT-CLN-03: dosya listesi acik (uzun goreli yol + "daha fazla" satiri)
            CV files = ready;
            files.files_open = cc::CleanupCategory::UserTemp;
            for (int k = 0; k < 30; ++k)
                files.files.push_back({L"NVIDIA Corporation\\NV_Cache\\very_long_folder_name_without_spaces_for_wrap_test_" + std::to_wstring(k) +
                                           L"\\ShaderCacheEntry_0123456789abcdef0123456789abcdef.bin",
                                       static_cast<std::uint64_t>(90000000 - k * 1000000)});
            files.files_total = 1347;
            CV failed_vol = ready;
            failed_vol.phase = cc::SessionPhase::Failed;
            failed_vol.volume_changed = true;
            CV other_vol = ready;
            other_vol.volume = L"D:\\";
            other_vol.volume_id.root = L"D:\\", other_vol.volume_id.serial = 0x55AA0011u, other_vol.volume_id.label.clear();
            for (auto& r : other_vol.rows)
                if (r.category != cc::CleanupCategory::RecycleBin) r.status = cc::PreviewStatus::OtherVolume, r.selectable = r.selected = false;
            CV confirm = ready;
            confirm.phase = cc::SessionPhase::Confirming;
            CV running = ready;
            running.phase = cc::SessionPhase::Running;
            running.done = 620;
            running.total = 1423;
            CV done = ready;
            done.phase = cc::SessionPhase::Done;
            cc::CleanupReport rep;
            cc::CategoryResult cr;
            cr.category = cc::CleanupCategory::UserTemp;
            cr.deleted.files = 1300, cr.deleted.bytes = 951000000ULL;
            cr.locked.files = 47, cr.locked.bytes = 33844284ULL;
            cr.reparse.files = 2;
            rep.categories = {cr};
            rep.volumes = {cc::VolumeSpace{L"C:\\", 689000000000ULL, 690100000000ULL}};
            rep.standby.ran = true, rep.standby.ok = true;  // T15: iki olcum de var -> once/sonra yazilir
            rep.standby.before_bytes = 3328000000ULL, rep.standby.after_bytes = 412000000ULL;
            done.report = rep;
            CV idle;
            // EXT-CLN: tarama surerken Iptal + depolama ayarlari gorunur (uzun tarama UI'yi kilitlemez).
            CV scanning = ready;
            scanning.phase = cc::SessionPhase::Scanning;
            // EXT-CLN: butce nedeniyle kismi tarama: bir kategori yarim, digeri hic taranmadi.
            CV partial_scan = ready;
            partial_scan.scan_partial = true;
            partial_scan.rows[0].partial = true;
            partial_scan.rows[1].status = cc::PreviewStatus::NotScanned;
            partial_scan.rows[1].selectable = partial_scan.rows[1].selected = false;
            CV cancelled_scan = idle;
            cancelled_scan.scan_cancelled = true;
            const std::pair<const wchar_t*, CV> cvars[] = {{L"cleanup_idle", idle}, {L"cleanup_ready", ready}, {L"cleanup_confirm", confirm},
                                                           {L"cleanup_running", running}, {L"cleanup_done", done},
                                                           {L"cleanup_files", files},     {L"cleanup_failed_volume", failed_vol},
                                                           {L"cleanup_other_volume", other_vol},
                                                           {L"cleanup_scanning", scanning}, {L"cleanup_partial", partial_scan},
                                                           {L"cleanup_cancelled", cancelled_scan}};
            for (float sc : scales)
                for (auto [w, h] : sizes)
                    for (const auto& [cname, cview] : cvars) {
                        const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                        ComPtr<IWICBitmap> bmp;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::Cleanup;
                        if (cview.files_open) st.scroll = 420.f;  // EXT-CLN-03: dosya listesi gorunur bolgede denetlensin
                        auto vm = fixture(false);
                        vm.cleanup = cview;
                        paint_audited(screen, *g, w, h, st, vm, 0);  // odak sirasi icin etkin anahtarlar
                        g->end();
                        g->begin();
                        g->clear_overflows();
                        st.focus = focus_order(st).back();
                        st.focus_visible = true;
                        const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        for (const auto& o : g->overflows()) {
                            ++overflow_total;
                            std::wprintf(L"OVERFLOW cleanup=%ls scale=%.2f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", cname, sc, o.box.w, o.box.h,
                                         o.need_w, o.need_h, o.text.c_str());
                        }
                        for (const auto& [id, r] : hits.items)
                            if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                ++big_buttons;
                                std::wprintf(L"BIG_CONTROL cleanup=%ls id=%d %.1fx%.1f\n", cname, static_cast<int>(id), r.w, r.h);
                            }
                        a11y_total += a11y_issues(st, hits, cname);
                        bool yes = false, no = false;
                        for (const auto& [id, r] : hits.items) yes |= id == Hit::CleanYes, no |= id == Hit::CleanNo;
                        if (cview.phase == cc::SessionPhase::Confirming && !(yes && no)) {
                            ++missing_controls;
                            std::wprintf(L"MISSING cleanup yes/no %ls\n", cname);
                        }
                        if (cview.phase == cc::SessionPhase::Confirming) {
                            // DS-04: geri donusuz silme onayi duz "Evet" etiketiyle cizilmez (ayri metin + danger stili).
                            const auto it = hits.meta.find(Hit::CleanYes);
                            const auto want = wide(cat.value().get(i18n::StringId::CleanupConfirmDelete));
                            if (it == hits.meta.end() || it->second.name != want) {
                                ++missing_controls;
                                std::wprintf(L"MISSING cleanup danger label %ls\n", cname);
                            }
                        }
                        if (cview.phase == cc::SessionPhase::Confirming) {
                            // EXT-AC-02: oyun calisirken Evet gosterilmez; Hayir ve neden karti kalir.
                            auto vg = vm;
                            vg.game_running = true;
                            vg.game_gate_blocked = true;
                            vg.game_block_reason_key = "gate.game_running";
                            UiState sg = st;
                            sg.focus_visible = false;
                            ComPtr<IWICBitmap> gb;
                            if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &gb))) return 2;
                            if (!g->bind_wic(gb.Get(), 96.f * sc) || !g->begin()) return 2;
                            g->clear_overflows();
                            const auto gh = paint_audited(screen, *g, w, h, sg, vg, 0);
                            if (!g->end()) return 2;
                            ++renders;
                            for (const auto& o : g->overflows()) {
                                ++overflow_total;
                                std::wprintf(L"OVERFLOW gate cleanup scale=%.2f \"%.60ls\"\n", sc, o.text.c_str());
                            }
                            a11y_total += a11y_issues(sg, gh, L"gate");
                            bool gy = false, gn = false;
                            for (const auto& [id, r] : gh.items) gy |= id == Hit::CleanYes, gn |= id == Hit::CleanNo;
                            if (gy || !gn) {
                                ++missing_controls;
                                std::wprintf(L"GATE cleanup yes shown while game running\n");
                            }
                            if (!out.empty() && loc == i18n::Locale::TrTR && sc == 1.f && w == 1180.f &&
                                !save_png(wic.Get(), gb.Get(), out + L"\\gate_tr_cleanup_confirm_1180x720.png"))
                                return 2;
                        }
                        if (!out.empty() && loc == i18n::Locale::TrTR && sc == 1.f && w == 1180.f) {
                            wchar_t name[120];
                            swprintf_s(name, L"\\win08_tr_%ls_1180x720.png", cname);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
        }
        // WIN-09: Baslangic sayfasi durumlari — 4 olcek x 2 boyut
        {
            namespace sa = ef::core::adapters::startup;
            using SV = ViewModel::StartupView;
            using SR = ViewModel::StartupRowView;
            using SP = ef::core::startup::StartupPhase;
            SV ready;
            ready.phase = SP::Ready;
            ready.rows = {SR{"Adobe Acrobat Synchronizer Update Helper Long Name", "startup.advice.updater", sa::Advice::RecommendDisable, true, false, true, false, false},
                          SR{"Discord", "startup.advice.optional_app", sa::Advice::RecommendDisable, false, false, true, false, true},
                          SR{"Steam", "startup.advice.launcher", sa::Advice::NoAdvice, true, true, true, false, false},
                          SR{"OneDrive", "startup.advice.cloud_sync", sa::Advice::NoAdvice, true, true, true, false, false},
                          SR{"SecurityHealth", "startup.advice.security", sa::Advice::Keep, true, true, false, true, false},
                          SR{"Riot Vanguard", "startup.advice.anticheat", sa::Advice::Keep, false, false, false, true, false},
                          SR{"RtkAudUService", "startup.advice.hardware", sa::Advice::Keep, true, true, false, true, false}};
            ready.services = {{"Spooler", "Yazdırma Biriktiricisi", "background.breaks.printing", true, true, true, true},
                              {"Fax", "Faks", "background.breaks.fax", false, false, false, false},
                              {"MapsBroker", "İndirilen Haritalar Yöneticisi", "background.breaks.offline_maps", true, false, true, false},
                              {"stisvc", "Windows Görüntü Alma (WIA)", "background.breaks.scanner", true, true, false, true}};
            ready.pending = 1;
            ready.last = ef::core::startup::ChangeResult{2, 1};
            SV confirm = ready;
            confirm.phase = SP::Confirming;
            SV applying = ready;
            applying.phase = SP::Applying;
            SV scanning;
            scanning.phase = SP::Scanning;
            SV empty;
            empty.phase = SP::Ready;
            const std::pair<const wchar_t*, SV> svars[] = {{L"startup_scanning", scanning}, {L"startup_ready", ready}, {L"startup_confirm", confirm},
                                                           {L"startup_applying", applying}, {L"startup_empty", empty}};
            for (float sc : scales)
                for (auto [w, h] : sizes)
                    for (const auto& [sname, sview] : svars) {
                        const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                        ComPtr<IWICBitmap> bmp;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::Startup;
                        auto vm = fixture(false);
                        vm.startup = sview;
                        paint_audited(screen, *g, w, h, st, vm, 0);
                        g->end();
                        g->begin();
                        g->clear_overflows();
                        st.focus = focus_order(st).back();
                        st.focus_visible = true;
                        const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        for (const auto& o : g->overflows()) {
                            ++overflow_total;
                            std::wprintf(L"OVERFLOW startup=%ls scale=%.2f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", sname, sc, o.box.w, o.box.h,
                                         o.need_w, o.need_h, o.text.c_str());
                        }
                        for (const auto& [id, r] : hits.items)
                            if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                ++big_buttons;
                                std::wprintf(L"BIG_CONTROL startup=%ls id=%d\n", sname, static_cast<int>(id));
                            }
                        a11y_total += a11y_issues(st, hits, sname);
                        bool yes = false, no = false;
                        for (const auto& [id, r] : hits.items) yes |= id == Hit::StartupYes, no |= id == Hit::StartupNo;
                        if (sview.phase == SP::Confirming && !(yes && no)) {
                            ++missing_controls;
                            std::wprintf(L"MISSING startup yes/no %ls\n", sname);
                        }
                        if (!out.empty() && loc == i18n::Locale::TrTR && sc == 1.f && w == 1180.f) {
                            wchar_t name[120];
                            swprintf_s(name, L"\\win09_tr_%ls_1180x720.png", sname);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
        }
        // UI-06: Ayarlar (bos, uzun adli oyunlar, 300 kayit sanal liste) — 4 olcek x 2 boyut
        {
            std::vector<std::pair<const wchar_t*, SettingsView>> sv;
            SettingsView empty;
            sv.push_back({L"settings_empty", empty});
            SettingsView some;
            some.autostart = true;
            some.games = {{"a", "Counter-Strike 2", true},
                          {"b", "Microsoft Flight Simulator 2024 — Deluxe Premium Edition (Steam)", false},
                          {"c", "Ölümsüz Şövalyeler: Çağın Öfkesi", true}};
            sv.push_back({L"settings_games", some});
            SettingsView many = some;
            many.games.clear();
            for (int i = 0; i < 300; ++i) many.games.push_back({"g" + std::to_string(i), "Oyun " + std::to_string(i), i % 2 == 0});
            sv.push_back({L"settings_many", many});
            SettingsView unknown = some;  // TH-01: baslangic kaydi okunamadi
            unknown.autostart_known = false;
            sv.push_back({L"settings_autostart_unknown", unknown});
            // R21: paket hazirlaniyor / kaydedildi (uzun yol) / basarisiz — durum satiri ve devre disi anahtar.
            SettingsView bundle_busy = some;
            bundle_busy.bundle_busy = true;
            bundle_busy.bundle_include_dumps = true;
            sv.push_back({L"settings_support_busy", bundle_busy});
            SettingsView bundle_saved = some;
            bundle_saved.bundle_done = true;
            bundle_saved.bundle_include_dumps = true;
            bundle_saved.bundle_path = L"C:\\Users\\OrnekKullanici\\Belgeler\\EcuFps\\destek\\EcuFps-support-2026-11-08.zip";
            sv.push_back({L"settings_support_saved", bundle_saved});
            SettingsView bundle_failed = some;
            bundle_failed.bundle_failed = true;
            sv.push_back({L"settings_support_failed", bundle_failed});
            SettingsView unverified = some;  // Uyumluluk B8 (bosluk 12): engellemeyen "bu cihazda test edilmedi" notu
            unverified.device_unverified = unverified.device_gpu_unverified = true;
            sv.push_back({L"settings_device_unverified", unverified});
            SettingsView unverified_both = bundle_failed;
            unverified_both.device_unverified = unverified_both.device_os_unverified = unverified_both.device_gpu_unverified = true;
            sv.push_back({L"settings_device_unverified_both", unverified_both});
            // R20: suruyor (kilitli ayarlar), hata sonucu (veri korundu + notlar). Onay artik kip penceresinde (spec B7 / E;
            // kip penceresi durumlari "uninstall" / "support").
            SettingsView uninstall_running = some;
            uninstall_running.uninstall_phase = SettingsView::UninstallPhase::Running;
            sv.push_back({L"settings_uninstall_running", uninstall_running});
            // ADR-030 / spec G: yazma basarisiz (anahtar kapali + neden satiri), ilk acilis bildirimi seridi.
            SettingsView auto_failed = some;
            auto_failed.autostart = false;
            auto_failed.autostart_failure = core::settings::AutostartFailure::WriteFailed;
            sv.push_back({L"settings_autostart_failed", auto_failed});
            SettingsView notice = some;
            notice.autostart_notice = true;
            sv.push_back({L"settings_autostart_notice", notice});
            // Spec B7: kapali "Oyun onaylari" / "Lisanslar" (pencere boyle baslar). P12-B3: "Gelismis" de kapali;
            // settings_advanced_only: Gelismis acik, Oyun onaylari ve Lisanslar kapali.
            sv.push_back({L"settings_collapsed", some});
            sv.push_back({L"settings_advanced_only", some});
            for (float sc : scales)
                for (auto [w, h] : sizes)
                    for (const auto& [sname, sview] : sv) {
                        const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                        ComPtr<IWICBitmap> bmp;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::Settings;
                        const bool collapsed = std::wstring(sname) == L"settings_collapsed";
                        const bool adv_only = std::wstring(sname) == L"settings_advanced_only";
                        st.settings_games_open = st.settings_licenses_open = !(collapsed || adv_only);
                        st.settings_advanced_open = !collapsed;
                        st.consent_rows = collapsed || adv_only ? 0 : sview.games.size();
                        auto vm = fixture(false);
                        vm.settings = sview;
                        st.focus = collapsed ? Hit::SettingsAdvanced : sview.games.empty() || adv_only ? Hit::Autostart : consent_reset_hit(0);
                        st.focus_visible = true;
                        g->clear_overflows();
                        const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        for (const auto& o : g->overflows()) {
                            ++overflow_total;
                            std::wprintf(L"OVERFLOW settings=%ls scale=%.2f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", sname, sc,
                                         o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                        }
                        for (const auto& [id, r] : hits.items) {
                            // Durum metni kontrol degildir: uzun kayit yolu metin kutusunu genisletir (UIA Text rolu).
                            const auto mi = hits.meta.find(id);
                            if (mi != hits.meta.end() && mi->second.role == AccRole::Text) continue;
                            if (r.h > theme.control_max_h + 0.5f || r.w > theme.sidebar_max * (theme.button_h / default_theme().button_h) + 0.5f) {
                                ++big_buttons;
                                std::wprintf(L"BIG_CONTROL settings=%ls id=%d %.1fx%.1f\n", sname, static_cast<int>(id), r.w, r.h);
                            }
                        }
                        a11y_total += a11y_issues(st, hits, sname);
                        if (!sview.autostart_known) {
                            // TH-01/DS-05: anahtar UIA'da kalir (devre disi, durum belirsiz), Tab sirasinda yok.
                            const auto it = hits.meta.find(Hit::Autostart);
                            const auto ord = focus_order(st, hits);
                            if (it == hits.meta.end() || it->second.enabled || it->second.toggle != -1 ||
                                std::find(ord.begin(), ord.end(), Hit::Autostart) != ord.end()) {
                                ++a11y_total;
                                std::wprintf(L"A11Y autostart unknown not exposed as disabled (%ls)\n", sname);
                            }
                        }
                        if (sview.bundle_busy) {
                            // R21: paket hazirlanirken anahtar + dugme UIA'da kalir ama devre disi; Tab almaz.
                            const auto ord = focus_order(st, hits);
                            // Spec B7: cokme kaydi secenegi sayfada yok (Olustur'un kip penceresinde).
                            if (hits.meta.count(Hit::SupportDumps)) {
                                ++a11y_total;
                                std::wprintf(L"A11Y support dumps toggle still on the page (%ls)\n", sname);
                            }
                            for (const Hit id : {Hit::SupportCreate}) {
                                const auto it = hits.meta.find(id);
                                if (it == hits.meta.end() || it->second.enabled ||
                                    std::find(ord.begin(), ord.end(), id) != ord.end()) {
                                    ++a11y_total;
                                    std::wprintf(L"A11Y support busy control not disabled (%ls id=%d)\n", sname, static_cast<int>(id));
                                }
                            }
                        }
                        if (sview.uninstall_phase == SettingsView::UninstallPhase::Running) {
                            // R20: silme surerken yazan ayarlar kilitli (UIA devre disi, Tab disi) ve eylem dugmesi yok.
                            const auto ord = focus_order(st, hits);
                            for (const Hit id : {Hit::Autostart, Hit::AppAnimations, Hit::SettingsStrip, Hit::SupportCreate}) {
                                const auto it = hits.meta.find(id);
                                if (it == hits.meta.end() || it->second.enabled ||
                                    std::find(ord.begin(), ord.end(), id) != ord.end()) {
                                    ++a11y_total;
                                    std::wprintf(L"A11Y uninstall running control not locked (%ls id=%d)\n", sname, static_cast<int>(id));
                                }
                            }
                        }
                        if (sview.games.size() == 300) {
                            // RDY-10: gorunmeyen satirlarin dugmeleri de kayitli (offscreen), Tab sirasinda
                            const auto ord = focus_order(st, hits);
                            const auto it = hits.meta.find(consent_reset_hit(static_cast<std::size_t>(kMaxConsentRows) - 1));
                            if (it == hits.meta.end() || !it->second.offscreen || std::find(ord.begin(), ord.end(), consent_reset_hit(static_cast<std::size_t>(kMaxConsentRows) - 1)) == ord.end()) {
                                ++a11y_total;
                                std::wprintf(L"A11Y offscreen consent row not registered (%ls)\n", sname);
                            }
                        }
                        {
                            // Spec B7 / G: Goster/Gizle durumu UIA'da (Selected = acik); kapaliyken oyun satiri dugmesi yok.
                            const auto gs = hits.meta.find(Hit::SettingsGamesShow);
                            const auto ls = hits.meta.find(Hit::SettingsLicenses);
                            const bool open = st.settings_games_open;
                            // P12-B3: "Gelismis" kapaliyken Oyun onaylari satiri ve altindakiler UIA'da yok; Gelismis = Expander (Selected = acik).
                            const auto ad = hits.meta.find(Hit::SettingsAdvanced);
                            const bool adv = st.settings_advanced_open;
                            if (ad == hits.meta.end() || ad->second.role != AccRole::Expander || ad->second.selected != adv ||
                                ls == hits.meta.end() || ls->second.selected != st.settings_licenses_open ||
                                (adv ? (gs == hits.meta.end() || gs->second.selected != open || (!open && hits.meta.count(consent_reset_hit(0))))
                                     : (gs != hits.meta.end() || hits.meta.count(Hit::Autostart) || hits.meta.count(Hit::AppAnimations) ||
                                        hits.meta.count(Hit::SettingsStrip) || hits.meta.count(Hit::SupportCreate) ||
                                        hits.meta.count(consent_reset_hit(0))))) {
                                ++a11y_total;
                                std::wprintf(L"A11Y settings show/hide state wrong (%ls)\n", sname);
                            }
                            {
                                // Tab sirasi gorunen siraya esit: Dil, Guncelleme, Gelismis (+ acikken alt satirlar), Hakkinda.
                                const auto tab = focus_order(st, hits);
                                // Devre disi kontroller (guncelleme denetimi / paket hazirlaniyor) Tab almaz: yalniz Tab'daki kimlikler karsilastirilir.
                                std::vector<Hit> want = {Hit::SettingsLanguage, Hit::SettingsUpdateCheck, Hit::SettingsAdvanced};
                                if (adv)
                                    for (const Hit id : {Hit::Autostart, Hit::AppAnimations, Hit::SettingsStrip, Hit::SettingsGamesShow, Hit::SupportCreate})
                                        want.push_back(id);
                                want.push_back(Hit::SettingsLicenses);
                                std::ptrdiff_t last = -1;
                                bool ordered = std::find(tab.begin(), tab.end(), Hit::SettingsAdvanced) != tab.end() &&
                                               std::find(tab.begin(), tab.end(), Hit::SettingsLicenses) != tab.end();
                                for (const Hit id : want) {
                                    const auto it = std::find(tab.begin(), tab.end(), id);
                                    if (it == tab.end()) continue;
                                    const auto idx = it - tab.begin();
                                    if (idx <= last) ordered = false;
                                    last = idx;
                                }
                                if (!ordered) {
                                    ++a11y_total;
                                    std::wprintf(L"A11Y settings tab order wrong (%ls)\n", sname);
                                }
                            }
                            for (const Hit id : {Hit::Autostart, Hit::AppAnimations, Hit::SettingsStrip, Hit::SupportCreate})
                                if (const auto it = hits.meta.find(id); it != hits.meta.end() && it->second.help.empty()) {
                                    ++a11y_total;  // spec A: ekrandan kalkan aciklama HelpText'te yasar
                                    std::wprintf(L"A11Y settings help text missing (%ls id=%d)\n", sname, static_cast<int>(id));
                                }
                            const auto has_text = [&](i18n::StringId sid) {
                                const auto want = wide(cat.value().get(sid));
                                for (const auto& [id, m] : hits.meta)
                                    if (m.role == AccRole::Text && m.name == want) return true;
                                return false;
                            };
                            if (sview.autostart_failure == core::settings::AutostartFailure::WriteFailed &&
                                (!has_text(i18n::StringId::SettingsAutostartFailedWrite) || hits.meta.at(Hit::Autostart).toggle != 0)) {
                                ++a11y_total;
                                std::wprintf(L"A11Y autostart failure reason not shown with the switch off (%ls)\n", sname);
                            }
                            const auto ord = focus_order(st, hits);
                            const bool notice_ok = !sview.autostart_notice ||
                                                   (hits.meta.count(Hit::AutostartNotice) && hits.meta.count(Hit::AutostartNoticeOff) &&
                                                    std::find(ord.begin(), ord.end(), Hit::AutostartNoticeOff) != ord.end());
                            if (!notice_ok || (!sview.autostart_notice && hits.meta.count(Hit::AutostartNoticeOff))) {
                                ++a11y_total;
                                std::wprintf(L"A11Y autostart notice wrong (%ls)\n", sname);
                            }
                        }
                        // R21/R20 kartlari listeyi asagi itebilir: ustte 0 satir gorunmesi hata degil (sanal liste);
                        // asiri cizim ve gorunmeyen satirin kayitli/Tab'da olmasi ayri denetlenir.
                        if (sview.games.size() == 300 && st.rows_drawn > 20) {
                            ++missing_controls;
                            std::wprintf(L"SETTINGS not virtualized rows=%zu\n", st.rows_drawn);
                        }
                        if (!out.empty() && loc == i18n::Locale::TrTR && sc == 1.f && w == 1180.f) {
                            wchar_t name[120];
                            swprintf_s(name, L"\\ui06_tr_%ls_1180x720.png", sname);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
        }
        // DIAG-08: depolama karti — Sistem araclari "Sistem" alt sayfasinda (spec 2026-11-12 B.4) — kanit goruntusu
        if (!out.empty() && loc == i18n::Locale::TrTR) {
            ComPtr<IWICBitmap> bmp;
            if (FAILED(wic->CreateBitmap(1180, 720, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
            if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
            UiState st;
            st.reduced_motion = true;
            st.page = Page::System;
            auto vm = fixture(false);
            const auto shits = paint_audited(screen, *g, 1180, 720, st, vm, 0);
            if (!g->end()) return 2;
            ++renders;
            overflow_total += static_cast<int>(g->overflows().size());
            a11y_total += a11y_issues(st, shits, L"system_page");
            // Spec 2026-11-12 B4: sekmesiz tek sayfa — alt sekmeler cizilmez; bolum basliklari (Temizlik, Baslangic
            // uygulamalari, Sistem) ve envanter satirlari (CPU ... ANAKART) UIA metni.
            const Hit sys_tab = nav_hit(static_cast<std::size_t>(Page::System));
            bool sys_ok = !shits.rect_of(sys_tab) && !shits.rect_of(Hit::SysTabCleanup) && !shits.rect_of(nav_hit(static_cast<std::size_t>(Page::Startup)));
            // Spec 2026-11-14 B12: kart baslik seridinin UIA metni "Baslik" ya da "Baslik · ozet".
            for (const auto id : {i18n::StringId::NavCleanup, i18n::StringId::StartupAppsTitle, i18n::StringId::SystoolsInfoTitle}) {
                const std::wstring title = wide(cat.value().get(id));
                sys_ok = sys_ok && (has_text_item(shits, title, 0) || has_text_item(shits, title + L" · ", 1));
            }
            sys_ok = sys_ok && has_text_item(shits, wide(cat.value().get(i18n::StringId::SysCellCpu)) + L": ", 1) &&
                     has_text_item(shits, wide(cat.value().get(i18n::StringId::SystoolsInvBoard)) + L": ", 1);
            if (!sys_ok) {
                ++missing_controls;
                std::wprintf(L"SYSTEM page structure mismatch (sub-tabs drawn or sections / inventory rows missing)\n");
            }
            if (!save_png(wic.Get(), bmp.Get(), out + L"\\diag08_tr_storage_1180x720.png")) return 2;
            g->clear_overflows();
        }
        // UI denetimi Track D: devre disi ogeler, Tab ile gorunur alana kaydirma, odak + secim birlikte.
        {
            auto fresh = [&](UINT pw, UINT ph, ComPtr<IWICBitmap>& bmp) {
                return SUCCEEDED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) &&
                       g->bind_wic(bmp.Get(), 96.f) && g->begin();
            };
            const float w = theme.min_window_w, h = theme.min_window_h;
            // (1) RDY-10: uzun Oyun Hazirligi listesi; son "Duzelt" dugmesi ekran disinda ama kayitli. Tab (reveal_focus)
            // ile odak gelince govde kaydirilir, oge gorunur ve halka cizilir.
            {
                namespace rd = ef::core::readiness;
                auto vm = fixture(false);
                vm.readiness.checks.clear();
                for (int i = 0; i < 40; ++i)
                    vm.readiness.checks.push_back({"t." + std::to_string(i), static_cast<rd::Section>(i % 4), rd::Level::Problem, "chk.win.game_mode_off", {},
                                                   rd::ActionKind::AutoFix, "fix.game_mode"});  // P12-B1: yalniz gercek duzeltmede dugme (args'siz refresh_rate dugmesiz)
                rd::sort_checks(vm.readiness.checks);
                // RDY-01: temizlik sayfasi ziyaret edilmis (birim listesi dolu); Duzelt kimlikleri yine kendi blogunda.
                vm.cleanup.volumes = {L"C:\\", L"D:\\"};
                vm.cleanup.volume = L"C:\\";
                UiState st;
                st.reduced_motion = true;
                st.page = Page::Readiness;
                ComPtr<IWICBitmap> bmp;
                if (!fresh(static_cast<UINT>(w), static_cast<UINT>(h), bmp)) return 2;
                g->clear_overflows();
                auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                {
                    const Hit fix0 = hit_of(HitKind::ChkAction, 0);
                    bool foreign = false;
                    for (const auto& [id, r] : hits.items) {
                        const auto kind = classify_hit(id).kind;
                        foreign |= kind == HitKind::CleanVolume || kind == HitKind::CleanFiles;
                    }
                    if (!hits.activatable(fix0) || classify_hit(fix0).kind != HitKind::ChkAction ||
                        hit_automation_id(fix0) != L"readiness.action.0" || foreign) {
                        ++a11y_total;
                        std::wprintf(L"A11Y readiness fix id collides with cleanup ids (RDY-01)\n");
                    }
                }
                const auto ord = focus_order(st, hits);
                const Hit last = ord.back();
                const auto it = hits.meta.find(last);
                const bool was_off = it != hits.meta.end() && it->second.offscreen;
                st.focus = last;
                st.focus_visible = true;
                st.reveal_focus = true;
                hits = paint_audited(screen, *g, w, h, st, vm, 0);
                if (!g->end()) return 2;
                ++renders;
                overflow_total += static_cast<int>(g->overflows().size());
                a11y_total += a11y_issues(st, hits, L"readiness_reveal");
                const auto it2 = hits.meta.find(last);
                const RectF* r = hits.rect_of(last);
                const bool now_on = it2 != hits.meta.end() && !it2->second.offscreen && r && r->bottom() <= h;
                std::wprintf(L"reveal: readiness last=%d was_offscreen=%d scroll=%.0f visible=%d ring=%d\n", static_cast<int>(last), was_off ? 1 : 0,
                             st.scroll, now_on ? 1 : 0, hits.focus_drawn ? 1 : 0);
                if (!was_off || !now_on || !hits.focus_drawn || st.scroll <= 0 || st.reveal_focus) {
                    ++a11y_total;
                    std::wprintf(L"A11Y reveal failed\n");
                }
                if (!out.empty() && loc == i18n::Locale::TrTR && !save_png(wic.Get(), bmp.Get(), out + L"\\uiD_tr_readiness_reveal_960x540.png")) return 2;
            }
            // (1b) Siki yerlesim (spec 2026-11-11 §2/§5): baslik seridi kaydirilmaz. Sistem araclari alt sekmeleri
            // seritte (ust menunun altinda, govdenin ustunde) durur; govde kaydirilinca yerleri degismez, ekran disi
            // sayilmaz, fareyle tiklanir; Tab ile odak gelince govde kaydirmasi oynamaz ve halka cizilir.
            {
                namespace sa = ef::core::adapters::startup;
                auto vm = fixture(false);
                vm.startup.phase = ef::core::startup::StartupPhase::Ready;
                // BOOST maketi (Sistem): "Temizlik geri alinamaz" rozeti "Bolumler:" seridine (govde) tasindi; sabit serit
                // ogesi denetimi ayni siki esikle Oyunlar sayfasinin baslik seridi dugmesine (Yeniden tara) yonlendirildi.
                for (int i = 0; i < 60; ++i) vm.games.push_back({"Oyun " + std::to_string(i), "steam", "1.0", "v" + std::to_string(i), false, false, ""});
                UiState st;
                st.reduced_motion = true;
                st.page = Page::Games;
                ComPtr<IWICBitmap> bmp;
                if (!fresh(static_cast<UINT>(w), static_cast<UINT>(h), bmp)) return 2;
                g->clear_overflows();
                // Spec 2026-11-12 B4: alt sekme yok; seritteki oge "Temizlik geri alinamaz" cipidir (salt okunur UIA Text).
                const Hit chip_id = Hit::GamesRescan;
                const Hit tab = nav_hit(static_cast<std::size_t>(Page::Games));
                auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                const RectF* c0 = hits.rect_of(chip_id);
                const auto shell = compute_shell(w, h, theme, TopbarMetrics{});
                bool ok = c0 && c0->w > 0 && st.content_height > h + 300.f && !hits.rect_of(Hit::SysTabCleanup) &&
                          !hits.rect_of(nav_hit(static_cast<std::size_t>(Page::Startup)));
                if (ok) {
                    const RectF a = *c0;
                    ok = a.y >= shell.header.y && a.bottom() <= shell.header.bottom() && a.right() <= shell.header.right();
                    st.scroll = 300.f;
                    st.focus = tab;
                    st.focus_visible = true;
                    st.reveal_focus = true;
                    hits = paint_audited(screen, *g, w, h, st, vm, 0);
                    const RectF* c1 = hits.rect_of(chip_id);
                    ok = ok && c1 && c1->x == a.x && c1->y == a.y && !hits.meta[chip_id].offscreen;
                    ok = ok && st.scroll == 300.f && !st.reveal_focus && hits.focus_drawn;
                    a11y_total += a11y_issues(st, hits, L"header_strip_fixed");
                }
                if (!g->end()) return 2;
                ++renders;
                overflow_total += static_cast<int>(g->overflows().size());
                if (!ok) {
                    ++a11y_total;
                    std::wprintf(L"A11Y header strip not fixed (header chip moved / offscreen / scroll changed)\n");
                }
                if (!out.empty() && loc == i18n::Locale::TrTR && !save_png(wic.Get(), bmp.Get(), out + L"\\l1_tr_header_fixed_960x540.png")) return 2;
            }
            // (2) DS-05: is surerken Performans halkasi ve temizlik birim cipleri UIA'da devre disi; Tab'da yok.
            {
                auto vm = fixture(false);
                PerfView pv;
                pv.phase = core::perf::Phase::Applying;
                pv.done = 1;
                pv.total = 4;
                vm.perf = pv;
                UiState st;
                st.reduced_motion = true;
                sync_perf_animations(st, PerfView{}, vm.perf, theme, 0);
                ComPtr<IWICBitmap> bmp;
                if (!fresh(static_cast<UINT>(w), static_cast<UINT>(h), bmp)) return 2;
                g->clear_overflows();
                auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                auto ord = focus_order(st, hits);
                const auto tg = hits.meta.find(Hit::Toggle);
                if (tg == hits.meta.end() || tg->second.enabled || std::find(ord.begin(), ord.end(), Hit::Toggle) != ord.end()) {
                    ++a11y_total;
                    std::wprintf(L"A11Y busy toggle not exposed as disabled\n");
                }
                a11y_total += a11y_issues(st, hits, L"perf_busy_disabled");
                vm.cleanup.phase = core::cleanup::SessionPhase::Running;
                vm.cleanup.volumes = {L"C:\\", L"D:\\"};
                vm.cleanup.volume = L"C:\\";
                vm.cleanup.total = 10;
                vm.cleanup.done = 3;
                st.page = Page::Cleanup;
                hits = paint_audited(screen, *g, w, h, st, vm, 0);
                ord = focus_order(st, hits);
                const auto cv = hits.meta.find(Hit::CleanVolume0);
                if (cv == hits.meta.end() || cv->second.enabled || !cv->second.selected || std::find(ord.begin(), ord.end(), Hit::CleanVolume0) != ord.end()) {
                    ++a11y_total;
                    std::wprintf(L"A11Y busy cleanup chip not exposed as disabled\n");
                }
                // RDY-01: birim cipleri cleanup.volume.N kimligi tasir.
                if (!hits.meta.count(hit_of(HitKind::CleanVolume, 1)) || hit_automation_id(hit_of(HitKind::CleanVolume, 1)) != L"cleanup.volume.1") {
                    ++a11y_total;
                    std::wprintf(L"A11Y cleanup volume automation id missing (RDY-01)\n");
                }
                a11y_total += a11y_issues(st, hits, L"cleanup_busy_disabled");
                if (!g->end()) return 2;
                ++renders;
                overflow_total += static_cast<int>(g->overflows().size());
            }
            // (3) TH-02/M08: secili VE odakli segment (Hizli) - iki tonlu odak halkasi secim cercevesinden ayri.
            if (!out.empty() && loc == i18n::Locale::TrTR) {
                auto vm = fixture(false);
                vm.measure = measure_variants()[0].second;
                UiState st;
                st.reduced_motion = true;
                st.page = Page::Measurements;
                st.measure = vm.measure;
                st.focus = vm.measure.mode == core::measure::TestMode::Full ? Hit::ModeFull : Hit::ModeQuick;
                st.focus_visible = true;
                ComPtr<IWICBitmap> bmp;
                if (!fresh(1180, 720, bmp)) return 2;
                g->clear_overflows();
                const auto hits = paint_audited(screen, *g, 1180, 720, st, vm, 0);
                if (!g->end()) return 2;
                ++renders;
                overflow_total += static_cast<int>(g->overflows().size());
                a11y_total += a11y_issues(st, hits, L"focus_selected");
                if (!save_png(wic.Get(), bmp.Get(), out + L"\\uiD_tr_focus_selected_1180x720.png")) return 2;
            }
        }
        // UI denetimi Track C (RDY-02..13): Oyun Hazirligi geri alma / Koru / mesgul / bildirim durumlari; iki boyut, tr + en PNG.
        {
            namespace rd = ef::core::readiness;
            const auto& cv = cat.value();
            const auto row_hit = [](Hit base, std::size_t k) { return static_cast<Hit>(static_cast<int>(base) + static_cast<int>(k)); };
            const auto starts = [](const std::wstring& s, const std::wstring& p) { return s.rfind(p, 0) == 0; };
            const wchar_t* lc = loc == i18n::Locale::TrTR ? L"tr" : L"en";
            struct RdyCase {
                const wchar_t* name;
                bool busy;
                const char* notice;  // bos = yok
                bool warning;
            };
            const RdyCase cases[] = {{L"undo", false, "", false},
                                     {L"notice_warning", false, "chk.notice.keep_failed", true},
                                     {L"notice_normal", false, "chk.notice.undo_done", false},
                                     {L"fix_running", true, "", false}};
            for (auto [w, h] : sizes)
                for (const auto& rc : cases) {
                    auto vm = fixture(false);
                    readiness_undo_fixture(vm);
                    vm.readiness.fix_running = rc.busy;
                    vm.readiness.notice_key = rc.notice;
                    vm.readiness.notice_warning = rc.warning;
                    // Bildirim ilgili satirda da: uyari -> Koru satiri (hags), normal -> Geri al satiri (ekran)
                    vm.readiness.notice_check_id = *rc.notice ? (rc.warning ? "windows.hags" : "display.0.refresh") : "";
                    const std::size_t k_undo = check_index(vm, "display.0.refresh"), k_retry = check_index(vm, "windows.game_dvr"),
                                      k_int = check_index(vm, "windows.game_mode"), k_conf = check_index(vm, "windows.hags"),
                                      k_three = check_index(vm, "windows.vrr"), k_net = check_index(vm, "net.none");
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Readiness;
                    st.focus = row_hit(Hit::ChkKeep0, k_conf);  // mesgulken de odak yerinde kalmali (RDY-09); Koru satiri PNG'de gorunur
                    st.focus_visible = true;
                    st.reveal_focus = true;  // RDY-10 ile: odakli satir gorunur alana gelir (PNG'de geri alma satirlari)
                    const UINT pw = static_cast<UINT>(w), ph = static_cast<UINT>(h);
                    ComPtr<IWICBitmap> bmp;
                    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                    if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
                    g->clear_overflows();
                    auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                    // Tum icerik (alt kartlar) da tasma denetiminden gecsin: en alta kaydirilmis ikinci cizim, sonra ust.
                    {
                        UiState sb = st;
                        sb.scroll = 1e6f;
                        paint_audited(screen, *g, w, h, sb, vm, 0);
                        hits = paint_audited(screen, *g, w, h, st, vm, 0);
                    }
                    const float content_with = st.content_height;
                    if (!g->end()) return 2;
                    ++renders;
                    if (!out.empty()) {
                        wchar_t name[200];
                        swprintf_s(name, L"\\uiC_%ls_readiness_%ls_%.0fx%.0f.png", lc, rc.name, w, h);
                        if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                    }
                    for (const auto& o : g->overflows()) {
                        ++overflow_total;
                        std::wprintf(L"OVERFLOW readiness=%ls loc=%ls size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", rc.name, lc, w, h,
                                     o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                    }
                    for (const auto& [id, r] : hits.items)
                        // Salt okunur UIA metni (halka skoru, sayim satiri, bulgu satiri metni) kontrol degildir (is_control).
                        if (is_control(hits, id) && (r.h > theme.control_max_h + 0.5f ||
                                                     r.w > theme.sidebar_max * (theme.button_h / default_theme().button_h) + 0.5f)) {
                            ++big_buttons;
                            std::wprintf(L"BIG_CONTROL readiness=%ls id=%d %.1fx%.1f\n", rc.name, static_cast<int>(id), r.w, r.h);
                        }
                    a11y_total += a11y_issues(st, hits, rc.name);
                    int bad = 0;
                    const auto need = [&](bool ok, const wchar_t* what) {
                        if (!ok) {
                            ++bad;
                            std::wprintf(L"READINESS %ls loc=%ls size=%.0fx%.0f: %ls\n", rc.name, lc, w, h, what);
                        }
                    };
                    const auto meta = [&](Hit hh) -> const HitMeta* {
                        const auto it = hits.meta.find(hh);
                        return it == hits.meta.end() ? nullptr : &it->second;
                    };
                    // Dugme varligi (mesgulken de: devre disi, kaybolmaz)
                    for (std::size_t k : {k_undo, k_retry, k_int, k_conf, k_three}) need(meta(row_hit(Hit::ChkUndo0, k)), L"undo button missing");
                    for (std::size_t k : {k_conf, k_three}) need(meta(row_hit(Hit::ChkKeep0, k)), L"keep button missing");
                    need(!meta(row_hit(Hit::ChkKeep0, k_int)), L"keep shown without conflict");
                    need(!meta(row_hit(Hit::ChkAction0, k_undo)), L"second Fix next to Undo");
                    // P12-B1: Windows ayar sayfasi acan yonlendirme dugmesi yok (k_three: Geri al + Koru kalir); gercek is dugmesi (ag testi) kalir.
                    need(!meta(row_hit(Hit::ChkAction0, k_three)), L"redirect button must not be shown");
                    need(meta(row_hit(Hit::ChkAction0, k_net)), L"action button missing");
                    need(meta(Hit::ChkRescan) != nullptr, L"rescan missing");
                    // RDY-07: etiketler
                    const auto undo_lbl = wide(cv.get(i18n::StringId::ChkActionUndo)), retry_lbl = wide(cv.get(i18n::StringId::ChkActionRetryUndo));
                    for (std::size_t k : {k_undo, k_int, k_conf})
                        if (const auto* m = meta(row_hit(Hit::ChkUndo0, k))) need(starts(m->name, undo_lbl + L": "), L"undo label must be Undo");
                    if (const auto* m = meta(row_hit(Hit::ChkUndo0, k_retry))) need(starts(m->name, retry_lbl + L": "), L"retry label");
                    // RDY-12: ad = eylem + bulgu metni (durum eklenmez); durum HelpText'te
                    const struct {
                        std::size_t k;
                        i18n::StringId state;
                    } states[] = {{k_retry, i18n::StringId::ChkUndoStateRecovery}, {k_int, i18n::StringId::ChkUndoStateInterrupted},
                                  {k_conf, i18n::StringId::ChkUndoStateConflict}};
                    for (const auto& sx : states) {
                        const auto stext = wide(cv.get(sx.state));
                        const auto check = screen.check_text(vm.readiness.checks[sx.k]);
                        for (Hit base : {Hit::ChkUndo0, Hit::ChkKeep0})
                            if (const auto* m = meta(row_hit(base, sx.k))) {
                                need(m->name.find(stext) == std::wstring::npos, L"state text in UIA name");
                                need(m->name.size() > check.size() && m->name.compare(m->name.size() - check.size(), check.size(), check) == 0,
                                     L"UIA name must end with check text");
                                need(m->help == stext, L"state text not in HelpText");
                            }
                    }
                    if (const auto* m = meta(row_hit(Hit::ChkUndo0, k_undo))) need(m->help.empty(), L"plain Undo has no state help");
                    // RDY-03/04: 2+ dugme metnin altinda, ayni satirda; Koru onerilen eylemden genis aralikla ayrilir.
                    for (std::size_t k : {k_conf, k_three}) {
                        const RectF* u = hits.rect_of(row_hit(Hit::ChkUndo0, k));
                        const RectF* kp = hits.rect_of(row_hit(Hit::ChkKeep0, k));
                        const RectF* a = hits.rect_of(row_hit(Hit::ChkAction0, k));
                        if (!u || !kp) continue;
                        need(u->y == kp->y && kp->x >= u->right() + theme.space(4) - 0.01f, L"keep gap/alignment");
                        if (a) need(a->y == u->y && a->right() <= u->x && u->x - a->right() < theme.space(4), L"action/undo order");
                    }
                    // RDY-09: mesgulken tum satir dugmeleri + Yeniden tara devre disi, Tab sirasinda yok; odak yerinde, halka cizili.
                    const auto ord = focus_order(st, hits);
                    for (const auto& [id, r] : hits.items) {
                        const int v = static_cast<int>(id);
                        const bool chk = id == Hit::ChkRescan || id == Hit::ChkNetTest ||
                                         (v >= static_cast<int>(Hit::ChkUndo0) && v < static_cast<int>(Hit::ChkKeep0) + kMaxReadinessRows) ||
                                         (v >= static_cast<int>(Hit::ChkAction0) && v < static_cast<int>(Hit::ChkAction0) + kMaxReadinessRows);
                        if (!chk) continue;
                        const bool en = hits.activatable(id);
                        need(en == !rc.busy, rc.busy ? L"busy button still enabled" : L"idle button disabled");
                        need((std::find(ord.begin(), ord.end(), id) != ord.end()) == !rc.busy, L"tab order vs enabled");
                    }
                    need(st.focus == row_hit(Hit::ChkKeep0, k_conf) && hits.focus_drawn, L"focus not stable");
                    // RDY-08: bildirim ilgili kutucukta da yazilir (ozet kartina ek olarak). Spec 2026-11-14 B11: kutucuklar esit
                    // satirdadir; kutucuk kendi esinin bos alanina sigabilir, bu yuzden sayfa yuksekligi yerine kutucugun
                    // metni ve metin alani denetlenir: bildirim yalniz ilgili kutucugun metninde gecer ve o kutucugun durum
                    // metni alani (baslik + deger + bildirim satiri) bildirimle uzar.
                    if (*rc.notice) {
                        const auto notice_id = i18n::id_of(rc.notice);
                        const std::wstring notice_text = notice_id ? wide(cv.get(*notice_id)) : std::wstring();
                        const auto& noted = vm.readiness.checks[check_index(vm, vm.readiness.notice_check_id)];
                        const auto tile_status = [&](const HitMap& hm) -> std::optional<std::pair<Hit, std::wstring>> {
                            for (const auto& [id, m] : hm.meta)
                                if (classify_hit(id).kind == HitKind::Status && m.name.find(screen.check_text(noted)) != std::wstring::npos)
                                    return std::pair{id, m.name};
                            return std::nullopt;
                        };
                        const auto tile_has_notice = [&](const HitMap& hm) {
                            const auto ts = tile_status(hm);
                            return ts && !notice_text.empty() && ts->second.find(notice_text) != std::wstring::npos;
                        };
                        const auto status_h = [&](const HitMap& hm) {
                            const auto ts = tile_status(hm);
                            const RectF* r = ts ? hm.rect_of(ts->first) : nullptr;
                            return r ? r->h : -1.f;
                        };
                        need(tile_has_notice(hits), L"inline row notice not drawn");
                        auto vm2 = vm;
                        vm2.readiness.notice_check_id.clear();
                        UiState s2 = st;
                        if (!g->begin()) return 2;
                        const auto hits2 = paint_audited(screen, *g, w, h, s2, vm2, 0);
                        g->end();
                        g->clear_overflows();
                        need(!tile_has_notice(hits2), L"row notice shown without a matching check");
                        need(status_h(hits2) > 0 && status_h(hits) > status_h(hits2) + 0.5f, L"inline row notice got no height");
                        need(content_with >= s2.content_height - 0.5f, L"inline row notice shrank the page");
                    }
                    missing_controls += bad;
                }
        }
        // UI-05: 1000 satirlik gecmis — yalniz gorunur satirlar cizilir; kare suresi olculur.
        {
            // Ada gore sec: varyant eklemeleri sira numarasini kaydirmasin.
            const auto mv = measure_variants();
            const auto it_big = std::find_if(mv.begin(), mv.end(), [](const auto& p) { return std::wstring(p.first) == L"result"; });
            auto big = it_big->second;  // sonuc + gecmis
            const auto proto = big.history.front();
            big.history.assign(1000, proto);
            ComPtr<IWICBitmap> bmp;
            if (FAILED(wic->CreateBitmap(1180, 720, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
            if (!g->bind_wic(bmp.Get(), 96.f)) return 2;
            UiState st;
            st.reduced_motion = true;
            st.page = Page::Measurements;
            auto vm = fixture(false);
            vm.measure = big;
            g->begin();
            paint_audited(screen, *g, 1180, 720, st, vm, 0);
            g->end();
            st.scroll = st.content_height / 2;  // listenin ortasi
            LARGE_INTEGER f, t0, t1;
            QueryPerformanceFrequency(&f);
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 20; ++i) {
                g->begin();
                paint_audited(screen, *g, 1180, 720, st, vm, 0);
                g->end();
            }
            QueryPerformanceCounter(&t1);
            const double ms = 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(f.QuadPart) / 20.0;
            std::wprintf(L"virtual_list: history=1000 rows_drawn=%zu paint_ms=%.2f\n", st.rows_drawn, ms);
            if (st.rows_drawn == 0 || st.rows_drawn > 20) ++missing_controls;
            g->clear_overflows();
        }
        // UI-05: yazilim cizim maliyeti (ekran disi, CPU) — genel bakis, 100% ve 200% olcek.
        if (loc == i18n::Locale::TrTR) {
            for (float sc : {1.f, 2.f}) {
                const UINT pw = static_cast<UINT>(1280 * sc), ph = static_cast<UINT>(720 * sc);
                ComPtr<IWICBitmap> bmp;
                if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                if (!g->bind_wic(bmp.Get(), 96.f * sc)) return 2;
                UiState st;
                st.reduced_motion = true;
                const auto vm = fixture(false);
                g->set_audit(false);
                LARGE_INTEGER f, t0, t1;
                QueryPerformanceFrequency(&f);
                QueryPerformanceCounter(&t0);
                for (int i = 0; i < 20; ++i) {
                    g->begin();
                    paint_audited(screen, *g, 1280, 720, st, vm, 0);
                    g->end();
                }
                QueryPerformanceCounter(&t1);
                g->set_audit(true);
                std::wprintf(L"paint_cost: overview %ux%u px paint_ms=%.2f\n", pw, ph,
                             1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(f.QuadPart) / 20.0);
            }
        }
        // TH-08/UI-04: yuksek kontrast — Windows "Yuksek Karsitlik #1" (siyah) sistem renk tablosu, calisma
        // zamaniyla ayni donusumden (high_contrast_palette_from) gecer. Tum sayfalar, iki dil, iki olcek/boyut.
        {
            auto hex = [](const char* h) { return *Color::from_hex(h); };
            const Palette hc = high_contrast_palette_from(
                SystemColorTable{hex("#000000"), hex("#000000"), hex("#FFFFFF"), hex("#1AEBFF"), hex("#000000"), hex("#FFFF00"),
                                 hex("#3FF23F")});
            screen.set_palette(&hc);
            a11y_total += palette_contrast_issues(hc, L"high_contrast");
            // Metin/zemin kontrasti: yuksek kontrast paletinde de >= 4.5
            if (contrast_ratio(hc.text_primary, hc.background) < 4.5 || contrast_ratio(hc.on_accent, hc.accent) < 4.5) ++a11y_total;
            const auto pv = perf_variants();
            const auto mv = measure_variants();
            for (float sc : {1.f, 2.f})
                for (auto [w, h] : sizes)
                    for (std::size_t pg = 0; pg < kPageCount; ++pg) {
                        const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                        ComPtr<IWICBitmap> bmp;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st;
                        st.reduced_motion = true;
                        st.page = static_cast<Page>(pg);
                        auto vm = fixture(false);
                        if (st.page == Page::Overview) {
                            vm.perf = pv[7].view;  // onay karti (en kalabalik)
                            sync_perf_animations(st, PerfView{}, vm.perf, theme, 0);
                        }
                        if (st.page == Page::Measurements) {
                            vm.measure = mv[3].second;
                            st.measure = vm.measure;
                        }
                        st.focus = focus_order(st).back();
                        st.focus_visible = true;
                        g->clear_overflows();
                        const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        overflow_total += static_cast<int>(g->overflows().size());
                        a11y_total += a11y_issues(st, hits, L"high_contrast");
                        if (!out.empty() && sc == 1.f && w == 1180.f) {
                            const wchar_t* lc = loc == i18n::Locale::TrTR ? L"tr" : L"en";
                            wchar_t name[120];
                            swprintf_s(name, L"\\ui04_%ls_high_contrast_%ls.png", lc, page_slug(st.page));
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
            // 0.2.1 Olcumler duzeltmeleri yuksek kontrastta: sonuc karti (bos / denge / genel), uyari simgesi, kart ici secenekler.
            for (const auto& [mname, mstate] : mv) {
                if (std::wcsncmp(mname, L"olc_", 4) != 0) continue;
                for (float sc : {1.f, 2.f})
                    for (auto [w, h] : sizes) {
                        const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                        ComPtr<IWICBitmap> bmp;
                        if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                        if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::Measurements;
                        auto vm = fixture(false);
                        vm.measure = mstate;
                        st.measure = vm.measure;
                        // B10: kart ici "Sure" secimi; test surerken secenekler devre disi (halka yok): durdurma dugmesi.
                        st.focus = core::measure::busy(mstate) ? Hit::TestStop : Hit::ModeQuick;
                        st.focus_visible = true;
                        g->clear_overflows();
                        const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                        if (!g->end()) return 2;
                        ++renders;
                        for (const auto& o : g->overflows()) {
                            ++overflow_total;
                            std::wprintf(L"OVERFLOW hc measure=%ls scale=%.2f size=%.0fx%.0f \"%.60ls\"\n", mname, sc, w, h, o.text.c_str());
                        }
                        a11y_total += a11y_issues(st, hits, L"high_contrast_olc");
                        if (!out.empty() && sc == 1.f && w == 1180.f) {
                            const wchar_t* lc = loc == i18n::Locale::TrTR ? L"tr" : L"en";
                            wchar_t name[160];
                            swprintf_s(name, L"\\ui04_%ls_high_contrast_%ls.png", lc, mname);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
            }
            screen.set_palette(nullptr);
        }
        // Siki yerlesim (spec 2026-11-11 §2): 52 DIP ust menu, buyuk logo (200 DIP). Asgari pencerede (960) ve
        // varsayilan pencerede gercek yazi olculeriyle: sekmeler soldan saga cakismadan dizilir, son sekme durum
        // gostergesine carpmaz, hicbir oge seridin ya da pencerenin disina cikmaz (iki dil).
        for (auto [w, h] : sizes) {
            ComPtr<IWICBitmap> bmp;
            if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                return 2;
            if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
            UiState st;
            st.reduced_motion = true;
            auto vm = fixture(false);
            g->clear_overflows();
            const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
            if (!g->end()) return 2;
            ++renders;
            overflow_total += static_cast<int>(g->overflows().size());
            const float bar_top = theme.strip_h, bar_bottom = theme.strip_h + theme.topbar_h;
            const RectF* status = hits.rect_of(Hit::TopbarStatus);
            bool ok = status && status->w > 0 && status->right() <= w - theme.page_margin + 0.5f;
            float prev_right = theme.page_margin;  // marka sutunun sol kenarindan baslar; sekmeler onun saginda
            for (const Page pg : kNavOrder) {
                const RectF* r = hits.rect_of(nav_hit(static_cast<std::size_t>(pg)));
                ok = ok && r && r->w > 0 && r->x >= prev_right && r->y >= bar_top && r->bottom() <= bar_bottom &&
                     r->h >= theme.touch_min && status && r->right() <= status->x;
                if (r) prev_right = r->right();
            }
            if (!ok) {
                ++missing_controls;
                std::wprintf(L"TOPBAR tabs overlap or leave the bar loc=%d size=%.0fx%.0f\n", static_cast<int>(loc), w, h);
            }
        }
        // Spec 2026-11-12 B5 (ust menu hizasi, sekiz sekme) ve B7 dugme kurali (etiket ortada, Evet / Hayir es boyda,
        // "Testi baslat" vurgu rengi dolgulu ana dugme).
        {
            const int bad_bar = topbar_b5_issues(wic.Get(), *g, screen, theme, cat.value(), out, renders, overflow_total);
            if (bad_bar < 0) return 2;
            const int bad_btn = button_rule_issues(wic.Get(), *g, screen, theme, variants, loc == i18n::Locale::TrTR, renders, overflow_total);
            if (bad_btn < 0) return 2;
            missing_controls += bad_bar + bad_btn;
        }
        // Siki yerlesim (spec 2026-11-11 §2): genis pencerede icerik sutunu en fazla 1280 DIP ve ORTALI; ust menu
        // ogeleri ayni sutuna hizali. Tum sayfalar 1920x1080'de cizilir: hicbir oge sutunun disina cikmaz.
        for (std::size_t pg = 0; pg < kPageCount; ++pg) {
            const float w = 1920.f, h = 1080.f;
            ComPtr<IWICBitmap> bmp;
            if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                return 2;
            if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
            UiState st;
            st.reduced_motion = true;
            st.page = static_cast<Page>(pg);
            auto vm = fixture(false);
            // Guncelleme seridi de ayni sutuna hizalanir (zemin pencere genisliginde, metin sutunda).
            if (st.page == Page::Overview) vm.update.outcome = core::update::UpdateOutcome::CheckFailed;
            g->clear_overflows();
            const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
            if (!g->end()) return 2;
            ++renders;
            overflow_total += static_cast<int>(g->overflows().size());
            a11y_total += a11y_issues(st, hits, L"wide");
            if (st.page == Page::Overview && !hits.meta.count(Hit::UpdateStatus)) {
                ++missing_controls;
                std::wprintf(L"WIDE update banner missing\n");
            }
            const float col_w = std::min(w - 2 * theme.page_margin, theme.content_max_w), col_x = (w - col_w) / 2;
            for (const auto& [id, r] : hits.items) {
                if (is_control(hits, id) && control_too_big(theme, id, r)) {
                    ++big_buttons;
                    std::wprintf(L"BIG_CONTROL wide page=%ls id=%d %.1fx%.1f\n", page_slug(st.page), static_cast<int>(id), r.w, r.h);
                }
                // Anahtarlarin isabet kutusu gorselden 4 DIP genistir; pay bununla sinirli.
                if (r.w > 0 && (r.x < col_x - 4.5f || r.right() > col_x + col_w + 4.5f)) {
                    ++missing_controls;
                    std::wprintf(L"WIDE item outside centered column page=%ls id=%d x=%.1f..%.1f column=%.1f..%.1f\n", page_slug(st.page),
                                 static_cast<int>(id), r.x, r.right(), col_x, col_x + col_w);
                }
            }
            if (!out.empty() && loc == i18n::Locale::TrTR && (st.page == Page::Overview || st.page == Page::Cleanup)) {
                wchar_t name[120];
                swprintf_s(name, L"\\l1_tr_%ls_1920x1080.png", page_slug(st.page));
                if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
            }
        }
        // Panel (spec 2026-11-12 B; kutucuk cizimi spec 2026-11-11 §4). Veri durumlari — varsayilan (Performans kapali,
        // bilesen raporu yukleniyor), dolu (Performans acik, bilesen raporu, GPU sicakligi), ilk acilis (test ve tarama
        // yok), bos (hic envanter/olcum yok: "—" + neden), kismi, onay acik (Evet/Hayir bolumun altinda) ve uzun parca
        // adlari (+ denge sonucu, cok birim). Iki pencere boyutu, %100 ve %200, koyu + yuksek kontrast, iki dil.
        // Kabul: 1180x720'de Panel kaydirmasiz tek ekrandir (onay acik durumu haric) — yuvarlak dugme, yan sayilar, bes
        // kutucuk ve iki es kart TAMAMEN gorunur; Sistem karti ve alt kartlar Panel'de yoktur.
        {
            using ef::contracts::Measurement;
            using ef::contracts::MissingReason;
            using ef::contracts::Unit;
            namespace dg = core::diagnosis;
            struct PanelCase {
                const wchar_t* name;
                ViewModel vm;
                bool fits = true;  // 1180x720'de kaydirmasiz sigmali (onay acikken icerik uzar: kaydirilabilir)
            };
            std::vector<PanelCase> pc;
            pc.push_back({L"default", fixture(false)});
            {
                // Performans kapali + yerlesik test kullanilabilir: "Testi baslat" kapali durumda da ana dugme (B.1).
                auto vm = fixture(false);
                vm.measure.builtin_available = true;
                pc.push_back({L"off_ready", vm});
            }
            {
                auto vm = fixture(false);
                vm.components = {{dg::Component::Cpu, dg::Verdict::NoIssueFound, {"runtime:cpu_headroom"}, ""},
                                 {dg::Component::Gpu, dg::Verdict::Limiting, {"runtime:gpu_bound"}, ""},
                                 {dg::Component::Ram, dg::Verdict::NoIssueFound, {"inventory:ram"}, ""},
                                 {dg::Component::Storage, dg::Verdict::Abstain, {}, "insufficient"},
                                 {dg::Component::Motherboard, dg::Verdict::NoIssueFound, {"inventory:board"}, ""}};
                vm.components_loading = false;
                vm.live.cpu_pct = Measurement::of(61.5, Unit::Percent);
                vm.live.cpu_max_core_pct = Measurement::of(100.0, Unit::Percent);
                vm.live.ram_pct = Measurement::of(78.2, Unit::Percent);
                vm.live.gpu_pct = Measurement::of(99.0, Unit::Percent);
                vm.live.gpu_temp_c = Measurement::of(71.0, Unit::Celsius);
                vm.live.disk_pct = Measurement::of(7.0, Unit::Percent);
                vm.live.disk_read_mbs = Measurement::of(0.4, Unit::MBps);
                vm.live.disk_write_mbs = Measurement::of(12.3, Unit::MBps);
                vm.live.net_down_mbps = Measurement::of(3.2, Unit::Mbps);
                vm.live.net_up_mbps = Measurement::of(0.6, Unit::Mbps);
                vm.live.net_link_mbps = Measurement::of(100.0, Unit::Mbps);
                vm.perf.phase = core::perf::Phase::On;
                vm.perf.switch_on = true;
                vm.perf.active = 3;
                vm.measure.builtin_available = true;  // seritteki "Testi baslat" etkin (ana dugme)
                // Kahraman kartta fark + taban: gosterilen kayit Olcumler'deki karsilastirmanin kaydi (ayni kaynak + iz).
                vm.measure.result = vm.measure.history.front();
                vm.measure.comparison = {true, core::measure::Verdict::Uncertain, 7.2};
                pc.push_back({L"full", vm});
            }
            {
                auto vm = fixture(false);
                vm.components = {{dg::Component::Cpu, dg::Verdict::Abstain, {}, "no_runtime_evidence"},
                                 {dg::Component::Gpu, dg::Verdict::Abstain, {}, "no_runtime_evidence"},
                                 {dg::Component::Ram, dg::Verdict::NoIssueFound, {"inventory:ram"}, ""},
                                 {dg::Component::Storage, dg::Verdict::NoIssueFound, {"inventory:storage"}, ""}};
                vm.components_loading = false;
                vm.measure.history.clear();
                vm.readiness.checks.clear();
                pc.push_back({L"first_run", vm});
            }
            {
                auto vm = fixture(false);
                vm.os.clear(), vm.cpu.clear(), vm.gpu.clear(), vm.ram.clear(), vm.board.clear(), vm.display.clear();
                vm.storage.clear();
                vm.components.clear();
                vm.components_loading = false;
                vm.measure.history.clear();
                vm.readiness.checks.clear();
                vm.live = core::metrics::LiveSnapshot{};  // hepsi "ilk olcum bekleniyor"
                vm.live.gpu_pct = Measurement::missing(Unit::Percent, MissingReason::NotSupported);
                vm.live.disk_pct = Measurement::missing(Unit::Percent, MissingReason::SourceError);
                pc.push_back({L"empty", vm});
            }
            {
                // Ag: baglanti hizi bilinmiyor (VPN/tunel) — deger var, kelime/LED yok; yukleme okunamadi.
                auto vm = fixture(false);
                vm.live.net_link_mbps = Measurement::missing(Unit::Mbps, MissingReason::NotSupported);
                vm.live.net_up_mbps = Measurement::missing(Unit::Mbps, MissingReason::NotSampled);
                vm.live.net_down_mbps = Measurement::of(10000.0, Unit::Mbps);
                vm.live.cpu_max_core_pct = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
                vm.live.disk_read_mbs = Measurement::missing(Unit::MBps, MissingReason::NotSampled);
                vm.measure.history.front().run_valid = false;
                pc.push_back({L"partial", vm});
            }
            pc.push_back({L"confirm", panel_confirm_vm(variants), false});
            pc.push_back({L"recovery_conflict", panel_recovery_conflict_vm(variants), false});
            pc.push_back({L"long_names", panel_long_names_vm()});
            auto hex = [](const char* hx) { return *Color::from_hex(hx); };
            const Palette hc_panel = high_contrast_palette_from(
                SystemColorTable{hex("#000000"), hex("#000000"), hex("#FFFFFF"), hex("#1AEBFF"), hex("#000000"), hex("#FFFF00"),
                                 hex("#3FF23F")});
            const Hit first_tile = hit_of(HitKind::Status, 3), last_tile = hit_of(HitKind::Status, 7);  // bolum sayilari 0..2, kutucuklar 3..7
            for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc_panel}) {
                screen.set_palette(pal);
                for (float sc : {1.f, 2.f})
                    for (auto [w, h] : sizes)
                        for (const auto& c : pc) {
                            const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                            ComPtr<IWICBitmap> bmp;
                            if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return 2;
                            if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return 2;
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Overview;
                            const auto& vm = c.vm;
                            sync_perf_animations(st, PerfView{}, vm.perf, theme, 0);
                            st.toggle_knob = Tween(vm.perf.switch_on ? 1.f : 0.f);
                            (void)paint_audited(screen, *g, w, h, st, vm, 0);  // odak sirasi durumu ogrenilir
                            if (!g->end() || !g->begin()) return 2;
                            st.focus = Hit::OpenMeasure;
                            st.focus_visible = true;
                            g->clear_overflows();
                            const auto hits = paint_audited(screen, *g, w, h, st, vm, 0);
                            if (!g->end()) return 2;
                            ++renders;
                            for (const auto& o : g->overflows()) {
                                ++overflow_total;
                                std::wprintf(L"OVERFLOW panel=%ls loc=%d hc=%d scale=%.2f size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n",
                                             c.name, static_cast<int>(loc), pal ? 1 : 0, sc, w, h, o.box.w, o.box.h, o.need_w, o.need_h,
                                             o.text.c_str());
                            }
                            for (const auto& [id, r] : hits.items)
                                if (is_control(hits, id) && control_too_big(theme, id, r)) {
                                    ++big_buttons;
                                    std::wprintf(L"BIG_CONTROL panel=%ls id=%d %.1fx%.1f\n", c.name, static_cast<int>(id), r.w, r.h);
                                }
                            a11y_total += a11y_issues(st, hits, c.name);
                            // Yapi: yuvarlak dugme, "Testi baslat", "Ayrinti" (Kapali/Acik), iki kart dugmesi, 3 bolum sayisi + 5
                            // kutucuk + iki es kart (+ onay acikken alt kart). Sistem karti ve alt kartlar Panel'de yok.
                            const auto ph2 = vm.perf.phase;
                            const bool idle_phase = ph2 == core::perf::Phase::Off || ph2 == core::perf::Phase::On;
                            bool ok = hits.meta.count(Hit::Toggle) && hits.meta.count(Hit::TestStart) && hits.meta.count(Hit::OpenMeasure) &&
                                      hits.meta.count(Hit::OpenReadiness) && (hits.meta.count(Hit::Details) > 0) == idle_phase &&
                                      !hits.meta.count(Hit::CheckStart) && hits.cards.size() == kLiveTileCount + 2 + (idle_phase ? 0u : 1u);
                            const RectF* tog = hits.rect_of(Hit::Toggle);
                            const RectF* start = hits.rect_of(Hit::TestStart);
                            const RectF* t0 = hits.rect_of(first_tile);
                            const RectF* t4 = hits.rect_of(last_tile);
                            const RectF* om = hits.rect_of(Hit::OpenMeasure);
                            const RectF* orr = hits.rect_of(Hit::OpenReadiness);
                            // Yuvarlak dugme: power_button_d capinda; "Testi baslat" saginda, ayni yukseklik araliginda (iki pencere
                            // boyutunda da yan yana). Yatay konum: BOOST genis duzeninde (koyu tema, kartin uc es sutunu sigdiginda)
                            // dugme metin sutunuyla birlikte ORTA sutunun icinde ortali (maket grid-cols-12 / col-span-4); "Ayrinti"
                            // da orta sutunda. Diger duzenlerde (yuksek kontrast, dar pencere) pencerede yatay ortali.
                            ok = ok && tog && start && std::fabs(tog->w - theme.power_button_d) < 0.5f && std::fabs(tog->h - theme.power_button_d) < 0.5f &&
                                 start->x > tog->right() && start->y >= tog->y && start->bottom() <= tog->bottom();
                            const RectF panel_area = overview_body(compute_shell(w, h, theme, TopbarMetrics{}), theme);
                            const float col3 = (panel_area.w - 2 * 15.f - 2 * 16.f) / 3, mid_l = panel_area.x + 15.f + col3 + 16.f, mid_r = mid_l + col3;
                            const bool boost_wide = !pal && tog && tog->x + tog->w / 2 < w / 2 - 1.f;
                            if (ok && boost_wide) {
                                const RectF* det = hits.rect_of(Hit::Details);
                                // Dugme + 8 DIP parlama payi (108'lik kapsayici) ve metin sutunu (Ayrinti) orta sutunda; grup ortali.
                                ok = tog->x - 8.f >= mid_l - 0.5f && (!det || det->right() <= mid_r + 0.5f) &&
                                     (!det || std::fabs((tog->x - 8.f - mid_l) - (mid_r - det->right())) <= 1.f || det->right() < mid_r - 1.f);
                            } else if (ok) {
                                ok = std::fabs(tog->x + tog->w / 2 - w / 2) < 1.f;
                            }
                            // Sol grup (SON TEST FPS, %1 DUSUK) dugmenin solunda, sag grup (HAZIRLIK) saginda.
                            const RectF* s0 = hits.rect_of(hit_of(HitKind::Status, 0));
                            const RectF* s1 = hits.rect_of(hit_of(HitKind::Status, 1));
                            const RectF* s2 = hits.rect_of(hit_of(HitKind::Status, 2));
                            ok = ok && s0 && s1 && s2 && s0->right() <= s1->x && s1->right() < tog->x && s2->x > tog->right() && s2->right() <= start->x;
                            // BOOST genis duzen: sol bolum sayilari sol sutunda, HAZIRLIK ve "Testi baslat" sag sutunda.
                            if (ok && boost_wide) ok = s1->right() <= mid_l - 16.f + 0.5f && s2->x >= mid_r + 16.f - 0.5f && start->right() <= panel_area.right() - 15.f + 0.5f;
                            // Bes kutucuk tek sirada, esit boyda ve en az kLiveTileMinW genisliginde (asgari pencere dahil).
                            ok = ok && t0 && t4 && std::fabs(t0->y - t4->y) < 0.5f && std::fabs(t0->h - t4->h) < 0.5f &&
                                 std::fabs(t0->w - t4->w) < 0.5f && t0->w >= kLiveTileMinW;
                            // Sira: dugme -> kutucuklar -> iki es kart (dugmeleri ayni satirda: "Test et" solda, "Duzelt" sagda).
                            ok = ok && om && orr && tog->bottom() < t0->y && t0->bottom() < om->y && std::fabs(om->y - orr->y) < 0.5f && om->x < orr->x;
                            // Onay acik: Evet/Hayir bolumun altinda (dugmenin altinda, kutucuklarin ustunde); akis kimlikleri ayni.
                            if (ok && ph2 == core::perf::Phase::Confirming) {
                                const RectF* yes = hits.rect_of(Hit::Yes);
                                const RectF* no = hits.rect_of(Hit::No);
                                ok = yes && no && yes->y > tog->bottom() && yes->bottom() < t0->y && no->x > yes->x && !hits.activatable(Hit::Toggle);
                            }
                            // A: yonlendirme cumlesi cizilmez; ekran okuyucuda dugmenin yardim metnidir (bilgi kaybolmaz).
                            if (ok && ph2 == core::perf::Phase::Off)
                                ok = hits.meta.at(Hit::Toggle).help == wide(cat.value().get(i18n::StringId::OverviewPerfHintOff));
                            if (ok) ok = !hits.meta.at(Hit::OpenMeasure).help.empty() && !hits.meta.at(Hit::OpenReadiness).help.empty();
                            // B.2: parca adi kisaltilmadan kutucugun UIA adinda (uzun ad "…" ile cizilse de); birim dolulugunun
                            // tam listesi DISK kutucugunun adinda ("+N" ile kisalsa da).
                            if (ok && !vm.cpu.empty()) ok = has_text_item(hits, wide(sys_cell_split(vm.cpu).main), 2);
                            if (ok && !vm.gpu.empty()) ok = has_text_item(hits, wide(std::string_view(vm.gpu).substr(0, vm.gpu.find('\n'))), 2);
                            if (ok && !vm.ram.empty()) ok = has_text_item(hits, wide(ram_tile_name(vm.ram)), 2);
                            if (ok && vm.live.disk_pct.has_value() && !vm.live.volumes.empty()) {
                                const auto fills = volume_fills(vm.live.volumes);
                                ok = has_text_item(hits, volume_fill_list(fills, fills.size(), loc == i18n::Locale::TrTR), 2);
                            }
                            // Kabul olcutu (spec B.4): 1180x720'de Panel kaydirmasiz tek ekran — icerik govdeye sigar, hicbir oge
                            // ekran disinda degil, son kart pencerenin icinde biter (%100 ve %200).
                            const auto shell = compute_shell(w, h, theme, TopbarMetrics{});
                            const RectF panel_body = overview_body(shell, theme);  // BOOST: Panel'de sayfa basligi seridi yok
                            float last_bottom = 0;
                            for (const auto& cr : hits.cards) last_bottom = std::max(last_bottom, cr.bottom());
                            if (ok && w == 1180.f && c.fits) {
                                ok = st.content_height <= panel_body.h + 0.5f && last_bottom <= h - theme.page_margin + 0.5f;
                                for (const auto& [id, m] : hits.meta) ok = ok && !m.offscreen;
                            }
                            // Asgari pencere (960x540): kullanilabilir — dugme, yan gruplar ve kutucuklar kaydirmadan gorunur.
                            if (ok && w < 1180.f && c.fits)
                                ok = !hits.meta.at(Hit::Toggle).offscreen && !hits.meta.at(Hit::TestStart).offscreen && !hits.meta.at(last_tile).offscreen &&
                                     t4->bottom() <= h + 0.5f;
                            if (!ok) {
                                ++missing_controls;
                                std::wprintf(L"PANEL layout/acceptance mismatch case=%ls loc=%d hc=%d scale=%.2f size=%.0fx%.0f content=%.1f body=%.1f "
                                             L"last_card_bottom=%.1f cards=%zu\n",
                                             c.name, static_cast<int>(loc), pal ? 1 : 0, sc, w, h, st.content_height, panel_body.h, last_bottom,
                                             hits.cards.size());
                            }
                            const bool tr_loc = loc == i18n::Locale::TrTR;
                            const bool png = !out.empty() && (tr_loc ? (sc == 1.f || (w == 1180.f && !pal)) && (!pal || w == 1180.f)
                                                                     : (sc == 1.f && !pal && std::wstring(c.name) == L"full"));
                            if (png) {
                                wchar_t name[200];
                                swprintf_s(name, L"\\l2_%ls_panel_%ls_%.0fx%.0f_%03d%ls.png", tr_loc ? L"tr" : L"en", c.name, w, h,
                                           static_cast<int>(sc * 100 + 0.5f), pal ? L"_hc" : L"");
                                if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                                dump_rects(out + name, hits);
                            }
                        }
            }
            screen.set_palette(nullptr);
            // Spec 2026-11-12 §5: simetri kaniti — Panel'in tamami tek goruntude (uzun pencere, kaydirmasiz): ekranin
            // altinda kalan satirlar da gozle denetlenir. Koyu tema, %100.
            if (!out.empty() && loc == i18n::Locale::TrTR)
                for (float w : {1180.f, 960.f})
                    for (const auto& c : pc) {
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::Overview;
                        sync_perf_animations(st, PerfView{}, c.vm.perf, theme, 0);
                        st.toggle_knob = Tween(c.vm.perf.switch_on ? 1.f : 0.f);
                        float h = 720.f;
                        ComPtr<IWICBitmap> bmp;
                        for (int pass = 0; pass < 2; ++pass) {  // 1: icerik yuksekligi olculur; 2: o yukseklikte cizilir
                            if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                                return 2;
                            if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
                            g->clear_overflows();
                            (void)paint_audited(screen, *g, w, h, st, c.vm, 0);
                            if (!g->end()) return 2;
                            ++renders;
                            overflow_total += static_cast<int>(g->overflows().size());
                            h = std::clamp(st.content_height + 150.f, 720.f, 4000.f);
                        }
                        wchar_t name[200];
                        swprintf_s(name, L"\\sym_tr_panel_%ls_%.0f.png", c.name, w);
                        if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                    }
            // Dar govde (asgari pencerenin altinda da bozulmaz): 720 DIP'te kutucuklar 3 + 2, 500 DIP'te tek sutun;
            // Performans bolumunun yan gruplari 720'de dugmenin iki yaninda, 500'de dugmenin altindadir. Metin tasmaz, her
            // etkilesimli oge erisilebilir kalir.
            for (float w : {720.f, 500.f})
                for (const auto& c : pc) {
                    const float h = 900.f;
                    ComPtr<IWICBitmap> bmp;
                    if (FAILED(wic->CreateBitmap(static_cast<UINT>(w), static_cast<UINT>(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)))
                        return 2;
                    if (!g->bind_wic(bmp.Get(), 96.f) || !g->begin()) return 2;
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Overview;
                    sync_perf_animations(st, PerfView{}, c.vm.perf, theme, 0);
                    g->clear_overflows();
                    const auto hits = paint_audited(screen, *g, w, h, st, c.vm, 0);
                    if (!g->end()) return 2;
                    ++renders;
                    for (const auto& o : g->overflows()) {
                        ++overflow_total;
                        std::wprintf(L"OVERFLOW panel_narrow=%ls loc=%d size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", c.name,
                                     static_cast<int>(loc), w, h, o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                    }
                    a11y_total += a11y_issues(st, hits, c.name);
                    // Kutucuk izgarasi: sutun sayisi saf islevle ayni; ilk satirdaki kutucuklar esit genislikte.
                    const float body_w = w - 2 * theme.page_margin;
                    const int cols = live_tile_columns(body_w, theme.card_gap);
                    const RectF* t0 = hits.rect_of(first_tile);
                    const RectF* t1 = hits.rect_of(hit_of(HitKind::Status, 4));
                    const RectF* t4 = hits.rect_of(last_tile);
                    const RectF* tog = hits.rect_of(Hit::Toggle);
                    const RectF* start = hits.rect_of(Hit::TestStart);
                    bool ok = t0 && t1 && t4 && tog && start && cols == (w == 720.f ? 3 : 1);
                    if (ok && cols == 3) ok = std::fabs(t0->y - t1->y) < 0.5f && t4->y > t0->y && std::fabs(t0->w - t1->w) < 0.5f && t4->w > t0->w;
                    if (ok && cols == 1) ok = t1->y > t0->y && std::fabs(t0->w - body_w) < 0.5f;
                    // Yan gruplar dugmenin altina indi: "Testi baslat" dugmenin altinda ve govdenin icinde.
                    if (ok && w == 500.f) ok = start->y > tog->bottom() && start->right() <= theme.page_margin + body_w + 0.5f;
                    // 720'de yan yana: "Testi baslat" dugmenin saginda, ayni yukseklik araliginda.
                    if (ok && w == 720.f) ok = start->x > tog->right() && start->y >= tog->y && start->bottom() <= tog->bottom();
                    if (!ok) {
                        ++missing_controls;
                        std::wprintf(L"PANEL narrow layout mismatch case=%ls loc=%d size=%.0fx%.0f cols=%d\n", c.name, static_cast<int>(loc), w, h, cols);
                    }
                    if (!out.empty() && loc == i18n::Locale::TrTR && (std::wstring(c.name) == L"full" || std::wstring(c.name) == L"first_run")) {
                        wchar_t name[200];
                        swprintf_s(name, L"\\l2_tr_panel_%ls_narrow_%.0f.png", c.name, w);
                        if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                    }
                }
            // Spec 2026-11-12 F: Panel hizli yollari (kutucuk bolgesi, Performans dugmesi bolgesi).
            {
                const int bad = panel_fast_path_issues(wic.Get(), *g, screen, theme, loc == i18n::Locale::TrTR, out, renders, overflow_total);
                if (bad < 0) return 2;
                missing_controls += bad;
            }
        }
        // Siki yerlesim (spec 2026-11-11 §5): Oyunlar (baslik seridi suzgecleri + siki kart izgarasi) ve Oyun hazirligi
        // (tam genislik puan seridi + satir listesi, spec 2026-11-12 §3; "Agi test et"/"Yeniden tara" baslik seridinde). Iki pencere boyutu, %100 ve
        // %200, koyu + yuksek kontrast, iki dil; ayrica dar govde (720 / 500 DIP).
        {
            namespace rd = core::readiness;
            struct GamesCase {
                const wchar_t* name;
                ViewModel vm;
                GamesFilter filter;
                int selected = -1;    // secili oyunun vm.games indeksi (-1 = secim yok)
                int hover = -1;       // fareyle uzerinde durulan kartin indeksi (-1 = yok); gecis tamamlanmis (deger 1)
                bool motion = false;  // hover durumunda hareket acik mi (acik: kart yukselir; kapali: yalniz ton)
                void apply(UiState& st) const {
                    if (selected >= 0 && static_cast<std::size_t>(selected) < vm.games.size())
                        st.games_selected = game_row_key(vm.games[static_cast<std::size_t>(selected)]);
                    if (hover >= 0) {
                        const Hit id = hit_of(HitKind::GameCard, static_cast<std::size_t>(hover));
                        st.hover = id;
                        st.hover_tween[id] = Tween(1.f);
                        st.reduced_motion = !motion;
                    }
                }
            };
            struct ReadyCase {
                const wchar_t* name;
                ViewModel vm;
                bool checkup_open = false;  // "Sistem kontrolu · N bulgu" bolumu acik (gate P3b)
                std::vector<std::string> details{};  // spec 2026-11-14 B11: "Ayrinti"si acik kutucuklarin bulgu kimlikleri
                std::uint32_t groups = 0;            // acik bulgu gruplari (bit = FindingGroup)
                bool bios_guide = false;             // T20: XMP bulgusunun "Adim adim" BIOS rehberi acik
                void apply(UiState& st) const {
                    st.bios_guide_open = bios_guide;
                    st.readiness_checkup_open = checkup_open;
                    st.readiness_detail_open = {details.begin(), details.end()};
                    st.readiness_group_open = groups;
                }
            };
            // Karisik liste: korumali, onerili, calisan, NVIDIA profilli ve Turkce adli oyunlar.
            auto mixed = fixture(false);
            mixed.games_notice_key.clear();
            mixed.games.push_back({"VALORANT", "riot", "09.08", "v", false, false, "gate.capture_blocked"});
            mixed.games.back().protection = "Vanguard";
            mixed.games.push_back({"Apex Legends", "steam", "3.0.71", "a", false, true, "gate.capture_unverified"});
            mixed.games.back().protection = "Easy Anti-Cheat";
            mixed.games.back().risk_scanned = true;
            mixed.games.back().risk_keys = {"risk.launch.high_priority", "risk.video.vsync"};
            mixed.games.push_back({"\xC3\x87\xC4\xB1lg\xC4\xB1n \xC5\x9Eof\xC3\xB6r \xC4\xB0stanbul", "epic", "1.4", "c", false, true, ""});
            mixed.games.back().risk_scanned = true;
            mixed.games.back().risk_keys = {"risk.video.msaa"};
            mixed.games.back().fs_known = true;
            mixed.games.push_back({"Cyberpunk 2077", "gog", "2.13", "p", false, true, "gate.capture_unknown"});
            mixed.games.back().fs_known = true;
            mixed.games.back().nv_exe = "Cyberpunk2077.exe";
            mixed.games.back().nv_has_profile = true;
            // B-012: satici kartinda AMD (runtime yuklu -> veri yetersiz) ve Intel (runtime yok -> desteklenmiyor) satirlari.
            mixed.vendor_ll = {{core::vendors::Vendor::Amd, core::vendors::RuntimeState::Loaded, std::nullopt},
                               {core::vendors::Vendor::Intel, core::vendors::RuntimeState::Missing, std::nullopt}};
            std::vector<GamesCase> gc;
            gc.push_back({L"default", fixture(false), GamesFilter::All});
            gc.back().vm.games_notice_key.clear();
            gc.push_back({L"mixed", mixed, GamesFilter::All});
            gc.push_back({L"protected", mixed, GamesFilter::Protected});
            gc.push_back({L"advice", mixed, GamesFilter::Advice});
            // Spec 2026-11-12 B2: secili oyun seridi. mixed indeksleri: 0 Counter-Strike 2 (calisiyor, "Geri al" +
            // "Profesyonel ayarlar"), 2 Fortnite (EXE yolu bilinmiyor), 3 Contoso Racing (elle eklenen), 4 VALORANT
            // (korumali), 5 Apex Legends (korumali + 2 oneri), 6 Cilgin Sofor Istanbul (oneri), 7 Cyberpunk 2077 (NVIDIA).
            gc.push_back({L"sel_running", mixed, GamesFilter::All, 0});
            {
                // Dort islem birden: Ekran iyilestirmeyi kapat, Profesyonel ayarlar, NVIDIA profilini ayarla, Listeden cikar.
                auto vm = mixed;
                vm.games[7].pro_slug = "cyberpunk-2077";
                vm.games[7].manual_exe = "D:\\Games\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe";
                gc.push_back({L"sel_actions", vm, GamesFilter::All, 7});
                // NVIDIA kaydi sonradan degismis: "Mevcut degeri koru" + gorunur durum uyarisi.
                vm.games[7].nv_applied = true;
                vm.games[7].nv_conflict = true;
                vm.games[7].nv_record_exe = "Cyberpunk2077.exe";
                gc.push_back({L"sel_nv_conflict", vm, GamesFilter::All, 7});
            }
            {
                auto vm = mixed;
                vm.games[4].fs_known = true;  // korumali, EXE yolu biliniyor: yalniz zorunlu uyari satiri
                gc.push_back({L"sel_protected", vm, GamesFilter::All, 4});
                gc.push_back({L"sel_protected_filter", vm, GamesFilter::Protected, 4});
            }
            {
                auto vm = mixed;
                vm.games[6].risk_keys = {"risk.video.msaa", "risk.launch.high_priority", "risk.video.borderless"};
                gc.push_back({L"sel_advice", vm, GamesFilter::All, 6});
            }
            gc.push_back({L"sel_start_once", mixed, GamesFilter::All, 2});
            gc.push_back({L"sel_all", mixed, GamesFilter::All, 5});       // korumali + oneriler + bir kez baslat
            gc.push_back({L"sel_manual", mixed, GamesFilter::All, 3});    // elle eklenen: "Listeden cikar"
            gc.push_back({L"sel_filtered_out", mixed, GamesFilter::Advice, 4});  // secili oyun suzgecte yok: serit yok
            gc.push_back({L"hover", mixed, GamesFilter::All, 0, 1, true});  // hareket acik: kart yukselir (isabet kutusu yerinde)
            gc.push_back({L"hover_static", mixed, GamesFilter::All, 0, 1});  // animasyonlar kapali: kart yerinde, yalniz ton
            {
                // Kaynaklar henuz taranmadi / okunamayan ve kismen okunan kaynak (uyari tonunda, durumu yazili).
                auto vm = mixed;
                vm.library_providers.clear();
                gc.push_back({L"sources_pending", vm, GamesFilter::All});
                vm.library_providers = {{"steam", "games.source.partial", 2}, {"epic", "games.source.unreadable", 0}, {"gog", "games.source.none", 0},
                                        {"ubisoft", "games.source.found", 3}, {"battlenet", "games.source.unsupported", 0}, {"riot", "games.source.found", 1},
                                        {"xbox", "games.source.disabled", 0}, {"ea", "games.source.unsupported", 0}, {"manual", "games.source.found", 1}};
                gc.push_back({L"sources_warn", vm, GamesFilter::All});
            }
            gc.push_back({L"filter_empty", gc.front().vm, GamesFilter::Advice});
            gc.push_back({L"loading", fixture(true), GamesFilter::All});
            {
                // Canli olcumu kapali oyun yok: "Neden?" baglantisi cizilmez, tam ekran notu govdede kalir. Genis harf
                // ciftleri ("WW", "MW") kisaltma kutusuna iki harf olarak sigar.
                auto vm = mixed;
                for (auto& gr : vm.games) gr.capture_key.clear();
                vm.games.push_back({"Wild West Online", "steam", "1.0", "w", false, false, ""});
                vm.games.push_back({"Modern Warfare", "steam", "2.1", "m", false, false, ""});
                gc.push_back({L"no_capture", vm, GamesFilter::All});
            }
            {
                // Spec 2026-11-12 §4: gercek ikonlu kartlar harf kutulu kartlarla yan yana. Ikon pikselleri kodla uretilir
                // (gercek kurulu oyun yok). Dorduncu satirin resmi GECERSIZ (eksik arabellek): harf kutusu kalmalidir.
                auto vm = mixed;
                for (int k = 0; k < 3; ++k) vm.games[static_cast<std::size_t>(k)].icon = generated_icon(k);
                auto broken = std::make_shared<ImagePixels>(*generated_icon(0));
                broken->bgra.resize(broken->bgra.size() - 4);
                vm.games[3].icon = broken;
                gc.push_back({L"icons", vm, GamesFilter::All});
            }
            {
                // Sahibin bildirdigi gorunum (2026-11-12): ust satirda dugmeli/cipli kartlar, alt satirda az icerikli kartlar;
                // Epic'in ham surum dizgisi cok uzun. Satir yuksekligi satir basinadir (alt satir uzatilmaz), uzun surum
                // kartta kisaltilir ama UIA adinda tam kalir, iki dugme sigiyorsa yan yana durur.
                ViewModel vm = fixture(false);
                vm.games_notice_key.clear();
                vm.games.clear();
                vm.games.push_back({"Counter-Strike 2", "steam", "26445667", "c2", true, true, "gate.capture_unverified", "cs2"});
                vm.games.back().protection = "VAC / Trusted Mode, FACEIT Anti-Cheat";
                vm.games.back().fs_known = true;
                vm.games.push_back({"Escape the Backrooms", "steam", "26307078", "e", false, false, "gate.capture_unknown"});
                vm.games.push_back({"Fortnite", "epic", "++Fortnite+Release-42.40-CL-59086525-Windows", "f", false, false, "gate.capture_blocked", "fortnite"});
                vm.games.back().protection = "Easy Anti-Cheat";
                vm.games.back().fs_known = true;
                vm.games.push_back({"Grand Theft Auto V Enhanced", "epic", "1.0.1158.16_v2", "g", false, false, "gate.capture_unknown"});
                vm.games.back().fs_known = true;
                for (const char* n : {"Pummel Party", "WE'RE ALL IN! Demo", "Who's Cheating?", "Windrose"})
                    vm.games.push_back({n, "steam", "20246854", "x", false, false, "gate.capture_unknown"});
                vm.games.back().launcher = "epic";
                vm.games.back().version = "0.10.0.9.32-22d39a16-release";
                gc.push_back({L"rows", vm, GamesFilter::All, 0});
            }
            {
                // BOOST maketi (2026-11-16) ile yan yana karsilastirma: maketteki dort oyunun benzeri (oyun / kaynak / koruma
                // durumlari test girdisidir; urunde yalniz gercek tarama sonucu gosterilir). Upscaler yalniz dosya varligi.
                using core::games::UpscalerKind;
                ViewModel vm = fixture(false);
                vm.games_notice_key.clear();
                vm.games.clear();
                vm.games.push_back({"Counter-Strike 2", "steam", "1.40.4.8", "c", false, true, "gate.capture_unverified"});
                vm.games.back().protection = "VAC";
                vm.games.back().risk_scanned = true;
                vm.games.back().risk_keys = {"risk.video.vsync", "risk.launch.high_priority"};
                vm.games.back().fs_known = true;
                vm.games.back().nv_exe = "cs2.exe";
                vm.games.back().nv_has_profile = true;
                vm.games.back().upscalers_scanned = true;
                vm.games.back().upscalers = {{UpscalerKind::Fsr, "amd_fidelityfx_dx12.dll", ""}};
                vm.games.push_back({"Cyberpunk 2077", "steam", "2.12H1", "p", false, true, "gate.capture_unknown"});
                vm.games.back().fs_known = true;
                vm.games.back().upscalers_scanned = true;
                vm.games.back().upscalers = {{UpscalerKind::Dlss, "nvngx_dlss.dll", ""}, {UpscalerKind::DlssFrameGeneration, "nvngx_dlssg.dll", ""}};
                vm.games.push_back({"Valorant", "riot", "9.09", "v", false, false, "gate.capture_blocked"});
                vm.games.back().protection = "Vanguard";
                vm.games.back().fs_known = true;
                vm.games.push_back({"Black Myth: Wukong", "steam", "1.0.12", "b", false, false, "gate.capture_unknown"});
                vm.games.back().fs_known = true;
                vm.games.back().upscalers_scanned = true;
                vm.games.back().upscalers = {{UpscalerKind::Dlss, "nvngx_dlss.dll", ""}};
                vm.library_providers = {{"steam", "games.source.found", 3}, {"riot", "games.source.found", 1}, {"epic", "games.source.none", 0},
                                        {"xbox", "games.source.none", 0}};
                vm.protections_loading = false;
                vm.protections = {{core::games::ProtectionSystem::EasyAntiCheat, true, false}, {core::games::ProtectionSystem::Vanguard, true, true}};
                gc.push_back({L"boost", vm, GamesFilter::All, 0});
            }
            std::vector<ReadyCase> rc;
            rc.push_back({L"default", fixture(false)});
            rc.push_back({L"checkup_open", fixture(false), true});
            // Spec 2026-11-14 B11: kutucuk "Ayrinti"lari acik (katlanan iyi / bilgi bulgulari dahil) ve tek bulgu grubu acik.
            rc.push_back({L"tiles_open", fixture(false), false, {"display.0.refresh", "gfx.driver", "net.none"}});
            rc.push_back({L"group_open", fixture(false), false, {}, 1u << static_cast<int>(FindingGroup::Hardware)});
            {
                // T20: XMP bulgusu + acik "Adim adim" rehberi (uretici bazli; ASUS + AMD ornegi).
                auto vm = fixture(false);
                vm.updates.board_vendor = "ASUSTeK COMPUTER INC.";
                vm.updates.cpu_vendor = "AuthenticAMD";
                rc.push_back({L"bios_guide_open", vm, false, {}, 1u << static_cast<int>(FindingGroup::Hardware), true});
            }
            {
                auto vm = fixture(false);
                readiness_undo_fixture(vm);
                rc.push_back({L"undo", vm});
            }
            {
                // Hepsi iyi: yalniz tek satirli satirlar, ana dugme yok.
                auto vm = fixture(false);
                vm.readiness.checks = {
                    {"display.0.refresh", rd::Section::Display, rd::Level::Good, "chk.display.at_max", {{"name", "DELL S2721DGF"}, {"hz", "165"}}, rd::ActionKind::None, ""},
                    {"windows.game_mode", rd::Section::Windows, rd::Level::Good, "chk.win.game_mode_on", {}, rd::ActionKind::None, ""},
                    {"windows.vrr", rd::Section::Windows, rd::Level::Info, "chk.win.vrr_off", {}, rd::ActionKind::OpenSettings, "ms-settings:display-advancedgraphics"},
                };
                rd::sort_checks(vm.readiness.checks);
                vm.readiness.undo.clear();
                rc.push_back({L"all_good", vm});
            }
            {
                auto vm = fixture(true);  // tarama suruyor: sayi uretilmez, dugmeler devre disi
                vm.readiness.checks.clear();
                rc.push_back({L"loading", vm});
            }
            {
                // BOOST maketi (view-readiness) ile yan yana karsilastirma: maketteki satirlarin benzeri (bulgular test girdisidir;
                // urunde yalniz gercek tarama sonucu). Maketteki "ReBAR / EXPO dogrulandi" satiri yok (uygulama yalniz tahmin eder).
                auto vm = fixture(false);
                vm.readiness.checks = {
                    {"windows.game_dvr", rd::Section::Windows, rd::Level::Advice, "chk.win.game_dvr_on", {}, rd::ActionKind::AutoFix, "fix.game_dvr"},
                    {"display.0.refresh", rd::Section::Display, rd::Level::Good, "chk.display.at_max", {{"name", "DELL S2721DGF"}, {"hz", "240"}}, rd::ActionKind::None, ""},
                    {"windows.game_mode", rd::Section::Windows, rd::Level::Good, "chk.win.game_mode_on", {}, rd::ActionKind::None, ""},
                    {"windows.hags", rd::Section::Windows, rd::Level::Good, "chk.win.hags_on", {}, rd::ActionKind::None, ""},
                    {"graphics.dx", rd::Section::Graphics, rd::Level::Good, "chk.graphics.dx12u", {}, rd::ActionKind::None, ""},
                };
                rd::sort_checks(vm.readiness.checks);
                vm.readiness.undo.assign(vm.readiness.checks.size(), rd::UndoState::None);
                vm.readiness.undo[check_index(vm, "display.0.refresh")] = rd::UndoState::Undo;
                rc.push_back({L"boost", vm});
            }
            auto hex = [](const char* hx) { return *Color::from_hex(hx); };
            const Palette hc_l3 = high_contrast_palette_from(
                SystemColorTable{hex("#000000"), hex("#000000"), hex("#FFFFFF"), hex("#1AEBFF"), hex("#000000"), hex("#FFFF00"),
                                 hex("#3FF23F")});
            const bool tr_loc = loc == i18n::Locale::TrTR;
            const auto fail = [&](const wchar_t* page, const wchar_t* cname, const Palette* pal, float sc, float w, float h, const wchar_t* what) {
                ++missing_controls;
                std::wprintf(L"L3 %ls layout mismatch case=%ls loc=%d hc=%d scale=%.2f size=%.0fx%.0f: %ls\n", page, cname, static_cast<int>(loc),
                             pal ? 1 : 0, sc, w, h, what);
            };
            // Ortak cizim: once durum ogrenilir, sonra alt (tasma denetimi) ve ust (PNG + isabetler) gorunum.
            const auto render = [&](UiState& st, const ViewModel& vm, float w, float h, float sc, ComPtr<IWICBitmap>& bmp, HitMap& hits) {
                const UINT pw = static_cast<UINT>(w * sc + 0.5f), ph = static_cast<UINT>(h * sc + 0.5f);
                if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return false;
                if (!g->bind_wic(bmp.Get(), 96.f * sc) || !g->begin()) return false;
                (void)paint_audited(screen, *g, w, h, st, vm, 0);
                if (!g->end() || !g->begin()) return false;
                g->clear_overflows();
                UiState sb = st;
                sb.scroll = 1e6f;
                (void)paint_audited(screen, *g, w, h, sb, vm, 0);
                hits = paint_audited(screen, *g, w, h, st, vm, 0);
                if (!g->end()) return false;
                ++renders;
                return true;
            };
            const auto audit = [&](const wchar_t* page, const wchar_t* cname, const Palette* pal, float sc, float w, float h, const UiState& st,
                                   const HitMap& hits) {
                for (const auto& o : g->overflows()) {
                    ++overflow_total;
                    std::wprintf(L"OVERFLOW l3_%ls=%ls loc=%d hc=%d scale=%.2f size=%.0fx%.0f box=%.1fx%.1f need=%.1fx%.1f \"%.60ls\"\n", page, cname,
                                 static_cast<int>(loc), pal ? 1 : 0, sc, w, h, o.box.w, o.box.h, o.need_w, o.need_h, o.text.c_str());
                }
                for (const auto& [id, r] : hits.items)
                    if (is_control(hits, id) && control_too_big(theme, id, r)) {
                        ++big_buttons;
                        std::wprintf(L"BIG_CONTROL l3_%ls=%ls id=%d %.1fx%.1f\n", page, cname, static_cast<int>(id), r.w, r.h);
                    }
                a11y_total += a11y_issues(st, hits, cname);
            };
            // Spec 2026-11-12 B2 denetimi: ince oyun kartlari (secilebilir; UIA RadioButton + HelpText), secili oyun seridi
            // (o oyunun butun islemleri, uyari / yonlendirme satiri, oneriler), tek satir kaynak etiketleri; "Neden?" satiri yok.
            const auto games_checks = [&](const GamesCase& c, const Palette* pal, float sc, float w, float h, const UiState& st, const HitMap& hits,
                                          bool strip_required) {
                const auto bad = [&](const wchar_t* what) { fail(L"games", c.name, pal, sc, w, h, what); };
                using i18n::StringId;
                const auto tx = [&](StringId id) { return wide(cat.value().get(id)); };
                const auto& vm = c.vm;
                const bool listed = !vm.games_loading && !vm.games.empty();
                const RectF* rescan = hits.rect_of(Hit::GamesRescan);
                if (strip_required && !rescan) bad(L"rescan missing in header strip");
                // Govde sutunu (compute_shell): en fazla content_max_w, pencerede ortali.
                const float body_w = std::min(w - 2 * theme.page_margin, theme.content_max_w);
                const float body_x = (w - body_w) / 2;
                if (listed && strip_required) {
                    // Suzgec cipleri: uc radyo dugmesi, tam biri secili, sayilar adda; baslik seridinde soldan saga.
                    const RectF* prev = nullptr;
                    for (const auto f : {GamesFilter::All, GamesFilter::Protected, GamesFilter::Advice}) {
                        const Hit id = games_filter_hit(f);
                        const auto it = hits.meta.find(id);
                        const RectF* r = hits.rect_of(id);
                        if (it == hits.meta.end() || !r) {
                            bad(L"filter chip missing");
                            continue;
                        }
                        if (it->second.role != AccRole::RadioButton || it->second.selected != (f == st.games_filter)) bad(L"filter chip role/selection");
                        if (it->second.name.find(std::to_wstring(games_filter_rows(vm.games, f).size())) == std::wstring::npos) bad(L"filter count not in name");
                        if (prev && r->x < prev->right()) bad(L"filter chips out of order");
                        if (rescan && (r->right() > rescan->x || std::fabs((r->y + r->h / 2) - (rescan->y + rescan->h / 2)) > 0.5f)) bad(L"filter chip not on the header row");
                        prev = r;
                    }
                }
                // "Canli olcum kapali · Neden?" satiri kalkti (spec B2): baglanti hicbir durumda cizilmez.
                if (hits.rect_of(Hit::GamesWhy) || st.games_why) bad(L"why link drawn (the row was removed)");
                const auto rows = games_filter_rows(vm.games, st.games_filters ? st.games_filter : GamesFilter::All);
                const auto shown = [&](std::size_t i) { return listed && std::find(rows.begin(), rows.end(), i) != rows.end(); };
                const auto sel = games_selected_row(vm.games, st.games_selected);
                const bool strip_shown = sel && shown(*sel);
                const HitKind kActions[] = {HitKind::GameNvidia, HitKind::GameFullscreen, HitKind::GameGuide, HitKind::GameManualRemove};
                // Islem dugmeleri yalniz secili oyunun seridindedir; kartta dugme yoktur.
                std::vector<RectF> btns;
                for (std::size_t i = 0; i < vm.games.size(); ++i)
                    for (const HitKind k : kActions) {
                        const RectF* r = hits.rect_of(hit_of(k, i));
                        if (!r) continue;
                        if (!strip_shown || i != *sel) bad(L"action button of a game that is not the selected one");
                        if (r->x < body_x - 0.5f || r->right() > body_x + body_w + 0.5f) bad(L"game button outside the body column");
                        if (std::fabs(r->h - theme.button_row_h) > 0.5f) bad(L"game button is not in-row size");
                        btns.push_back(*r);
                    }
                // Izgara: her gorunen oyunun karti secilebilir bir ogedir; saf sutun islevinin verdigi sutunlara oturur, butun
                // kartlar ayni (ince) boydadir, ayni satirdakiler ayni ust kenardadir.
                const int ncol = games_grid_columns(body_w);
                const auto nc = static_cast<std::size_t>(ncol);
                const float cw = std::floor((body_w - static_cast<float>(ncol - 1) * theme.card_gap) / static_cast<float>(ncol));
                const float card_h = game_card_h(theme);
                std::vector<RectF> frames;  // gorunen kart sirasiyla kart cerceveleri
                for (std::size_t k = 0; listed && k < rows.size(); ++k) {
                    const auto& gr = vm.games[rows[k]];
                    const Hit id = hit_of(HitKind::GameCard, rows[k]);
                    if (id == Hit::None) continue;  // kimlik blogunun disi: secilemez kart
                    const auto it = hits.meta.find(id);
                    const RectF* r = hits.rect_of(id);
                    if (it == hits.meta.end() || !r || it->second.role != AccRole::RadioButton) {
                        bad(L"game card is not a selectable UIA item");
                        continue;
                    }
                    const auto& nm = it->second.name;
                    const auto& help = it->second.help;
                    if (!nm.starts_with(wide(gr.name))) bad(L"game card UIA name does not start with the game name");
                    if (it->second.selected != (sel && *sel == rows[k])) bad(L"game card selected state");
                    // Karttan kalkan metinler HelpText'te: surumun TAM metni, koruma adi, canli olcum nedeni.
                    if (!gr.version.empty() && help.find(wide(gr.version)) == std::wstring::npos) bad(L"full version text missing in the card HelpText");
                    if (!gr.protection.empty() && help.find(wide(gr.protection)) == std::wstring::npos) bad(L"protection name missing in the card HelpText");
                    if (const auto cap = i18n::id_of(gr.capture_key); cap && help.find(tx(*cap)) == std::wstring::npos)
                        bad(L"live measurement reason missing in the card HelpText");
                    if (help.find(tx(StringId::GamesFsStatus)) == std::wstring::npos) bad(L"fullscreen optimisation state missing in the card HelpText");
                    // Eski "Neden?" blogunun tam ekran notu ve eski risk kartinin durum / not satiri da HelpText'te
                    // (secim yapilmadan, dugmesi olmayan oyunda da okunur).
                    if (help.find(tx(StringId::GamesFsNote)) == std::wstring::npos) bad(L"fullscreen optimisation note missing in the card HelpText");
                    if (help.find(tx(!gr.risk_keys.empty() ? StringId::GamesRiskNote : gr.risk_scanned ? StringId::GamesRiskNone : StringId::GamesRiskUnscanned)) ==
                        std::wstring::npos)
                        bad(L"risk scan state missing in the card HelpText");
                    if (!gr.config_read && help.find(tx(StringId::GamesNoAdapter)) == std::wstring::npos) bad(L"settings adapter state missing in the card HelpText");
                    if (gr.config_read && help.find(tx(StringId::GamesConfigRead)) == std::wstring::npos) bad(L"read-only settings state missing in the card HelpText");
                    if (!gr.capture_key.empty() && help.find(tx(StringId::GamesCaptureOff)) == std::wstring::npos) bad(L"live measurement off state missing in the card HelpText");
                    // En cok iki durum etiketi; adlari UIA adinda.
                    const GameChips chips = game_status_chips(gr);
                    if (chips.count < 1 || chips.count > 2) bad(L"game card chip count");
                    if (gr.running && nm.find(tx(StringId::GamesRunning)) == std::wstring::npos) bad(L"running state missing in the card UIA name");
                    bool framed = false;
                    for (const auto& cr : hits.cards)
                        framed = framed || (std::fabs(cr.x - r->x) <= 0.5f && std::fabs(cr.y - r->y) <= 0.5f && std::fabs(cr.w - r->w) <= 0.5f && std::fabs(cr.h - r->h) <= 0.5f);
                    if (!framed) bad(L"game card frame not logged");
                    const float rel = (r->x - body_x) / (cw + theme.card_gap);
                    if (std::fabs(rel - static_cast<float>(k % nc)) > 0.02f) bad(L"game card off the grid columns");
                    if (std::fabs(r->w - cw) > 0.5f || std::fabs(r->h - card_h) > 0.5f) bad(L"game card is not the slim card size");
                    frames.push_back(*r);
                }
                for (std::size_t i = 0; i < vm.games.size(); ++i)
                    if (!shown(i) && hits.rect_of(hit_of(HitKind::GameCard, i))) bad(L"card of a filtered-out game");
                if (frames.size() == rows.size())
                    for (std::size_t k = 0; k < frames.size(); ++k)
                        if (std::fabs(frames[k].y - frames[k - k % nc].y) > 0.5f) bad(L"game cards of one row differ in top edge");
                // Secili oyun seridi: izgaranin altinda tam genislik kart; baslik metni UIA'da (ad + surumun tam metni);
                // oyunun butun islemleri seridin icinde ve odak sirasinda secili kartin hemen ardinda.
                if (!strip_shown) {
                    if (st.games_strip.h > 0) bad(L"strip drawn without a visible selected game");
                } else {
                    const auto& gr = vm.games[*sel];
                    const Hit head = hit_of(HitKind::Status, 0);
                    const auto it = hits.meta.find(head);
                    const RectF* tr = hits.rect_of(head);
                    const RectF* strip = nullptr;
                    if (it == hits.meta.end() || !tr || it->second.role != AccRole::Text) {
                        bad(L"strip header is not a UIA text item");
                    } else {
                        if (!it->second.name.starts_with(wide(gr.name))) bad(L"strip UIA text does not start with the game name");
                        if (!gr.version.empty() && it->second.name.find(wide(gr.version)) == std::wstring::npos) bad(L"full version text missing in the strip UIA text");
                        if (!gr.protection.empty() && it->second.name.find(wide(gr.protection)) == std::wstring::npos) bad(L"protection name missing in the strip UIA text");
                        for (const auto& cr : hits.cards)
                            if (tr->x >= cr.x && tr->right() <= cr.right() + 0.5f && tr->y >= cr.y && tr->bottom() <= cr.bottom() + 0.5f) strip = &cr;
                    }
                    if (!strip) {
                        bad(L"strip frame not found");
                    } else {
                        if (std::fabs(strip->x - body_x) > 0.5f || std::fabs(strip->w - body_w) > 0.5f) bad(L"strip is not a full-width row");
                        for (const auto& f : frames)
                            if (strip->y < f.bottom() - 0.5f) bad(L"strip is not below the grid");
                        for (const auto& b : btns)
                            if (b.x < strip->x + theme.card_pad - 0.5f || b.right() > strip->right() - theme.card_pad + 0.5f || b.y < strip->y || b.bottom() > strip->bottom())
                                bad(L"action button outside the strip");
                        for (std::size_t a = 0; a < btns.size(); ++a)
                            for (std::size_t b = a + 1; b < btns.size(); ++b)
                                if (rects_overlap(btns[a], btns[b])) bad(L"strip buttons overlap");
                    }
                    // Mevcut butun islemler seritte (kosullar eski kartla ayni; fixture'da is surmuyor).
                    const bool idle = !vm.readiness.fix_running;
                    const bool want[] = {idle && (!gr.nv_exe.empty() || !gr.nv_record_exe.empty()) && (gr.nv_applied || (gr.nv_has_profile && !gr.nv_external)),
                                         idle && gr.fs_known && gr.fs_read_ok && (gr.fs_applied || !gr.fs_disabled), !gr.pro_slug.empty(),
                                         !gr.manual_exe.empty() && !gr.running};
                    std::vector<Hit> actions;
                    for (std::size_t k = 0; k < std::size(kActions); ++k) {
                        const Hit id = hit_of(kActions[k], *sel);
                        if ((hits.rect_of(id) != nullptr) != want[k]) bad(L"strip action set differs from the game's available actions");
                        if (want[k]) actions.push_back(id);
                    }
                    const auto order = focus_order(st, hits);
                    const auto at = std::find(order.begin(), order.end(), hit_of(HitKind::GameCard, *sel));
                    if (at == order.end()) bad(L"selected card not in the tab order");
                    else
                        for (std::size_t k = 0; k < actions.size(); ++k)
                            if (static_cast<std::size_t>(order.end() - at) <= k + 1 || *(at + static_cast<std::ptrdiff_t>(k) + 1) != actions[k])
                                bad(L"strip actions do not follow the selected card in the tab order");
                    // Zorunlu uyari / yonlendirme tek satir; oneriler madde madde (her biri UIA metni).
                    if (!gr.protection.empty() != has_text_item(hits, tx(StringId::GamesStripProtected), 0)) bad(L"protected warning line");
                    if (!gr.fs_known != has_text_item(hits, tx(StringId::GamesStripStartOnce), 0)) bad(L"start-once guidance line");
                    std::size_t bullets = 0;
                    for (const auto& [id, meta] : hits.meta)
                        if (meta.role == AccRole::Text && meta.name.starts_with(L"• ")) ++bullets;
                    if (bullets != gr.risk_keys.size()) bad(L"advice bullets differ from the game's advice count");
                }
                // Kaynaklar: tek satir etiket dizisi (her etiket UIA metni) + "Oyun EXE'si ekle" (eski not HelpText'te).
                {
                    const auto title = tx(StringId::GamesSourcesTitle);
                    std::size_t labels = 0;
                    for (const auto& [id, meta] : hits.meta)
                        if (meta.role == AccRole::Text && meta.name.starts_with(title)) ++labels;
                    if (labels != std::max<std::size_t>(1, vm.library_providers.size())) bad(L"source labels differ from the provider count");
                    const RectF* add = hits.rect_of(Hit::GamesAddExe);
                    const auto am = hits.meta.find(Hit::GamesAddExe);
                    if (!add || am == hits.meta.end() || am->second.help != tx(StringId::GamesSourcesNote)) bad(L"add-exe button or its HelpText missing");
                    else if (std::fabs(add->h - theme.button_row_h) > 0.5f || add->right() > body_x + body_w + 0.5f) bad(L"add-exe button size / position");
                }
            };
            // Spec 2026-11-12 §4 denetimi ("icons" durumu). Ayni durum ikonsuz ve yalniz gecersiz ikonla yeniden cizilir:
            //  - koyu palet: ikon cizilir (ilk kartin kutu merkezi uretilen renktir, kose yuvarlak), gecersiz resim ve
            //    ikonsuz cizim bayt bayt aynidir (harf kutusu), hedef yeniden baglaninca cizim degismez;
            //  - yuksek kontrast: ikon hic cizilmez (ikonsuz cizimle bayt bayt ayni, bitmap onbellegi bos).
            const auto icon_checks = [&](const GamesCase& c, const Palette* pal, float sc, float w, float h, const UiState& st, IWICBitmap* shot,
                                         const HitMap& hits) {
                const auto bad = [&](const wchar_t* what) { fail(L"games", c.name, pal, sc, w, h, what); };
                const std::size_t cached = g->image_cache_size();  // bu cizimin hedefinde olusan bitmap sayisi
                const auto pixels_of = [](IWICBitmap* b, UINT& pw) {
                    UINT ph = 0;
                    std::vector<std::uint32_t> px;
                    if (FAILED(b->GetSize(&pw, &ph))) return px;
                    px.resize(static_cast<std::size_t>(pw) * ph);
                    if (FAILED(b->CopyPixels(nullptr, pw * 4, static_cast<UINT>(px.size() * 4), reinterpret_cast<BYTE*>(px.data())))) px.clear();
                    return px;
                };
                ViewModel plain = c.vm, broken = c.vm;
                for (auto& gr : plain.games) gr.icon.reset();
                for (std::size_t k = 0; k < broken.games.size(); ++k)
                    if (k != 3) broken.games[k].icon.reset();
                const auto redraw = [&](const ViewModel& vm, std::vector<std::uint32_t>& px) {
                    UiState s2 = st;
                    ComPtr<IWICBitmap> b;
                    HitMap h2;
                    UINT w2 = 0;
                    if (!render(s2, vm, w, h, sc, b, h2)) return false;
                    px = pixels_of(b.Get(), w2);
                    return !px.empty();
                };
                UINT pw = 0;
                const auto with = pixels_of(shot, pw);
                std::vector<std::uint32_t> none, invalid, again;
                if (with.empty() || !redraw(plain, none) || !redraw(broken, invalid) || !redraw(c.vm, again)) return false;
                if (invalid != none) bad(L"invalid icon pixels changed the picture (initials tile expected)");
                if (again != with) bad(L"icon drawing differs after the target was rebound");
                if (pal) {
                    if (with != none) bad(L"icon drawn in high contrast (initials tile expected)");
                    if (cached != 0) bad(L"icon bitmap created in high contrast");
                    return true;
                }
                if (with == none) bad(L"icon not drawn in the dark palette");
                if (cached < 1 || cached > 3) bad(L"icon bitmap cache size");
                // Ilk kart (indeks 0): kartin kendi isabet kutusu -> 32 DIP kutu kartin ic sol ustunde.
                const RectF* card = hits.rect_of(hit_of(HitKind::GameCard, 0));
                if (!card) {
                    bad(L"icon card frame not found");
                    return true;
                }
                const float tx = card->x + kGameCardInset, ty = card->y + kGameCardInset;  // maket: p-3 + 1 DIP kenarlik
                const auto at = [&](float dx, float dy) {
                    const auto x = static_cast<std::size_t>((tx + dx) * sc), y = static_cast<std::size_t>((ty + dy) * sc);
                    return y * pw + x;
                };
                const std::size_t mid = at(16.f, 16.f), corner = at(0.f, 0.f);
                if (mid >= with.size() || corner >= with.size()) {
                    bad(L"icon tile outside the picture");
                    return true;
                }
                const std::uint32_t m = with[mid];
                const auto close_to = [](std::uint32_t v, float want) { return std::fabs(static_cast<float>(v & 0xFF) - want) <= 3.f; };
                if (!close_to(m >> 16, kIconCentre[0].r) || !close_to(m >> 8, kIconCentre[0].g) || !close_to(m, kIconCentre[0].b))
                    bad(L"icon tile centre is not the generated icon colour");
                if (with[corner] != none[corner]) bad(L"icon corner is not rounded (pixel outside the radius changed)");
                return true;
            };
            const auto ready_checks = [&](const ReadyCase& c, const Palette* pal, float sc, float w, float h, const UiState& st, const HitMap& hits,
                                          bool strip_required) {
                const auto bad = [&](const wchar_t* what) { fail(L"readiness", c.name, pal, sc, w, h, what); };
                const auto& rv = c.vm.readiness;
                const RectF* rescan = hits.rect_of(Hit::ChkRescan);
                const RectF* net = hits.rect_of(Hit::ChkNetTest);
                const float body_w = std::min(w, theme.content_max_w) - 2 * theme.page_margin;
                const float body_x = (w - std::min(w, theme.content_max_w)) / 2 + theme.page_margin;
                // BOOST maketi (2026-11-16 birebir; eski kartsiz 92 DIP halka + baslik seridi dugmeleri yerine): skor karti govdenin
                // ilk kartidir (tam genislik); 84 DIP halka kartin icinde (p-4 + kenar = 17), ozet satiri halkanin yaninda;
                // "Agi test et" | "Yeniden tara" ayni kartta, halkanin saginda (genis govdede kartin sag ic kenarinda, ayni
                // satirda); is surerken devre disi.
                const RectF* ring = hits.rect_of(hit_of(HitKind::Status, 0));
                const RectF* counts = hits.rect_of(hit_of(HitKind::Status, 1));
                const RectF* strip = ring;  // satir dugmeleri halkanin altinda olmali
                const RectF* score_card = nullptr;
                if (ring)
                    for (const auto& card : hits.cards)
                        if (ring->x >= card.x && ring->right() <= card.right() && ring->y >= card.y && ring->bottom() <= card.bottom()) score_card = &card;
                if (!ring || std::fabs(ring->w - kReadinessRingD) > 0.5f || std::fabs(ring->h - kReadinessRingD) > 0.5f)
                    bad(L"readiness ring is not an 84 DIP ring");
                if (!score_card || std::fabs(score_card->x - body_x) > 0.5f || std::fabs(score_card->w - body_w) > 0.5f ||
                    (ring && std::fabs(ring->x - (score_card->x + 17.f)) > 0.5f))
                    bad(L"readiness ring is not inside the full-width score card");
                if (!rescan || (rv.net_available && !net)) bad(L"score card buttons missing");
                if (rescan && net && (net->right() > rescan->x || std::fabs(net->y + net->h / 2 - rescan->y - rescan->h / 2) > 0.5f)) bad(L"score card button order");
                if (score_card)
                    for (const RectF* b : {rescan, net})
                        if (b && (b->x < score_card->x || b->right() > score_card->right() - 17.f + 0.5f || b->y < score_card->y || b->bottom() > score_card->bottom()))
                            bad(L"score card button outside the card");
                if (strip_required && score_card && rescan && ring &&
                    (std::fabs(rescan->right() - (score_card->right() - 17.f)) > 0.5f || rescan->y >= ring->bottom() || rescan->bottom() <= ring->y))
                    bad(L"rescan not at the right of the ring row");
                {
                    const bool busy = rv.loading || rv.net_running || rv.fix_running;
                    if (rescan && hits.activatable(Hit::ChkRescan) == busy) bad(L"rescan enabled state");
                    if (net && hits.activatable(Hit::ChkNetTest) == busy) bad(L"net test enabled state");
                }
                // Ozet satiri halkanin yaninda (sagda, dikeyde halkayla ortusur) ve skor kartinin icinde. Halka 84 DIP sabittir
                // (maket); buyuk yazi olceginde / dar govdede sarilan 18px ozet halkanin boyunu asabilir (metin blogu halkayla
                // ortali), bu yuzden tam icerilme yerine ortusme + kart icinde kalma denetlenir.
                if (!counts || !ring || counts->x < ring->right() || counts->right() > body_x + body_w + 0.5f || counts->y >= ring->bottom() ||
                    counts->bottom() <= ring->y || (score_card && (counts->y < score_card->y || counts->bottom() > score_card->bottom() ||
                                                                   counts->right() > score_card->right() - 17.f + 0.5f)))
                    bad(L"count line is not beside the ring");
                for (std::size_t k = 0; k < 2; ++k)  // skor + sayim satiri UIA'da okunur metin
                    if (const auto it = hits.meta.find(hit_of(HitKind::Status, k)); it == hits.meta.end() || it->second.role != AccRole::Text || it->second.name.empty())
                        bad(L"readiness ring text missing");
                {
                    // Ekrandan kalkan "N kontrol ... kurallar vN" satiri ve sayfa aciklamasi halkanin HelpText'inde yasar.
                    const auto it = hits.meta.find(hit_of(HitKind::Status, 0));
                    const auto intro = wide(cat.value().get(i18n::StringId::ChkIntro));
                    if (it == hits.meta.end() || it->second.help.find(intro) == std::wstring::npos) bad(L"page description missing from the ring HelpText");
                    if (it != hits.meta.end() && !rv.loading && it->second.help.find(std::to_wstring(ef::core::readiness::kReadinessRulesVersion)) == std::wstring::npos)
                        bad(L"coverage summary missing from the ring HelpText");
                }
                // Bulgu satiri tek satirdir: dugmeler ayni satirda; "Sorun" disinda dolu (ana) dugme kurali birim testte.
                // Her bulgu ekranda bir satir ya da etiket olarak okunur: bulgu cumlesinin tamami bir UIA adinda gecer.
                for (std::size_t k = 0; k < st.readiness_rows; ++k) {
                    const auto full_text = screen.check_text(rv.checks[k]);
                    bool found = false;
                    for (const auto& [id, m] : hits.meta)
                        if (m.name.find(full_text) != std::wstring::npos) found = true;
                    if (!found) bad(L"finding text is not reachable through UIA");
                }
                const float list_x = body_x;
                for (std::size_t k = 0; k < st.readiness_rows; ++k)
                    for (const HitKind kind : {HitKind::ChkAction, HitKind::ChkUndo, HitKind::ChkKeep}) {
                        const RectF* r = hits.rect_of(hit_of(kind, k));
                        if (!r) continue;
                        if (r->x < list_x + theme.card_pad - 0.5f || r->right() > body_x + body_w - theme.card_pad + 0.5f) bad(L"row button outside the list card");
                        if (strip && r->y < strip->bottom()) bad(L"row button above the score strip");
                        if (std::fabs(r->h - theme.button_row_h) > 0.5f) bad(L"row button is not in-row size");
                    }
                // BOOST maketi: her bulgu tek liste satiri (eski B11 kutucuk + "Ayrinti" yerine). Her cizilen bulgunun kendi satir
                // metni (UIA Text, bulgu cumlesinin tamami) gorunen sirada yukaridan asagi, tek sutunda (ayni x) ve halkanin altinda;
                // "Ayrinti" ac/kapa dugmesi cizilmez (cumle ekranda tam yazilir).
                std::vector<RectF> row_rects;
                for (const std::size_t k : st.readiness_order) {
                    const auto full_text = screen.check_text(rv.checks[k]);
                    const RectF* found = nullptr;
                    for (const auto& [id, m] : hits.meta)
                        if (classify_hit(id).kind == HitKind::Status && id != hit_of(HitKind::Status, 0) && id != hit_of(HitKind::Status, 1) &&
                            m.role == AccRole::Text && m.name.find(full_text) != std::wstring::npos && m.name.find(L": " + full_text) != std::wstring::npos)
                            found = hits.rect_of(id);
                    if (!found) {
                        bad(L"finding row text missing");
                        continue;
                    }
                    row_rects.push_back(*found);
                    if (hits.meta.count(hit_of(HitKind::ChkDetail, k))) bad(L"details expander still drawn");
                }
                if (row_rects.size() != st.readiness_order.size() || st.readiness_order.size() != st.readiness_rows) bad(L"not one row per finding");
                for (std::size_t i = 0; i < row_rects.size(); ++i) {
                    if (std::fabs(row_rects[i].x - row_rects.front().x) > 0.5f) bad(L"finding rows are not one column");
                    if (i > 0 && row_rects[i].y <= row_rects[i - 1].bottom()) bad(L"finding rows out of order or overlapping");
                    if (ring && row_rects[i].y < ring->bottom()) bad(L"finding row above the score card");
                }
                for (const FindingGroup grp : st.readiness_groups) {
                    const auto it = hits.meta.find(hit_of(HitKind::ChkGroup, static_cast<std::size_t>(grp)));
                    if (it == hits.meta.end() || it->second.role != AccRole::Expander) bad(L"finding group tile missing");
                    else if (it->second.selected != (st.readiness_checkup_open || ((st.readiness_group_open >> static_cast<int>(grp)) & 1u)))
                        bad(L"finding group tile state");
                }
            };
            for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc_l3}) {
                screen.set_palette(pal);
                for (float sc : {1.f, 2.f})
                    for (auto [w, h] : sizes) {
                        const bool png = !out.empty() && (pal ? (tr_loc && sc == 1.f && w == 1180.f) : tr_loc ? (sc == 1.f || w == 1180.f) : (sc == 1.f && w == 1180.f));
                        for (const auto& c : gc) {
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Games;
                            st.games_filter = c.filter;
                            st.games_guides_open = false;
                            c.apply(st);
                            // Odak: secim varsa secili kartta (kart odak halkasi), yoksa suzgec cipinde.
                            st.focus = c.selected >= 0 && c.filter == GamesFilter::All ? hit_of(HitKind::GameCard, static_cast<std::size_t>(c.selected))
                                                                                     : games_filter_hit(c.filter);
                            st.focus_visible = true;
                            ComPtr<IWICBitmap> bmp;
                            HitMap hits;
                            if (!render(st, c.vm, w, h, sc, bmp, hits)) return 2;
                            audit(L"games", c.name, pal, sc, w, h, st, hits);
                            games_checks(c, pal, sc, w, h, st, hits, true);
                            if (std::wstring(c.name) == L"icons" && !icon_checks(c, pal, sc, w, h, st, bmp.Get(), hits)) return 2;
                            // Yuksek kontrastta kart zemini pencere zeminiyle ayni renktir: piksel ayirt etmez (denetim koyu palette).
                            if (c.hover >= 0 && !pal) {
                                // Hareket acikken kart card_lift DIP yukari cizilir: kartin ust kenarinin hemen ustundeki
                                // piksel artik zemin degildir. Animasyonlar kapaliyken kart yerindedir (piksel zemin).
                                // Zemin degeri: ayni durum hover olmadan cizilir (koyu temada pencere zemini isilti ve komsu kart
                                // golgeleri tasir; duz arka plan rengi degildir). Piksel o referansla birebir karsilastirilir.
                                const RectF* card = hits.rect_of(hit_of(HitKind::GameCard, static_cast<std::size_t>(c.hover)));
                                GamesCase ref_case = c;
                                ref_case.hover = -1;
                                UiState rst;
                                rst.reduced_motion = !c.motion;
                                rst.page = Page::Games;
                                rst.games_filter = c.filter;
                                rst.games_guides_open = false;
                                ref_case.apply(rst);
                                rst.focus = st.focus;
                                rst.focus_visible = true;
                                ComPtr<IWICBitmap> ref_bmp;
                                HitMap ref_hits;
                                if (!render(rst, c.vm, w, h, sc, ref_bmp, ref_hits)) return 2;
                                UINT pw = 0, ph = 0;
                                std::uint32_t px = 0, ref_px = 0;
                                const WICRect at{card ? static_cast<INT>((card->x + card->w / 2) * sc) : 0,
                                                 card ? static_cast<INT>((card->y - theme.card_lift / 2) * sc) : 0, 1, 1};
                                if (!card || FAILED(bmp->GetSize(&pw, &ph)) || at.X >= static_cast<INT>(pw) || at.Y >= static_cast<INT>(ph) ||
                                    FAILED(bmp->CopyPixels(&at, 4, 4, reinterpret_cast<BYTE*>(&px))) ||
                                    FAILED(ref_bmp->CopyPixels(&at, 4, 4, reinterpret_cast<BYTE*>(&ref_px)))) {
                                    fail(L"games", c.name, pal, sc, w, h, L"hover card pixel not readable");
                                } else {
                                    const bool is_bg = (px & 0xFFFFFFu) == (ref_px & 0xFFFFFFu);
                                    if (is_bg == c.motion) fail(L"games", c.name, pal, sc, w, h, c.motion ? L"hovered card is not lifted" : L"hovered card moved while animations are off");
                                }
                            }
                            if (png && (tr_loc || std::wstring(c.name) == L"mixed")) {
                                wchar_t name[200];
                                swprintf_s(name, L"\\l3_%ls_games_%ls_%.0fx%.0f_%03d%ls.png", tr_loc ? L"tr" : L"en", c.name, w, h,
                                           static_cast<int>(sc * 100 + 0.5f), pal ? L"_hc" : L"");
                                if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                                dump_rects(out + name, hits);
                            }
                        }
                        for (const auto& c : rc) {
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Readiness;
                            c.apply(st);
                            ComPtr<IWICBitmap> bmp;
                            HitMap hits;
                            if (!render(st, c.vm, w, h, sc, bmp, hits)) return 2;
                            audit(L"readiness", c.name, pal, sc, w, h, st, hits);
                            ready_checks(c, pal, sc, w, h, st, hits, true);
                            if (png && (tr_loc || std::wstring(c.name) == L"default")) {
                                wchar_t name[200];
                                swprintf_s(name, L"\\l3_%ls_readiness_%ls_%.0fx%.0f_%03d%ls.png", tr_loc ? L"tr" : L"en", c.name, w, h,
                                           static_cast<int>(sc * 100 + 0.5f), pal ? L"_hc" : L"");
                                if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                                dump_rects(out + name, hits);
                            }
                        }
                    }
            }
            screen.set_palette(nullptr);
            // Spec 2026-11-12 B2: "Calisiyor" noktasinin nabzi ve hizli yolu.
            {
                const int bad = games_pulse_issues(wic.Get(), *g, screen, theme, tr_loc, renders);
                if (bad < 0) return 2;
                missing_controls += bad;
            }
            // Spec 2026-11-12 B2: izgara ekrandan uzunken (40 oyun) oyun secilince secili oyun seridi gorunur alana
            // kaydirilir (serit izgaranin altinda acilir; kullanici onu gormeden kalmaz). Secim bayragi yokken kaydirma olmaz.
            {
                ViewModel vm = fixture(false);
                vm.games_notice_key.clear();
                vm.games.clear();
                for (int k = 0; k < 40; ++k) {
                    vm.games.push_back({"Game " + std::to_string(k + 1), "steam", "1." + std::to_string(k), "g" + std::to_string(k), false, false,
                                        "gate.capture_unknown"});
                    vm.games.back().fs_known = true;
                }
                const GamesCase c{L"many", vm, GamesFilter::All, 1};
                for (auto [w, h] : sizes)
                    for (const bool reveal : {false, true}) {
                        UiState st;
                        st.reduced_motion = true;
                        st.page = Page::Games;
                        st.games_guides_open = false;
                        c.apply(st);
                        st.games_reveal_strip = reveal;
                        ComPtr<IWICBitmap> bmp;
                        HitMap hits;
                        if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                        audit(L"games_many", c.name, nullptr, 1.f, w, h, st, hits);
                        games_checks(c, nullptr, 1.f, w, h, st, hits, true);
                        const bool in_view = st.games_strip.h > 0 && st.games_strip.y >= theme.topbar_h && st.games_strip.bottom() <= h + 0.5f;
                        if (st.games_reveal_strip) fail(L"games_many", c.name, nullptr, 1.f, w, h, L"reveal flag not consumed");
                        if (reveal && (st.scroll <= 0 || !in_view)) fail(L"games_many", c.name, nullptr, 1.f, w, h, L"selected-game strip not scrolled into view");
                        if (!reveal && (st.scroll != 0 || in_view)) fail(L"games_many", c.name, nullptr, 1.f, w, h, L"page scrolled without a new selection");
                        if (!out.empty() && tr_loc && reveal) {
                            wchar_t name[200];
                            swprintf_s(name, L"\\l3_tr_games_many_reveal_%.0fx%.0f.png", w, h);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
            }
            // Genis pencere (sahibin bildirdigi gercek goruntunun boyutu, 1800 x 1100): govde content_max_w ile sinirlidir,
            // 4 sutun. "rows" durumunda secili oyunun iki islemi seritte ayni satirdadir.
            for (const auto& c : gc) {
                if (std::wstring(c.name) != L"rows" && std::wstring(c.name) != L"icons") continue;
                const float w = 1800.f, h = 1100.f;
                UiState st;
                st.reduced_motion = true;
                st.page = Page::Games;
                st.games_filter = c.filter;
                st.games_guides_open = false;
                c.apply(st);
                ComPtr<IWICBitmap> bmp;
                HitMap hits;
                if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                audit(L"games_wide", c.name, nullptr, 1.f, w, h, st, hits);
                games_checks(c, nullptr, 1.f, w, h, st, hits, true);
                if (std::wstring(c.name) == L"rows") {
                    // Secili oyunun (indeks 0) iki islemi seritte yan yana: "Ekran iyilestirmeyi kapat" | "Profesyonel ayarlar".
                    const RectF* a = hits.rect_of(hit_of(HitKind::GameFullscreen, 0));
                    const RectF* b = hits.rect_of(hit_of(HitKind::GameGuide, 0));
                    if (!a || !b || std::fabs(a->y - b->y) > 0.5f || b->x < a->right())
                        fail(L"games_wide", c.name, nullptr, 1.f, w, h, L"two strip buttons are not side by side in the widest window");
                }
                if (!out.empty() && tr_loc) {
                    wchar_t name[200];
                    swprintf_s(name, L"\\l3_tr_games_%ls_wide_%.0fx%.0f.png", c.name, w, h);
                    if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                }
            }
            // Dar govde (asgari pencerenin altinda da bozulmaz): oyun izgarasi 2 / 1 sutun; baslik seridine sigmayan oge
            // cizilmez (suzgec yoksa tum oyunlar gorunur); 500 DIP'te puan seridinin uc sayisi alt satira iner.
            for (float w : {720.f, 500.f}) {
                const float h = 900.f;
                for (const auto& c : gc) {
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Games;
                    st.games_filter = c.filter;
                    st.games_guides_open = false;
                    c.apply(st);
                    ComPtr<IWICBitmap> bmp;
                    HitMap hits;
                    if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                    audit(L"games_narrow", c.name, nullptr, 1.f, w, h, st, hits);
                    games_checks(c, nullptr, 1.f, w, h, st, hits, false);
                    if (games_grid_columns(w - 2 * theme.page_margin) != (w == 720.f ? 2 : 1)) fail(L"games_narrow", c.name, nullptr, 1.f, w, h, L"column count");
                    if (!out.empty() && tr_loc && std::wstring(c.name) == L"mixed") {
                        wchar_t name[200];
                        swprintf_s(name, L"\\l3_tr_games_%ls_narrow_%.0f.png", c.name, w);
                        if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                    }
                }
                for (const auto& c : rc) {
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Readiness;
                    c.apply(st);
                    ComPtr<IWICBitmap> bmp;
                    HitMap hits;
                    if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                    audit(L"readiness_narrow", c.name, nullptr, 1.f, w, h, st, hits);
                    ready_checks(c, nullptr, 1.f, w, h, st, hits, false);
                    {
                        // Dar govdede de halka solda, sayim satiri yaninda ve govdenin icinde (ust uste binmez; sigmayan sayim sarilir).
                        const RectF* ring = hits.rect_of(hit_of(HitKind::Status, 0));
                        const RectF* counts = hits.rect_of(hit_of(HitKind::Status, 1));
                        if (!ring || !counts || counts->x < ring->right() || counts->right() > w - theme.page_margin + 0.5f || counts->y >= ring->bottom() ||
                            counts->bottom() <= ring->y)
                            fail(L"readiness_narrow", c.name, nullptr, 1.f, w, h, L"ring / count line");
                    }
                    if (!out.empty() && tr_loc &&
                        (std::wstring(c.name) == L"default" || std::wstring(c.name) == L"undo" || std::wstring(c.name) == L"tiles_open")) {
                        wchar_t name[200];
                        swprintf_s(name, L"\\l3_tr_readiness_%ls_narrow_%.0f.png", c.name, w);
                        if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                    }
                }
            }
            // ---- L4 (spec 2026-11-11 §5): Olcumler, Sistem araclari (Temizlik / Baslangic), Gecmis, Ayarlar ----
            // Koyu + yuksek kontrast, %100 / %200, 960x540 / 1180x720 ve dar govde (720 / 500). Tasma, denetim boyutu ve
            // a11y ortak denetimden gecer; ayrica sayfaya ozgu yerlesim kurallari (baslik seridi ogeleri, sutunlar, es
            // kartlarda dugme hizasi, ac/kapa durumu) dogrulanir.
            {
                namespace cm = core::measure;
                namespace cc = ef::core::cleanup;
                namespace cr = ef::core::recovery;
                namespace sa = ef::core::adapters::startup;
                using ef::contracts::Measurement;
                using ef::contracts::Unit;
                using BalV = ef::core::diagnosis::BalanceVerdict;
                struct L4Case {
                    const wchar_t* page;  // dosya adi parcasi
                    const wchar_t* name;
                    Page pg;
                    ViewModel vm;
                    bool history_all_open = true;  // Gecmis: tum satir ayrintilari acik
                    std::set<std::string> history_open;
                    HistoryFilter filter = HistoryFilter::All;
                    Hit hover = Hit::None;
                    float scroll = 0;
                    float scroll_back = 0;  // scroll > 0 iken: sinirlanan konumdan bu kadar yukari (alttan onceki kart gorunur)
                    bool narrow_png = false, en_png = false;
                    int recent_open = -1;  // Olcumler: ayrintisi acik "Son 3 test" satiri (-1 = yok)
                    bool cleanup_more_open = false;  // Sistem (B12): uzun kategori listesi acik
                    HistoryTab tab = HistoryTab::All;  // Gecmis (B13): gorunum cipi
                    bool settings_collapsed = false;   // Ayarlar (P12-B3): "Gelismis" kapali (pencere boyle baslar)
                };
                std::vector<L4Case> cs;
                const auto mv = measure_variants();
                const auto mstate = [&](const wchar_t* n) {
                    for (const auto& [name, state] : mv)
                        if (std::wstring(name) == n) return state;
                    return cm::MeasureState{};
                };
                // --- Olcumler ---
                {
                    auto vm = fixture(false);
                    auto ms = mstate(L"builtin_result");
                    ms.storage_roots = {"C:\\", "D:\\"};
                    // Saniye basina gercek FPS serisi (40 nokta; bir saniye belirgin yavas): kare suresi cubuklari + grafik.
                    for (int k = 0; k < 40; ++k) ms.result->fps_series.push_back(88.0 + 9.0 * std::sin(k * 0.7) + (k == 23 ? -31.0 : 0.0));
                    ms.result->low01_fps = Measurement::of(61.4, Unit::Fps);
                    ms.result->p99_9_ms = Measurement::of(16.28, Unit::Milliseconds);
                    ms.result->pattern = cm::FramePattern::MicroStutter;
                    auto ingame = *mstate(L"result").result;
                    ingame.avg_fps = Measurement::of(241.6, Unit::Fps);
                    ms.history = {*ms.result, ingame, ms.history.back(), *mstate(L"result_missing").result};
                    ms.history[2].when_unix_ms = 1793600000000;
                    vm.measure = ms;
                    // B10: Genel test kaydi var, yayin kapisi kapali: CPU/GPU "Olculdu · karar yok".
                    vm.components = {{core::diagnosis::Component::Cpu, core::diagnosis::Verdict::Abstain, {}, "diagnosis_calibration_unverified"},
                                     {core::diagnosis::Component::Gpu, core::diagnosis::Verdict::Abstain, {}, "diagnosis_calibration_unverified"}};
                    vm.diagnosis_gate_key = "diag.gate.no_real_holdout";
                    vm.components_loading = false;
                    vm.speed.phase = core::net::SpeedPhase::Done;
                    vm.speed.down = 945.6, vm.speed.up = 51.3, vm.speed.latency = 12.4, vm.speed.bytes = 268'000'000;
                    cs.push_back({L"measure", L"full", Page::Measurements, vm});
                    cs.back().narrow_png = cs.back().en_png = true;
                    {
                        auto sel = vm;  // B10: depolama birimi secili (varsayilan "Yapma" degil): kart ici kisa yazma notu
                        sel.measure.storage_choice = 1;
                        cs.push_back({L"measure", L"full_storage_selected", Page::Measurements, sel});
                    }
                    {
                        // B15: Sonuc karti durumlari. "full" = onceki test yok + kare serisi var. Fark cipi yalniz
                        // karsilastirilabilir onceki testte (artis / dusus); seri yoksa grafik hic cizilmez.
                        auto up = vm;
                        up.measure.comparison = {true, cm::Verdict::Uncertain, 3.24};
                        cs.push_back({L"measure", L"result_prev_up", Page::Measurements, up});
                        cs.back().narrow_png = true;
                        auto down = vm;
                        down.measure.result->fps_series.clear();
                        down.measure.comparison = {true, cm::Verdict::Uncertain, -4.71};
                        cs.push_back({L"measure", L"result_prev_down_no_frames", Page::Measurements, down});
                        cs.back().en_png = true;
                        auto flat = vm;  // B15 inceleme: %0,04 bir ondaliga %0,0 yuvarlanir: yonsuz ("ayni") cip
                        flat.measure.comparison = {true, cm::Verdict::Uncertain, 0.04};
                        cs.push_back({L"measure", L"result_prev_flat", Page::Measurements, flat});
                        cs.back().en_png = true;
                        auto noframes = vm;
                        noframes.measure.result->fps_series.clear();
                        cs.push_back({L"measure", L"result_no_frames", Page::Measurements, noframes});
                        auto nc = vm;  // onceki test var ama surum farkli: cip yok, karsilastirma karti nedeni soyler
                        nc.measure.comparison = {true, cm::Verdict::Uncertain, std::nullopt};
                        nc.measure.comparison.comparable = false;
                        nc.measure.comparison.note_key = "comparison.version_mismatch";
                        cs.push_back({L"measure", L"result_prev_not_comparable", Page::Measurements, nc});
                    }
                    {
                        // BOOST maketi (view-measurements) ile yan yana karsilastirma: maketteki kartlarin benzeri (degerler test
                        // girdisidir; urunde yalniz gercek olcum). Maketteki hedef FPS ve %0,1 satiri yok; disli / "Canli Arena HUD"
                        // yok (secenekler her zaman acik); "Cloudflare" yazisi gercek sunucu adidir.
                        namespace dgx = core::diagnosis;
                        using BVx = dgx::BalanceVerdict;
                        auto bst = vm;
                        bst.measure.comparison = {true, cm::Verdict::Uncertain, 3.24};
                        bst.measure.storage_roots = {"C:\\", "D:\\"};
                        dgx::DropReport nd;
                        nd.run_id = "snapshot";
                        nd.outcome = dgx::DropOutcome::NoDrops;
                        nd.valid_frames = 2763;
                        nd.measured_ms = 15000;
                        nd.total_windows = 15;
                        nd.analyzed_windows = 15;
                        nd.confidence = dgx::DropConfidence::Corroborated;
                        bst.measure.result->drops = std::move(nd);
                        cm::BalanceView bb;
                        bb.cpu_fps = 264.0;
                        bb.rows[0] = {BVx::CpuLimited, "", 264.0, false, 264.0};
                        bb.rows[1] = {BVx::Balanced, "", 184.2, false, 184.2};
                        bb.rows[2] = {BVx::GpuLimited, "", 98.4, false, 98.4};
                        bb.adapter = "NVIDIA GeForce RTX 5090";
                        bst.measure.balance_latest = bb;
                        for (const auto& pv : perf_variants())
                            if (std::wstring(pv.name) == L"on_ba_report") bst.perf = pv.view, bst.before_after = pv.ba;
                        cs.push_back({L"measure", L"boost", Page::Measurements, bst});
                        cs.push_back({L"measure", L"boost_mid", Page::Measurements, bst});  // Darbogaz + takilma + once/sonra
                        cs.back().scroll = 560.f;
                        cs.push_back({L"measure", L"boost_bottom", Page::Measurements, bst});
                        cs.back().scroll = 1e6f;
                    }
                    cs.push_back({L"measure", L"full_bottom", Page::Measurements, vm});
                    cs.back().scroll = 1e6f;
                    // Spec 2026-11-12 C: "Son 3 test" satirina basinca o testin sonuc ayrintisi satirin altinda acilir
                    // (kanit maddeli Uygulama Testi satiri; kanitsiz Oyunda Test satiri: sonuc basligi + olculen sayilar).
                    cs.push_back({L"measure", L"recent_open", Page::Measurements, vm});
                    cs.back().recent_open = 0;
                    cs.back().scroll = 250.f;
                    cs.back().narrow_png = cs.back().en_png = true;
                    cs.push_back({L"measure", L"recent_open_ingame", Page::Measurements, vm});
                    cs.back().recent_open = 1;
                    cs.back().scroll = 250.f;
                }
                {
                    auto vm = fixture(false);
                    vm.measure = mstate(L"drop_found");
                    vm.measure.source = cm::TestSource::InGame;
                    vm.speed.running = true;
                    vm.speed.phase = core::net::SpeedPhase::Download;
                    vm.speed.live_mbps = 312.4;
                    vm.speed.bytes = 41'200'000;
                    cs.push_back({L"measure", L"ingame", Page::Measurements, vm});
                }
                // Spec 2026-11-12 B: "bu oyunda ne sinirladi" karti (bes durum). Kart sonuc kartlarinin altindadir:
                // kaydirmasiz durumlar tam sayfa (sym_*) goruntusuyle, "_card" durumu kartin gorundugu kaydirilmis
                // goruntuyle (yuksek kontrast ve %200 dahil) denetlenir. Ilk durum dar pencere ve Ingilizce de cizilir.
                {
                    const std::pair<const wchar_t*, const wchar_t*> limit_cases[] = {{L"limit_gpu_full", L"ingame_limit_gpu"},
                                                                                     {L"limit_gpu_waited", L"ingame_limit_cpu"},
                                                                                     {L"limit_frame_cap", L"ingame_limit_cap"},
                                                                                     {L"limit_undetermined", L"ingame_limit_none"},
                                                                                     {L"limit_conflict", L"ingame_limit_conflict"}};
                    for (const auto& lc : limit_cases) {
                        auto vm = fixture(false);
                        vm.measure = mstate(lc.first);
                        vm.measure.source = cm::TestSource::InGame;
                        cs.push_back({L"measure", lc.second, Page::Measurements, vm});
                        if (&lc == &limit_cases[0]) {
                            cs.back().narrow_png = cs.back().en_png = true;
                            cs.push_back({L"measure", L"ingame_limit_gpu_card", Page::Measurements, vm});
                            cs.back().scroll = 1e6f;
                            cs.back().scroll_back = 330.f;
                        }
                    }
                }
                {
                    auto vm = fixture(false);
                    cm::MeasureState fresh;
                    fresh.builtin_available = fresh.ingame_available = true;
                    vm.measure = fresh;
                    cs.push_back({L"measure", L"empty", Page::Measurements, vm});
                    cs.back().narrow_png = true;
                    vm.measure = mstate(L"running_builtin_open");
                    cs.push_back({L"measure", L"running", Page::Measurements, vm});
                    cs.back().narrow_png = cs.back().en_png = true;
                    // Baslatilamaz (Oyunda Test bagli degil): yuvarlak dugme devre disi, neden tek satir.
                    vm.measure = fresh;
                    vm.measure.source = cm::TestSource::InGame;
                    vm.measure.ingame_available = false;
                    cs.push_back({L"measure", L"ingame_unavailable", Page::Measurements, vm});
                }
                // --- Denge testi (spec 2026-11-12 A5): kapsam secimi, Denge kartinin butun durumlari, Panel basligi.
                // Gorunumler kodla kurulur (kayittan uretilen gorunumle ayni alanlar); sayilar ornek fikstur degeridir. ---
                {
                    using BV = ef::core::diagnosis::BalanceVerdict;
                    const auto bal_view = [](int variant) {
                        cm::BalanceView b;
                        b.cpu_fps = 143.2;
                        b.adapter = "NVIDIA GeForce RTX 4070 SUPER";
                        b.quick = variant != 1;
                        b.when_unix_ms = 1793600000000;
                        b.cpu_sim_share = 0.62, b.cpu_draw_share = 0.38;
                        if (variant == 0) {  // uc hukum; 1080p GPU degeri alt sinir (>=); GPU suruklenme notu
                            b.rows[0] = {BV::CpuLimited, {}, 412.6, true, 139.8};
                            b.rows[1] = {BV::Balanced, {}, 151.9, false, 137.5};
                            b.rows[2] = {BV::GpuLimited, {}, 68.4, false, 67.1};
                            b.notes = {"gpu_drift", "background_gpu_unknown"};
                        } else if (variant == 1) {  // Belirlenemedi + neden, hukum, Olculemedi + neden; CPU degeri alt sinir
                            b.cpu_lower_bound = true;
                            b.rows[0] = {BV::Undetermined, "coupled_mismatch", 150.2, false, 104.9};
                            b.rows[1] = {BV::GpuLimited, {}, 96.3, false, 95.0};
                            b.rows[2] = {BV::Unmeasurable, "vram_budget", std::nullopt, false, std::nullopt};
                            b.notes = {"background_cpu_unknown", "cpu_drift", "cpu_speed_drop", "cpu_firmware_limit_logged"};
                            b.pcie_link = "x8 / x16, Gen 3 / Gen 4";  // C2: yuk altinda azaminin altinda olculen link
                            b.cpu_sim_share = b.cpu_draw_share = std::nullopt;
                            b.adapter.clear();
                        } else if (variant == 2) {  // genel neden: butun satirlar Belirlenemedi (olculen sayilar gorunur)
                            b.global_reasons = {"on_battery", "background_cpu"};
                            b.rows[0] = {BV::Undetermined, "on_battery", 204.7, false, 121.3};
                            b.rows[1] = {BV::Undetermined, "on_battery", 118.0, false, 110.2};
                            b.rows[2] = {BV::Undetermined, "on_battery", 52.9, false, 52.1};
                        } else {  // CPU adimi gecersiz: CPU degeri yok, GPU degerleri var; hukum yok
                            b.cpu_fps.reset();
                            b.cpu_sim_share = b.cpu_draw_share = std::nullopt;
                            b.global_reasons = {"cpu_step_invalid"};
                            b.cpu_step_reason = "unstable";  // denge_v4: asil neden karta ek satir olarak yazilir
                            b.rows[0] = {BV::Undetermined, "cpu_step_invalid", 388.1, false, std::nullopt};
                            b.rows[1] = {BV::Undetermined, "cpu_step_invalid", 240.5, false, std::nullopt};
                            b.rows[2] = {BV::Undetermined, "cpu_step_invalid", 1012.4, true, std::nullopt};
                        }
                        return b;
                    };
                    const auto bal_vm = [&](int variant) {
                        auto vm = fixture(false);
                        auto ms = mstate(L"builtin_result");
                        ms.storage_roots = {"C:\\", "D:\\"};
                        ms.scope = cm::BuiltinScope::Balance;
                        const auto previous = *ms.result;
                        cm::ResultView r;
                        r.source = cm::TestSource::Builtin;
                        r.when_unix_ms = 1793600000000;
                        r.show_confidence = false;
                        r.balance = bal_view(variant);
                        r.mode = r.balance->quick ? cm::TestMode::Quick : cm::TestMode::Full;
                        ms.mode = r.mode;
                        ms.result = r;
                        ms.history = {r, previous};
                        ms.comparison = {};
                        ms.balance_latest = r.balance;
                        vm.measure = ms;
                        vm.components = {{core::diagnosis::Component::Cpu, core::diagnosis::Verdict::Abstain, {}, "no_runtime_evidence"},
                                         {core::diagnosis::Component::Gpu, core::diagnosis::Verdict::Abstain, {}, "no_runtime_evidence"}};
                        vm.diagnosis_gate_key = "diag.gate.no_real_holdout";
                        vm.components_loading = false;
                        return vm;
                    };
                    cs.push_back({L"measure", L"balance", Page::Measurements, bal_vm(0)});
                    cs.back().narrow_png = cs.back().en_png = true;
                    cs.push_back({L"measure", L"balance_reasons", Page::Measurements, bal_vm(1)});
                    cs.back().narrow_png = true;
                    cs.push_back({L"measure", L"balance_reasons_bottom", Page::Measurements, bal_vm(1)});  // dar govdede kartin kendisi
                    cs.back().scroll = 1e6f;
                    cs.back().narrow_png = true;
                    cs.push_back({L"measure", L"balance_global", Page::Measurements, bal_vm(2)});
                    {
                        auto vm = bal_vm(0);  // birincil ekran uc kutudan hicbirine denk gelmiyor: vurgu yok
                        vm.display = "AW3423DWF · 3440×1440 · 165 Hz";
                        cs.push_back({L"measure", L"balance_no_display_match", Page::Measurements, vm});
                    }
                    {
                        auto vm = bal_vm(0);  // genel test sonucu gorunurken en yeni denge kaydi Darbogaz bolumunde kalir
                        vm.measure.result = vm.measure.history.back();
                        vm.measure.scope = cm::BuiltinScope::General;
                        cs.push_back({L"measure", L"balance_latest_only", Page::Measurements, vm});
                    }
                    cs.push_back({L"measure", L"balance_no_cpu", Page::Measurements, bal_vm(3)});
                    {
                        auto vm = bal_vm(3);  // P3: "Raporu kaydet" kaydedildi (yol tek satir)
                        vm.balance_report.done = true;
                        vm.balance_report.path = L"C:\\Users\\Oyuncu\\Desktop\\EcuFps-denge-raporu-20261117-031500.zip";
                        cs.push_back({L"measure", L"balance_report_saved", Page::Measurements, vm});
                        cs.back().narrow_png = true;
                        cs.push_back({L"measure", L"balance_report_saved_bottom", Page::Measurements, vm});  // kartin kendisi
                        cs.back().scroll = 1e6f;
                        cs.back().narrow_png = true;
                    }
                    {
                        auto vm = bal_vm(2);  // P3: genel neden + rapor kaydedilemedi
                        vm.balance_report.failed = true;
                        cs.push_back({L"measure", L"balance_report_failed", Page::Measurements, vm});
                    }
                    {
                        auto vm = bal_vm(0);  // calisan denge testi: adim 5/9, onceki sonuc gorunur
                        vm.measure.phase = cm::TestPhase::Running;
                        vm.measure.stage_key = "measure.stage_balance_k1";
                        vm.measure.elapsed_s = 22;
                        vm.measure.total_s = 0;
                        cs.push_back({L"measure", L"balance_running", Page::Measurements, vm});
                    }
                    {
                        auto vm = bal_vm(0);  // kapsam Denge testi secili, henuz denge sonucu yok (Genel test sonucu gorunur)
                        vm.measure.result = vm.measure.history.back();
                        vm.measure.history = {vm.measure.history.back()};
                        vm.measure.balance_latest.reset();
                        cs.push_back({L"measure", L"balance_scope_only", Page::Measurements, vm});
                    }
                    {
                        auto vm = bal_vm(0);  // "Canli test ekrani" kapatilmis (kalici tercih): anahtar kapali cizilir
                        vm.measure.live_view.user_on = false;
                        cs.push_back({L"measure", L"balance_live_off", Page::Measurements, vm});
                    }
                    cs.push_back({L"overview", L"balance", Page::Overview, bal_vm(0)});
                    cs.back().narrow_png = cs.back().en_png = true;
                    cs.push_back({L"overview", L"balance_reasons", Page::Overview, bal_vm(1)});
                    cs.push_back({L"overview", L"balance_global", Page::Overview, bal_vm(2)});
                }
                // --- Temizlik ---
                {
                    using CV = ViewModel::CleanupView;
                    using CR = ViewModel::CleanupRow;
                    CV ready;
                    ready.phase = cc::SessionPhase::Ready;
                    ready.rows = {CR{cc::CleanupCategory::UserTemp, cc::PreviewStatus::Ready, 1347, 0, 984844284ULL, false, true, true},
                                  CR{cc::CleanupCategory::WindowsTemp, cc::PreviewStatus::NeedsAdmin, 0, 0, 0, true, false, false},
                                  CR{cc::CleanupCategory::ThumbnailCache, cc::PreviewStatus::Ready, 30, 0, 176276384ULL, false, true, true},
                                  CR{cc::CleanupCategory::WerReportsUser, cc::PreviewStatus::NotPresent, 0, 0, 0, false, false, false},
                                  CR{cc::CleanupCategory::WerReportsSystem, cc::PreviewStatus::NeedsAdmin, 0, 0, 0, true, false, false},
                                  CR{cc::CleanupCategory::DeliveryOptimization, cc::PreviewStatus::NeedsAdmin, 0, 0, 0, true, false, false},
                                  CR{cc::CleanupCategory::RecycleBin, cc::PreviewStatus::Ready, 1, 0, 7483040ULL, false, true, false},
                                  CR{cc::CleanupCategory::AppTestFiles, cc::PreviewStatus::Ready, 3, 0, 5120000ULL, false, true, false},
                                  CR{cc::CleanupCategory::StandbyMemory, cc::PreviewStatus::Ready, 0, 0, 3328000000ULL, false, true, false}};
                    ready.any_selected = true;
                    ready.selected_bytes = 984844284ULL + 176276384ULL;
                    ready.selected_allocated = 1012000000ULL + 176500000ULL;
                    ready.volumes = {L"C:\\", L"D:\\"};
                    ready.volume_free = {689000000000ULL, 1200000000000ULL};
                    ready.volume = L"C:\\";
                    ready.volume_id.root = L"C:\\", ready.volume_id.serial = 0x1A2B3C4Du, ready.volume_id.cluster = 4096;
                    ready.volume_id.fs = L"NTFS", ready.volume_id.label = L"Windows", ready.volume_id.known = true;
                    auto vm = fixture(false);
                    vm.cleanup = ready;
                    cs.push_back({L"cleanup", L"ready", Page::Cleanup, vm});
                    cs.back().narrow_png = cs.back().en_png = true;
                    // Spec 2026-11-14 B12: uzun kategori listesi acik ("Daha az goster").
                    cs.push_back({L"cleanup", L"more_open", Page::Cleanup, vm});
                    cs.back().cleanup_more_open = true;
                    cs.back().narrow_png = cs.back().en_png = true;
                    cs.push_back({L"cleanup", L"hover", Page::Cleanup, vm});  // aciklama satiri: uzerine gelinen kategori
                    cs.back().hover = hit_of(HitKind::CleanToggle, 2);
                    CV files = ready;
                    files.files_open = cc::CleanupCategory::UserTemp;
                    for (int k = 0; k < 12; ++k)
                        files.files.push_back({L"NVIDIA Corporation\\NV_Cache\\ShaderCacheEntry_" + std::to_wstring(k) + L"_0123456789abcdef.bin",
                                               static_cast<std::uint64_t>(90000000 - k * 1000000)});
                    files.files_total = 1347;
                    vm.cleanup = files;
                    cs.push_back({L"cleanup", L"files", Page::Cleanup, vm});
                    CV confirm = ready;
                    confirm.phase = cc::SessionPhase::Confirming;
                    vm.cleanup = confirm;
                    cs.push_back({L"cleanup", L"confirm", Page::Cleanup, vm});
                    CV running = ready;
                    running.phase = cc::SessionPhase::Running;
                    running.done = 620, running.total = 1377;
                    vm.cleanup = running;
                    cs.push_back({L"cleanup", L"running", Page::Cleanup, vm});
                    vm.cleanup = CV{};
                    cs.push_back({L"cleanup", L"idle", Page::Cleanup, vm});
                }
                // --- Baslangic ---
                {
                    using SV = ViewModel::StartupView;
                    using SR = ViewModel::StartupRowView;
                    using SP = ef::core::startup::StartupPhase;
                    SV ready;
                    ready.phase = SP::Ready;
                    ready.rows = {SR{"Discord", "startup.advice.optional_app", sa::Advice::RecommendDisable, false, false, true, false, true},
                                  SR{"Steam", "startup.advice.launcher", sa::Advice::NoAdvice, true, true, true, false, false},
                                  SR{"OneDrive", "startup.advice.cloud_sync", sa::Advice::NoAdvice, true, true, true, false, false},
                                  SR{"SecurityHealth", "startup.advice.security", sa::Advice::Keep, true, true, false, true, false},
                                  SR{"Riot Vanguard", "startup.advice.anticheat", sa::Advice::Keep, false, false, false, false, false},
                                  SR{"Adobe Acrobat Synchronizer Update Helper", "startup.advice.updater", sa::Advice::RecommendDisable, true, false, true, false, false}};
                    ready.services = {{"Spooler", "Yazdırma Biriktiricisi", "background.breaks.printing", true, true, true, true},
                                      {"Fax", "Faks", "background.breaks.fax", false, false, false, false},
                                      {"MapsBroker", "İndirilen Haritalar Yöneticisi", "background.breaks.offline_maps", true, false, true, false}};
                    ready.pending = 2;
                    auto vm = fixture(false);
                    vm.startup = ready;
                    cs.push_back({L"startup", L"ready", Page::Startup, vm});
                    cs.back().narrow_png = cs.back().en_png = true;
                    SV confirm = ready;
                    confirm.phase = SP::Confirming;
                    vm.startup = confirm;
                    cs.push_back({L"startup", L"confirm", Page::Startup, vm});
                    SV none;
                    none.phase = SP::Ready;
                    vm.startup = none;
                    cs.push_back({L"startup", L"empty", Page::Startup, vm});
                }
                // --- Gecmis ---
                {
                    using ef::contracts::OperationId;
                    auto vm = fixture(false);
                    cs.push_back({L"history", L"open", Page::History, vm});
                    cs.back().narrow_png = cs.back().en_png = true;
                    // Gercek kullanim: satirlar kapali, biri acik; farkli kapsam ve durumlar.
                    auto many = vm;
                    auto& rows = many.history.view.rows;
                    const cr::HistoryStatus sts[] = {cr::HistoryStatus::Active, cr::HistoryStatus::Restored, cr::HistoryStatus::Restored,
                                                     cr::HistoryStatus::KeptCurrent, cr::HistoryStatus::Restored, cr::HistoryStatus::Incomplete};
                    const char* scopes[] = {"user", "fixes", "machine", "startup", "machine_fixes", "user"};
                    for (int k = 0; k < 9; ++k) {
                        cr::HistoryTxRow r{scopes[k % 6], "m" + std::to_string(k), 1762000000000 - k * 86400000LL, sts[k % 6], {}};
                        r.steps.push_back({k % 2 ? OperationId::GameModeSet : OperationId::PowerSchemeSet, "x", true, k % 2 ? "disabled" : "381b4222-f694-41f0-9685-ff5bb260df2e",
                                           k % 2 ? "enabled" : "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", cr::StepState::Verified, false});
                        rows.push_back(r);
                    }
                    many.history.view.total = rows.size();
                    many.history.view.corrupt = 0;
                    many.history.now_unix_ms = 1762300000000 + 3600000;  // B13: ilk kayit "Bugun" grubunda
                    cs.push_back({L"history", L"collapsed", Page::History, many});
                    cs.back().history_all_open = false;
                    cs.back().history_open = {history_row_key(rows[0])};
                    cs.back().narrow_png = true;
                    // Spec 2026-11-14 B13: gorunum cipleri — yalniz degisiklikler / yalniz testler.
                    cs.push_back({L"history", L"tab_changes", Page::History, many});
                    cs.back().history_all_open = false;
                    cs.back().tab = HistoryTab::Changes;
                    cs.back().narrow_png = true;
                    cs.push_back({L"history", L"tab_tests", Page::History, vm});
                    cs.back().tab = HistoryTab::Tests;
                    cs.back().narrow_png = cs.back().en_png = true;
                    cs.push_back({L"history", L"filter_startup", Page::History, many});
                    cs.back().history_all_open = false;
                    cs.back().filter = HistoryFilter::Startup;
                    cs.push_back({L"history", L"filter_empty", Page::History, vm});
                    cs.back().filter = HistoryFilter::Fixes;
                    auto none = vm;
                    none.history.view = {};
                    none.history.tests.clear();
                    none.history.tests_total = 0;
                    none.history.selected.clear();
                    none.history.comparison.reset();
                    cs.push_back({L"history", L"empty", Page::History, none});
                }
                // --- Ayarlar ---
                {
                    auto vm = fixture(false);
                    SettingsView some;
                    some.autostart = true;
                    some.games = {{"a", "Counter-Strike 2", true},
                                  {"b", "Microsoft Flight Simulator 2024 — Deluxe Premium Edition (Steam)", false},
                                  {"c", "Ölümsüz Şövalyeler: Çağın Öfkesi", true}};
                    vm.settings = some;
                    cs.push_back({L"settings", L"games", Page::Settings, vm});
                    cs.back().narrow_png = cs.back().en_png = true;
                    vm.settings = SettingsView{};
                    cs.push_back({L"settings", L"default", Page::Settings, vm});
                    // P12-B3: pencerenin acilis durumu — "Gelismis" kapali; yalniz Dil, Guncelleme, Gelismis, Hakkinda.
                    vm.settings = some;
                    cs.push_back({L"settings", L"collapsed", Page::Settings, vm});
                    cs.back().settings_collapsed = true;
                    cs.back().narrow_png = true;
                    vm.settings = SettingsView{};
                    // Spec B7: kaldirma onayi kip penceresinde; sayfada satir alti durum satirlari (paket kaydedildi, kaldirma
                    // notu, baslangic kaydi hatasi) satirlarin hizasini bozmaz.
                    SettingsView lines = some;
                    lines.bundle_done = true;
                    lines.bundle_path = L"C:\\Users\\OrnekKullanici\\Belgeler\\EcuFps\\destek\\EcuFps-support-2026-11-08.zip";
                    lines.autostart = false;
                    lines.autostart_failure = core::settings::AutostartFailure::VerifyFailed;
                    vm.settings = lines;
                    cs.push_back({L"settings", L"status_lines", Page::Settings, vm});
                }
                // --- Guncellemeler (spec 2026-11-12 H): denetlenmedi, denetleniyor, guncel, N bulundu (firmware "Elle
                // denetlenir" dahil), denetlenemedi (cevrimdisi / hata kodu), oyun acik. ---
                {
                    namespace cd = ef::core::drivers;
                    auto vm = fixture(false);
                    cs.push_back({L"updates", L"never", Page::Updates, vm});
                    cs.back().narrow_png = true;
                    auto checking = vm;
                    checking.updates.scan.state = cd::ScanState::Checking;
                    cs.push_back({L"updates", L"checking", Page::Updates, checking});
                    auto up = vm;
                    up.updates.scan.state = cd::ScanState::UpToDate;
                    up.updates.last_check = "15.11.2026 14:32";
                    cs.push_back({L"updates", L"up_to_date", Page::Updates, up});
                    // T17: sorunlu aygitlar blogu (kod 28 + kod 43 ariza, kod 22 bilgi, bilinmeyen kod) ve okunamadi durumu.
                    {
                        auto pd = up;
                        pd.updates.problem_devices_loaded = true;
                        pd.updates.problem_devices.enumerated = true;
                        pd.updates.problem_devices.devices = {{"Realtek PCIe GbE Family Controller", "Net", "PCI#VEN_10EC&DEV_8168", 28},
                                                              {"USB Aygit", "USB", "USB#VID_0000&PID_0002", 43},
                                                              {"Standart PS/2 Klavye", "Keyboard", "ACPI#PNP0303", 77},
                                                              {"Microsoft Basic Display Adapter", "Display", "ROOT#BASICDISPLAY", 22}};
                        cs.push_back({L"updates", L"problem_devices", Page::Updates, pd});
                        auto unread = up;
                        unread.updates.problem_devices_loaded = true;
                        cs.push_back({L"updates", L"problem_devices_unread", Page::Updates, unread});
                    }
                    auto found = vm;
                    {
                        int next_id = 0;
                        const auto mk = [&next_id](std::string title, std::string cls, std::string provider, int y, int m, int d, std::uint64_t size,
                                                   std::string hwid = {}) {
                            cd::DriverUpdate u;
                            // Spec H2: kurulum kimligi (ornek GUID; firmware satirinda da var, "Kur" yine cizilmez).
                            char id[40];
                            std::snprintf(id, sizeof id, "0f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5%02d", ++next_id);
                            u.update_id = id;
                            u.revision = 200 + next_id;
                            u.title = std::move(title);
                            u.driver_class = std::move(cls);
                            u.provider = std::move(provider);
                            u.hardware_id = std::move(hwid);
                            u.version = cd::version_from_title(u.title);
                            u.year = y, u.month = m, u.day = d;
                            if (size) u.size_bytes = size;
                            return u;
                        };
                        cd::SearchOutcome o;
                        o.ok = true;
                        // Ilk ikisi bu bilgisayarda gercek yukseltilmemis aramanin dondurdugu basliklar (2026-11-13).
                        o.updates = {mk("Apple, Inc. USBDevice Driver Update (552.0.0.0)", "OtherHardware", "Apple, Inc.", 2025, 4, 25, 101998),
                                     mk("Microsoft Corporation AudioProcessingObject Driver Update (1.0.4.55794)", "OtherHardware",
                                        "Microsoft Corporation", 2026, 10, 16, 32248281),
                                     mk("Realtek Semiconductor Corp. - Net - 10.72.1203.2025", "Net", "Realtek", 2025, 12, 3, 4194304),
                                     mk("Gigabyte - Firmware - 1.0.0.11", "Firmware", "Gigabyte", 2025, 5, 20, 0, "UEFI\\RES_{11112222-3333-4444-5555-666677778888}")};
                        found.updates.scan = cd::scan_view_from(o);
                    }
                    found.updates.last_check = "15.11.2026 14:32";
                    cs.push_back({L"updates", L"found", Page::Updates, found});
                    cs.back().narrow_png = cs.back().en_png = true;
                    // Spec H2: kurulum suruyor (satir cipi "Kuruluyor…", butun "Kur" ve "Denetle" devre disi), sonuc kartlari
                    // (kuruldu + yeniden baslatma + geri yukleme noktasi yok; basarisiz + kod) ve oyun acikken "Kur" devre disi.
                    {
                        const auto& u0 = found.updates.scan.updates.front();
                        auto installing = found;
                        installing.updates.install.running = true;
                        installing.updates.install.target = cd::install_target(u0.update_id, u0.revision);
                        installing.updates.install.name = cd::display_name(u0);
                        cs.push_back({L"updates", L"installing", Page::Updates, installing});
                        cs.back().narrow_png = true;
                        auto done = found;
                        done.updates.install.target = installing.updates.install.target;
                        done.updates.install.name = installing.updates.install.name;
                        done.updates.install.notice_key = "updates.install.done";
                        done.updates.install.reboot = true;
                        done.updates.install.no_restore_point = true;
                        done.updates.install.warn = true;
                        cs.push_back({L"updates", L"installed_reboot", Page::Updates, done});
                        cs.back().narrow_png = cs.back().en_png = true;
                        auto failed = done;
                        failed.updates.install.notice_key = "updates.install.failed";
                        failed.updates.install.reboot = failed.updates.install.no_restore_point = false;
                        failed.updates.install.hresult = static_cast<std::int32_t>(0x80240022u);
                        cs.push_back({L"updates", L"install_failed", Page::Updates, failed});
                        auto fgame = found;
                        fgame.game_running = true;
                        fgame.game_gate_blocked = true;
                        fgame.game_block_reason_key = "gate.game_running";
                        fgame.game_block_title_key = "gate.game_running_title";
                        cs.push_back({L"updates", L"found_game", Page::Updates, fgame});
                    }
                    // ADR-032: ekran karti satirinin Windows Update onerisi ("Guncelleme var <surum>" + "Indir ve kur"), resmi
                    // paket yolu (kopya / imza denetimi / kurulum / sonuc + yeniden baslatma / yanlis imzalayan) ve yonga seti
                    // (dugmesiz, Windows Update yolu) ile BIOS (yalniz bilgi) satirlari. HC ve 960x540 her durumda cizilir.
                    {
                        auto gpu_up = vm;
                        cd::DriverUpdate nv;
                        nv.title = "NVIDIA - Display - 32.0.15.7000";
                        nv.driver_class = "Display";
                        nv.provider = "NVIDIA";
                        nv.hardware_id = "PCI\\VEN_10DE&DEV_2C02&SUBSYS_51761458";
                        nv.version = cd::version_from_title(nv.title);
                        nv.year = 2026, nv.month = 10, nv.day = 28;
                        nv.size_bytes = 912ull << 20;
                        nv.update_id = "1f6f7e1a-1b2c-4d5e-8f90-a1b2c3d4e5f6";
                        nv.revision = 1;
                        cd::SearchOutcome o;
                        o.ok = true;
                        o.updates = {nv};
                        gpu_up.updates.scan = cd::scan_view_from(o);
                        gpu_up.updates.last_check = "15.11.2026 14:32";
                        cs.push_back({L"updates", L"gpu_update", Page::Updates, gpu_up});
                        cs.back().narrow_png = cs.back().en_png = true;
                        auto copying = up;
                        copying.updates.local.stage = ViewModel::UpdatesView::LocalView::Stage::Copying;
                        copying.updates.local.gpu = 0;
                        copying.updates.local.name = "591.44-desktop-win10-win11-64bit-international-dch-whql.exe";
                        copying.updates.local.done = 377ull << 20;
                        copying.updates.local.total = 912ull << 20;
                        cs.push_back({L"updates", L"local_copying", Page::Updates, copying});
                        cs.back().narrow_png = cs.back().en_png = true;
                        auto verifying = copying;
                        verifying.updates.local.stage = ViewModel::UpdatesView::LocalView::Stage::Verifying;
                        cs.push_back({L"updates", L"local_verifying", Page::Updates, verifying});
                        auto linst = up;
                        linst.updates.install.running = true;
                        linst.updates.install.target = cd::vendor_target(cd::GpuVendor::Nvidia, "0123456789abcdef");
                        linst.updates.install.name = copying.updates.local.name;
                        cs.push_back({L"updates", L"local_installing", Page::Updates, linst});
                        cs.back().narrow_png = true;
                        auto ldone = up;
                        ldone.updates.install.target = linst.updates.install.target;
                        ldone.updates.install.name = linst.updates.install.name;
                        ldone.updates.install.notice_key = "updates.local.done_unread";
                        ldone.updates.install.reboot = true;
                        cs.push_back({L"updates", L"local_done_reboot", Page::Updates, ldone});
                        cs.back().en_png = true;
                        auto lver = ldone;
                        lver.updates.install.notice_key = "updates.local.done";
                        lver.updates.install.reboot = false;
                        lver.updates.install.version = "32.0.15.7000";
                        lver.updates.gpus[0].driver_version = "32.0.15.7000";
                        cs.push_back({L"updates", L"local_done", Page::Updates, lver});
                        auto lsig = up;
                        lsig.updates.install.name = linst.updates.install.name;
                        lsig.updates.install.notice_key = "updates.local.wrong_signer";
                        lsig.updates.install.warn = true;
                        cs.push_back({L"updates", L"local_bad_signature", Page::Updates, lsig});
                        cs.back().narrow_png = cs.back().en_png = true;
                        auto lfail = lsig;
                        lfail.updates.install.target = linst.updates.install.target;
                        lfail.updates.install.notice_key = "updates.local.failed";
                        lfail.updates.install.exit_code = 17;
                        cs.push_back({L"updates", L"local_failed", Page::Updates, lfail});
                    }
                    auto offline = vm;
                    offline.updates.scan.state = cd::ScanState::Failed;
                    offline.updates.scan.failure = cd::ScanFailure::Offline;
                    offline.updates.scan.hresult = static_cast<std::int32_t>(0x8024402Cu);
                    offline.updates.last_check = "15.11.2026 14:32";
                    cs.push_back({L"updates", L"failed_offline", Page::Updates, offline});
                    auto err = offline;
                    err.updates.scan.failure = cd::ScanFailure::Error;
                    err.updates.scan.hresult = static_cast<std::int32_t>(0x80240438u);
                    cs.push_back({L"updates", L"failed_error", Page::Updates, err});
                    cs.back().en_png = true;
                    auto game = vm;
                    game.game_running = true;
                    game.game_gate_blocked = true;
                    game.game_block_reason_key = "gate.game_running";
                    game.game_block_title_key = "gate.game_running_title";
                    cs.push_back({L"updates", L"game", Page::Updates, game});
                }

                const float pad = theme.card_pad, cg = theme.card_gap;
                const auto center_y = [](const RectF& r) { return r.y + r.h / 2; };
                const auto l4_checks = [&](const L4Case& c, const Palette* pal, float sc, float w, float h, const UiState& st, const HitMap& hits,
                                           bool strip_required) {
                    const auto bad = [&](const wchar_t* what) { fail(c.page, c.name, pal, sc, w, h, what); };
                    const float body_w = std::min(w, theme.content_max_w) - 2 * theme.page_margin;
                    const float body_x = (w - std::min(w, theme.content_max_w)) / 2 + theme.page_margin;
                    const auto& vm = c.vm;
                    const Hit strip_text = hit_of(HitKind::Status, static_cast<std::size_t>(kMaxStatusItems) - 1);
                    if (c.pg == Page::Measurements) {
                        const RectF* src = hits.rect_of(Hit::SourceBuiltin);
                        const RectF* src2 = hits.rect_of(Hit::SourceGame);
                        const RectF* start = hits.rect_of(Hit::TestStart);
                        if (!start) start = hits.rect_of(Hit::TestStop);
                        const RectF* speed = hits.rect_of(Hit::SpeedStart);
                        if (!speed) speed = hits.rect_of(Hit::SpeedStop);
                        const RectF* result = hits.rect_of(static_cast<Hit>(static_cast<int>(Hit::Status0) + 2));
                        if (!src || !src2 || !speed || !start || !result) {
                            bad(L"top row controls missing");
                            return;
                        }
                        // BOOST maketi (M1, 2026-11-16): baslat / durdur kucuk dugmesi Test kartinin alt satirinda, kaynak dugmelerinin altinda;
                        // sol sutunda kalir (yuvarlak buyuk dugme kalkti).
                        if (start->x < src->right() && start->y < src->bottom()) bad(L"start button overlaps the source buttons");  // C: baslat dugmesi serit sag ucunda
                        if (start->h > theme.control_max_h + 0.5f) bad(L"start button size");
                        // Secenekler (maketteki "Test ayarlari" paneli): ust satirin altinda tam genislik kartta, her zaman acik.
                        const float top_bottom = src->y - 0.5f;  // sahip duzeni (C): Kapsam/Sure/Depolama kaynak dugmeleriyle ayni seritte (sigmazsa altinda)
                        // B10: "Test ayarlari" acilir bolumu yok. Uygulama Testi'nde "Sure" (Hizli / Tam) ve Genel kapsamda birim
                        // varken "Depolama testi" (Yapma + birimler) Test kartinin icinde, kaynak / kapsam haplarinin altinda; secili
                        // deger UIA Selected; test islerken devre disi. Oyunda Test'te bu satirlar yok.
                        const bool locked = cm::busy(vm.measure) || vm.measure.endurance.running;
                        float card_low = top_bottom;  // en alttaki secenek denetimi
                        if (const RectF* sg = hits.rect_of(Hit::ScopeGeneral)) card_low = std::max(card_low, sg->bottom());
                        const auto in_card = [&](const RectF& r) { return r.x >= body_x - 0.5f && r.right() <= body_x + body_w + 0.5f; };
                        {
                            const auto mq = hits.meta.find(Hit::ModeQuick);
                            const auto mf = hits.meta.find(Hit::ModeFull);
                            const RectF* rq = hits.rect_of(Hit::ModeQuick);
                            if (vm.measure.source == cm::TestSource::Builtin) {
                                if (mq == hits.meta.end() || mf == hits.meta.end() || !rq) bad(L"duration row missing in test card");
                                else {
                                    if (rq->y < top_bottom - 0.5f || !in_card(*rq)) bad(L"duration row placement");
                                    if (mq->second.role != AccRole::RadioButton || mq->second.selected == (vm.measure.mode == cm::TestMode::Full) ||
                                        mf->second.selected != (vm.measure.mode == cm::TestMode::Full) || mq->second.enabled == locked)
                                        bad(L"duration row state");
                                    card_low = std::max(card_low, rq->bottom());
                                    if (const RectF* rf = hits.rect_of(Hit::ModeFull)) card_low = std::max(card_low, rf->bottom());
                                }
                                const bool want_storage = !vm.measure.storage_roots.empty();  // Darbogaz kapsaminda da gorunur (devre disi)
                                const auto s0 = hits.meta.find(hit_of(HitKind::StorageChoice, 0));
                                if (want_storage != (s0 != hits.meta.end())) bad(L"storage row presence");
                                else if (want_storage) {
                                    const bool off_scope = vm.measure.scope == cm::BuiltinScope::Balance;
                                    if (s0->second.selected != (!off_scope && vm.measure.storage_choice == 0) || s0->second.enabled == (locked || off_scope)) bad(L"storage row state");
                                    for (std::size_t k = 0; k <= vm.measure.storage_roots.size(); ++k)
                                        if (const RectF* r = hits.rect_of(hit_of(HitKind::StorageChoice, k))) {
                                            if (r->y < top_bottom - 0.5f || !in_card(*r)) bad(L"storage row placement");
                                            card_low = std::max(card_low, r->bottom());
                                        }
                                }
                            } else if (mq != hits.meta.end() || hits.meta.count(hit_of(HitKind::StorageChoice, 0))) {
                                bad(L"test options drawn for in-game source");
                            }
                            if (const RectF* lv = hits.rect_of(Hit::LiveView)) card_low = std::max(card_low, lv->bottom());  // isabet kutusu satir boyunda
                            // B10 inceleme: secenek notlari (birim secildiyse yazma + ek sure notu, Darbogaz'da karsilastirma
                            // notu) kartin en alt ogesidir; kartin icinde kalir ve hizalama denetimine girer.
                            const bool want_note = vm.measure.source == cm::TestSource::Builtin &&
                                                   ((vm.measure.scope == cm::BuiltinScope::General && vm.measure.storage_choice > 0 &&
                                                     vm.measure.storage_choice <= vm.measure.storage_roots.size()) ||
                                                    (vm.measure.scope == cm::BuiltinScope::Balance && !vm.measure.storage_roots.empty()));
                            if (want_note != st.measure_option_note) bad(L"test card option note presence");
                            if (st.measure_option_note) {
                                // BOOST maketi: secenekler yan yana kutularda; not kendi kutusunun denetiminin altindadir (butun kartin en
                                // altinda degil). Denge kapsaminda canli ekran anahtari, Genel kapsamda depolama cipleri.
                                float own = -1e9f;  // sayfa kaydirilinca koordinatlar negatif olabilir
                                for (std::size_t k = 0; k <= vm.measure.storage_roots.size(); ++k)
                                    if (const RectF* r = hits.rect_of(hit_of(HitKind::StorageChoice, k))) own = std::max(own, r->bottom());
                                if (st.measure_option_note_bottom < own - 0.5f || st.measure_option_note_bottom < top_bottom) bad(L"test card option note above the options");
                                card_low = std::max(card_low, st.measure_option_note_bottom);
                            }
                        }
                        // Ust satir: uc es kart Test | Sonuc | Internet hizi (genis) / Test | Sonuc, altinda Internet hizi (orta) /
                        // alt alta (dar).
                        const int tcols = std::min(measure_top_columns(body_w, cg), 2);  // sahip duzeni: ust satir hep Test | Sonuc, Internet hizi altinda
                        if (tcols == 3) {
                            const float cw = std::floor((body_w - 2 * cg) / 3);
                            if (src->x < body_x || src->right() > body_x + cw + 0.5f || start->right() > body_x + cw + 0.5f) bad(L"test card is not the first column");
                            if (result->x < body_x + cw + cg - 0.5f || result->right() > body_x + 2 * cw + cg + 0.5f) bad(L"result card is not the second column");
                            if (speed->x < body_x + 2 * (cw + cg) - 0.5f) bad(L"speed card is not the third column");
                            // Es kartlarda en alt ogeler (kart ici secenekler + notlari / dugme) ayni hizada; not kart disina tasmaz.
                            if (std::fabs(start->bottom() - speed->bottom()) > 0.5f) bad(L"top row bottoms not aligned");
                        } else if (tcols == 2) {
                            const float cw = std::floor((body_w - cg) / 2);
                            if (src->right() > body_x + body_w + 0.5f || result->y < start->bottom() || result->right() > body_x + cw + 0.5f || speed->y <= result->y) bad(L"two column top row");  // C: serit tam genislik; altinda Sonuc | Darbogaz; sonra Internet hizi
                        } else if (result->y <= start->y || speed->y <= result->y) {
                            bad(L"stacked top row order");
                        }
                        // B15 (sahip onayli maket): Sonuc karti. Ust oge (Status0+2) baslik + ortalama FPS (+ fark); uc kutu
                        // (%1 dusuk FPS, ortalama kare ms, kare p99 ms) ayri metin ogeleri. Fark cipi YALNIZ karsilastirilabilir
                        // onceki test varken; kare suresi grafigi YALNIZ kayitta gercek saniye serisi varken; Islemci / Ekran
                        // karti kutulari Uygulama Testi sonucunda bilesen raporundan.
                        {
                            const auto& rn = hits.meta.find(static_cast<Hit>(static_cast<int>(Hit::Status0) + 2))->second.name;
                            const auto str = [&](i18n::StringId id) { return wide(cat.value().get(id)); };
                            const bool busy_now = cm::busy(vm.measure);
                            const cm::ResultView* rv = vm.measure.result && !busy_now ? &*vm.measure.result : nullptr;
                            const bool numbers = busy_now || (rv && !rv->balance);
                            if (numbers && rn.find(str(i18n::StringId::MeasureResultAvgLabel)) == std::wstring::npos) bad(L"result card average missing in UIA");
                            if (numbers)
                                for (const auto id : {i18n::StringId::MeasureResultLow1, i18n::StringId::MeasureResultFrameAvg, i18n::StringId::MeasureResultFrameP99})
                                    if (!has_text_item(hits, str(id) + L" ", 1)) bad(L"result card tile missing in UIA");
                            if (!vm.measure.result && !busy_now && rn.find(str(i18n::StringId::MeasureResultNone)) == std::wstring::npos)
                                bad(L"result card empty line");
                            if (busy_now && rn.find(str(i18n::StringId::MeasureResultMeasuring)) == std::wstring::npos)
                                bad(L"result card measuring state");
                            const auto& cmp = vm.measure.comparison;
                            const bool want_delta = rv && !rv->balance && rv->source != cm::TestSource::Endurance && cmp.available && cmp.comparable &&
                                                    cmp.delta_percent && std::isfinite(*cmp.delta_percent);
                            // B15 inceleme: yon de denetlenir. Sablonun {pct} oncesi + sonrasi (yuzde metni en cok 12 karakter) UIA
                            // metninde aranir; TR'de artis/dusus oneki ayni oldugu icin sonek ayirt eder. Beklenen yon cizimle ayni
                            // kural: bir ondaliga yuvarlanan |fark| 0 ise duz, degilse isarete gore artis/dusus.
                            const auto delta_in = [&](i18n::StringId id) {
                                const auto tpl = str(id);
                                const auto at = tpl.find(L"{pct}");
                                if (at == std::wstring::npos) return rn.find(tpl) != std::wstring::npos;
                                const auto pre = tpl.substr(0, at), post = tpl.substr(at + 5);
                                for (auto p0 = rn.find(pre); p0 != std::wstring::npos; p0 = rn.find(pre, p0 + 1)) {
                                    const auto q = rn.find(post, p0 + pre.size());
                                    if (q != std::wstring::npos && q - (p0 + pre.size()) >= 2 && q - (p0 + pre.size()) <= 12) return true;
                                }
                                return false;
                            };
                            const bool d_up = delta_in(i18n::StringId::MeasureResultDeltaUpAcc);
                            const bool d_down = delta_in(i18n::StringId::MeasureResultDeltaDownAcc);
                            const bool d_flat = delta_in(i18n::StringId::MeasureResultDeltaFlatAcc);
                            if (want_delta != (d_up || d_down || d_flat)) bad(L"result card delta chip presence");
                            if (want_delta) {
                                const double d = *cmp.delta_percent;
                                const bool flat = std::round(std::fabs(d) * 10.0) == 0.0;
                                if (d_up != (!flat && d > 0) || d_down != (!flat && d < 0) || d_flat != flat) bad(L"result card delta chip direction");
                            }
                            std::size_t series = 0;
                            if (rv && !rv->balance)
                                for (const double f : rv->fps_series) series += std::isfinite(f) && f > 0 ? 1 : 0;
                            if ((series >= 2) != has_text_item(hits, str(i18n::StringId::MeasureResultFrameChart), 1)) bad(L"frame time chart presence");
                            // Eski kare suresi karti (ayri kart) yok: "Kare suresi" ile baslayan her metin ogesi hata; yalniz sonuc
                            // kartindaki yeni grafik basligi (MeasureResultFrameChart ile baslayan) haric.
                            {
                                const auto old_ft = str(i18n::StringId::MeasureFrameTime), chart_ft = str(i18n::StringId::MeasureResultFrameChart);
                                for (const auto& [hid, meta] : hits.meta)
                                    if (meta.role == AccRole::Text && meta.name.starts_with(old_ft) && !meta.name.starts_with(chart_ft)) {
                                        bad(L"frame time card drawn");
                                        break;
                                    }
                            }
                            const bool want_comp = rv && !rv->balance && rv->source == cm::TestSource::Builtin && !vm.components_loading &&
                                                   std::any_of(vm.components.begin(), vm.components.end(),
                                                               [](const auto& f) { return f.component == core::diagnosis::Component::Cpu; });
                            if (want_comp != has_text_item(hits, str(i18n::StringId::MeasureResultCpu) + L": ", 1)) bad(L"result card CPU tile presence");
                            const bool want_gpu = rv && !rv->balance && rv->source == cm::TestSource::Builtin && !vm.components_loading &&
                                                  std::any_of(vm.components.begin(), vm.components.end(),
                                                              [](const auto& f) { return f.component == core::diagnosis::Component::Gpu; });
                            if (want_gpu != has_text_item(hits, str(i18n::StringId::MeasureResultGpu) + L": ", 1)) bad(L"result card GPU tile presence");
                        }
                        // Son 3 test: en cok uc satir; satir dugmesi Selected = ayrinti acik; acik satirin ayrintisi satirin altinda.
                        {
                            const std::size_t n = std::min<std::size_t>(vm.measure.history.size(), static_cast<std::size_t>(kMaxMeasureRecent));
                            for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxMeasureRecent); ++k) {
                                const Hit rid = hit_of(HitKind::MeasureRecent, k);
                                const auto it = hits.meta.find(rid);
                                if ((k < n) != (it != hits.meta.end())) bad(L"recent test row count");
                                else if (it != hits.meta.end() && (it->second.role != AccRole::Expander || it->second.selected != (st.measure_recent_open == static_cast<int>(k))))
                                    bad(L"recent test row state");
                            }
                            if (!has_text_item(hits, wide(cat.value().get(i18n::StringId::MeasureResultDetail)), 1) == (st.measure_recent_open >= 0 && st.measure_recent_open < static_cast<int>(n)))
                                bad(L"recent test details state");
                        }
                        // Spec 2026-11-12 A5 + C: kapsam haplari yalniz Uygulama Testi seciliyken; kaynak sirasinin altinda, tek
                        // satirda, UIA'da secili kapsam Selected.
                        {
                            const RectF* sg = hits.rect_of(Hit::ScopeGeneral);
                            const RectF* sb = hits.rect_of(Hit::ScopeBalance);
                            const auto ig = hits.meta.find(Hit::ScopeGeneral);
                            const auto ib = hits.meta.find(Hit::ScopeBalance);
                            if (vm.measure.source == cm::TestSource::Builtin) {
                                const bool balance = vm.measure.scope == cm::BuiltinScope::Balance;
                                if (!sg || !sb || ig == hits.meta.end() || ib == hits.meta.end()) bad(L"scope chips missing");
                                else if (sg->y < top_bottom - 0.5f || sb->x < sg->right() || std::fabs(sg->y - sb->y) > 0.5f) bad(L"scope chips placement");
                                else if (ig->second.selected == balance || ib->second.selected != balance) bad(L"scope selection state");
                                else if (ig->second.role != AccRole::RadioButton || ig->second.enabled == (cm::busy(vm.measure) || vm.measure.endurance.running))
                                    bad(L"scope chip role / enabled state");
                                
                                // Spec 2026-11-12 A4 + B10: "Canli test ekrani" anahtari yalniz Denge testi kapsaminda, Test kartinin
                                // icinde; varsayilan acik; test islerken devre disi (onizleme acik / kapali kosunun kosuludur).
                                {
                                    if (hits.meta.count(Hit::LiveView)) bad(L"live view toggle must not be drawn");  // her zaman acik
                                }
                            } else if (sg || sb) {
                                bad(L"scope chips drawn for in-game source");
                            }
                        }
                        // Darbogaz (spec 2026-11-12 C / D): uc es kutu (UIA metni "bu test sahnesinde" kapsamiyla), ayni boyda; yan
                        // yana (ayni ust kenar, soldan saga) ya da dar kartta alt alta. Birincil ekrana denk gelen kutu "senin ekranin".
                        const cm::BalanceView* bvp = vm.measure.result && vm.measure.result->balance ? &*vm.measure.result->balance
                                                     : vm.measure.balance_latest                       ? &*vm.measure.balance_latest
                                                                                                       : nullptr;
                        if (bvp) {
                            const auto& bv = *bvp;
                            const std::wstring cpu_bar = wide(cat.value().get(i18n::StringId::BalanceCardCpuBar));
                            const std::wstring lead = wide(cat.value().get(i18n::StringId::BalanceCardRowAcc));
                            const std::wstring scoped = lead.substr(0, lead.find(L'{'));  // "Bu test sahnesinde " / "In this test scene at "
                            const std::wstring yours = wide(cat.value().get(i18n::StringId::MeasureBottleneckYourDisplay));
                            std::vector<RectF> rows;
                            bool all_scoped = true;
                            int mine = 0;
                            for (const auto& [id, r] : hits.items) {
                                const auto it = hits.meta.find(id);
                                if (it == hits.meta.end() || it->second.role != AccRole::Text || it->second.name.find(cpu_bar) == std::wstring::npos) continue;
                                rows.push_back(r);
                                all_scoped = all_scoped && !scoped.empty() && it->second.name.starts_with(scoped);
                                mine += it->second.name.find(yours) != std::wstring::npos ? 1 : 0;
                            }
                            if (rows.size() != bv.rows.size()) bad(L"bottleneck tiles missing");
                            else {
                                if (!all_scoped) bad(L"bottleneck tile text is not scoped to the test scene");
                                std::sort(rows.begin(), rows.end(), [](const RectF& a, const RectF& b) { return a.y < b.y || (a.y == b.y && a.x < b.x); });
                                const float bal_w = std::min(measure_top_columns(body_w, cg), 2) >= 2 ? std::ceil((body_w - cg) / 2) : body_w;  // C: Darbogaz karti Sonuc ile yan yana (yari genislik)
                                const bool side = bottleneck_tile_columns(bal_w - 2 * 15.f, theme.space(2)) == 3;
                                for (std::size_t k = 1; k < rows.size(); ++k) {
                                    if (std::fabs(rows[k].h - rows[0].h) > 0.5f || std::fabs(rows[k].w - rows[0].w) > 0.5f) bad(L"bottleneck tiles unequal");
                                    if (side ? (std::fabs(rows[k].y - rows[0].y) > 0.5f || rows[k].x < rows[k - 1].right())
                                             : (std::fabs(rows[k].x - rows[0].x) > 0.5f || rows[k].y < rows[k - 1].bottom()))
                                        bad(L"bottleneck tiles arrangement");
                                }
                            }
                            if (mine != (balance_display_row(vm.display) ? 1 : 0)) bad(L"your display highlight state");
                            if (!has_text_item(hits, wide(cat.value().get(i18n::StringId::BalanceCardScopeNote)), 2)) bad(L"balance scope note missing in UIA");
                            if (!has_text_item(hits, wide(cat.value().get(i18n::StringId::PanelBottleneckTitle)), 1)) bad(L"bottleneck title text missing");
                            const bool tips = bv.has(BalV::CpuLimited) || bv.has(BalV::GpuLimited);
                            if (tips != has_text_item(hits, wide(cat.value().get(i18n::StringId::BalanceCardTipLabel)), 2)) bad(L"balance tip label state");
                            if (!bv.global_reasons.empty() && !has_text_item(hits, wide(cat.value().get(i18n::StringId::BalanceReasonOnBattery)), 2) &&
                                !has_text_item(hits, wide(cat.value().get(i18n::StringId::BalanceReasonCpuStepInvalid)), 2))
                                bad(L"balance global reason text missing");
                        } else if (has_text_item(hits, wide(cat.value().get(i18n::StringId::BalanceCardScopeNote)), 2)) {
                            bad(L"bottleneck section drawn without a balance result");
                        }
                    } else if (c.pg == Page::Cleanup) {
                        const auto& cv = vm.cleanup;
                        const RectF* chip = hits.rect_of(strip_text);
                        if (strip_required) {
                            // Spec 2026-11-12 B4: alt sekme yok; zorunlu cip seridin sag yuvasinda.
                            const auto it = hits.meta.find(strip_text);
                            if (!chip || it == hits.meta.end() || it->second.name != wide(cat.value().get(i18n::StringId::CleanupIrreversible)))
                                bad(L"irreversible chip missing in header strip");
                            else if (hits.rect_of(Hit::SysTabCleanup)) bad(L"old sub-tab still drawn");
                        }
                        // Spec 2026-11-12 B4: kategoriler iki sutun kutu (dar govdede tek sutun); anahtar adi aciklamayi da tasir.
                        // Spec 2026-11-14 B12: bolum kendi kartinda (izgara kartin ic kenarinda); uzun liste kapaliyken ilk
                        // kCleanupCollapsedRows satir + secili kategoriler gorunur, kalanlar "+N kategori daha" dugmesinin
                        // (UIA Expander, HelpText'te adlari) arkasinda.
                        const float in_x = body_x + pad, in_w = body_w - 2 * pad;
                        const float col_gap = theme.space(2);
                        const int cols = systools_columns(in_w, col_gap, 2, kSysToolsTileMinW);
                        const float cw = (in_w - col_gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
                        std::size_t last_needed = cv.rows.size();
                        for (std::size_t k = 0; k < cv.rows.size(); ++k)
                            if (cv.rows[k].selected || (cv.files_open && *cv.files_open == cv.rows[k].category)) last_needed = k;
                        const std::size_t ucols = static_cast<std::size_t>(cols);
                        (void)last_needed;
                        (void)ucols;
                        const std::size_t shown = cv.rows.size();  // BOOST maketi: tum kategoriler gorunur, "+N daha" dugmesi yok
                        const bool collapsible = false;
                        const RectF* more = hits.rect_of(Hit::CleanMore);
                        const auto more_it = hits.meta.find(Hit::CleanMore);
                        if (collapsible != (more != nullptr)) bad(L"more-categories button presence");
                        if (more && (more_it == hits.meta.end() || more_it->second.role != AccRole::Expander || more_it->second.selected != st.cleanup_more_open))
                            bad(L"more-categories button is not an expander with the open state");
                        if (more && !st.cleanup_more_open && more_it != hits.meta.end() && more_it->second.help.empty()) bad(L"hidden category names not in HelpText");
                        if (more && std::fabs(more->x - in_x) > 0.5f) bad(L"more-categories button off the grid edge");
                        for (std::size_t k = 0; k < cv.rows.size(); ++k) {
                            const Hit id = hit_of(HitKind::CleanToggle, k);
                            const RectF* r = hits.rect_of(id);
                            const auto it = hits.meta.find(id);
                            if (k >= shown) {
                                if (r) bad(L"collapsed category still drawn");
                                continue;
                            }
                            if (!r || it == hits.meta.end()) {
                                bad(L"category toggle missing");
                                continue;
                            }
                            if (more && r->bottom() > more->y) bad(L"more-categories button above a category");
                            const float want_x = in_x + theme.space(2) + static_cast<float>(k % static_cast<std::size_t>(cols)) * (cw + col_gap) - 4.f;
                            if (std::fabs(r->x - want_x) > 1.f) bad(L"category toggle off the column grid");
                            if (cols == 2 && k % 2 == 1) {
                                const RectF* l = hits.rect_of(hit_of(HitKind::CleanToggle, k - 1));
                                if (l && std::fabs(l->y - r->y) > 0.5f) bad(L"category pair not on one row");
                            }
                            const auto& inf = cc::info(cv.rows[k].category);
                            const auto desc_id = i18n::id_of(inf.desc_key);
                            if (!desc_id || it->second.name.find(wide(cat.value().get(*desc_id))) == std::wstring::npos) bad(L"category description not in the name");
                        }
                        // Spec 2026-11-14 B12: Onizleme ve Temizle kart baslik seridinde, ayni satirda, kartin ic sag kenarina
                        // kadar; kutular altta.
                        const RectF* scan = hits.rect_of(Hit::CleanScan);
                        const RectF* run = hits.rect_of(Hit::CleanRun);
                        if (cv.phase == cc::SessionPhase::Ready && cv.selected_bytes > 0) {
                            if (!scan || !run) bad(L"action row buttons missing");
                            else if (scan->right() > run->x || std::fabs(scan->y - run->y) > 0.5f || run->right() > in_x + in_w + 0.5f)
                                bad(L"action row order");
                            if (run && !cv.rows.empty()) {
                                const RectF* first = hits.rect_of(hit_of(HitKind::CleanToggle, 0));
                                if (first && run->bottom() > first->y) bad(L"action row below the category tiles");
                            }
                        }
                    } else if (c.pg == Page::Startup) {
                        const auto& sv = vm.startup;
                        const RectF* first = nullptr;
                        // Spec 2026-11-12 B4: iki sutun satir (dar govdede tek sutun). B12: kendi kartinda (kartin ic kenari).
                        const float in_x = body_x + pad, in_w = body_w - 2 * pad;
                        const float scg = theme.space(4);
                        const int scols = systools_columns(in_w, scg, 2, kSysToolsTileMinW);
                        const float scw = (in_w - scg * static_cast<float>(scols - 1)) / static_cast<float>(scols);
                        for (std::size_t k = 0; k < sv.rows.size(); ++k) {
                            const RectF* r = hits.rect_of(hit_of(HitKind::StartupToggle, k));
                            if (!r) {
                                bad(L"startup toggle missing");
                                continue;
                            }
                            // BOOST maketi: girdi cukuru (p-3) icinde; anahtar cukurun 12 DIP iceri girintisinde.
                            const float want_x = in_x + static_cast<float>(k % static_cast<std::size_t>(scols)) * (scw + scg) + 12.f - 4.f;
                            if (std::fabs(r->x - want_x) > 1.f) bad(L"startup toggle off the table column");
                            if (!first) first = r;
                        }
                        const RectF* apply = hits.rect_of(Hit::StartupApply);
                        const bool want_apply = sv.phase == ef::core::startup::StartupPhase::Ready && sv.pending > 0;
                        if (want_apply != (apply != nullptr)) bad(L"apply button presence");
                        // "Uygula" kart baslik seridinde, kartin ic sag kenarinda (baslik eylemi boyutu: button_h).
                        // BOOST maketi: "Uygula" bolum basligi satirinda (kartin disinda, sag kenara hizali), kartin ustunde.
                        if (apply && (std::fabs(apply->right() - (body_x + body_w)) > 0.5f || std::fabs(apply->h - theme.button_h) > 0.5f ||
                                      (first && apply->bottom() > first->y)))
                            bad(L"apply button placement");
                    } else if (c.pg == Page::Updates) {
                        // Spec H: ust kartta tek dugme (Denetle / Yeniden dene ya da arama surerken Durdur); oyun acikken Denetle
                        // devre disi. Satirlar es yukseklikte; satir dugmeleri ayni sag kenarda. Firmware satiri "Elle
                        // denetlenir" tasir, "Guncelleme var" degil. Basarisiz arama satir uretmez ("Guncel" denmez).
                        // ADR-032: uretici satirlari disari yonlendirmez (UpdatesLink hic cizilmez). Ekran karti satiri eslesen
                        // Windows Update onerisinde "Guncelleme var <surum>" + "Indir ve kur", yoksa durum cipi + (taninan
                        // ureticide) "Dosyadan kur"; yonga seti eslesme yoksa dugmesiz; BIOS yalniz bilgi ("BIOS guncellemesi bu
                        // uygulamadan yapilmaz" satirda, eski "Elle denetlenir" UIA HelpText'te).
                        namespace cd = ef::core::drivers;
                        using LS = ViewModel::UpdatesView::LocalView::Stage;
                        const bool checking = vm.updates.scan.state == cd::ScanState::Checking;
                        const RectF* chk = hits.rect_of(Hit::UpdatesCheck);
                        const RectF* stop = hits.rect_of(Hit::UpdatesCancel);
                        if (checking ? (!stop || chk) : (!chk || stop)) bad(L"updates top button");
                        // Spec H2: kurulum (ve ADR-032 paket hazirligi) surerken de "Denetle" devre disi.
                        const bool installing = vm.updates.install.running;
                        const bool preparing = vm.updates.local.stage != LS::None;
                        if (chk && hits.meta.at(Hit::UpdatesCheck).enabled != (!vm.game_gate_blocked && !installing && !preparing))
                            bad(L"updates check enabled under the game gate or during an install");
                        const auto& cv = cat.value();
                        const std::wstring manual = wide(cv.get(i18n::StringId::UpdatesChipManual));
                        const std::wstring avail = wide(cv.get(i18n::StringId::UpdatesChipAvailable));
                        const std::wstring running_chip = wide(cv.get(i18n::StringId::UpdatesInstallRunning));
                        const std::wstring info_chip = wide(cv.get(i18n::StringId::UpdatesChipInfo));
                        const std::wstring bios_note = wide(cv.get(i18n::StringId::UpdatesBiosNoUpdate));
                        const std::wstring dl_label = wide(cv.get(i18n::StringId::UpdatesDownloadInstall));
                        const std::wstring pick_label = wide(cv.get(i18n::StringId::UpdatesLocalPick));
                        const std::wstring stop_label = wide(cv.get(i18n::StringId::UpdatesCancel));
                        const auto vrows = updates_vendor_rows(vm);
                        std::vector<std::size_t> wu_list;
                        if (vm.updates.scan.state == cd::ScanState::Found)
                            for (std::size_t i = 0; i < vm.updates.scan.updates.size(); ++i) {
                                bool merged = false;
                                for (const auto& vr : vrows) merged = merged || vr.wu == std::optional<std::size_t>(i);
                                if (!merged) wu_list.push_back(i);
                            }
                        const std::size_t want_rows = wu_list.size() + vrows.size();
                        float row_h = -1, link_right = -1;
                        std::size_t got = 0;
                        const bool can_act = !installing && !preparing && !vm.game_gate_blocked;
                        const auto check_btn = [&](Hit bid, const RectF* r, bool want_enabled, const wchar_t* what) {
                            const RectF* br = hits.rect_of(bid);
                            if (!br) return bad((std::wstring(L"updates ") + what + L" button missing").c_str());
                            if (hits.meta.at(bid).enabled != want_enabled) bad((std::wstring(L"updates ") + what + L" enabled state").c_str());
                            if (std::fabs((br->y + br->h / 2) - (r->y + r->h / 2)) > 1.f) bad((std::wstring(L"updates ") + what + L" button not centred in row").c_str());
                            if (link_right < 0) link_right = br->right();
                            if (std::fabs(br->right() - link_right) > 0.5f) bad(L"updates row buttons not aligned");
                            if (br->right() > body_x + body_w + 0.5f) bad(L"updates row button outside the column");
                        };
                        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxUpdatesRows); ++k) {
                            const Hit id = hit_of(HitKind::UpdatesRow, k);
                            const RectF* r = hits.rect_of(id);
                            if (!r) break;
                            ++got;
                            if (row_h < 0) row_h = r->h;
                            if (std::fabs(r->h - row_h) > 0.5f) bad(L"updates rows unequal");
                            const auto& name = hits.meta.at(id).name;
                            if (k < wu_list.size()) {
                                const std::size_t ui_k = wu_list[k];
                                const auto& u = vm.updates.scan.updates[ui_k];
                                const bool fw = cd::is_firmware(u);
                                const std::string tgt = fw ? std::string() : cd::install_target(u.update_id, u.revision);
                                const bool is_running = installing && !tgt.empty() && tgt == vm.updates.install.target;
                                if (is_running) {
                                    if (name.find(running_chip) == std::wstring::npos) bad(L"updates installing row chip");
                                } else if (fw != (name.find(manual) != std::wstring::npos) || fw == (name.find(avail) != std::wstring::npos)) {
                                    bad(L"updates firmware row chip");
                                }
                                // Spec H2: "Kur" yalniz kimligi okunan, firmware olmayan oneride; tek kurulum / oyun / arama
                                // sirasinda devre disi; satirin dikey ortasinda ve diger satir dugmeleriyle ayni sag kenarda.
                                const Hit iid = hit_of(HitKind::UpdatesInstall, ui_k);
                                if (tgt.empty() != (hits.rect_of(iid) == nullptr)) bad(L"updates install button presence");
                                if (!tgt.empty()) check_btn(iid, r, can_act && !checking, L"install");
                                continue;
                            }
                            const auto& vr = vrows[k - wu_list.size()];
                            if (vr.kind == UpdatesVendorKind::Bios) {
                                if (name.find(info_chip) == std::wstring::npos || name.find(bios_note) == std::wstring::npos) bad(L"updates BIOS row is not info only");
                                if (hits.meta.at(id).help.find(manual) == std::wstring::npos) bad(L"updates BIOS old text missing from UIA");
                                continue;
                            }
                            if (name.find(manual) != std::wstring::npos) bad(L"updates vendor row still manual");
                            if (vr.wu) {
                                const Hit iid = hit_of(HitKind::UpdatesInstall, *vr.wu);
                                const auto& u = vm.updates.scan.updates[*vr.wu];
                                const bool is_running = installing && vm.updates.install.target == cd::install_target(u.update_id, u.revision);
                                if (name.find(is_running ? running_chip : avail) == std::wstring::npos) bad(L"updates vendor row update chip");
                                if (hits.meta.at(iid).name != dl_label) bad(L"updates vendor row download label");
                                check_btn(iid, r, can_act && !checking, L"download");
                            } else if (vr.local) {
                                const Hit lid = hit_of(HitKind::UpdatesLocal, *vr.local);
                                const bool copying = vm.updates.local.stage == LS::Copying && vm.updates.local.gpu == *vr.local;
                                if (hits.meta.count(lid) && hits.meta.at(lid).name != (copying ? stop_label : pick_label)) bad(L"updates local label");
                                check_btn(lid, r, copying || can_act, L"local");
                            } else if (hits.rect_of(hit_of(HitKind::UpdatesLocal, k)) && vr.kind == UpdatesVendorKind::Chipset) {
                                bad(L"updates chipset row offers a local package");
                            }
                        }
                        if (got != want_rows) bad(L"updates row count");
                        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxUpdatesLinks); ++k)
                            if (hits.rect_of(hit_of(HitKind::UpdatesLink, k))) bad(L"updates external vendor link drawn");
                    } else if (c.pg == Page::History) {
                        const auto& hv = vm.history;
                        const RectF* exp_btn = hits.rect_of(Hit::HistoryExport);
                        if (strip_required && !exp_btn) bad(L"export missing in the tests section heading");
                        // BOOST maketi (Gecmis): gorunum cipleri yok (iki bolum her zaman alt alta); "Disa aktar" 02 bolum basliginin
                        // saginda, tek bir dugme. Eski sart "tam bir cip secili" yerine: hicbir gorunum cipi cizilmez.
                        for (const auto v2 : {HistoryTab::All, HistoryTab::Changes, HistoryTab::Tests})
                            if (hits.rect_of(history_tab_hit(v2))) bad(L"view chip still drawn");
                        if (st.history_tabs) bad(L"history tabs flag set");
                        const HistoryTab tab = st.history_tabs ? st.history_tab : HistoryTab::All;
                        const float card_right = body_x + body_w - theme.card_pad;  // kart ic sag kenari
                        const float head_right = body_x + body_w;  // bolum basligi satirinin sag kenari (kartin disi)
                        // Okunamayan kayit uyarisi zorunlu: her gorunumde (Tumu / Degisiklikler / Testler) ekranda ve UIA'da.
                        if (const std::size_t unread = hv.view.corrupt + hv.view.foreign + hv.view.newer; unread > 0) {
                            std::wstring want_txt = wide(cat.value().get(i18n::StringId::HistoryUnreadableShort));
                            if (const auto at = want_txt.find(L"{count}"); at != std::wstring::npos) want_txt.replace(at, 7, std::to_wstring(unread));
                            bool found = false;
                            for (const auto& [id, m] : hits.meta)
                                if (m.name.find(want_txt) != std::wstring::npos && hits.rect_of(id)) found = true;
                            if (!found) bad(L"unreadable-record notice missing");
                        }
                        // Kapsam alt suzgeci degisiklik kartinin baslik seridinde: ilk cip "Tumu"; sayisi 0 olan kapsam cipi yalniz
                        // seciliyse; sayi UIA adinda; tam bir secili; cipler kartin ic sag kenarina dayali.
                        if (st.history_filters) {
                            if (!history_shows_changes(tab)) bad(L"scope filter drawn without the changes card");
                            const RectF* prev = nullptr;
                            int selected = 0;
                            for (const auto f : {HistoryFilter::All, HistoryFilter::Perf, HistoryFilter::Fixes, HistoryFilter::Startup}) {
                                const Hit id = history_filter_hit(f);
                                const auto it = hits.meta.find(id);
                                const RectF* r = hits.rect_of(id);
                                const bool want = f == HistoryFilter::All || f == st.history_filter || !history_filter_rows(hv.view, f).empty();
                                if (!want) {
                                    if (r) bad(L"zero-count filter chip drawn");
                                    continue;
                                }
                                if (it == hits.meta.end() || !r) {
                                    bad(L"filter chip missing");
                                    continue;
                                }
                                selected += it->second.selected ? 1 : 0;
                                if (it->second.role != AccRole::RadioButton || it->second.selected != (f == st.history_filter)) bad(L"filter chip role/selection");
                                if (it->second.name.find(std::to_wstring(history_filter_rows(hv.view, f).size())) == std::wstring::npos) bad(L"filter count not in name");
                                if (prev && r->x < prev->right()) bad(L"filter chips out of order");
                                prev = r;
                            }
                            if (prev && std::fabs(prev->right() - head_right) > 0.5f) bad(L"scope filter not on the section heading's right edge");
                            if (selected != 1) bad(L"scope filter selection count");
                        }
                        // Journal tablosu: suzgece uyan her kaydin ac/kapa dugmesi var; Selected = ayrinti acik.
                        const auto shown = history_filter_rows(hv.view, st.history_filters ? st.history_filter : HistoryFilter::All);
                        float journal_bottom = 0;
                        std::size_t toggles = 0;
                        for (const auto& [id, r] : hits.items)
                            if (classify_hit(id).kind == HitKind::HistoryRow) ++toggles;
                        if (toggles != (hv.loading || !history_shows_changes(tab) ? 0 : shown.size())) bad(L"row toggle count");
                        std::size_t selects = 0;
                        for (const auto& [id, r] : hits.items)
                            if (classify_hit(id).kind == HitKind::HistorySelect) ++selects;
                        if (selects != (history_shows_tests(tab) ? std::min<std::size_t>(hv.loading ? 0 : hv.tests.size(), static_cast<std::size_t>(kMaxMeasureRecent)) : 0))
                            bad(L"test row count");
                        for (const std::size_t ri : shown) {
                            const Hit id = hit_of(HitKind::HistoryRow, ri);
                            const auto it = hits.meta.find(id);
                            const RectF* r = hits.rect_of(id);
                            if (it == hits.meta.end() || !r) continue;
                            const bool open = st.history_all_open || st.history_open.count(history_row_key(hv.view.rows[ri])) != 0;
                            if (it->second.role != AccRole::Expander || it->second.selected != open) bad(L"row toggle state");
                            // B13: satirin kendisi; degisiklik kartinin ic genisligi.
                            if (std::fabs(r->x - (body_x + theme.card_pad)) > 0.5f || std::fabs(r->right() - card_right) > 0.5f) bad(L"change row not card width");
                            journal_bottom = std::max(journal_bottom, r->bottom());
                        }
                        // Test karti degisiklik kartinin altinda; satirlar kartin ic genisliginde (sec dugmeleri ic sag kenarda).
                        for (std::size_t i = 0; i < hv.tests.size(); ++i)
                            if (const RectF* r = hits.rect_of(hit_of(HitKind::HistorySelect, i));
                                r && (std::fabs(r->x - (body_x + theme.card_pad)) > 0.5f || std::fabs(r->right() - card_right) > 0.5f || r->y < journal_bottom))
                                bad(L"test rows are not card width below the changes card");
                        // Iki kart ust uste (esit satir kaydi yok); akran durumu tek kisa satir.
                        if (!hits.rows.empty()) bad(L"history page must not use equal-row cards");
                    } else if (c.pg == Page::Settings) {
                        const auto it = hits.meta.find(strip_text);
                        if (strip_required && (it == hits.meta.end() || it->second.name.find(wide(vm.version)) == std::wstring::npos)) bad(L"version line missing in header strip");
                        // Spec 2026-11-12 B7 / P12-B3: tek, ortali liste; Dil, Guncelleme, Gelismis, (acikken Windows ile baslat,
                        // Animasyonlar, RGB serit, Oyun onaylari, Destek paketi), Hakkinda sirayla; kontroller sagda tek hizada;
                        // listedeki dugmeler es genislik ve es yukseklikte.
                        std::vector<Hit> order = {Hit::SettingsLanguage, Hit::SettingsUpdateCheck, Hit::SettingsAdvanced};
                        if (st.settings_advanced_open)
                            for (const Hit id : {Hit::Autostart, Hit::AppAnimations, Hit::SettingsStrip, Hit::SettingsGamesShow, Hit::SupportCreate})
                                order.push_back(id);
                        order.push_back(Hit::SettingsLicenses);
                        if (!st.settings_advanced_open)
                            for (const Hit id : {Hit::Autostart, Hit::AppAnimations, Hit::SettingsStrip, Hit::SettingsGamesShow, Hit::SupportCreate,
                                                 consent_reset_hit(0)})
                                if (hits.meta.count(id) || hits.rect_of(id)) bad(L"settings advanced rows drawn while collapsed");
                        {
                            const auto adv = hits.meta.find(Hit::SettingsAdvanced);
                            if (adv == hits.meta.end() || adv->second.role != AccRole::Expander || adv->second.selected != st.settings_advanced_open ||
                                adv->second.help.empty())
                                bad(L"settings advanced expander state/role");
                            const auto ord = focus_order(st, hits);
                            if (std::find(ord.begin(), ord.end(), Hit::SettingsAdvanced) == ord.end()) bad(L"settings advanced not in tab order");
                        }
                        std::vector<const RectF*> rs;
                        for (const Hit id : order) rs.push_back(hits.rect_of(id));
                        for (const RectF* r : rs)
                            if (!r) {
                                bad(L"settings controls missing");
                                return;
                            }
                        const float lw = settings_list_width(body_w);
                        const float list_right = body_x + std::floor((body_w - lw) / 2) + lw;
                        for (std::size_t i = 1; i < rs.size(); ++i)
                            if (rs[i]->y <= rs[i - 1]->y) bad(L"settings rows out of order");
                        // Anahtar isabet kutusu 4 DIP tasar (toggle_hit); dugmeler tam sag kenarda.
                        for (std::size_t i = 0; i < rs.size(); ++i) {
                            const bool tog = hits.meta.at(order[i]).role == AccRole::ToggleSwitch;
                            const float want = list_right - pad + (tog ? 4.f : 0.f);
                            if (std::fabs(rs[i]->right() - want) > 0.5f) bad(L"settings control not on the list's right edge");
                        }
                        for (std::size_t i = 1; i < rs.size(); ++i)
                            if (hits.meta.at(order[i]).role != AccRole::ToggleSwitch &&
                                (std::fabs(rs[i]->w - rs[0]->w) > 0.5f || std::fabs(rs[i]->h - rs[0]->h) > 0.5f))
                                bad(L"settings buttons not equal size");
                        if (!hits.rows.empty()) bad(L"settings page must not use equal-row cards");
                    }
                };
                const auto l4_state = [](const L4Case& c) {
                    UiState st;
                    st.reduced_motion = true;
                    st.page = c.pg;
                    st.measure_recent_open = c.recent_open;
                    st.cleanup_more_open = c.cleanup_more_open;
                    st.history_all_open = c.history_all_open;
                    st.history_open = c.history_open;
                    st.history_filter = c.filter;
                    st.history_tab = c.tab;
                    if (c.settings_collapsed) st.settings_advanced_open = st.settings_games_open = st.settings_licenses_open = false;
                    st.hover = c.hover;
                    st.scroll = c.scroll;
                    return st;
                };
                for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc_l3}) {
                    screen.set_palette(pal);
                    for (float sc : {1.f, 2.f})
                        for (auto [w, h] : sizes) {
                            const bool png = !out.empty() && (pal ? (tr_loc && sc == 1.f && w == 1180.f) : tr_loc ? (sc == 1.f || w == 1180.f) : (sc == 1.f && w == 1180.f));
                            for (const auto& c : cs) {
                                UiState st = l4_state(c);
                                ComPtr<IWICBitmap> bmp;
                                HitMap hits;
                                if (!render(st, c.vm, w, h, sc, bmp, hits)) return 2;
                                if (c.scroll > 0) {
                                    // Kaydirma ilk cizimde (icerik yuksekligi bilinmeden) sifirlanir: yukseklik ogrenildikten sonra yeniden.
                                    st.scroll = c.scroll;
                                    if (!render(st, c.vm, w, h, sc, bmp, hits)) return 2;
                                    if (c.scroll_back > 0) {
                                        st.scroll = std::max(0.f, st.scroll - c.scroll_back);
                                        if (!render(st, c.vm, w, h, sc, bmp, hits)) return 2;
                                    }
                                }
                                audit(c.page, c.name, pal, sc, w, h, st, hits);
                                l4_checks(c, pal, sc, w, h, st, hits, true);
                                if (png && (tr_loc || c.en_png)) {
                                    wchar_t name[200];
                                    swprintf_s(name, L"\\l4_%ls_%ls_%ls_%.0fx%.0f_%03d%ls.png", tr_loc ? L"tr" : L"en", c.page, c.name, w, h,
                                               static_cast<int>(sc * 100 + 0.5f), pal ? L"_hc" : L"");
                                    if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                                    dump_rects(out + name, hits);
                                }
                            }
                        }
                }
                screen.set_palette(nullptr);
                // Dar govde (asgari pencerenin altinda da bozulmaz): ust satir 2 / 1 sutun, kategoriler ve ayarlar tek sutun.
                for (float w : {720.f, 500.f}) {
                    const float h = 900.f;
                    const float body_w = w - 2 * theme.page_margin;
                    if (measure_top_columns(body_w, cg) != (w == 720.f ? 2 : 1) || cleanup_columns(body_w - 2 * pad, theme.space(4)) != 1 ||
                        settings_columns(body_w, cg) != 1)
                        fail(L"l4_narrow", L"columns", nullptr, 1.f, w, h, L"column count");
                    for (const auto& c : cs) {
                        UiState st = l4_state(c);
                        ComPtr<IWICBitmap> bmp;
                        HitMap hits;
                        if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                        if (c.scroll > 0) {  // kaydirma ilk cizimde sifirlanir: icerik yuksekligi ogrenildikten sonra yeniden
                            st.scroll = c.scroll;
                            if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                            if (c.scroll_back > 0) {
                                st.scroll = std::max(0.f, st.scroll - c.scroll_back);
                                if (!render(st, c.vm, w, h, 1.f, bmp, hits)) return 2;
                            }
                        }
                        audit(c.page, c.name, nullptr, 1.f, w, h, st, hits);
                        l4_checks(c, nullptr, 1.f, w, h, st, hits, false);
                        if (!out.empty() && tr_loc && c.narrow_png) {
                            wchar_t name[200];
                            swprintf_s(name, L"\\l4_tr_%ls_%ls_narrow_%.0f.png", c.page, c.name, w);
                            if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                        }
                    }
                }
                // B6 inceleme (bosluk 18): pencere calisma alanina sigdirildiginda istemci alani asgari boyutun altina
                // inebilir (1920x1080, %200 olcek, gorev cubugu: yaklasik 944x450 DIP). Bu boyutta da tum sayfalar
                // tasmasiz ve denetimler erisilebilir olmali (govde dikey kaydirilir). Koyu + yuksek kontrast.
                for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc_l3}) {
                    screen.set_palette(pal);
                    for (float sc : {1.f, 1.75f, 2.f}) {
                        const float w = 944.f, h = 450.f;
                        const auto shot = [&](const wchar_t* page, const wchar_t* cname, UiState st, const ViewModel& vm, float scroll,
                                              float scroll_back) {
                            ComPtr<IWICBitmap> bmp;
                            HitMap hits;
                            if (!render(st, vm, w, h, sc, bmp, hits)) return false;
                            if (scroll > 0) {  // kaydirma ilk cizimde sifirlanir: icerik yuksekligi ogrenildikten sonra yeniden
                                st.scroll = scroll;
                                if (!render(st, vm, w, h, sc, bmp, hits)) return false;
                                if (scroll_back > 0) {
                                    st.scroll = std::max(0.f, st.scroll - scroll_back);
                                    if (!render(st, vm, w, h, sc, bmp, hits)) return false;
                                }
                            }
                            audit(page, cname, pal, sc, w, h, st, hits);
                            if (!out.empty() && tr_loc && !pal && sc == 2.f) {
                                wchar_t name[200];
                                swprintf_s(name, L"\\short_tr_%ls_%ls_944x450_200.png", page, cname);
                                if (!save_png(wic.Get(), bmp.Get(), out + name)) return false;
                            }
                            return true;
                        };
                        {
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Overview;
                            if (!shot(L"overview", L"default", st, fixture(false), 0, 0)) return 2;
                        }
                        for (const auto& c : gc) {
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Games;
                            st.games_filter = c.filter;
                            c.apply(st);
                            if (!shot(L"games", c.name, st, c.vm, 0, 0)) return 2;
                        }
                        for (const auto& c : rc) {
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Readiness;
                            st.readiness_checkup_open = c.checkup_open;
                            if (!shot(L"readiness", c.name, st, c.vm, 0, 0)) return 2;
                        }
                        for (const auto& c : cs) {
                            UiState st = l4_state(c);
                            const float scroll = st.scroll;
                            st.scroll = 0;
                            if (!shot(c.page, c.name, st, c.vm, scroll, c.scroll_back)) return 2;
                        }
                    }
                }
                screen.set_palette(nullptr);
                // Spec 2026-11-12 §5: simetri kaniti — her sayfanin tamami tek goruntude (uzun pencere, kaydirmasiz):
                // ekranin altinda kalan satirlar da gozle denetlenir. Koyu tema, %100, 1180 ve 960 genislik. Ortak
                // denetimler (tasma, boyut, a11y, simetri) bu cizimlerde de calisir.
                if (!out.empty() && tr_loc)
                    for (float w : {1180.f, 960.f}) {
                        const auto tall = [&](const wchar_t* page, const wchar_t* cname, UiState st, const ViewModel& vm) {
                            st.scroll = 0;
                            ComPtr<IWICBitmap> bmp;
                            HitMap hits;
                            if (!render(st, vm, w, 720.f, 1.f, bmp, hits)) return false;  // icerik yuksekligi olculur
                            const float h = std::clamp(st.content_height + 150.f, 720.f, 4000.f);
                            if (!render(st, vm, w, h, 1.f, bmp, hits)) return false;
                            audit(page, cname, nullptr, 1.f, w, h, st, hits);
                            wchar_t name[200];
                            swprintf_s(name, L"\\sym_tr_%ls_%ls_%.0f.png", page, cname, w);
                            return save_png(wic.Get(), bmp.Get(), out + name);
                        };
                        for (const auto& c : gc) {
                            if (std::wstring(c.name) != L"mixed" && std::wstring(c.name) != L"default" && std::wstring(c.name) != L"icons" &&
                                std::wstring(c.name) != L"rows" && std::wstring(c.name) != L"sel_all" && std::wstring(c.name) != L"sel_actions")
                                continue;
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Games;
                            st.games_filter = c.filter;
                            st.games_guides_open = std::wstring(c.name) == L"mixed";  // rehber kartlari acik / kapali
                            c.apply(st);
                            if (!tall(L"games", c.name, st, c.vm)) return 2;
                        }
                        for (const auto& c : rc) {
                            UiState st;
                            st.reduced_motion = true;
                            st.page = Page::Readiness;
                            c.apply(st);
                            if (!tall(L"readiness", c.name, st, c.vm)) return 2;
                        }
                        for (const auto& c : cs)
                            if ((c.pg == Page::Measurements || c.pg == Page::History || c.pg == Page::Settings || c.pg == Page::Updates || systools_page(c.pg)) && c.scroll <= 0)
                                if (!tall(c.page, c.name, l4_state(c), c.vm)) return 2;
                    }
            }
            // ---- Canli test ekrani (spec 2026-11-12 A4): denge testi surerken Olcumler sayfasinin icerik alani onizleme +
            // gostergelerdir. Kodla uretilmis onizleme pikselleriyle birkac adim; koyu + yuksek kontrast, %100 / %200,
            // 960x540 / 1180x720. Ortak denetimler (tasma, boyut, a11y, simetri) + gorunume ozgu yerlesim / UIA kurallari. ----
            {
                namespace cm = core::measure;
                const std::vector<LiveCase> lc = live_cases(theme);  // dosya duzeyinde kurulur (wmain yigin cercevesi buyumesin)
                const auto live_state = [&](const LiveCase& c) {
                    UiState st;
                    st.reduced_motion = true;
                    st.page = Page::Measurements;
                    st.focus = Hit::TestStop;  // klavye odagi durdurma dugmesinde: odak halkasi cizilmeli
                    st.focus_visible = true;
                    screen.set_live_sample(st, c.sample, 0);
                    return st;
                };
                const bool tr_fmt = tr_loc;
                const auto live_checks = [&](const LiveCase& c, const Palette* pal, float sc, float w, float h, const UiState& st, const HitMap& hits) {
                    const auto bad = [&](const wchar_t* what) { fail(L"live", c.name, pal, sc, w, h, what); };
                    const auto& m = c.vm.measure;
                    const auto& cv = cat.value();
                    if (!st.live_view || st.live_region.h <= 0 || st.live_region.w != w) bad(L"live region not recorded");
                    // Icerik alani canli gorunumdur: sayfa ogeleri (kaynak, baslat, test secenekleri, internet hizi) cizilmez; ust menu kalir.
                    for (const Hit gone : {Hit::SourceBuiltin, Hit::SourceGame, Hit::TestStart, Hit::ModeQuick, Hit::SpeedStart, Hit::ScopeBalance, Hit::LiveView})
                        if (hits.meta.count(gone)) bad(L"page control drawn under the live view");
                    for (const Page pg : kNavOrder)
                        if (!hits.meta.count(nav_hit(static_cast<std::size_t>(pg)))) bad(L"top menu tab missing");
                    const RectF* step = hits.rect_of(Hit::LiveStep);
                    const RectF* fps = hits.rect_of(Hit::LiveFps);
                    const RectF* load = hits.rect_of(Hit::LiveLoad);
                    const RectF* stop = hits.rect_of(Hit::TestStop);
                    const RectF* chart = nullptr;
                    const std::wstring chart_title = wide(cv.get(i18n::StringId::LiveViewChart));
                    const RectF* preview = nullptr;
                    const std::wstring preview_name = wide(cv.get(i18n::StringId::LiveViewAccPreview));
                    for (const auto& [id, r] : hits.items) {
                        const auto it = hits.meta.find(id);
                        if (it == hits.meta.end() || it->second.role != AccRole::Text) continue;
                        if (it->second.name.starts_with(chart_title)) chart = &r;
                        if (it->second.name == preview_name) preview = &r;
                    }
                    if (!step || !fps || !load || !stop || !chart || !preview) {
                        bad(L"live view elements missing");
                        return;
                    }
                    // Paneller pencerenin icinde, ust menunun altinda; birbirinin ustune binmez. Kardes paneller esit boyda.
                    const RectF* panels[] = {step, fps, load, chart};
                    for (const RectF* r : panels)
                        if (r->x < 0 || r->right() > w + 0.5f || r->y < st.live_region.y || r->bottom() > h + 0.5f) bad(L"panel outside the live area");
                    for (std::size_t i = 0; i < std::size(panels); ++i)
                        for (std::size_t j = i + 1; j < std::size(panels); ++j)
                            if (rects_overlap(*panels[i], *panels[j])) bad(L"panels overlap");
                    if (rects_overlap(*stop, *chart) || rects_overlap(*stop, *load) || stop->right() > w || stop->bottom() > h) bad(L"stop button placement");
                    if (std::fabs(step->h - fps->h) > 0.5f || std::fabs(step->y - fps->y) > 0.5f) bad(L"top panels unequal");
                    if (std::fabs(load->h - chart->h) > 0.5f || std::fabs(load->y - chart->y) > 0.5f) bad(L"bottom panels unequal");
                    if (step->bottom() > load->y || fps->x < step->right()) bad(L"panel rows out of order");
                    // Onizleme alani: 16:9, panelleri kapsar (gostergeler onizlemenin ustundedir) ve isabet sirasinda EN SONDA.
                    if (std::fabs(preview->w / preview->h - 16.f / 9.f) > 0.02f) bad(L"preview aspect ratio");
                    if (hits.items.empty() || &hits.items.back().second != preview) bad(L"preview item is not last in hit order");
                    if (hits.element_at(step->x + 4, step->y + 4) != Hit::LiveStep || hits.element_at(fps->x + 4, fps->y + 4) != Hit::LiveFps) bad(L"panel not hit before the preview");
                    // Durdurma dugmesi: calisirken etkin "Testi durdur"; Durduruluyor'da devre disi.
                    {
                        const auto it = hits.meta.find(Hit::TestStop);
                        const bool can = cm::can_stop(m);
                        if (it == hits.meta.end() || it->second.enabled != can || it->second.role != AccRole::Button) bad(L"stop button state");
                        else if (it->second.name != wide(cv.get(can ? i18n::StringId::TestStop : i18n::StringId::MeasureStoppingButton))) bad(L"stop button label");
                        else if (can && it->second.help != wide(cv.get(i18n::StringId::LiveViewEscHint))) bad(L"esc hint not exposed");
                        // Durduruluyor: Esc bir sey yapmaz; ipucu ne UIA yardiminda ne de herhangi bir ogenin adinda / yardiminda.
                        if (!can) {
                            const std::wstring esc = wide(cv.get(i18n::StringId::LiveViewEscHint));
                            for (const auto& [id, mt] : hits.meta)
                                if (mt.name.find(esc) != std::wstring::npos || mt.help.find(esc) != std::wstring::npos) bad(L"esc hint exposed while stopping");
                        }
                    }
                    // UIA metinleri: adim (asama adi + n/9), kare hizi (olculen deger ya da "olculmedi"), yuk yuzdeleri, ilerleme.
                    {
                        const auto& sname = hits.meta.at(Hit::LiveStep).name;
                        const auto stage_id = i18n::id_of(m.stage_key);
                        if (m.phase != cm::TestPhase::Stopping && (!stage_id || sname.find(wide(cv.get(*stage_id))) == std::wstring::npos)) bad(L"step name not in UIA text");
                        if (m.phase == cm::TestPhase::Running && sname.find(std::to_wstring(c.sample.step_index) + L"/" + std::to_wstring(c.sample.step_total)) == std::wstring::npos)
                            bad(L"step n/total not in UIA text");
                        if (m.phase == cm::TestPhase::Stopping && !sname.starts_with(wide(cv.get(i18n::StringId::MeasureStopping)))) bad(L"stopping state not in UIA text");
                        const auto& fname = hits.meta.at(Hit::LiveFps).name;
                        if (c.sample.has_numbers) {
                            const auto v = wide(cm::format_number(c.sample.fps, c.sample.fps < 99.95 ? 1 : 0, tr_fmt));
                            if (fname.find(v + L" FPS") == std::wstring::npos || fname.find(L"ms") == std::wstring::npos) bad(L"fps value not in UIA text");
                        } else if (fname != wide(cv.get(i18n::StringId::LiveViewAccFpsNone))) {
                            bad(L"fps text must say not measured");
                        }
                        const auto& lname = hits.meta.at(Hit::LiveLoad).name;
                        const auto pct = [&](double v) {
                            const auto n = std::to_wstring(static_cast<long long>(std::floor(v + 0.5)));
                            return tr_fmt ? L"%" + n : n + L"%";
                        };
                        const auto cpu = cm::live_cpu_load_pct(c.sample), gpu = cm::live_gpu_load_pct(c.sample);
                        const std::wstring missing = wide(cv.get(i18n::StringId::MetricMissing));
                        if (lname.find(cpu ? pct(*cpu) : missing) == std::wstring::npos) bad(L"cpu load not in UIA text");
                        if (lname.find(gpu ? pct(*gpu) : missing) == std::wstring::npos) bad(L"gpu load not in UIA text");
                        if (!cpu && !gpu && lname.find(L'%') != std::wstring::npos) bad(L"load percent shown without a measurement");
                        const auto pit = hits.meta.find(Hit::Progress0);
                        const auto frac = cm::live_step_fraction(c.sample);
                        if (frac && (pit == hits.meta.end() || pit->second.role != AccRole::ProgressBar || std::fabs(pit->second.range_value - *frac * 100.0) > 0.01))
                            bad(L"progress value");
                        if (!frac && m.phase == cm::TestPhase::Preparing && pit != hits.meta.end()) bad(L"progress shown before the first step");
                        // Kapsam: sayilar "bu test sahnesinde" anlik olcumdur (grafik panelinin metni + kare hizi metni).
                        const auto note = wide(cv.get(i18n::StringId::LiveViewScopeNote));
                        if (!has_text_item(hits, note, 2)) bad(L"scope note missing");
                    }
                };
                for (const Palette* pal : {static_cast<const Palette*>(nullptr), &hc_l3}) {
                    screen.set_palette(pal);
                    for (float sc : {1.f, 2.f})
                        for (auto [w, h] : sizes)
                            for (const auto& c : lc) {
                                UiState st = live_state(c);
                                ComPtr<IWICBitmap> bmp;
                                HitMap hits;
                                if (!render(st, c.vm, w, h, sc, bmp, hits)) return 2;
                                audit(L"live", c.name, pal, sc, w, h, st, hits);
                                live_checks(c, pal, sc, w, h, st, hits);
                                const bool png = !out.empty() && (tr_loc ? (sc == 1.f || (c.big_png && !pal && w == 1180.f)) : (c.en_png && !pal && sc == 1.f && w == 1180.f));
                                if (png) {
                                    wchar_t name[200];
                                    swprintf_s(name, L"\\live_%ls_%ls_%.0fx%.0f_%03d%ls.png", tr_loc ? L"tr" : L"en", c.name, w, h, static_cast<int>(sc * 100 + 0.5f),
                                               pal ? L"_hc" : L"");
                                    if (!save_png(wic.Get(), bmp.Get(), out + name)) return 2;
                                }
                            }
                }
                screen.set_palette(nullptr);
                // Gorunum yalniz Olcumler sayfasinin icerigidir: baska sayfada (kullanici sekme degistirdi) sayfa aynen cizilir.
                {
                    const auto& c = lc[2];
                    UiState st = live_state(c);
                    st.page = Page::Overview;
                    st.focus = Hit::None;
                    ComPtr<IWICBitmap> bmp;
                    HitMap hits;
                    if (!render(st, c.vm, 1180.f, 720.f, 1.f, bmp, hits)) return 2;
                    audit(L"live", L"other_page", nullptr, 1.f, 1180.f, 720.f, st, hits);
                    if (st.live_view || hits.meta.count(Hit::LiveStep) || !hits.meta.count(Hit::Toggle)) fail(L"live", L"other_page", nullptr, 1.f, 1180.f, 720.f, L"live view drawn on another page");
                }
                // Kosu canli ekransiz basladiysa (ayar kapali / pencere gorunmuyordu) Olcumler sayfasi aynen cizilir.
                {
                    const auto c = std::make_unique<LiveCase>(lc[2]);
                    c->vm.measure.live_view.active = false;
                    UiState st = live_state(*c);
                    ComPtr<IWICBitmap> bmp;
                    HitMap hits;
                    if (!render(st, c->vm, 1180.f, 720.f, 1.f, bmp, hits)) return 2;
                    audit(L"live", L"inactive", nullptr, 1.f, 1180.f, 720.f, st, hits);
                    if (st.live_view || hits.meta.count(Hit::LiveStep) || !hits.meta.count(Hit::SourceBuiltin) || !hits.meta.count(Hit::TestStop))
                        fail(L"live", L"inactive", nullptr, 1.f, 1180.f, 720.f, L"live view drawn for a run started without it");
                }
                // Hizli yol (MainWindow: yalniz canli bolge yeniden cizilir): bolge cizimi, ayni ornekle yapilan tam cizimle
                // piksel piksel ayni olmali (onceki ornekten artik kalmaz) ve canli ogelerin UIA adlarini guncellemeli.
                for (auto [w, h] : sizes) {
                    const auto& from = lc[1];
                    const auto& to = lc[3];
                    const ViewModel& vm = to.vm;
                    UiState sa = live_state(to);
                    ComPtr<IWICBitmap> full, fast;
                    const UINT pw = static_cast<UINT>(w + 0.5f), ph = static_cast<UINT>(h + 0.5f);
                    if (FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &full)) ||
                        FAILED(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &fast)))
                        return 2;
                    if (!g->bind_wic(full.Get(), 96.f) || !g->begin()) return 2;
                    const HitMap full_hits = paint_audited(screen, *g, w, h, sa, vm, 0);
                    if (!g->end()) return 2;
                    UiState sb = live_state(from);
                    if (!g->bind_wic(fast.Get(), 96.f) || !g->begin()) return 2;
                    (void)paint_audited(screen, *g, w, h, sb, vm, 0);  // onceki ornekle tam kare
                    if (!g->end() || !g->begin()) return 2;
                    screen.set_live_sample(sb, to.sample, 0);
                    HitMap fresh;
                    screen.paint_live_region(*g, sb, vm, 0, fresh);
                    if (!g->end()) return 2;
                    ++renders;
                    g->clear_overflows();
                    if (!bitmaps_equal(full.Get(), fast.Get())) fail(L"live", L"fast_path", nullptr, 1.f, w, h, L"region repaint differs from the full paint");
                    const auto a = full_hits.meta.find(Hit::LiveFps);
                    const auto b = fresh.meta.find(Hit::LiveFps);
                    if (a == full_hits.meta.end() || b == fresh.meta.end() || a->second.name != b->second.name) fail(L"live", L"fast_path", nullptr, 1.f, w, h, L"UIA text not refreshed");
                    for (const auto& [id, meta] : fresh.meta)
                        if (!full_hits.meta.count(id)) fail(L"live", L"fast_path", nullptr, 1.f, w, h, L"region repaint registered an unknown element");
                    if (!out.empty() && tr_loc && w == 1180.f && !save_png(wic.Get(), fast.Get(), out + L"\\live_tr_fast_path_1180x720_100.png")) return 2;
                }
            }
        }
        // Spec 2026-11-11 §3: vektor ikon seti (cizim dile bagli degil; bir kez denetlenir).
        if (loc == i18n::Locale::TrTR) {
            const int bad = icon_render_issues(wic.Get(), *g, theme);
            if (bad < 0) return 2;
            missing_controls += bad;
            if (!out.empty() && (!save_icon_sheet(wic.Get(), *g, theme, 1.f, out + L"\\l1_icons_100.png") ||
                                 !save_icon_sheet(wic.Get(), *g, theme, 2.f, out + L"\\l1_icons_200.png")))
                return 2;
            g->clear_overflows();
        }
        g->release_target();
    }
    // BOOST Panel: mono roller EXE'ye gomulu JetBrains Mono'dan (bellek ici koleksiyon) gelir; yuklenemediyse (sistem yedegi)
    // bulgu sayilir.
    const bool mono_ok = Graphics::mono_embedded();
    if (!mono_ok) ++missing_controls, std::wprintf(L"FONT embedded JetBrains Mono could not be loaded\n");
    std::wprintf(L"ui_snapshot: renders=%d overflow=%d big_controls=%d missing_controls=%d a11y=%d row_unequal=%d orphan_card=%d font=\"%ls\" mono_embedded=%d\n",
                 renders, overflow_total, big_buttons, missing_controls, a11y_total, g_row_unequal, g_orphan_card, family.c_str(), mono_ok ? 1 : 0);
    wic.Reset();  // COM kapanmadan once serbest birak
    CoUninitialize();
    return overflow_total == 0 && big_buttons == 0 && missing_controls == 0 && a11y_total == 0 && g_row_unequal == 0 && g_orphan_card == 0 ? 0 : 1;
}
