#include "core/perf/background_candidates.h"
#include "ef_test.h"

using namespace ef::core::perf;

namespace {
BgProcessInfo discord(std::uint32_t pid = 100, double cpu = 2.0) {
    BgProcessInfo p;
    p.pid = pid;
    p.creation = 1000 + pid;
    p.name = "Discord.exe";
    p.path = "C:\\Users\\x\\AppData\\Local\\Discord\\app-1.0.9\\Discord.exe";
    p.publisher = "Discord Inc.";
    p.signature_valid = true;
    p.cpu_pct = cpu;
    p.working_set_bytes = 300ull << 20;
    return p;
}
BgProcessInfo onedrive(double cpu = 1.0) {
    BgProcessInfo p;
    p.pid = 200;
    p.creation = 2000;
    p.name = "OneDrive.exe";
    p.path = "C:\\Program Files\\Microsoft OneDrive\\OneDrive.exe";
    p.publisher = "Microsoft Corporation";
    p.signature_valid = true;
    p.cpu_pct = cpu;
    p.working_set_bytes = 150ull << 20;
    return p;
}
BgVerdict v(const BgProcessInfo& p, const BgOptions& o = {}) { return classify_process(p, o); }
}  // namespace

EF_TEST(bgc_allow_list_matches_name_signature_path) {
    EF_CHECK(v(discord()) == BgVerdict::Candidate);
    EF_CHECK(v(onedrive()) == BgVerdict::Candidate);
    std::string rule;
    EF_CHECK(classify_process(discord(), {}, &rule) == BgVerdict::Candidate && rule == "discord");
}

EF_TEST(bgc_spoofed_name_is_not_candidate) {
    auto wrong_path = discord();
    wrong_path.path = "C:\\Users\\x\\Downloads\\Discord.exe";
    EF_CHECK(v(wrong_path) == BgVerdict::NameMatchedButUnverified);
    auto wrong_signer = discord();
    wrong_signer.publisher = "Totally Legit Ltd";
    EF_CHECK(v(wrong_signer) == BgVerdict::NameMatchedButUnverified);
    auto unsigned_bin = discord();
    unsigned_bin.signature_valid = false;
    EF_CHECK(v(unsigned_bin) == BgVerdict::NameMatchedButUnverified);
    auto no_path = discord();
    no_path.path.clear();
    EF_CHECK(v(no_path) == BgVerdict::NameMatchedButUnverified);
    auto signed_no_publisher = discord();
    signed_no_publisher.publisher.clear();
    EF_CHECK(v(signed_no_publisher) == BgVerdict::NameMatchedButUnverified);
    // Dogru imza/yol ama listede olmayan ad.
    auto other = discord();
    other.name = "notdiscord.exe";
    EF_CHECK(v(other) == BgVerdict::NotListed);
}

EF_TEST(bgc_never_classes) {
    auto fg = discord();
    fg.foreground = true;
    EF_CHECK(v(fg) == BgVerdict::Foreground);
    auto el = discord();
    el.elevated = true;
    EF_CHECK(v(el) == BgVerdict::Elevated);
    auto svc = onedrive();
    svc.is_service = true;
    EF_CHECK(v(svc) == BgVerdict::Service);
    auto other_session = discord();
    other_session.user_session = false;
    EF_CHECK(v(other_session) == BgVerdict::OtherSession);
    auto game = discord();
    game.is_game_or_launcher = true;
    EF_CHECK(v(game) == BgVerdict::GameOrLauncher);
    auto self = discord();
    self.is_self = true;
    EF_CHECK(v(self) == BgVerdict::Self);
    auto busy = onedrive();
    busy.busy_transfer = true;
    EF_CHECK(v(busy) == BgVerdict::BusyTransfer);
    auto inter = discord();
    inter.recent_interaction = true;
    EF_CHECK(v(inter) == BgVerdict::RecentInteraction);
    auto ov = discord();
    ov.overlay_hint = true;
    EF_CHECK(v(ov) == BgVerdict::Overlay);

    for (const char* n : {"explorer.exe", "MsMpEng.exe", "EasyAntiCheat.exe", "BEService.exe", "vgc.exe", "NVDisplay.Container.exe",
                          "obs64.exe", "steam.exe", "steamwebhelper.exe", "svchost.exe", "dwm.exe", "EasyAntiCheat_EOS.exe"}) {
        BgProcessInfo p;
        p.pid = 4;
        p.creation = 1;
        p.name = n;
        p.path = "C:\\Program Files\\Whatever\\x.exe";
        p.publisher = "Whoever";
        p.signature_valid = true;
        p.cpu_pct = 50;
        EF_CHECK(v(p) == BgVerdict::NeverHardName);
    }
    // Windows dizini altindaki dosya adi listedekiyle ayni olsa da sistemdir.
    auto sys = discord();
    sys.path = "C:\\Windows\\System32\\Discord.exe";
    EF_CHECK(v(sys) == BgVerdict::NeverHardName);
}

EF_TEST(bgc_steam_and_browsers_policy) {
    BgProcessInfo st;
    st.name = "steam.exe";
    st.path = "C:\\Program Files (x86)\\Steam\\steam.exe";
    st.publisher = "Valve Corp.";
    st.signature_valid = true;
    st.cpu_pct = 5;
    EF_CHECK(v(st) == BgVerdict::NeverHardName);

    BgProcessInfo ch;
    ch.pid = 300;
    ch.creation = 3000;
    ch.name = "chrome.exe";
    ch.path = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    ch.publisher = "Google LLC";
    ch.signature_valid = true;
    ch.cpu_pct = 3;
    EF_CHECK(v(ch) == BgVerdict::Candidate);
    auto rows = select_background_candidates({ch});
    EF_CHECK(rows.size() == 1 && rows[0].cls == BgClass::Browser && rows[0].lower_priority_only);
}

