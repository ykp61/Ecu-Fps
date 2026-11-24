// Uyumluluk B8 (bosluk 8, 12, 17, 20): destek paketi diag.json, balance.log satirlari, cihaz dogrulama notu, AMD/Intel
// "desteklenmiyor (uretici API'si yok)" metinleri ve islem siniri izninin kitaplik yeniden kullanim kurali.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/capabilities/device_verification.h"
#include "core/diagnosis/balance.h"
#include "core/diagnosis/drops.h"
#include "core/games/library_scan.h"
#include "core/i18n/catalog.h"
#include "core/measure/balance_log.h"
#include "core/measure/balance_view.h"
#include "core/readiness/readiness.h"
#include "core/serialization/json.h"
#include "core/support/bundle.h"
#include "core/support/diag.h"
#include "ef_test.h"

namespace cap = ef::core::capabilities;
namespace dg = ef::core::diagnosis;
namespace cg = ef::core::games;
namespace cm = ef::core::measure;
namespace cr = ef::core::readiness;
namespace cs = ef::core::support;
namespace json = ef::core::json;

namespace {
bool has_note(const std::vector<std::string_view>& v, std::string_view n) { return std::find(v.begin(), v.end(), n) != v.end(); }
bool has_str(const std::vector<std::string>& v, std::string_view n) { return std::find(v.begin(), v.end(), n) != v.end(); }
const cr::Check* find_check(const std::vector<cr::Check>& v, std::string_view id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}
}  // namespace

// ---- Bosluk 12: cihaz dogrulama ----

EF_TEST(compat_b8_os_build_major) {
    EF_CHECK(cap::os_build_major("26300.9711") == 26300u);
    EF_CHECK(cap::os_build_major("19045") == 19045u);
    EF_CHECK(!cap::os_build_major("?.?").has_value());
    EF_CHECK(!cap::os_build_major("").has_value());
    EF_CHECK(!cap::os_build_major("0.1").has_value());
    EF_CHECK(!cap::os_build_major("99999999999.1").has_value());
}

EF_TEST(compat_b8_verified_device_only_with_real_evidence_row) {
    EF_CHECK(!cap::verified_combinations().empty());
    const auto ok = cap::verify_device("26300.9711", 0x10DEu);
    EF_CHECK(ok.verified && ok.os_verified && ok.gpu_verified && ok.os_known && ok.gpu_known);

    const auto amd = cap::verify_device("26300.9711", 0x1002u);  // AMD: bu build'de test edilmedi
    EF_CHECK(!amd.verified && amd.os_verified && !amd.gpu_verified);

    const auto win10 = cap::verify_device("19045.5000", 0x10DEu);
    EF_CHECK(!win10.verified && !win10.os_verified && win10.gpu_verified);

    // Okunamayan deger "dogrulandi" sayilmaz.
    const auto unknown = cap::verify_device("?.?", std::nullopt);
    EF_CHECK(!unknown.verified && !unknown.os_known && !unknown.gpu_known && !unknown.os_verified && !unknown.gpu_verified);
    EF_CHECK(!cap::verify_device("26300.1", 0u).gpu_known);  // 0 = okunamadi

    const auto j = json::write(cap::to_json(amd));
    EF_CHECK(j.find("\"verified\":false") != std::string::npos || j.find("\"verified\": false") != std::string::npos);
    EF_CHECK(j.find("verified_combinations") != std::string::npos);
}

// ---- Bosluk 8: diag.json ve destek paketi ----

