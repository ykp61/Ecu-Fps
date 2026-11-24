#include "core/games/cs2.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(cs2_video_cfg_parse_typed) {
    // Bu PC'de gozlenen dosyanin kisaltilmis hali (Version 16).
    const char* t = "\"video.cfg\"\n{\n\t\"Version\"\t\t\"16\"\n\t\"setting.defaultres\"\t\t\"2560\"\n\t\"setting.defaultresheight\"\t\t\"1440\"\n"
                    "\t\"setting.refreshrate_numerator\"\t\t\"480168\"\n\t\"setting.refreshrate_denominator\"\t\t\"1000\"\n\t\"setting.fullscreen\"\t\t\"1\"\n"
                    "\t\"setting.mat_vsync\"\t\t\"0\"\n\t\"setting.msaa_samples\"\t\t\"4\"\n\t\"setting.videocfg_fsr_detail\"\t\t\"0\"\n"
                    "\t\"setting.r_low_latency\"\t\t\"2\"\n\t\"setting.videocfg_hdr_detail\"\t\t\"-1\"\n}\n";
    const auto kv = cs2::parse_video_cfg(t);
    EF_CHECK(kv.has_value());
    const auto v = cs2::typed(*kv);
    EF_CHECK(v.version == 16 && v.width == 2560 && v.height == 1440 && v.fullscreen == true && v.vsync == false);
    EF_CHECK(v.refresh_hz && *v.refresh_hz > 480.1 && *v.refresh_hz < 480.2);
    EF_CHECK(v.msaa_samples == 4 && v.fsr_detail == 0 && v.low_latency == 2 && !v.shadow_quality);  // eksik alan nullopt
}

EF_TEST(cs2_parse_rejects_corrupt_and_adapter_is_read_only) {
    EF_CHECK(!cs2::parse_video_cfg("\"video.cfg\"\n{\n\t\"Version\"\t\"16\"\n"));  // kapanmamis
    EF_CHECK(!cs2::parse_video_cfg("\"a\"{\"b\"{\"c\"\"d\"}}"));                   // beklenmeyen ic bolum
    EF_CHECK(!cs2::parse_video_cfg("garbage"));
    EF_CHECK(!cs2::parse_video_cfg(""));
    const auto rec = cs2::adapter_record();
    GameIdentity g{"steam", "730", "Counter-Strike 2", "C:\\cs2", "26393695", "", ""};
    const auto e = effective(g, {rec}, false);
    EF_CHECK(!e.has(Capability::ConfigWrite) && !e.has(Capability::BuiltinBenchmark));
    EF_CHECK(e.has(Capability::Observation) && e.reasons[0] == Invalidation::VersionChanged);  // dogrulanmis surum yok
}

EF_TEST(cs2_field_edit_preserves_others_and_restore_conflicts) {
    const std::string orig = "\"video.cfg\"\n{\n\t\"Version\"\t\t\"16\"\n\t\"setting.mat_vsync\"\t\t\"1\"\n\t\"setting.msaa_samples\"\t\t\"4\"\n}\n";
    const auto w = cs2::set_field(orig, "setting.mat_vsync", "0");
    EF_CHECK(w && w->size() == orig.size());
    EF_CHECK(cs2::parse_video_cfg(*w)->at("setting.mat_vsync") == "0" && cs2::parse_video_cfg(*w)->at("setting.msaa_samples") == "4");
    EF_CHECK(!cs2::set_field(orig, "setting.missing", "1"));   // olmayan alan eklenmez
    EF_CHECK(!cs2::set_field(orig, "Version", "1\"x"));        // enjeksiyon reddi
    EF_CHECK(!cs2::set_field("bozuk {", "Version", "1"));      // bozuk dosyaya yazilmaz
    // Oyun sonradan BASKA bir alani degistirdi: geri alma yalniz bizim alanimizi eski haline getirir.
    auto later = cs2::set_field(*w, "setting.msaa_samples", "8");
    auto r = cs2::restore_field(*later, "setting.mat_vsync", "1", "0");
    EF_CHECK(r.text && !r.conflict);
    const auto kv = cs2::parse_video_cfg(*r.text);
    EF_CHECK(kv->at("setting.mat_vsync") == "1" && kv->at("setting.msaa_samples") == "8");
    // Kullanici AYNI alani degistirdi: cakisma, ustune yazma yok.
    auto user = cs2::set_field(*w, "setting.mat_vsync", "2");
    r = cs2::restore_field(*user, "setting.mat_vsync", "1", "0");
    EF_CHECK(r.conflict && !r.text);
}
