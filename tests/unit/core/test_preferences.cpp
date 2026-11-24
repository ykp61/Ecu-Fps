#include "core/settings/preferences.h"
#include "ef_test.h"

using ef::core::settings::Preferences;

EF_TEST(preferences_defaults_and_roundtrip) {
    Preferences d;
    EF_CHECK(!d.welcome_seen && d.app_animations && d.item_selected("plan.power_mode"));  // varsayilanlar
    Preferences p;
    p.welcome_seen = true;
    p.app_animations = false;
    p.plan_item_selected["plan.visual_effects"] = false;
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok());
    EF_CHECK(r.value().welcome_seen && !r.value().app_animations);
    EF_CHECK(!r.value().item_selected("plan.visual_effects") && r.value().item_selected("plan.power_mode"));
}

EF_TEST(preferences_reject_bad_and_ignore_unknown) {
    EF_CHECK(!Preferences::parse("{").ok());
    EF_CHECK(!Preferences::parse(R"({"schema_version":9})").ok());
    auto r = Preferences::parse(R"({"schema_version":1,"future_field":3,"app_animations":"yes"})");
    EF_CHECK(r.ok() && r.value().app_animations);  // yanlis tur yok sayilir, varsayilan kalir
}

// B10: hedef FPS ozelligi kaldirildi. Eski dosyadaki "target_fps" alani (gecerli ya da bozuk) dosyayi okunamaz
// yapmaz, diger alanlar okunur ve alan yeniden yazilmaz.
EF_TEST(preferences_old_target_fps_key_is_tolerated_and_dropped) {
    for (const char* text : {R"({"schema_version":1,"target_fps":144,"welcome_seen":true})", R"({"schema_version":1,"target_fps":"x","welcome_seen":true})",
                             R"({"schema_version":1,"target_fps":-60,"welcome_seen":true})"}) {
        const auto r = Preferences::parse(text);
        EF_CHECK(r.ok() && r.value().welcome_seen);
        EF_CHECK(r.ok() && r.value().serialize().find("target_fps") == std::string::npos);
    }
}

// WIN-10: oturumluk hizmet secimi kalici; bozuk/uzun/tekrarli girdi yok sayilir, sinir 8.
EF_TEST(preferences_session_stop_services_round_trip_and_limits) {
    Preferences p;
    EF_CHECK(p.session_stop_services.empty());
    p.session_stop_services = {"Spooler", "Fax"};
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().session_stop_services == std::vector<std::string>({"Spooler", "Fax"}));
    r = Preferences::parse(R"({"schema_version":1})");
    EF_CHECK(r.ok() && r.value().session_stop_services.empty());  // eski dosya: varsayilan bos
    const auto bad = Preferences::parse(
        R"({"schema_version":1,"session_stop_services":["Spooler","Spooler","","012345678901234567890123456789012345",7,"Fax"]})");
    EF_CHECK(bad.ok() && bad.value().session_stop_services == std::vector<std::string>({"Spooler", "Fax"}));
    Preferences many;
    many.session_stop_services = {"a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9"};
    const auto cut = Preferences::parse(many.serialize());
    EF_CHECK(cut.ok() && cut.value().session_stop_services.size() == 8);  // fazlasi yazilmaz/okunmaz
}

EF_TEST(preferences_old_theme_key_is_ignored_and_not_written) {
    // UI-14: tema tercihi kaldirildi. Eski dosyadaki "theme" alani (light/system/dark/neon) hatasiz yuklenir
    // ve bilinmeyen alan sayilir; yeni kayitta tema anahtari yazilmaz.
    for (const char* old : {R"({"schema_version":1,"theme":"light"})", R"({"schema_version":1,"theme":"system"})",
                            R"({"schema_version":1,"theme":"neon"})"}) {
        const auto r = ef::core::settings::Preferences::parse(old);
        EF_CHECK(r.ok());
    }
    ef::core::settings::Preferences p;
    const auto out = p.serialize();
    EF_CHECK(out.find("\"theme\"") == std::string::npos);
}

EF_TEST(preferences_incident_days_round_trip_and_validation) {
    Preferences p;
    EF_CHECK(p.incident_days == 7);
    p.incident_days = 30;
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().incident_days == 30);
    p.incident_days = 12;  // bozuk bellek degeri dosyaya tasinmaz
    r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().incident_days == 7);
    r = Preferences::parse(R"({"schema_version":1})");
    EF_CHECK(r.ok() && r.value().incident_days == 7);
    for (const char* text : {R"({"schema_version":1,"incident_days":14})", R"({"schema_version":1,"incident_days":"30"})",
                             R"({"schema_version":1,"incident_days":-30})"}) {
        r = Preferences::parse(text);
        EF_CHECK(r.ok() && r.value().incident_days == 7);
    }
}