EF_TEST(compat_b8_diag_json_fields_and_unknowns_are_null) {
    cs::DiagInput in;
    in.app_version = "0.6.0";
    in.os_build = "26300.9711";
    in.cpu.brand = "AMD Ryzen 5 5600 6-Core Processor";
    in.cpu.physical_cores = 6;
    in.cpu.logical_processors = 12;
    in.cpu.smt = true;
    cs::DiagAdapter a;
    a.index = 0;
    a.name = "AMD Radeon RX 6600";
    a.vendor_id = 0x1002;
    a.luid = "00000000:0000d1a2";
    a.dedicated_vram_mib = 8176;
    a.kind = "discrete";
    a.high_performance = true;
    in.adapters.push_back(a);
    cs::DiagAdapter b;
    b.index = 1;
    b.name = "Microsoft Basic Render Driver";
    b.luid = "00000000:0000aaaa";
    in.adapters.push_back(b);
    cs::DiagDisplay d;
    d.width = 1920;
    d.height = 1080;
    d.hz_current = 143.981;
    d.dpi = 144;
    in.displays.push_back(d);
    in.power.on_ac = true;
    in.system_disk_used_pct = 91.237;
    in.language = {"system", "tr-TR", "tr-TR"};
    in.flags.push_back({"EF_UPDATE_CHECK", "0123456789", "update_check_off"});
    in.device = cap::verify_device(in.os_build, 0x1002u);

    const auto v = cs::diag_json(in);
    EF_CHECK(v.find("schema_version") && v.find("schema_version")->as_int() == cs::kDiagSchemaVersion);
    EF_CHECK(v.find("game_adapter_luid") && v.find("game_adapter_luid")->as_string() == "00000000:0000d1a2");
    const auto* cpu = v.find("cpu");
    EF_CHECK(cpu && cpu->find("logical_processors")->as_int() == 12 && cpu->find("smt")->as_bool());
    EF_CHECK(cpu && cpu->find("packages")->is(json::Value::Type::Null));  // okunamadi: 0 yazilmaz
    const auto* adapters = v.find("adapters");
    EF_CHECK(adapters && adapters->items().size() == 2);
    EF_CHECK(adapters && adapters->items()[1].find("vendor_id")->is(json::Value::Type::Null));
    const auto* displays = v.find("displays");
    EF_CHECK(displays && displays->items().size() == 1 && displays->items()[0].find("dpi")->as_int() == 144);
    EF_CHECK(displays && displays->items()[0].find("hdr_enabled")->is(json::Value::Type::Null));
    EF_CHECK(v.find("system_disk_used_pct") && v.find("system_disk_used_pct")->as_double() > 91.2);
    const auto* flags = v.find("env_flags");
    EF_CHECK(flags && flags->items()[0].find("value")->as_string() == "01234567");  // en cok 8 karakter
    const auto* dev = v.find("device_verification");
    EF_CHECK(dev && !dev->find("verified")->as_bool());
    EF_CHECK(v.find("support_matrix") && v.find("support_matrix")->is(json::Value::Type::Null));
}

EF_TEST(compat_b8_bundle_adds_redacted_diag_json) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "ef_b8_bundle";
    fs::remove_all(root);
    fs::create_directories(root / "logs");
    {
        std::ofstream f(root / "logs" / "balance.log", std::ios::binary);
        f << "2026-11-14T10:00:00Z pid=1 step=S1 status=measured fps=120.0\n";
    }
    ef::core::logging::RedactionContext ctx;
    ctx.user_name = "testuser";
    ctx.machine_name = "DESKTOP-B8";
    cs::DiagInput in;
    in.cpu.brand = "Test CPU";
    cs::DiagAdapter a;
    a.name = "C:\\Users\\testuser\\fake DESKTOP-B8";  // savunma: alan kisisel degildir, yine de redakte edilir
    in.adapters.push_back(a);
    auto req = cs::make_bundle_request(root, false, "0.6.0", "26300.9711");
    req.diag_json = json::write(cs::diag_json(in));
    const auto r = cs::build_bundle(req, ctx);
    EF_CHECK(r.ok());
    EF_CHECK(has_str(r.value().entries, "diag.json"));
    EF_CHECK(has_str(r.value().entries, "logs/balance.log"));
    const auto& zip = r.value().zip_bytes;
    EF_CHECK(zip.find("Test CPU") != std::string::npos);
    EF_CHECK(zip.find("testuser") == std::string::npos);
    EF_CHECK(zip.find("DESKTOP-B8") == std::string::npos);

    // diag.json bos: paket eskisi gibi (yalniz loglar + manifest).
    req.diag_json.clear();
    const auto r2 = cs::build_bundle(req, ctx);
    EF_CHECK(r2.ok() && !has_str(r2.value().entries, "diag.json"));
    fs::remove_all(root);
}

