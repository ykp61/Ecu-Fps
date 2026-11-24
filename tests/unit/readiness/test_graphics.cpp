// CHK-03/04: grafik ve yazilim/BIOS kurallari (saf) + okuyucu saf yardimcilari.
#include <string>
#include <vector>

#include "core/readiness/readiness.h"
#include "ef_test.h"
#include "platform/windows/readiness/graphics_reader.h"

using namespace ef::core::readiness;
namespace pr = ef::platform::readiness;

namespace {

constexpr std::int64_t kNow = 1793750400;  // 2026-11-04 00:00 UTC
constexpr std::int64_t kDay = 86400;

const Check* find(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}

GraphicsInfo ultimate_gpu() {
    GraphicsInfo g;
    g.gpu_name = "NVIDIA GeForce RTX 4070";
    g.vendor = "nvidia";
    g.driver_version = "32.0.15.9144";
    g.driver_date_unix = kNow - 30 * kDay;
    g.d3d12_max_feature_level = 0xc200;
    g.shader_model = 0x68;
    g.raytracing_tier = 11;
    g.mesh_shaders = true;
    g.vrs_tier2 = true;
    g.sampler_feedback = true;
    g.dx9_legacy_runtime = true;
    g.gsync_enabled = true;
    g.gsync_possible = true;
    g.now_unix = kNow;
    return g;
}

}  // namespace

EF_TEST(readiness_graphics_dx12_ultimate_requires_all_features) {
    auto g = ultimate_gpu();
    EF_CHECK(dx12_ultimate(g));
    auto a = g;
    a.d3d12_max_feature_level = 0xc100;
    EF_CHECK(!dx12_ultimate(a));
    a = g;
    a.raytracing_tier = 10;
    EF_CHECK(!dx12_ultimate(a));
    a = g;
    a.mesh_shaders = false;
    EF_CHECK(!dx12_ultimate(a));
    a = g;
    a.vrs_tier2.reset();
    EF_CHECK(!dx12_ultimate(a));
    a = g;
    a.sampler_feedback = false;
    EF_CHECK(!dx12_ultimate(a));
    EF_CHECK(!dx12_ultimate(GraphicsInfo{}));
}

EF_TEST(readiness_graphics_dx_levels_are_info) {
    auto g = ultimate_gpu();
    auto v = graphics_checks(g);
    const auto* dx = find(v, "graphics.dx");
    EF_CHECK(dx && dx->level == Level::Info && dx->text_key == "chk.graphics.dx12u");
    EF_CHECK(dx && dx->section == Section::Graphics);
    EF_CHECK(dx && dx->args.at("fl") == "12.2" && dx->args.at("sm") == "6.8" && dx->args.at("rt") == "1.1");
    EF_CHECK(dx && dx->args.at("name") == "NVIDIA GeForce RTX 4070");

    g.d3d12_max_feature_level = 0xc100;
    g.mesh_shaders = false;
    dx = find(v = graphics_checks(g), "graphics.dx");
    EF_CHECK(dx && dx->level == Level::Info && dx->text_key == "chk.graphics.dx12" && dx->args.at("fl") == "12.1");

    g.d3d12_max_feature_level = 0xb100;
    g.raytracing_tier = 0;
    dx = find(v = graphics_checks(g), "graphics.dx");
    EF_CHECK(dx && dx->text_key == "chk.graphics.dx11" && dx->args.at("fl") == "11.1");
    EF_CHECK(dx && dx->args.count("rt") == 0);

    g.d3d12_max_feature_level.reset();
    dx = find(v = graphics_checks(g), "graphics.dx");
    EF_CHECK(dx && dx->level == Level::Unknown && dx->text_key == "chk.graphics.dx_unknown");

    // D3D12 cihazi olusmadi, D3D11 yedegi calisti
    g.d3d11_feature_level = 0xa100;
    dx = find(v = graphics_checks(g), "graphics.dx");
    EF_CHECK(dx && dx->level == Level::Info && dx->text_key == "chk.graphics.dx11_only");
    EF_CHECK(dx && dx->args.at("fl") == "10.1" && dx->args.count("sm") == 0);
    // D3D12 okunduysa D3D11 alani yok sayilir
    g.d3d12_max_feature_level = 0xc000;
    dx = find(v = graphics_checks(g), "graphics.dx");
    EF_CHECK(dx && dx->text_key == "chk.graphics.dx12" && dx->args.at("fl") == "12.0");
}

