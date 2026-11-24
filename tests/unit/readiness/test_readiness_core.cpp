#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;

EF_TEST(readiness_action_uri_allowlist) {
    EF_CHECK(action_uri_allowed("ms-settings:display-advanced"));
    EF_CHECK(action_uri_allowed("windowsdefender://coreisolation"));
    EF_CHECK(action_uri_allowed("https://www.nvidia.com/Download/index.aspx"));
    EF_CHECK(action_uri_allowed("https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist"));
    EF_CHECK(!action_uri_allowed("https://evil.com/nvidia.com"));
    EF_CHECK(!action_uri_allowed("https://nvidia.com.evil.com/"));
    EF_CHECK(!action_uri_allowed("https://www.nvidia.com/x?y=1"));  // sorgu dizesi yok
    EF_CHECK(!action_uri_allowed("http://www.nvidia.com/"));
    EF_CHECK(!action_uri_allowed("file:///C:/Windows/notepad.exe"));
    EF_CHECK(!action_uri_allowed("ms-settings:"));
    EF_CHECK(!action_uri_allowed("ms-settings:a&calc"));
    EF_CHECK(fallback_settings_uri("fix.game_mode") == "ms-settings:gaming-gamemode" && fallback_settings_uri("x").empty());
}

EF_TEST(readiness_sort_by_section_then_severity) {
    std::vector<Check> v{{"a", Section::Network, Level::Good}, {"b", Section::Display, Level::Good}, {"c", Section::Display, Level::Problem}};
    sort_checks(v);
    EF_CHECK(v[0].id == "c" && v[1].id == "b" && v[2].id == "a");
}

EF_TEST(readiness_pro_settings_slug_known_games_only) {
    EF_CHECK(pro_settings_slug("steam", "730", "Counter-Strike 2") == "cs2");
    EF_CHECK(pro_settings_slug("epic", "Fortnite", "Fortnite") == "fortnite");
    EF_CHECK(pro_settings_slug("steam", "271590", "Grand Theft Auto V").empty());
    EF_CHECK(action_uri_allowed("https://prosettings.net/games/" + pro_settings_slug("steam", "1172470", "") + "/"));
}

// Oyun Hazirligi "Geri al": bulgu -> duzeltme hedefi; yalniz fixes journal'inda ETKIN duzeltmesi olan satir.
EF_TEST(readiness_undo_rows_follow_active_fix_journal) {
    EF_CHECK(fix_target_for_check("windows.game_mode") == "gaming:game_mode");
    EF_CHECK(fix_target_for_check("windows.game_dvr") == "gaming:dvr_background");
    EF_CHECK(fix_target_for_check("windows.windowed_opt") == "gaming:windowed_opt");
    EF_CHECK(fix_target_for_check("windows.hags") == "hags:hwschmode");
    EF_CHECK(fix_target_for_check("windows.fast_startup") == "faststartup:hiberboot");
    EF_CHECK(fix_target_for_check("display.1.refresh") == "display:1:refresh");
    EF_CHECK(fix_target_for_check("display.x.refresh").empty());
    EF_CHECK(fix_target_for_check("display.refresh").empty());
    EF_CHECK(fix_target_for_check("windows.vbs").empty());
    std::vector<Check> checks(4);
    checks[0].id = "windows.game_mode";
    checks[1].id = "windows.hags";
    checks[2].id = "display.0.refresh";
    checks[3].id = "network.gateway";
    const auto none = undo_rows(checks, {});
    EF_CHECK(none.size() == 4 && !none[0] && !none[1] && !none[2] && !none[3]);
    const auto some = undo_rows(checks, {"hags:hwschmode", "display:0:refresh", "gaming:dvr_background"});
    EF_CHECK(!some[0] && some[1] && some[2] && !some[3]);
}

// Uyumluluk B6 (bosluk 10): yeni yenileme hizi kaydi GDI aygit adi tasir; "Geri al" satiri yine eslesir.
EF_TEST(readiness_refresh_undo_matches_device_suffixed_target) {
    EF_CHECK(fix_target_matches("display:0:refresh", "display:0:refresh"));
    EF_CHECK(fix_target_matches("display:0:refresh", "display:0:refresh:DISPLAY2"));
    EF_CHECK(!fix_target_matches("display:0:refresh", "display:1:refresh:DISPLAY2"));
    EF_CHECK(!fix_target_matches("display:0:refresh", "display:0:refresh:bogus"));
    EF_CHECK(!fix_target_matches("display:1:refresh", "display:10:refresh:DISPLAY1"));
    EF_CHECK(!fix_target_matches("gaming:game_mode", "gaming:game_mode:DISPLAY1"));
    std::vector<Check> checks(2);
    checks[0].id = "display.0.refresh";
    checks[1].id = "display.1.refresh";
    const auto rows = undo_rows(checks, {"display:1:refresh:DISPLAY3"});
    EF_CHECK(!rows[0] && rows[1]);
}

