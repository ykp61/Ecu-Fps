#include "core/perf/background_candidates.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace ef::core::perf {
namespace {

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
std::string norm_path(std::string_view s) {
    std::string r = lower(s);
    for (char& c : r)
        if (c == '/') c = '\\';
    return r;
}
bool contains(const std::string& hay, std::string_view needle) { return hay.find(needle) != std::string::npos; }

// Derlenmis izinli liste. Uc kosul birden: dosya adi (tam) + dogrulanmis yayinci (parca) + yol parcasi (herhangi biri).
// Parcalar kucuk harf; yol parcalari ters egik cizgili. Steam bilerek YOK (oyun Steam'e bagli olabilir).
struct Rule {
    const char* id;
    const char* display;
    BgClass cls;
    std::vector<const char*> names;
    const char* publisher;
    std::vector<const char*> path_parts;
};

const std::vector<Rule>& rules() {
    static const std::vector<Rule> k = {
        // Bulut senkron
        {"onedrive", "OneDrive", BgClass::CloudSync, {"onedrive.exe"}, "microsoft corporation", {"\\microsoft onedrive\\", "\\onedrive\\"}},
        {"dropbox", "Dropbox", BgClass::CloudSync, {"dropbox.exe"}, "dropbox", {"\\dropbox\\"}},
        {"googledrive", "Google Drive", BgClass::CloudSync, {"googledrivefs.exe"}, "google", {"\\google\\drive"}},
        // RGB / periferik yazilimi (kapatilmaz; yalniz yavaslatilir, isik profili degismez)
        {"icue", "Corsair iCUE", BgClass::Peripheral, {"icue.exe"}, "corsair", {"\\corsair\\"}},
        {"ghub", "Logitech G HUB", BgClass::Peripheral, {"lghub.exe", "lghub_agent.exe"}, "logitech", {"\\lghub\\"}},
        {"synapse", "Razer Synapse", BgClass::Peripheral, {"razer synapse 3.exe"}, "razer", {"\\razer\\"}},
        // Sohbet (overlay kullananlar BgProcessInfo::overlay_hint ile elenir)
        {"discord", "Discord", BgClass::Chat, {"discord.exe"}, "discord", {"\\discord\\"}},
        {"teams", "Microsoft Teams", BgClass::Chat, {"ms-teams.exe"}, "microsoft corporation", {"\\teams\\", "\\msteams"}},
        {"slack", "Slack", BgClass::Chat, {"slack.exe"}, "slack technologies", {"\\slack\\"}},
        {"telegram", "Telegram", BgClass::Chat, {"telegram.exe"}, "telegram", {"\\telegram desktop\\"}},
        // Muzik
        {"spotify", "Spotify", BgClass::Music, {"spotify.exe"}, "spotify", {"\\spotify\\", "\\spotifyab.spotifymusic"}},
        // Lansor arka plan kipi (Steam HARIC)
        {"epic", "Epic Games Launcher", BgClass::Launcher, {"epicgameslauncher.exe"}, "epic games", {"\\epic games\\launcher\\"}},
        {"battlenet", "Battle.net", BgClass::Launcher, {"battle.net.exe"}, "blizzard entertainment", {"\\battle.net\\"}},
        {"ubisoft", "Ubisoft Connect", BgClass::Launcher, {"ubisoftconnect.exe", "upc.exe"}, "ubisoft", {"\\ubisoft game launcher\\"}},
        {"gog", "GOG Galaxy", BgClass::Launcher, {"galaxyclient.exe"}, "gog", {"\\gog galaxy\\"}},
        // Tarayicilar: yalniz "Yavaslat"; kapatma bu surumde hic yok
        {"chrome", "Google Chrome", BgClass::Browser, {"chrome.exe"}, "google", {"\\google\\chrome\\"}},
        {"edge", "Microsoft Edge", BgClass::Browser, {"msedge.exe"}, "microsoft corporation", {"\\microsoft\\edge\\"}},
        {"firefox", "Mozilla Firefox", BgClass::Browser, {"firefox.exe"}, "mozilla", {"\\mozilla firefox\\"}},
    };
    return k;
}

// Sert kod "asla" adlari (listede olmayanlar zaten dokunulmaz; bu yalniz hukmu ve onceligi aciklar).
bool never_name(const std::string& n, const std::string& path) {
    static const char* exact[] = {
        // sistem/kabuk
        "explorer.exe", "dwm.exe", "csrss.exe", "winlogon.exe", "svchost.exe", "services.exe", "lsass.exe", "audiodg.exe",
        "system", "smss.exe", "wininit.exe", "sihost.exe", "ctfmon.exe", "fontdrvhost.exe", "conhost.exe",
        "runtimebroker.exe", "taskhostw.exe", "searchhost.exe", "startmenuexperiencehost.exe", "shellexperiencehost.exe",
        // guvenlik
        "msmpeng.exe", "nissrv.exe", "mssense.exe", "securityhealthservice.exe", "securityhealthsystray.exe", "avp.exe",
        // anti-cheat
        "vgc.exe", "vgtray.exe", "beservice.exe", "easyanticheat.exe", "easyanticheat_eos.exe",
        // surucu/GPU hizmetleri
        "nvdisplay.container.exe", "amdrsserv.exe", "radeonsoftware.exe",
        // kayit/yayin
        "obs64.exe", "obs32.exe", "medal.exe",
        // Steam: oyun ona bagli olabilir
        "steam.exe", "steamwebhelper.exe", "steamservice.exe",
    };
    for (const char* e : exact)
        if (n == e) return true;
    static const char* prefix[] = {"easyanticheat", "beservice", "faceit", "mbam", "avast", "nvcontainer", "streamlabs", "xsplit"};
    for (const char* p : prefix)
        if (n.rfind(p, 0) == 0) return true;
    // Windows dizini altindaki her sey sistemdir.
    if (contains(path, ":\\windows\\")) return true;
    return false;
}

const Rule* match_rule(const BgProcessInfo& p, const std::string& n, const std::string& path, bool* name_hit) {
    *name_hit = false;
    for (const Rule& r : rules()) {
        bool nm = false;
        for (const char* x : r.names)
            if (n == x) nm = true;
        if (!nm) continue;
        *name_hit = true;
        if (!p.signature_valid || p.publisher.empty() || !contains(lower(p.publisher), r.publisher)) continue;
        bool pm = false;
        for (const char* x : r.path_parts)
            if (contains(path, x)) pm = true;
        if (!pm) continue;
        return &r;
    }
    return nullptr;
}

// Esik ve gizleme disindaki tum kapilar.
BgVerdict gate(const BgProcessInfo& p, const Rule** rule_out) {
    *rule_out = nullptr;
    const std::string n = lower(p.name);
    const std::string path = norm_path(p.path);
    if (p.is_self) return BgVerdict::Self;
    if (never_name(n, path)) return BgVerdict::NeverHardName;
    if (p.foreground) return BgVerdict::Foreground;
    if (p.is_game_or_launcher) return BgVerdict::GameOrLauncher;
    if (p.is_service) return BgVerdict::Service;
    if (!p.user_session) return BgVerdict::OtherSession;
    if (p.elevated) return BgVerdict::Elevated;
    bool name_hit = false;
    const Rule* r = match_rule(p, n, path, &name_hit);
    if (!r) return name_hit ? BgVerdict::NameMatchedButUnverified : BgVerdict::NotListed;
    if (p.busy_transfer) return BgVerdict::BusyTransfer;
    if (p.recent_interaction) return BgVerdict::RecentInteraction;
    if (p.overlay_hint) return BgVerdict::Overlay;
    *rule_out = r;
    return BgVerdict::Candidate;
}

bool is_hidden(const BgOptions& o, std::string_view id) {
    for (const auto& h : o.hidden_rule_ids)
        if (h == id) return true;
    return false;
}

}  // namespace