EF_TEST(readiness_graphics_dx9_legacy_runtime) {
    auto g = ultimate_gpu();
    auto v = graphics_checks(g);
    const auto* c = find(v, "graphics.dx9");
    EF_CHECK(c && c->level == Level::Good && c->action == ActionKind::None);

    g.dx9_legacy_runtime = false;
    c = find(v = graphics_checks(g), "graphics.dx9");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.graphics.dx9_missing");
    EF_CHECK(c && c->action == ActionKind::OpenUrl &&
              c->action_arg == "https://www.microsoft.com/en-us/download/details.aspx?id=35");

    g.dx9_legacy_runtime.reset();
    c = find(v = graphics_checks(g), "graphics.dx9");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.dx9_unknown");
}

EF_TEST(readiness_graphics_driver_age_rules) {
    auto g = ultimate_gpu();
    auto v = graphics_checks(g);
    const auto* c = find(v, "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Good && c->text_key == "chk.graphics.driver_recent");
    EF_CHECK(c && c->args.at("version") == "591.44");  // NVIDIA pazarlama surumu
    EF_CHECK(c && c->args.at("date") == "2026-10-05" && c->args.at("days") == "30");

    g.driver_date_unix = kNow - 180 * kDay;  // sinir: 180 gun eski sayilmaz
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Good);

    g.driver_date_unix = kNow - 181 * kDay;
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.graphics.driver_old");
    EF_CHECK(c && c->action == ActionKind::OpenUrl && c->action_arg == "https://www.nvidia.com/Download/index.aspx");
    EF_CHECK(c && c->args.at("months") == "6");

    g.vendor = "amd";
    g.driver_version = "32.0.21025.10016";
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->action_arg == "https://www.amd.com/en/support/download/drivers.html");
    EF_CHECK(c && c->args.at("version") == "32.0.21025.10016");
    g.vendor = "intel";
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->action_arg == "https://www.intel.com/content/www/us/en/download-center/home.html");
    g.vendor = "other";  // taninmayan uretici: yas tavsiyesi yok
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.driver_vendor_unknown");
    EF_CHECK(c && c->action == ActionKind::None && c->action_arg.empty() && c->args.empty());
    g.latest_driver_version = "99.0.0.1";
    EF_CHECK(find(v = graphics_checks(g), "graphics.driver.latest") == nullptr);
}

EF_TEST(readiness_graphics_microsoft_adapter_without_proof_is_unknown) {
    // Yazilim olmayan 0x1414 adaptor (RDP / Hyper-V / dolayli ekran): "surucu yok" DENMEZ.
    auto g = ultimate_gpu();
    g.vendor = "microsoft";
    g.gpu_name = "Microsoft Remote Display Adapter";
    g.latest_driver_version = "1.0";
    auto v = graphics_checks(g);
    EF_CHECK(find(v, "graphics.driver.missing") == nullptr);
    const auto* c = find(v, "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.driver_ms_adapter");
    EF_CHECK(c && c->args.at("name") == "Microsoft Remote Display Adapter");
    EF_CHECK(c && c->action == ActionKind::None && c->action_arg.empty());
    EF_CHECK(find(v, "graphics.driver.latest") == nullptr);
    g.gpu_name.clear();  // ad yok: yer tutucusuz genel metin
    v = graphics_checks(g);
    c = find(v, "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.driver_unknown" && c->args.empty());
}

