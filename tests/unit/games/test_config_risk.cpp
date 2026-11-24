#include <string>
#include <vector>

#include "core/games/config_risk.h"
#include "ef_test.h"

using namespace ef::core::games;

namespace {
bool has(const std::vector<ConfigRisk>& v, std::string_view code) {
    for (const auto& r : v)
        if (r.code == code) return true;
    return false;
}
}  // namespace

EF_TEST(config_risk_launch_options_flags_documented_patterns) {
    auto r = scan_launch_options("-insecure -allow_third_party_software -high -threads 8 -w 1280 -h 720");
    EF_CHECK(has(r, "risk.launch.insecure"));
    EF_CHECK(has(r, "risk.launch.third_party"));
    EF_CHECK(has(r, "risk.launch.high_priority"));
    EF_CHECK(has(r, "risk.launch.threads"));
    EF_CHECK(has(r, "risk.launch.resolution_override"));
    EF_CHECK(r.size() == 5);  // -w/-h tek kod
    for (const auto& x : r) EF_CHECK(!x.code.empty() && !x.detail.empty());
}

EF_TEST(config_risk_launch_options_case_insensitive_and_quoted_tokens) {
    auto r = scan_launch_options("\"-HIGH\" -Insecure \"-novid -console\"");
    EF_CHECK(has(r, "risk.launch.high_priority"));
    EF_CHECK(has(r, "risk.launch.insecure"));
    EF_CHECK(!has(r, "risk.launch.threads"));
    EF_CHECK(r.size() == 2);  // tirnak icindeki parca tek belirtec, alt kalip sayilmaz
}

EF_TEST(config_risk_launch_options_clean_and_empty) {
    EF_CHECK(scan_launch_options("").empty());
    EF_CHECK(scan_launch_options("-novid -console +fps_max 0").empty());
}

EF_TEST(config_risk_cs2_video_flags_vsync_borderless_reflex_msaa) {
    cs2::VideoSettings v;
    v.vsync = true;
    v.fullscreen = false;
    v.low_latency = 0;
    v.msaa_samples = 8;
    auto r = scan_cs2_video(v);
    EF_CHECK(has(r, "risk.video.vsync"));
    EF_CHECK(has(r, "risk.video.borderless"));
    EF_CHECK(has(r, "risk.video.reflex_off"));
    EF_CHECK(has(r, "risk.video.msaa"));
    EF_CHECK(r.size() == 4);
}

EF_TEST(config_risk_cs2_video_clean_and_unknown_fields) {
    cs2::VideoSettings v;  // tum alanlar nullopt: risk uretilmez
    EF_CHECK(scan_cs2_video(v).empty());
    v.vsync = false;
    v.fullscreen = true;
    v.low_latency = 2;
    v.msaa_samples = 4;
    EF_CHECK(scan_cs2_video(v).empty());
}

EF_TEST(config_risk_merge_dedups_by_code) {
    auto a = scan_launch_options("-high");
    auto b = scan_launch_options("-high -insecure");
    auto m = merge_risks(a, b);
    EF_CHECK(m.size() == 2);
    EF_CHECK(has(m, "risk.launch.high_priority"));
    EF_CHECK(has(m, "risk.launch.insecure"));
}