// B6 inceleme: satir kendi GDI aygit adini tasiyorsa baska aygitin ayni konumlu kaydi eslesmez; ayni aygitin
// kaydi konum numarasi kaymis olsa da eslesir; aygit bilgisi olmayan eski kayit ayni konumla eslesir.
EF_TEST(readiness_refresh_undo_row_uses_own_device) {
    Check c;
    c.id = "display.1.refresh";
    EF_CHECK(fix_targets_for_check(c) == std::vector<std::string>{"display:1:refresh"});  // aygit yok: eski bicim
    c.args["device"] = "DISPLAY2";
    EF_CHECK(fix_targets_for_check(c) == std::vector<std::string>{"display:1:refresh:DISPLAY2"});
    c.args["device"] = "bogus";
    EF_CHECK(fix_targets_for_check(c) == std::vector<std::string>{"display:1:refresh"});
    EF_CHECK(fix_target_matches("display:1:refresh:DISPLAY2", "display:1:refresh:DISPLAY2"));
    EF_CHECK(!fix_target_matches("display:1:refresh:DISPLAY2", "display:1:refresh:DISPLAY3"));
    EF_CHECK(fix_target_matches("display:1:refresh:DISPLAY2", "display:0:refresh:DISPLAY2"));
    EF_CHECK(fix_target_matches("display:1:refresh:DISPLAY2", "display:1:refresh"));
    EF_CHECK(!fix_target_matches("display:1:refresh:DISPLAY2", "display:0:refresh"));
    EF_CHECK(!fix_target_matches("display:1:refresh:DISPLAY2", "display:1:refresh:bogus"));
    std::vector<Check> checks(2);
    checks[0].id = "display.0.refresh";
    checks[0].args["device"] = "DISPLAY1";
    checks[1].id = "display.1.refresh";
    checks[1].args["device"] = "DISPLAY2";
    // DISPLAY3 ekrani cikarildi; kaydi konum 1'de kaldi: mevcut konum 1 (DISPLAY2) satiri Geri al gostermez.
    auto rows = undo_rows(checks, {"display:1:refresh:DISPLAY3"});
    EF_CHECK(!rows[0] && !rows[1]);
    // Klon birlestirme sonrasi DISPLAY2 konum 0'dan 1'e kaydi: kendi satiri eslesir, DISPLAY1 satiri eslesmez.
    rows = undo_rows(checks, {"display:0:refresh:DISPLAY2"});
    EF_CHECK(!rows[0] && rows[1]);
}

// F-73: arka plan kaydi duzeltmesi uc hedefi tek transaction'da kapatir; geri alma durumu hedeflerden herhangi
// biriyle eslesir (tek anahtar kalmis olsa bile satir "Geri al" gosterir).
EF_TEST(readiness_multi_target_fix_state_follows_any_target) {
    const auto dvr = fix_targets_for_check("windows.game_dvr");
    EF_CHECK(dvr.size() == 3);
    EF_CHECK(dvr[0] == "gaming:dvr_background" && dvr[1] == "gaming:app_capture" && dvr[2] == "gaming:dvr_enabled");
    EF_CHECK(fix_targets_for_check("windows.game_mode").size() == 1);
    EF_CHECK(fix_targets_for_check("windows.vbs").empty());
    std::vector<Check> checks(2);
    checks[0].id = "windows.game_dvr";
    checks[1].id = "windows.game_mode";
    FixTargets t;
    t.active = {"gaming:app_capture"};  // yalniz ikincil hedef etkin
    auto st = undo_states(checks, t);
    EF_CHECK(st[0] == UndoState::Undo && st[1] == UndoState::None);
    t.active.clear();
    t.conflict = {"gaming:dvr_enabled"};  // catisma oncelikli
    st = undo_states(checks, t);
    EF_CHECK(st[0] == UndoState::Conflict);
    t.conflict.clear();
    t.recovery = {"gaming:dvr_background"};
    st = undo_states(checks, t);
    EF_CHECK(st[0] == UndoState::Retry);
}

// "Geri al / Yeniden dene": geri alinmasi dogrulanamamis kayit Retry; ikisi birden varsa Retry; ilgisiz satir None.
EF_TEST(readiness_undo_states_mark_recovery_required_as_retry) {
    std::vector<Check> checks(4);
    checks[0].id = "windows.game_mode";
    checks[1].id = "windows.hags";
    checks[2].id = "display.0.refresh";
    checks[3].id = "network.gateway";
    FixTargets t;
    t.active = {"gaming:game_mode", "hags:hwschmode", "network.gateway"};
    t.recovery = {"hags:hwschmode", "display:0:refresh"};
    const auto st = undo_states(checks, t);
    EF_CHECK(st.size() == 4);
    EF_CHECK(st[0] == UndoState::Undo && st[1] == UndoState::Retry && st[2] == UndoState::Retry && st[3] == UndoState::None);
    EF_CHECK(undo_states(checks, {}).size() == 4 && undo_states(checks, {})[1] == UndoState::None);
}