EF_TEST(readiness_reader_graphics_vendor_key_requires_basic_display_proof) {
    using V = std::optional<std::vector<std::string>>;
    // Surucusuzluk kaniti: yalniz tum ekran aygitlari BasicDisplay ise.
    EF_CHECK(pr::basic_display_only(V(std::vector<std::string>{"BasicDisplay"})) == std::optional<bool>(true));
    EF_CHECK(pr::basic_display_only(V(std::vector<std::string>{"basicdisplay", "BASICDISPLAY"})) == std::optional<bool>(true));
    EF_CHECK(pr::basic_display_only(V(std::vector<std::string>{"BasicDisplay", "nvlddmkm"})) == std::optional<bool>(false));
    EF_CHECK(pr::basic_display_only(V(std::vector<std::string>{"HyperVideo"})) == std::optional<bool>(false));
    EF_CHECK(pr::basic_display_only(V(std::vector<std::string>{"BasicDisplay", ""})) == std::optional<bool>(false));
    EF_CHECK(pr::basic_display_only(V(std::vector<std::string>{})) == std::optional<bool>(false));
    EF_CHECK(!pr::basic_display_only(std::nullopt).has_value());

    const std::optional<bool> yes(true), no(false), unk;
    // Gercek uretici her zaman kazanir.
    EF_CHECK(pr::graphics_vendor_key(0x10DEu, yes) == "nvidia");
    EF_CHECK(pr::graphics_vendor_key(0x1002u, unk) == "amd");
    EF_CHECK(pr::graphics_vendor_key(0x8086u, no) == "intel");
    EF_CHECK(pr::graphics_vendor_key(0x1234u, unk) == "other");
    // Gercek surucusuz PC: DXGI'de yalniz (elenen) yazilim adaptoru + SetupAPI kaniti -> basic_display.
    EF_CHECK(pr::graphics_vendor_key(std::nullopt, yes) == "basic_display");
    EF_CHECK(pr::graphics_vendor_key(0x1414u, yes) == "basic_display");
    // Kanit yok: Microsoft adaptoru "bilinmiyor"; adaptor de yoksa bos.
    EF_CHECK(pr::graphics_vendor_key(0x1414u, no) == "microsoft");
    EF_CHECK(pr::graphics_vendor_key(0x1414u, unk) == "microsoft");
    EF_CHECK(pr::graphics_vendor_key(std::nullopt, no).empty());
    EF_CHECK(pr::graphics_vendor_key(std::nullopt, unk).empty());
}

EF_TEST(readiness_graphics_no_hardware_adapter_without_proof_is_unknown) {
    // DXGI'de yazilim olmayan adaptor yok ve kanit yok -> vendor "" -> yalniz "dogrulanamadi".
    GraphicsInfo g;
    g.now_unix = kNow;
    g.vendor = pr::graphics_vendor_key(std::nullopt, std::nullopt);
    auto v = graphics_checks(g);
    EF_CHECK(find(v, "graphics.driver.missing") == nullptr);
    const auto* c = find(v, "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.driver_unknown");
    // Ayni durum + SetupAPI kaniti -> Problem no_driver.
    g.vendor = pr::graphics_vendor_key(std::nullopt, pr::basic_display_only(std::vector<std::string>{"BasicDisplay"}));
    v = graphics_checks(g);
    c = find(v, "graphics.driver.missing");
    EF_CHECK(c && c->level == Level::Problem && c->text_key == "chk.graphics.no_driver");
    EF_CHECK(find(v, "graphics.driver.age") == nullptr);
}

EF_TEST(readiness_graphics_basic_display_is_no_driver) {
    auto g = ultimate_gpu();
    g.vendor = "basic_display";
    g.gpu_name = "Microsoft Basic Display Adapter";
    g.latest_driver_version = "1.0";
    auto v = graphics_checks(g);
    const auto* c = find(v, "graphics.driver.missing");
    EF_CHECK(c && c->level == Level::Problem && c->text_key == "chk.graphics.no_driver");
    EF_CHECK(c && c->action == ActionKind::OpenSettings && c->action_arg == "ms-settings:windowsupdate");
    EF_CHECK(find(v, "graphics.driver.age") == nullptr);
    EF_CHECK(find(v, "graphics.driver.latest") == nullptr);
    EF_CHECK(find(v, "graphics.gsync") == nullptr);
}

