// WIN-09: baslangic tavsiyesi kurallari (saf cekirdek, salt okuma). Gercekci girdi listesi.
#include "core/adapters/startup/recommend.h"
#include "ef_test.h"

using namespace ef::core::adapters::startup;

namespace {
StartupItem item(std::string name, std::string cmd, bool enabled = true, Location l = Location::UserRun, std::string pub = {}) {
    StartupItem i;
    i.name = std::move(name);
    i.command = std::move(cmd);
    i.enabled = enabled;
    i.location = l;
    i.publisher = std::move(pub);
    return i;
}
Recommendation rec(std::string name, std::string cmd, bool enabled = true, std::string pub = {}) {
    return recommend(item(std::move(name), std::move(cmd), enabled, Location::UserRun, std::move(pub)));
}
}  // namespace

EF_TEST(windows_and_security_entries_are_never_recommended) {
    const auto sec = rec("SecurityHealth", "%windir%\\system32\\SecurityHealthSystray.exe");
    EF_CHECK(sec.advice == Advice::Keep && sec.rule == AdviceRule::Security);
    const auto ctf = rec("ctfmon", "C:\\Windows\\System32\\ctfmon.exe");
    EF_CHECK(ctf.advice == Advice::Keep && ctf.rule == AdviceRule::WindowsComponent);
    for (const auto& r : {rec("avast! Antivirus", "\"C:\\Program Files\\Avast Software\\Avast\\AvLaunch.exe\" /gui"),
                          rec("ESET", "\"C:\\Program Files\\ESET\\ESET Security\\ecmds.exe\" /run"),
                          rec("Malwarebytes Tray", "\"C:\\Program Files\\Malwarebytes\\Anti-Malware\\mbamtray.exe\""),
                          rec("Riot Vanguard", "\"C:\\Program Files\\Riot Vanguard\\vgtray.exe\""),
                          rec("FACEIT", "\"C:\\Program Files\\FACEIT AC\\faceitclient.exe\"")})
        EF_CHECK(r.advice == Advice::Keep);
    // rundll32 ucuncu taraf DLL aciyorsa Windows bileseni sayilmaz (taninmaz -> NoAdvice)
    const auto rd = rec("VendorThing", "C:\\Windows\\System32\\rundll32.exe \"C:\\Program Files\\Vendor\\thing.dll\",Run");
    EF_CHECK(rd.rule != AdviceRule::WindowsComponent && rd.advice == Advice::NoAdvice);
}

EF_TEST(drivers_control_panels_and_accessibility_are_kept) {
    for (const auto& r : {rec("RtkAudUService", "\"C:\\Windows\\System32\\RtkAudUService64.exe\" -background"),
                          rec("NvBackend", "\"C:\\Program Files (x86)\\NVIDIA Corporation\\Update Core\\NvBackend.exe\""),
                          rec("AMDNoiseSuppression", "\"C:\\Program Files\\AMD\\CNext\\CNext\\cncmd.exe\" startup"),
                          rec("LGHUB", "\"C:\\Program Files\\LGHUB\\lghub.exe\" --background"),
                          rec("Razer Synapse", "\"C:\\Program Files (x86)\\Razer\\Synapse3\\WPFUI\\Framework\\Razer Synapse 3 Host\\Razer Synapse 3.exe\""),
                          rec("iCUE", "\"C:\\Program Files\\Corsair\\CORSAIR iCUE 5 Software\\iCUE.exe\" --autorun"),
                          rec("WavesSvc", "\"C:\\Program Files\\Waves\\MaxxAudio\\WavesSvc64.exe\" -Jack"),
                          rec("Narrator", "C:\\Windows\\System32\\Narrator.exe")}) {
        EF_CHECK(r.advice == Advice::Keep);
        EF_CHECK(r.rule == AdviceRule::HardwareControl || r.rule == AdviceRule::Accessibility);
    }
}