// Durum ayrimi: Conflict > Retry > Interrupted > Undo (metin durust: yarim kalmis kayitta "geri alma denendi" denmez).
EF_TEST(readiness_undo_states_separate_interrupted_and_conflict) {
    std::vector<Check> checks(4);
    checks[0].id = "windows.game_mode";
    checks[1].id = "windows.hags";
    checks[2].id = "display.0.refresh";
    checks[3].id = "windows.game_dvr";
    FixTargets t;
    t.active = {"gaming:game_mode", "gaming:dvr_background"};
    t.recovery = {"display:0:refresh"};
    t.interrupted = {"gaming:game_mode", "display:0:refresh"};
    t.conflict = {"hags:hwschmode", "gaming:dvr_background"};
    const auto st = undo_states(checks, t);
    EF_CHECK(st[0] == UndoState::Interrupted && st[1] == UndoState::Conflict && st[2] == UndoState::Retry && st[3] == UndoState::Conflict);
}
static Check lv(Level l, std::string id = "x") {
    Check c;
    c.id = std::move(id);
    c.level = l;
    return c;
}

EF_TEST(x17_t01_all_unknown_is_not_success) {
    const auto s = summarize_checks({lv(Level::Unknown), lv(Level::Unknown)});
    EF_CHECK(s.total == 2 && s.verified == 0 && s.unknown == 2 && s.action == 0);
}

EF_TEST(x17_t02_summary_is_order_independent) {
    std::vector<Check> a{lv(Level::Good, "a"), lv(Level::Problem, "b"), lv(Level::Advice, "c"), lv(Level::Unknown, "d"), lv(Level::Info, "e")};
    auto b = a;
    std::reverse(b.begin(), b.end());
    const auto x = summarize_checks(a), y = summarize_checks(b);
    EF_CHECK(x.total == y.total && x.verified == y.verified && x.action == y.action && x.unknown == y.unknown && x.has_problem == y.has_problem);
    EF_CHECK(x.total == 4 && x.verified == 1 && x.action == 2 && x.unknown == 1 && x.has_problem);  // Info payda disi
}

EF_TEST(x17_t03_no_weighted_score_in_summary) {
    // Ozet yalniz adetlerden olusur; bir kuralin sonucunu degistirmek adet degisikligidir, kazanc degil.
    const auto s = summarize_checks({lv(Level::Good), lv(Level::Good)});
    EF_CHECK(s.verified + s.action + s.unknown == s.total);
}

// T06: fare rapor hizi satiri Gecikme bolumunde, MeasureMouse eylemiyle; olcum yokken sayi yok.
EF_TEST(readiness_mouse_polling_check) {
    namespace cr = ef::core::readiness;
    namespace cm = ef::core::measure;
    const auto idle = cr::mouse_polling_check(false, std::nullopt);
    EF_CHECK(idle.id == "latency.mouse_polling" && idle.section == cr::Section::Latency);
    EF_CHECK(idle.action == cr::ActionKind::MeasureMouse && idle.level == cr::Level::Info && idle.args.empty());
    EF_CHECK(idle.text_key == "chk.latency.mouse_polling");
    const auto busy = cr::mouse_polling_check(true, std::nullopt);
    EF_CHECK(busy.action == cr::ActionKind::None && busy.text_key == "chk.latency.mouse_polling.measuring");
    cm::PollingResult bad;  // Insufficient
    EF_CHECK(cr::mouse_polling_check(false, bad).text_key == "chk.latency.mouse_polling.insufficient");
    EF_CHECK(cr::mouse_polling_check(false, bad).args.empty());
    cm::PollingResult ok;
    ok.state = cm::PollingResult::State::Ok;
    ok.hz = 997.6;
    ok.median_interval_ms = 1.0024;
    const auto r = cr::mouse_polling_check(false, ok);
    EF_CHECK(r.text_key == "chk.latency.mouse_polling.result" && r.args.at("hz") == "998" && r.args.at("ms") == "1.00");
    ok.at_least = true;
    EF_CHECK(cr::mouse_polling_check(false, ok).text_key == "chk.latency.mouse_polling.result_min");
    // Bilgi satiri puana girmez
    EF_CHECK(cr::summarize_checks({r}).total == 0);
}