EF_TEST(readiness_graphics_driver_unknown_cases) {
    auto g = ultimate_gpu();
    g.driver_date_unix.reset();
    auto v = graphics_checks(g);
    const auto* c = find(v, "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.driver_date_unknown");
    EF_CHECK(c && c->args.at("version") == "591.44");
    EF_CHECK(c && c->action == ActionKind::None && c->action_arg.empty());  // Unknown eylem tasimaz

    g.driver_version.clear();
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.driver_unknown");
    EF_CHECK(c && c->args.count("version") == 0);

    g = ultimate_gpu();
    g.driver_version.clear();  // surum yok ama tarih var: yas kurali calisir, surumsuz metin
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Good && c->text_key == "chk.graphics.driver_recent_nover");
    EF_CHECK(c && c->args.count("version") == 0 && c->args.at("date") == "2026-10-05");
    g.driver_date_unix = kNow - 200 * kDay;
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.graphics.driver_old_nover");
    EF_CHECK(c && c->args.count("version") == 0 && c->action == ActionKind::OpenUrl);

    g = ultimate_gpu();
    g.driver_version.clear();
    g.now_unix = 0;  // ne surum ne yas
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->text_key == "chk.graphics.driver_unknown");

    g = ultimate_gpu();
    g.now_unix = 0;  // saat bilinmiyor: yas hesaplanmaz
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Unknown);

    g = ultimate_gpu();
    g.driver_date_unix = kNow + 10 * kDay;  // gelecek tarih -> 0 gun
    c = find(v = graphics_checks(g), "graphics.driver.age");
    EF_CHECK(c && c->level == Level::Good && c->args.at("days") == "0");
}

EF_TEST(readiness_graphics_latest_driver_compare) {
    auto g = ultimate_gpu();
    auto v = graphics_checks(g);
    EF_CHECK(find(v, "graphics.driver.latest") == nullptr);  // cevrimici kontrol yoksa bulgu yok

    g.latest_driver_version = "596.02";
    const auto* c = find(v = graphics_checks(g), "graphics.driver.latest");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.graphics.driver_update");
    EF_CHECK(c && c->args.at("installed") == "591.44" && c->args.at("latest") == "596.02");
    EF_CHECK(c && c->action == ActionKind::OpenUrl);

    g.latest_driver_version = "591.44";
    c = find(v = graphics_checks(g), "graphics.driver.latest");
    EF_CHECK(c && c->level == Level::Good && c->text_key == "chk.graphics.driver_latest");
    EF_CHECK(c && c->action == ActionKind::None);

    g.latest_driver_version = "591.9";  // sayisal karsilastirma: 591.9 < 591.44
    c = find(v = graphics_checks(g), "graphics.driver.latest");
    EF_CHECK(c && c->level == Level::Good);

    // Intel: Windows surumu ile ayni bicim
    g.vendor = "intel";
    g.driver_version = "32.0.101.6913";
    g.latest_driver_version = "32.0.101.8132";
    c = find(v = graphics_checks(g), "graphics.driver.latest");
    EF_CHECK(c && c->level == Level::Advice);

    // AMD Adrenalin surumu (25.10.1) Windows surumuyle karsilastirilamaz -> bulgu yok
    g.vendor = "amd";
    g.driver_version = "32.0.21025.10016";
    g.latest_driver_version = "25.10.1";
    EF_CHECK(find(v = graphics_checks(g), "graphics.driver.latest") == nullptr);

    g.latest_driver_version = "abc";
    EF_CHECK(find(v = graphics_checks(g), "graphics.driver.latest") == nullptr);
    g.latest_driver_version = "";
    EF_CHECK(find(v = graphics_checks(g), "graphics.driver.latest") == nullptr);
}