std::string_view to_string(BgClass c) noexcept {
    switch (c) {
        case BgClass::CloudSync: return "cloud_sync";
        case BgClass::Peripheral: return "peripheral";
        case BgClass::Chat: return "chat";
        case BgClass::Music: return "music";
        case BgClass::Launcher: return "launcher";
        case BgClass::Browser: return "browser";
    }
    return "?";
}

std::string_view reason_text_tr(BgClass c) noexcept {
    switch (c) {
        case BgClass::CloudSync: return "Bulut senkron programı arka planda çalışıyor";
        case BgClass::Peripheral: return "Çevre birimi yazılımı arka planda çalışıyor";
        case BgClass::Chat: return "Sohbet programı arka planda çalışıyor";
        case BgClass::Music: return "Müzik programı arka planda çalışıyor";
        case BgClass::Launcher: return "Oyun başlatıcısı arka planda çalışıyor";
        case BgClass::Browser: return "Tarayıcı arka planda çalışıyor";
    }
    return {};
}

std::string_view to_string(BgVerdict v) noexcept {
    switch (v) {
        case BgVerdict::Candidate: return "candidate";
        case BgVerdict::NotListed: return "not_listed";
        case BgVerdict::NeverHardName: return "never_hard_name";
        case BgVerdict::Self: return "self";
        case BgVerdict::Foreground: return "foreground";
        case BgVerdict::GameOrLauncher: return "game_or_launcher";
        case BgVerdict::Service: return "service";
        case BgVerdict::OtherSession: return "other_session";
        case BgVerdict::Elevated: return "elevated";
        case BgVerdict::BusyTransfer: return "busy_transfer";
        case BgVerdict::RecentInteraction: return "recent_interaction";
        case BgVerdict::Overlay: return "overlay";
        case BgVerdict::NameMatchedButUnverified: return "name_matched_but_unverified";
        case BgVerdict::BelowThreshold: return "below_threshold";
        case BgVerdict::Hidden: return "hidden";
    }
    return "?";
}