EF_TEST(compat_b8_balance_log_lines) {
    cm::BalanceStepLog s;
    s.step = "S2";
    s.status = "measured";
    s.attempts = 2;
    s.frames = 480;
    s.fps = 123.456;
    s.cv = 0.0612;
    s.gpu_busy = 0.9731;
    s.disjoint_frames = 3;
    s.invalid = dg::balance_reason::kTimestampDisjoint;
    s.other_cpu_pct = 4.25;
    const auto line = cm::balance_step_log_line(s);
    EF_CHECK(line == "step=S2 status=measured attempts=2 frames=480 fps=123.5 cv=0.0612 gpu_busy=0.973 disjoint=3 "
                      "invalid=timestamp_disjoint other_cpu=4.2 error=-" ||
              line == "step=S2 status=measured attempts=2 frames=480 fps=123.5 cv=0.0612 gpu_busy=0.973 disjoint=3 "
                      "invalid=timestamp_disjoint other_cpu=4.3 error=-");

    // Olculmeyen adim: deger uydurulmaz.
    cm::BalanceStepLog u;
    u.step = "S4";
    u.status = "unmeasurable";
    u.attempts = 1;
    u.invalid = dg::balance_reason::kVramBudget;
    u.error = "vram budget";
    const auto l2 = cm::balance_step_log_line(u);
    EF_CHECK(l2 == "step=S4 status=unmeasurable attempts=1 frames=0 fps=- cv=- gpu_busy=- disjoint=0 invalid=vram_budget other_cpu=- error=vram_budget");

    cm::BalanceRunLog r;
    r.rule = dg::kBalanceRuleVersion;
    r.adapter_vendor_id = 0x1002;
    r.quick = true;
    r.ok = true;
    r.retries = 1;
    EF_CHECK(cm::balance_run_log_line(r) == "run rule=denge_v4 vendor=1002 mode=quick ok=1 cancelled=0 retries=1 other_cpu=- error=-");
    cm::BalanceRunLog none;
    EF_CHECK(cm::balance_run_log_line(none).find("vendor=- mode=full ok=0") != std::string::npos);
}

// ---- Bosluk 17: AMD/Intel "desteklenmiyor (uretici API'si yok)" ----

EF_TEST(compat_b8_vendor_limit_api_missing_only_amd_intel) {
    EF_CHECK(dg::vendor_limit_api_missing(0x1002));
    EF_CHECK(dg::vendor_limit_api_missing(0x8086));
    EF_CHECK(!dg::vendor_limit_api_missing(0x10DE));
    EF_CHECK(!dg::vendor_limit_api_missing(0x1414));
    EF_CHECK(!dg::vendor_limit_api_missing(0));
}

EF_TEST(compat_b8_balance_note_vendor_limit_no_api) {
    dg::BalanceInputs in;
    in.confounders.vendor_limit_no_api = true;
    const auto r = dg::evaluate_balance(in);
    EF_CHECK(has_note(r.notes, dg::balance_note::kVendorLimitNoApi));
    EF_CHECK(r.rule == dg::kBalanceRuleVersion);
    dg::BalanceInputs off;
    EF_CHECK(!has_note(dg::evaluate_balance(off).notes, dg::balance_note::kVendorLimitNoApi));
    // Not katalogda var (kayittan geri okunurken atilmaz).
    EF_CHECK(!cm::balance_note_key(dg::balance_note::kVendorLimitNoApi).empty());
}

EF_TEST(compat_b8_drops_vendor_api_note_only_when_drops_found) {
    std::vector<double> intervals(240, 16.7);
    for (const std::size_t i : {59u, 119u, 179u, 239u}) intervals[i] = 60.0;
    dg::DropInput in;
    in.run.run_id = "b8";
    double t = 0;
    for (const double d : intervals) {
        t += d;
        in.frames.push_back({t, d});
    }
    in.run.gpu_vendor_limit_api_missing = true;
    const auto r = dg::detect_drops(in);
    EF_CHECK(r.outcome == dg::DropOutcome::DropsFound);
    EF_CHECK(has_str(r.notes, std::string(dg::kEvGpuVendorApiMissing)));
    EF_CHECK(dg::drop_evidence_key(dg::kEvGpuVendorApiMissing) == "drop.evidence.gpu_vendor_api_missing");
    EF_CHECK(ef::core::i18n::id_of("drop.evidence.gpu_vendor_api_missing").has_value());

    dg::DropInput flat;
    flat.run.gpu_vendor_limit_api_missing = true;
    t = 0;
    for (int i = 0; i < 240; ++i) {
        t += 16.7;
        flat.frames.push_back({t, 16.7});
    }
    const auto r2 = dg::detect_drops(flat);
    EF_CHECK(r2.outcome == dg::DropOutcome::NoDrops);
    EF_CHECK(!has_str(r2.notes, std::string(dg::kEvGpuVendorApiMissing)));

    in.run.gpu_vendor_limit_api_missing = false;  // NVIDIA / bilinmeyen: not yok
    EF_CHECK(!has_str(dg::detect_drops(in).notes, std::string(dg::kEvGpuVendorApiMissing)));
}