EF_TEST(readiness_graphics_gsync_rules) {
    auto g = ultimate_gpu();
    auto v = graphics_checks(g);
    const auto* c = find(v, "graphics.gsync");
    EF_CHECK(c && c->level == Level::Good && c->text_key == "chk.graphics.gsync_on");

    g.gsync_enabled = false;  // VRR bit anlamlari dogrulanmadi: yalniz "dogrulayin" bilgisi
    c = find(v = graphics_checks(g), "graphics.gsync");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.graphics.gsync_off");
    EF_CHECK(c && c->action == ActionKind::None);  // yonlendirmeli: NVIDIA Denetim Masasi

    g.gsync_possible = false;
    c = find(v = graphics_checks(g), "graphics.gsync");
    EF_CHECK(c && c->level == Level::Info && c->text_key == "chk.graphics.gsync_na");

    g.gsync_possible.reset();
    c = find(v = graphics_checks(g), "graphics.gsync");
    EF_CHECK(c && c->level == Level::Unknown && c->text_key == "chk.graphics.gsync_unknown");

    g.gsync_enabled.reset();
    g.gsync_possible = true;
    c = find(v = graphics_checks(g), "graphics.gsync");
    EF_CHECK(c && c->level == Level::Unknown);

    g.vendor = "amd";
    EF_CHECK(find(v = graphics_checks(g), "graphics.gsync") == nullptr);
}

EF_TEST(readiness_graphics_empty_info_is_unknown_not_guess) {
    GraphicsInfo g;
    const auto v = graphics_checks(g);
    for (const auto& c : v) {
        EF_CHECK(c.level == Level::Unknown);
        EF_CHECK(c.text_key.rfind("chk.graphics.", 0) == 0);
    }
    EF_CHECK(find(v, "graphics.dx") && find(v, "graphics.dx9") && find(v, "graphics.driver.age"));
}

EF_TEST(readiness_unknown_checks_carry_no_action) {
    GraphicsInfo g;
    g.vendor = "nvidia";
    g.driver_version = "32.0.15.9144";  // tarih yok -> driver_date_unknown
    for (const auto& c : graphics_checks(g))
        if (c.level == Level::Unknown) EF_CHECK(c.action == ActionKind::None && c.action_arg.empty());
    SoftwareInfo s;
    s.board_vendor = "ASUSTeK COMPUTER INC.";
    for (const auto& c : software_checks(s))
        if (c.level == Level::Unknown) EF_CHECK(c.action == ActionKind::None && c.action_arg.empty());
}

EF_TEST(readiness_graphics_urls_are_https_official) {
    auto g = ultimate_gpu();
    g.dx9_legacy_runtime = false;
    g.driver_date_unix = kNow - 400 * kDay;
    g.latest_driver_version = "999.99";
    for (const auto& c : graphics_checks(g)) {
        if (c.action != ActionKind::OpenUrl) continue;
        EF_CHECK(c.action_arg.rfind("https://www.", 0) == 0);
    }
}

// ---- Yazilim / BIOS ----

namespace {
SoftwareInfo base_software() {
    SoftwareInfo s;
    s.now_unix = kNow;
    s.items.push_back({"Microsoft Visual C++ v14 Redistributable (x64)", "14.44.35211.0", kNow - 100 * kDay});
    s.items.push_back({"Microsoft Visual C++ v14 Redistributable (x86)", "14.44.35211.0", std::nullopt});
    s.items.push_back({"Steam", "2.10.91.91", kNow - 400 * kDay});
    s.items.push_back({"NVIDIA App", "11.0.5.420", std::nullopt});
    s.items.push_back({"EA app", "", std::nullopt});
    s.bios_date_unix = kNow - 100 * kDay;
    s.board_vendor = "ASUSTeK COMPUTER INC.";
    s.board_model = "ROG STRIX B650-A GAMING WIFI";
    return s;
}
}  // namespace