EF_TEST(bgc_threshold_and_hide_list) {
    EF_CHECK(v(discord(100, 0.1)) == BgVerdict::BelowThreshold);
    BgOptions o;
    o.hidden_rule_ids = {"discord"};
    EF_CHECK(v(discord(), o) == BgVerdict::Hidden);
    EF_CHECK(select_background_candidates({discord(), onedrive()}, o).size() == 1);
    // Bilinmeyen kimlik gizleme yoluyla yeni program EKLEYEMEZ ve sonucu degistirmez.
    BgOptions bogus;
    bogus.hidden_rule_ids = {"does_not_exist", "*", ""};
    EF_CHECK(select_background_candidates({discord(), onedrive()}, bogus).size() == 2);
    EF_CHECK(is_known_rule_id("discord") && !is_known_rule_id("does_not_exist") && !is_known_rule_id("steam"));
    // Listede olmayan program hicbir secenekle aday olmaz.
    BgProcessInfo unknown;
    unknown.name = "randomtool.exe";
    unknown.path = "C:\\Tools\\randomtool.exe";
    unknown.publisher = "Someone";
    unknown.signature_valid = true;
    unknown.cpu_pct = 90;
    EF_CHECK(select_background_candidates({unknown}).empty());
}

EF_TEST(bgc_top5_ranked_by_cpu_then_ram) {
    std::vector<BgProcessInfo> snap;
    auto mk = [](const char* name, const char* pub, const char* path, std::uint32_t pid, double cpu, std::uint64_t mb) {
        BgProcessInfo p;
        p.pid = pid;
        p.creation = 10 + pid;
        p.name = name;
        p.publisher = pub;
        p.path = path;
        p.signature_valid = true;
        p.cpu_pct = cpu;
        p.working_set_bytes = mb << 20;
        return p;
    };
    snap.push_back(mk("discord.exe", "Discord Inc.", "C:\\u\\Discord\\discord.exe", 1, 4.0, 300));
    snap.push_back(mk("OneDrive.exe", "Microsoft Corporation", "C:\\Program Files\\Microsoft OneDrive\\OneDrive.exe", 2, 9.0, 100));
    snap.push_back(mk("Dropbox.exe", "Dropbox, Inc", "C:\\Program Files\\Dropbox\\Client\\Dropbox.exe", 3, 4.0, 500));
    snap.push_back(mk("Spotify.exe", "Spotify AB", "C:\\u\\Spotify\\Spotify.exe", 4, 2.0, 400));
    snap.push_back(mk("slack.exe", "Slack Technologies", "C:\\u\\slack\\slack.exe", 5, 1.5, 400));
    snap.push_back(mk("telegram.exe", "Telegram FZ-LLC", "C:\\u\\Telegram Desktop\\Telegram.exe", 6, 1.0, 400));
    snap.push_back(mk("lghub.exe", "Logitech Inc", "C:\\Program Files\\LGHUB\\lghub.exe", 7, 0.7, 400));
    auto rows = select_background_candidates(snap);
    EF_CHECK(rows.size() == 5);
    EF_CHECK(rows[0].rule_id == "onedrive");
    EF_CHECK(rows[1].rule_id == "dropbox");  // CPU esit (4.0), RAM buyuk once
    EF_CHECK(rows[2].rule_id == "discord");
    EF_CHECK(rows[3].rule_id == "spotify");
    EF_CHECK(rows[4].rule_id == "slack");
}

EF_TEST(bgc_multi_process_program_aggregates_and_foreground_drops_whole_program) {
    auto a = discord(10, 1.0), b = discord(11, 1.5);
    auto rows = select_background_candidates({a, b});
    EF_CHECK(rows.size() == 1 && rows[0].processes.size() == 2);
    EF_CHECK(rows[0].cpu_pct > 2.49 && rows[0].cpu_pct < 2.51);
    EF_CHECK(rows[0].working_set_bytes == (600ull << 20));
    // Alt sureclerden biri on plandaysa program bir butun olarak elenir.
    b.foreground = true;
    EF_CHECK(select_background_candidates({a, b}).empty());
    // Ayni adli ama imzasiz taklit, dogru programi dusurmez ve gruba katilmaz.
    auto fake = discord(12, 80.0);
    fake.signature_valid = false;
    auto ok_rows = select_background_candidates({discord(10, 1.0), fake});
    EF_CHECK(ok_rows.size() == 1 && ok_rows[0].processes.size() == 1 && ok_rows[0].processes[0].pid == 10);
    // Girdi degismez; yol/kimlik satira tasinir.
    EF_CHECK(ok_rows[0].processes[0].creation == 1010 && !ok_rows[0].processes[0].path.empty());
}

EF_TEST(bgc_browsers_can_be_excluded_and_name_lookup) {
    BgProcessInfo ch;
    ch.pid = 300;
    ch.creation = 3000;
    ch.name = "chrome.exe";
    ch.path = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    ch.publisher = "Google LLC";
    ch.signature_valid = true;
    ch.cpu_pct = 5.0;
    BgOptions no_browsers;
    no_browsers.include_browsers = false;
    EF_CHECK(select_background_candidates({ch}, no_browsers).empty());
    EF_CHECK(select_background_candidates({ch}).size() == 1);
    EF_CHECK(bg_class_for_name("CHROME.EXE") == BgClass::Browser);
    EF_CHECK(bg_class_for_name("Discord.exe") == BgClass::Chat);
    EF_CHECK(!bg_class_for_name("explorer.exe").has_value());
    EF_CHECK(!bg_class_for_name("steam.exe").has_value());
}