EF_TEST(compat_b8_readiness_amd_intel_vrr_unsupported_wording) {
    cr::GraphicsInfo g;
    g.gpu_name = "AMD Radeon RX 6600";
    g.vendor = "amd";
    auto v = cr::graphics_checks(g);
    const auto* c = find_check(v, "graphics.vrr_vendor");
    EF_CHECK(c && c->level == cr::Level::Info && c->text_key == "chk.graphics.vendor_api_unsupported");
    EF_CHECK(ef::core::i18n::id_of("chk.graphics.vendor_api_unsupported").has_value());
    g.vendor = "intel";
    EF_CHECK(find_check(v = cr::graphics_checks(g), "graphics.vrr_vendor") != nullptr);
    g.vendor = "nvidia";
    EF_CHECK(find_check(v = cr::graphics_checks(g), "graphics.vrr_vendor") == nullptr);
    g.vendor = "other";
    EF_CHECK(find_check(v = cr::graphics_checks(g), "graphics.vrr_vendor") == nullptr);

    cr::DisplayInfo d;
    d.width = 1920;
    d.height = 1080;
    d.hz_current = 144.0;
    d.gpu_known = true;
    d.gpu_vendor = "amd";
    d.gpu_name = "AMD Radeon RX 6600";
    auto dv = cr::display_checks({d});
    const auto* vrr = find_check(dv, "display.vrr");
    EF_CHECK(vrr && vrr->level == cr::Level::Info && vrr->text_key == "chk.display.vrr_vendor_unsupported");
    EF_CHECK(ef::core::i18n::id_of("chk.display.vrr_vendor_unsupported").has_value());

    // Yol GPU'su bilinmiyor ya da NVIDIA: "bilinmiyor" kalir (desteklenmiyor iddia edilmez).
    d.gpu_known = false;
    vrr = find_check(dv = cr::display_checks({d}), "display.vrr");
    EF_CHECK(vrr && vrr->level == cr::Level::Unknown && vrr->text_key == "chk.display.vrr_unknown");
    d.gpu_known = true;
    cr::DisplayInfo n = d;
    n.index = 1;
    n.gpu_vendor = "nvidia";
    vrr = find_check(dv = cr::display_checks({d, n}), "display.vrr");
    EF_CHECK(vrr && vrr->text_key == "chk.display.vrr_unknown");

    // B8 inceleme: hibrit dizustu (Optimus): paneli tumlesik Intel surer, oyun NVIDIA kartta -> "bilinmiyor" kalir.
    cr::DisplayInfo h = d;
    h.gpu_vendor = "intel";
    h.gpu_name = "Intel(R) UHD Graphics";
    h.multi_gpu = true;
    h.gpu_high_perf = false;
    vrr = find_check(dv = cr::display_checks({h}), "display.vrr");
    EF_CHECK(vrr && vrr->level == cr::Level::Unknown && vrr->text_key == "chk.display.vrr_unknown");
    // Cok GPU'lu ama ekrani yuksek performans AMD karti suruyor: iddia gecerli.
    h.gpu_vendor = "amd";
    h.gpu_high_perf = true;
    vrr = find_check(dv = cr::display_checks({h}), "display.vrr");
    EF_CHECK(vrr && vrr->text_key == "chk.display.vrr_vendor_unsupported");
}

// ---- Bosluk 20: izin kitaplik yeniden kullanim kurali ----

EF_TEST(compat_b8_permit_library_reuse_rule) {
    using S = cg::PermitLibrarySource;
    EF_CHECK(!cg::permit_library_reusable(S::None, true, true, 0));
    EF_CHECK(cg::permit_library_reusable(S::OwnScan, false, true, cg::kPermitOwnScanReuseMs));  // eski davranis: 2 sn
    EF_CHECK(cg::permit_library_reusable(S::OwnScan, true, false, cg::kPermitOwnScanReuseMs));  // kendi bos taramasi: 2 sn
    EF_CHECK(!cg::permit_library_reusable(S::OwnScan, true, true, cg::kPermitOwnScanReuseMs + 1));
    EF_CHECK(cg::permit_library_reusable(S::Watcher, true, true, cg::kPermitWatcherReuseMs));
    EF_CHECK(!cg::permit_library_reusable(S::Watcher, true, true, cg::kPermitWatcherReuseMs + 1));
    EF_CHECK(!cg::permit_library_reusable(S::Watcher, false, true, 10));  // guvenilmez izleyici sonucu kullanilmaz
    EF_CHECK(!cg::permit_library_reusable(S::Watcher, true, true, -1));    // saat geri gitti
    // B8 inceleme: izleyicinin bos sonucu taze olsa da kullanilmaz (surec denetimi atlanmaz).
    EF_CHECK(!cg::permit_library_reusable(S::Watcher, true, false, 0));
    EF_CHECK(!cg::permit_library_reusable(S::Watcher, true, false, 10));
    // Pencere izleyicinin oyunlu yoklama araligini kapsar (her islemde UI'da yeniden tarama olmaz).
    static_assert(cg::kPermitWatcherReuseMs > cg::kLibraryPollGamesMs);
}