// Zehir yesili: RGB isik seridi tercihi varsayilan acik, kalici; eski dosyada alan yoksa acik kalir.
EF_TEST(preferences_strip_enabled_round_trip) {
    Preferences d;
    EF_CHECK(d.strip_enabled);
    Preferences p;
    p.strip_enabled = false;
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && !r.value().strip_enabled);
    auto old = Preferences::parse(R"({"schema_version":1,"app_animations":false})");
    EF_CHECK(old.ok() && old.value().strip_enabled && !old.value().app_animations);
    auto bad = Preferences::parse(R"({"schema_version":1,"strip_enabled":"no"})");
    EF_CHECK(bad.ok() && bad.value().strip_enabled);
}

// Zorunlu guncelleme tekrar oynatma tabani (spec §2): kalici, eski dosyada yoksa 0, bozuk/negatif deger okunmaz.
EF_TEST(preferences_release_manifest_seen_round_trip) {
    Preferences p;
    EF_CHECK(p.release_manifest_seen == 0);
    EF_CHECK(p.serialize().find("release_manifest_seen") == std::string::npos);  // 0 yazilmaz
    p.release_manifest_seen = 42;
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().release_manifest_seen == 42);
    auto old = Preferences::parse(R"({"schema_version":1})");
    EF_CHECK(old.ok() && old.value().release_manifest_seen == 0);
    auto neg = Preferences::parse(R"({"schema_version":1,"release_manifest_seen":-5})");
    EF_CHECK(neg.ok() && neg.value().release_manifest_seen == 0);
    auto str = Preferences::parse(R"({"schema_version":1,"release_manifest_seen":"9"})");
    EF_CHECK(str.ok() && str.value().release_manifest_seen == 0);
}

// Denetim 5.2: dil tercihi kaydedilir ve geri okunur; eski dosya / gecersiz deger "system".
EF_TEST(prefs_ui_language_roundtrip_and_validation) {
    Preferences p;
    EF_CHECK(p.ui_language == "system");
    EF_CHECK(p.serialize().find("ui_language") == std::string::npos);  // varsayilan yazilmaz (eski surumle ayni dosya)
    p.ui_language = "en-US";
    auto back = Preferences::parse(p.serialize());
    EF_CHECK(back.ok() && back.value().ui_language == "en-US");
    p.ui_language = "tr-TR";
    back = Preferences::parse(p.serialize());
    EF_CHECK(back.ok() && back.value().ui_language == "tr-TR");
    std::string bad = Preferences{}.serialize();
    bad.insert(bad.rfind('}'), R"(,"ui_language":"xx-YY")");
    back = Preferences::parse(bad);
    EF_CHECK(back.ok() && back.value().ui_language == "system");
}

// Uyumluluk #26: kurulum penceresi bayat kopyasiyla tepsi uygulamasinin tercihlerini (dil, oyunlar) geri almaz.
EF_TEST(preferences_setup_merge_keeps_tray_fields) {
    Preferences disk;  // tepsi uygulamasinin kurulum acikken yazdigi guncel dosya
    disk.ui_language = "en-US";
    disk.manual_game_exes = {R"(C:\Games\a.exe)"};
    disk.live_test_view = false;
    disk.release_manifest_seen = 9;
    disk.autostart_initialized = true;
    disk.start_with_windows = false;
    Preferences setup;  // kurulumun acilista okudugu eski kopya + kendi degisiklikleri
    setup.ui_language = "system";
    setup.release_manifest_seen = 7;
    setup.autostart_initialized = true;
    setup.start_with_windows = true;
    setup.autostart_notice_pending = false;

    const auto m = ef::core::settings::merge_setup_fields(disk, setup, true);
    EF_CHECK(m.ui_language == "en-US");                       // dil geri alinmaz (saha hatasi)
    EF_CHECK(m.manual_game_exes == disk.manual_game_exes && !m.live_test_view);
    EF_CHECK(m.release_manifest_seen == 9);                   // tekrar oynatma korumasi asla geriye yazilmaz
    EF_CHECK(m.autostart_initialized && m.start_with_windows);  // kurulumun kendi alani yazilir

    setup.release_manifest_seen = 12;
    const auto n = ef::core::settings::merge_setup_fields(disk, setup, false);
    EF_CHECK(n.release_manifest_seen == 12);
    EF_CHECK(!n.start_with_windows);  // bu kayit baslatma alanina dokunmaz: tepsideki secim korunur
    EF_CHECK(n.ui_language == "en-US");
    EF_CHECK(Preferences::parse(n.serialize()).value().ui_language == "en-US");
}