EF_TEST(readiness_software_vcredist_present_and_missing) {
    auto s = base_software();
    auto v = software_checks(s);
    const auto* x64 = find(v, "software.vcredist.x64");
    const auto* x86 = find(v, "software.vcredist.x86");
    EF_CHECK(x64 && x64->level == Level::Good && x64->text_key == "chk.software.vcredist_ok");
    EF_CHECK(x64 && x64->args.at("version") == "14.44.35211.0" && x64->args.at("arch") == "x64");
    EF_CHECK(x86 && x86->level == Level::Good && x86->section == Section::Software);
    EF_CHECK(find(v, "software.item.microsoft_visual_c_v14_redistributable_x64") == nullptr);

    s.items.erase(s.items.begin(), s.items.begin() + 2);
    v = software_checks(s);
    x64 = find(v, "software.vcredist.x64");
    x86 = find(v, "software.vcredist.x86");
    EF_CHECK(x64 && x64->level == Level::Advice && x64->text_key == "chk.software.vcredist_missing_x64");
    EF_CHECK(x64 && x64->action == ActionKind::OpenUrl &&
              x64->action_arg == "https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist");
    EF_CHECK(x86 && x86->level == Level::Info && x86->text_key == "chk.software.vcredist_missing_x86");
}

EF_TEST(readiness_software_dotnet_present_missing_and_old) {
    auto s = base_software();
    auto v = software_checks(s);  // .NET verisi yok: kurulu degil, ama uyari degil (yalniz bilgi)
    const auto* d = find(v, "software.dotnet.desktop");
    const auto* f = find(v, "software.dotnet.framework");
    EF_CHECK(d && d->level == Level::Info && d->text_key == "chk.software.dotnet_missing");
    EF_CHECK(d && d->action == ActionKind::OpenUrl && d->action_arg == "https://learn.microsoft.com/dotnet/core/install/windows");
    EF_CHECK(f && f->level == Level::Info && f->text_key == "chk.software.dotnetfx_old");

    s.dotnet_desktop_versions = {"6.0.36", "8.0.11", "8.0.2"};
    s.dotnet_framework_release = 533320;
    v = software_checks(s);
    d = find(v, "software.dotnet.desktop");
    f = find(v, "software.dotnet.framework");
    EF_CHECK(d && d->level == Level::Good && d->text_key == "chk.software.dotnet_ok" && d->args.at("version") == "8.0.11");
    EF_CHECK(f && f->level == Level::Good && f->args.at("version") == "4.8.1");

    s.dotnet_framework_release = 528040;
    v = software_checks(s);
    f = find(v, "software.dotnet.framework");
    EF_CHECK(f && f->level == Level::Good && f->args.at("version") == "4.8");
    s.dotnet_framework_release = 461808;  // 4.7.2: 4.8 degil
    v = software_checks(s);
    f = find(v, "software.dotnet.framework");
    EF_CHECK(f && f->level == Level::Info && f->text_key == "chk.software.dotnetfx_old");
}

EF_TEST(readiness_software_vcredist_name_variants_and_highest) {
    SoftwareInfo s;
    s.now_unix = kNow;
    s.items.push_back({"Microsoft Visual C++ 2015-2022 Redistributable (x64) - 14.38.33135", "14.38.33135.0", {}});
    s.items.push_back({"Microsoft Visual C++ 2015-2022 Redistributable (x64) - 14.40.33810", "14.40.33810.0", {}});
    s.items.push_back({"Microsoft Visual C++ 2013 Redistributable (x86) - 12.0.40664", "12.0.40664.0", {}});
    auto v = software_checks(s);
    const auto* x64 = find(v, "software.vcredist.x64");
    EF_CHECK(x64 && x64->level == Level::Good && x64->args.at("version") == "14.40.33810.0");
    const auto* x86 = find(v, "software.vcredist.x86");  // 2013 (v12) sayilmaz
    EF_CHECK(x86 && x86->level == Level::Info);
}