EF_TEST(compat_b8_permit_offer_acceptance) {
    // Kabul: HKCU, ayni elle eklenen liste, guvenilir, elde yok ya da daha yeni.
    EF_CHECK(cg::permit_offer_accepted(true, true, true, 100, -1));
    EF_CHECK(cg::permit_offer_accepted(true, true, true, 101, 100));
    // Ret dallari.
    EF_CHECK(!cg::permit_offer_accepted(false, true, true, 100, -1));  // baska kullanicinin kovani (broker)
    EF_CHECK(!cg::permit_offer_accepted(true, false, true, 100, -1));  // eski elle eklenen liste
    EF_CHECK(!cg::permit_offer_accepted(true, true, false, 100, -1));  // guvenilmez tarama
    EF_CHECK(!cg::permit_offer_accepted(true, true, true, 100, 100));  // ayni zaman: elimizdeki kalir
    EF_CHECK(!cg::permit_offer_accepted(true, true, true, 99, 100));   // daha eski
    // set_manual_exes sonrasi elde kayit yok (-1): yeni liste ile gelen ilk taze tarama alinir.
    EF_CHECK(cg::permit_offer_accepted(true, true, true, 0, -1));
}

EF_TEST(compat_b8_permit_reuse_rescans_on_unknown_library_process) {
    ef::core::inventory::GameInstall steam;
    steam.launcher = "steam";
    steam.install_dir = "D:\\SteamLibrary\\steamapps\\common\\Known Game";
    ef::core::inventory::GameInstall manual;
    manual.launcher = "manual";
    manual.exe_hint = "E:\\Tools\\MyGame\\game.exe";
    const std::vector<ef::core::inventory::GameInstall> games{steam, manual};

    auto proc = [](std::string path) {
        cg::ProcessInfo p;
        p.exe_path = std::move(path);
        p.path_readable = !p.exe_path.empty();
        return p;
    };
    cg::ProcessSnapshot s;
    s.state = cg::DetectionState::Ok;
    s.processes = {proc("c:\\windows\\explorer.exe"), proc("d:\\steamlibrary\\steamapps\\common\\known game\\bin\\kg.exe"),
                   proc("e:\\tools\\mygame\\game.exe"), proc("e:\\tools\\other.exe"), proc("")};
    EF_CHECK(!cg::permit_reuse_needs_rescan(games, s));  // bilinen oyunlar / ilgisiz surecler: yeniden kullanim

    // Ayni kitaplik kokunde listede olmayan oyun (izleyici taramasindan sonra kurulmus): yeniden tara.
    auto s2 = s;
    s2.processes.push_back(proc("d:\\steamlibrary\\steamapps\\common\\new game\\new.exe"));
    EF_CHECK(cg::permit_reuse_needs_rescan(games, s2));
    // Yeni bir kokteki tipik launcher klasorleri de yeniden taratir.
    auto s3 = s;
    s3.processes.push_back(proc("f:\\steamapps\\common\\fresh\\fresh.exe"));
    EF_CHECK(cg::permit_reuse_needs_rescan(games, s3));
    s3.processes.back() = proc("c:\\program files\\epic games\\fresh\\fresh.exe");
    EF_CHECK(cg::permit_reuse_needs_rescan(games, s3));
    s3.processes.back() = proc("c:\\xboxgames\\fresh\\content\\fresh.exe");
    EF_CHECK(cg::permit_reuse_needs_rescan(games, s3));
    // Elle eklenen EXE'nin klasoru kok sayilmaz (ortak klasordeki baska surec oyun degildir).
    auto s4 = s;
    s4.processes.push_back(proc("e:\\tools\\mygame\\helper.exe"));
    EF_CHECK(!cg::permit_reuse_needs_rescan(games, s4));
    // Onek benzerligi klasor esligi degildir: "known game2" bilinen oyun degil ama ayni kokte -> yeniden tara.
    auto s5 = s;
    s5.processes.push_back(proc("d:\\steamlibrary\\steamapps\\common\\known game2\\x.exe"));
    EF_CHECK(cg::permit_reuse_needs_rescan(games, s5));
}

EF_TEST(compat_b8_device_note_strings_exist) {
    EF_CHECK(ef::core::i18n::id_of("settings.device_unverified").has_value());
    EF_CHECK(ef::core::i18n::id_of("settings.device_unverified_os").has_value());
    EF_CHECK(ef::core::i18n::id_of("settings.device_unverified_gpu").has_value());
}