EF_TEST(preferences_tray_merge_keeps_setup_fields) {
    Preferences baseline;  // tepsinin acilista okudugu dosya
    baseline.release_manifest_seen = 7;
    baseline.autostart_initialized = true;
    baseline.start_with_windows = true;
    Preferences disk = baseline;  // kurulum tepsi acikken yazdi
    disk.release_manifest_seen = 11;
    disk.start_with_windows = false;
    Preferences tray = baseline;  // tepsi yalniz dili degistirdi
    tray.ui_language = "en-US";

    const auto m = ef::core::settings::merge_tray_fields(tray, disk, baseline);
    EF_CHECK(m.release_manifest_seen == 11);  // tekrar oynatma korumasi geriye yazilmaz
    EF_CHECK(m.autostart_initialized && !m.start_with_windows);  // kurulumun yazdigi Run secimi korunur
    EF_CHECK(m.ui_language == "en-US");                           // tepsinin kendi degisikligi yazilir

    Preferences changed = tray;  // tepsi bu kayitta baslatmayi kendisi kapatti
    changed.start_with_windows = false;
    changed.release_manifest_seen = 13;
    disk.start_with_windows = true;  // diskte acik (kurulum dokunmadi), bildirimi kurulum yazdi
    disk.autostart_notice_pending = true;
    const auto n = ef::core::settings::merge_tray_fields(changed, disk, baseline);
    EF_CHECK(!n.start_with_windows);          // tepsinin secimi yazilir
    EF_CHECK(n.release_manifest_seen == 13);  // buyuk olan
    EF_CHECK(n.autostart_notice_pending);     // bildirim alanina tepsi dokunmadi: diskteki korunur
}

// P4: "Fark yok" grup kaydi kalici; eski dosyada yok (bos), bos liste yazilmaz, bozuk/tekrarli girdi yok sayilir.
EF_TEST(preferences_proof_no_effect_groups_round_trip) {
    Preferences p;
    EF_CHECK(p.proof_no_effect_groups.empty());
    EF_CHECK(p.serialize().find("proof_no_effect_groups") == std::string::npos);
    p.proof_no_effect_groups = {"plan.power_mode", "plan.vfx"};
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && r.value().proof_no_effect_groups == p.proof_no_effect_groups);
    auto old = Preferences::parse(R"({"schema_version":1,"welcome_seen":true})");
    EF_CHECK(old.ok() && old.value().proof_no_effect_groups.empty() && old.value().welcome_seen);
    auto bad = Preferences::parse(R"({"schema_version":1,"proof_no_effect_groups":["plan.vfx","plan.vfx","",3,null,"plan.power_mode"]})");
    EF_CHECK(bad.ok() && bad.value().proof_no_effect_groups == std::vector<std::string>({"plan.vfx", "plan.power_mode"}));
    auto wrong = Preferences::parse(R"({"schema_version":1,"proof_no_effect_groups":"plan.vfx"})");
    EF_CHECK(wrong.ok() && wrong.value().proof_no_effect_groups.empty());
}
// R-C2: oyun kapaninca temizlik onerisi tercihi (yalniz bildirim; varsayilan acik, eski dosyalar okunur).
EF_TEST(rc2_cleanup_hint_pref_defaults_on_and_round_trips) {
    Preferences p;
    EF_CHECK(p.cleanup_hint_after_game);
    p.cleanup_hint_after_game = false;
    auto r = Preferences::parse(p.serialize());
    EF_CHECK(r.ok() && !r.value().cleanup_hint_after_game);
    r = Preferences::parse(R"({"schema_version":1,"welcome_seen":true})");  // eski dosya: alan yok -> varsayilan acik
    EF_CHECK(r.ok() && r.value().cleanup_hint_after_game);
    r = Preferences::parse(R"({"schema_version":1,"cleanup_hint_after_game":"no"})");  // yanlis tur yok sayilir
    EF_CHECK(r.ok() && r.value().cleanup_hint_after_game);
    r = Preferences::parse(R"({"schema_version":1,"cleanup_hint_after_game":false})");
    EF_CHECK(r.ok() && !r.value().cleanup_hint_after_game);
}