EF_TEST(readiness_software_items_are_info_with_version_and_age) {
    auto v = software_checks(base_software());
    const auto* steam = find(v, "software.item.steam");
    EF_CHECK(steam && steam->level == Level::Info && steam->text_key == "chk.software.item");
    EF_CHECK(steam && steam->args.at("version") == "2.10.91.91" && steam->args.at("days") == "400");
    EF_CHECK(steam && steam->args.at("months") == "13" && steam->args.at("date") == "2025-09-30");
    const auto* nv = find(v, "software.item.nvidia_app");
    EF_CHECK(nv && nv->text_key == "chk.software.item_nodate" && nv->args.count("date") == 0);
    const auto* ea = find(v, "software.item.ea_app");
    EF_CHECK(ea && ea->text_key == "chk.software.item_bare" && ea->args.at("name") == "EA app");

    // Ayni ad iki kez: tek bulgu
    auto s = base_software();
    s.items.push_back({"Steam", "2.10.91.91", std::nullopt});
    v = software_checks(s);
    int n = 0;
    for (const auto& c : v) n += c.id == "software.item.steam" ? 1 : 0;
    EF_CHECK(n == 1);
}

EF_TEST(readiness_software_bios_rules) {
    auto s = base_software();
    auto v = software_checks(s);
    const auto* b = find(v, "software.bios");
    EF_CHECK(b && b->level == Level::Info && b->text_key == "chk.software.bios_ok");
    EF_CHECK(b && b->args.at("vendor") == "ASUSTeK COMPUTER INC." && b->args.count("model") == 1);

    s.bios_date_unix = kNow - 548 * kDay;
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->level == Level::Info);  // yeni tarih "sorun yok/guncel" demek degil

    s.bios_date_unix = kNow - 549 * kDay;
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->level == Level::Advice && b->text_key == "chk.software.bios_old");
    EF_CHECK(b && b->action == ActionKind::OpenUrl && b->action_arg == "https://www.asus.com/support/download-center/");

    s.board_vendor = "Micro-Star International Co., Ltd.";
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->action_arg == "https://www.msi.com/support/download");
    s.board_vendor = "Gigabyte Technology Co., Ltd.";
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->action_arg == "https://www.gigabyte.com/Support");
    s.board_vendor = "ASRock";
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->action_arg == "https://www.asrock.com/support/index.asp");
    s.board_vendor = "LENOVO";  // guvenilir esleme yok: baglanti verilmez
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->level == Level::Advice && b->action == ActionKind::None && b->action_arg.empty());

    s.bios_date_unix.reset();
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->level == Level::Unknown && b->text_key == "chk.software.bios_unknown");
    s.bios_date_unix = kNow - 900 * kDay;
    s.now_unix = 0;
    b = find(v = software_checks(s), "software.bios");
    EF_CHECK(b && b->level == Level::Unknown);
}

EF_TEST(readiness_software_empty_info) {
    const auto v = software_checks(SoftwareInfo{});
    EF_CHECK(v.size() == 5);  // vc x64, vc x86, .NET masaustu, .NET Framework, bios
    const auto* b = find(v, "software.bios");
    EF_CHECK(b && b->level == Level::Unknown && b->args.empty());
}

// ---- Okuyucu saf yardimcilari ----

EF_TEST(readiness_reader_date_parsers) {
    EF_CHECK(pr::smbios_date_to_unix("11/04/2026") == kNow);
    EF_CHECK(pr::smbios_date_to_unix("01/01/1970") == std::nullopt);  // 1980 oncesi gecersiz
    EF_CHECK(pr::smbios_date_to_unix("03/15/99") == pr::smbios_date_to_unix("03/15/1999"));
    EF_CHECK(pr::smbios_date_to_unix("03/15/05") == pr::smbios_date_to_unix("03/15/2005"));
    EF_CHECK(pr::smbios_date_to_unix("13/01/2024") == std::nullopt);
    EF_CHECK(pr::smbios_date_to_unix("2024-01-01") == std::nullopt);
    EF_CHECK(pr::smbios_date_to_unix("") == std::nullopt);
    EF_CHECK(pr::smbios_date_to_unix("12/31/2023 ").has_value());
    EF_CHECK(pr::install_date_to_unix("20261104") == kNow);
    EF_CHECK(pr::install_date_to_unix("2026114") == std::nullopt);
    EF_CHECK(pr::install_date_to_unix("2026x104") == std::nullopt);
    EF_CHECK(pr::install_date_to_unix("20260230").has_value() == true);  // gun 1..31 kaba kontrol
}