EF_TEST(cloud_sync_launchers_and_microsoft_apps_get_no_advice) {
    const auto od = rec("OneDrive", "\"C:\\Users\\u\\AppData\\Local\\Microsoft\\OneDrive\\OneDrive.exe\" /background");
    EF_CHECK(od.advice == Advice::NoAdvice && od.rule == AdviceRule::CloudSync);
    EF_CHECK(rec("GoogleDriveFS", "\"C:\\Program Files\\Google\\Drive File Stream\\launch.bat\"").rule == AdviceRule::CloudSync);
    for (const auto& r : {rec("Steam", "\"C:\\Program Files (x86)\\Steam\\steam.exe\" -silent"),
                          rec("EpicGamesLauncher", "\"C:\\Program Files (x86)\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe\" -silent"),
                          rec("Battle.net", "\"C:\\Program Files (x86)\\Battle.net\\Battle.net Launcher.exe\" --autostarted"),
                          rec("EADM", "\"C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe\" -silent")}) {
        EF_CHECK(r.advice == Advice::NoAdvice && r.rule == AdviceRule::GameLauncher);
    }
    const auto edge = rec("MicrosoftEdgeAutoLaunch_0123", "\"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" --no-startup-window");
    EF_CHECK(edge.advice == Advice::NoAdvice && edge.rule == AdviceRule::MicrosoftApp);
    const auto teams = rec("ms-teams", "\"C:\\Users\\u\\AppData\\Local\\Packages\\x\\teams.exe\"", true, "Microsoft Corporation");
    EF_CHECK(teams.rule == AdviceRule::MicrosoftApp);
}

EF_TEST(updaters_and_optional_apps_are_recommended) {
    for (const auto& r : {rec("Adobe ARM", "\"C:\\Program Files (x86)\\Common Files\\Adobe\\ARM\\1.0\\AdobeARM.exe\""),
                          rec("SunJavaUpdateSched", "\"C:\\Program Files (x86)\\Common Files\\Java\\Java Update\\jusched.exe\""),
                          rec("Opera Browser Assistant", "C:\\Users\\u\\AppData\\Local\\Programs\\Opera\\assistant\\browser_assistant.exe"),
                          rec("GoogleUpdateTaskMachineCore", "\"C:\\Program Files (x86)\\Google\\Update\\GoogleUpdate.exe\" /c")}) {
        EF_CHECK(r.advice == Advice::RecommendDisable);
    }
    EF_CHECK(rec("Adobe ARM", "\"C:\\Program Files (x86)\\Common Files\\Adobe\\ARM\\1.0\\AdobeARM.exe\"").rule == AdviceRule::Updater);
    for (const auto& r : {rec("Spotify", "\"C:\\Users\\u\\AppData\\Roaming\\Spotify\\Spotify.exe\" --autostart --minimized"),
                          rec("Discord", "\"C:\\Users\\u\\AppData\\Local\\Discord\\Update.exe\" --processStart Discord.exe"),
                          rec("com.squirrel.slack.slack", "\"C:\\Users\\u\\AppData\\Local\\slack\\slack.exe\" --process-start-args --startup"),
                          rec("iTunesHelper", "\"C:\\Program Files\\iTunes\\iTunesHelper.exe\"")}) {
        EF_CHECK(r.advice == Advice::RecommendDisable && r.rule == AdviceRule::OptionalApp);
        EF_CHECK(r.reason_key == "startup.advice.optional_app");
    }
}

EF_TEST(disabled_and_unknown_entries_get_no_advice) {
    const auto off = rec("Spotify", "\"C:\\Users\\u\\AppData\\Roaming\\Spotify\\Spotify.exe\"", false);
    EF_CHECK(off.advice == Advice::NoAdvice && off.rule == AdviceRule::AlreadyDisabled);
    // Kapali koruma girdisi Keep bilgisini korur
    EF_CHECK(rec("SecurityHealth", "%windir%\\system32\\SecurityHealthSystray.exe", false).advice == Advice::Keep);
    const auto unk = rec("MyTool", "\"C:\\Tools\\mytool.exe\"");
    EF_CHECK(unk.advice == Advice::NoAdvice && unk.rule == AdviceRule::Unknown && unk.reason_key == "startup.advice.unknown");
}

EF_TEST(recommend_all_matches_from_entry_listing) {
    std::vector<StartupItem> items;
    Entry e;
    e.location = Location::MachineRun64;
    e.name = "SecurityHealth";
    e.command = "C:\\Windows\\system32\\SecurityHealthSystray.exe";
    e.approval = Approval::Enabled;
    items.push_back(from_entry(e));
    e.location = Location::UserFolder;
    e.name = "Spotify.lnk";
    e.command = "C:\\Users\\u\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\Spotify.lnk";
    items.push_back(from_entry(e));
    const auto out = recommend_all(items);
    EF_CHECK(out.size() == 2);
    EF_CHECK(out[0].advice == Advice::Keep);
    // Kisayol Startup klasorunde (\Microsoft\Windows\ altinda) olsa da Windows bileseni sayilmaz
    EF_CHECK(out[1].advice == Advice::RecommendDisable);
    for (auto a : {Advice::RecommendDisable, Advice::Keep, Advice::NoAdvice}) EF_CHECK(!to_string(a).empty() && to_string(a) != "?");
}