bool is_known_rule_id(std::string_view id) noexcept {
    for (const Rule& r : rules())
        if (id == r.id) return true;
    return false;
}

std::optional<BgClass> bg_class_for_name(std::string_view file_name) {
    const std::string n = lower(file_name);
    for (const Rule& r : rules())
        for (const char* x : r.names)
            if (n == x) return r.cls;
    return std::nullopt;
}

BgVerdict classify_process(const BgProcessInfo& p, const BgOptions& opt, std::string* rule_out) {
    const Rule* r = nullptr;
    const BgVerdict g = gate(p, &r);
    if (g != BgVerdict::Candidate) return g;
    if (rule_out) *rule_out = r->id;
    if (is_hidden(opt, r->id)) return BgVerdict::Hidden;
    if (p.cpu_pct < opt.min_cpu_pct) return BgVerdict::BelowThreshold;
    return BgVerdict::Candidate;
}

std::vector<BgCandidate> select_background_candidates(const std::vector<BgProcessInfo>& snapshot, const BgOptions& opt) {
    struct Group {
        const Rule* rule = nullptr;
        BgCandidate cand;
        bool dropped = false;
    };
    std::map<std::string, Group> groups;
    // Birinci gecis: bir kuralin HERHANGI bir sureci "asla" kapisina takilirsa (on plan, yogun is, etkilesim, ...)
    // programin tamami elenir; ayni adli ama kural eslesmeyen/imzasiz surec gruba katilmaz, grubu da dusurmez.
    for (const auto& p : snapshot) {
        const Rule* r = nullptr;
        const BgVerdict g = gate(p, &r);
        if (g == BgVerdict::Candidate && (opt.include_browsers || r->cls != BgClass::Browser)) {
            auto& grp = groups[r->id];
            grp.rule = r;
            grp.cand.rule_id = r->id;
            grp.cand.display_name = r->display;
            grp.cand.cls = r->cls;
            grp.cand.cpu_pct += p.cpu_pct;
            grp.cand.working_set_bytes += p.working_set_bytes;
            grp.cand.processes.push_back({p.pid, p.creation, p.path});
        }
    }
    // Program duzeyinde eleme: dogrulanmis kurala uyan ama baska kapiya takilan surec ayni programdansa grup duser.
    for (const auto& p : snapshot) {
        const std::string n = lower(p.name);
        const std::string path = norm_path(p.path);
        bool nh = false;
        const Rule* r = match_rule(p, n, path, &nh);
        if (!r) continue;
        const Rule* rr = nullptr;
        if (gate(p, &rr) == BgVerdict::Candidate) continue;
        if (never_name(n, path)) continue;
        auto it = groups.find(r->id);
        if (it != groups.end()) it->second.dropped = true;
    }
    std::vector<BgCandidate> out;
    for (auto& [id, g] : groups) {
        if (g.dropped || is_hidden(opt, id)) continue;
        if (g.cand.cpu_pct < opt.min_cpu_pct) continue;
        out.push_back(std::move(g.cand));
    }
    std::sort(out.begin(), out.end(), [](const BgCandidate& a, const BgCandidate& b) {
        if (a.cpu_pct != b.cpu_pct) return a.cpu_pct > b.cpu_pct;
        if (a.working_set_bytes != b.working_set_bytes) return a.working_set_bytes > b.working_set_bytes;
        return a.rule_id < b.rule_id;
    });
    if (out.size() > opt.max_rows) out.resize(opt.max_rows);
    return out;
}

}  // namespace ef::core::perf