EF_TEST(readiness_reader_canonical_software_names) {
    EF_CHECK(pr::canonical_software_name("Microsoft Visual C++ 2015-2022 Redistributable (x64) - 14.40.33810") ==
              "Microsoft Visual C++ v14 Redistributable (x64)");
    EF_CHECK(pr::canonical_software_name("Microsoft Visual C++ v14 Redistributable (x86) - 14.50.35719") ==
              "Microsoft Visual C++ v14 Redistributable (x86)");
    EF_CHECK(pr::canonical_software_name("Microsoft Visual C++ 2013 Redistributable (x64) - 12.0.40664").empty());
    EF_CHECK(pr::canonical_software_name("Microsoft Visual C++ 2022 X64 Minimum Runtime - 14.40.33810").empty());
    EF_CHECK(pr::canonical_software_name("Steam") == "Steam");
    EF_CHECK(pr::canonical_software_name("Steamworks Common Redistributables").empty());
    EF_CHECK(pr::canonical_software_name("NVIDIA app 11.0.5.420") == "NVIDIA App");
    EF_CHECK(pr::canonical_software_name("NVIDIA GeForce Experience 3.28.0.417") == "NVIDIA GeForce Experience");
    EF_CHECK(pr::canonical_software_name("AMD Software") == "AMD Software: Adrenalin Edition");
    EF_CHECK(pr::canonical_software_name("Intel(R) Graphics Software") == "Intel Graphics Software");
    EF_CHECK(pr::canonical_software_name("Intel(R) Arc Control") == "Intel Arc Control");
    EF_CHECK(pr::canonical_software_name("Epic Games Launcher") == "Epic Games Launcher");
    EF_CHECK(pr::canonical_software_name("Battle.net") == "Battle.net");
    EF_CHECK(pr::canonical_software_name("EA app") == "EA app");
    EF_CHECK(pr::canonical_software_name("Ubisoft Connect") == "Ubisoft Connect");
    EF_CHECK(pr::canonical_software_name("Riot Client") == "Riot Client");
    EF_CHECK(pr::canonical_software_name("Notepad++ (64-bit x64)").empty());
}

EF_TEST(readiness_reader_software_version_compare_numeric) {
    EF_CHECK(pr::software_version_less("14.9.0", "14.10.0"));  // dizgede "14.9" > "14.10" olurdu
    EF_CHECK(!pr::software_version_less("14.10.0", "14.9.0"));
    EF_CHECK(pr::software_version_less("2.9.91.91", "2.10.91.91"));
    EF_CHECK(!pr::software_version_less("1.2", "1.2.0") && !pr::software_version_less("1.2.0", "1.2"));
    EF_CHECK(pr::software_version_less("", "1.0"));
    EF_CHECK(pr::software_version_less("abc", "abd"));  // sayisal degil: sozluk sirasi
}

EF_TEST(readiness_reader_merge_software_item_keeps_highest) {
    std::vector<SoftwareItem> items;
    pr::merge_software_item(items, {"Steam", "2.10.91.91", std::nullopt});
    pr::merge_software_item(items, {"Steam", "2.9.91.91", kNow});  // dusuk surum: degismez
    EF_CHECK(items.size() == 1 && items[0].version == "2.10.91.91" && !items[0].date_unix);
    pr::merge_software_item(items, {"Steam", "2.10.91.91", kNow});  // ayni surum + tarih: tarihli kalir
    EF_CHECK(items.size() == 1 && items[0].date_unix == kNow);
    pr::merge_software_item(items, {"Steam", "", std::nullopt});  // bos surum yenisini ezmez
    EF_CHECK(items[0].version == "2.10.91.91");
    pr::merge_software_item(items, {"Steam", "2.11.0.0", std::nullopt});
    EF_CHECK(items[0].version == "2.11.0.0");
    pr::merge_software_item(items, {"EA app", "13.1", std::nullopt});
    EF_CHECK(items.size() == 2);
}
