#include "core/games/protection_gate.h"

namespace ef::core::games {

std::string_view to_string(CompatState s) noexcept {
    switch (s) {
        case CompatState::Unverified: return "unverified";
        case CompatState::Blocked: return "blocked";
        case CompatState::VerifiedForBuild: return "verified_for_build";
        case CompatState::Stale: return "stale";
    }
    return "unverified";
}

const std::vector<CompatEntry>& builtin_matrix() {
    // F-60: bilinen koruma sistemleri oyun bazında listelenir. TÜMÜ Unverified'dır: bu makinede
    // ilgili oyun/koruma sürümüyle canlı ölçüm doğrulanmadı. Liste "bu oyun EAC/BattlEye kullanır"
    // bilgisidir; doğrulanmış uyumluluk veya ban garantisi değildir. Vanguard yalnız makine geneli
    // servis tespitiyle raporlanır (Riot oyunları Steam/Epic taramasında yok).
    static const std::vector<CompatEntry> m{
        {"730", "Counter-Strike 2", "VAC / Trusted Mode", CompatState::Unverified, "", "", "", "", false},
        {"730", "Counter-Strike 2", "FACEIT Anti-Cheat", CompatState::Unverified, "", "", "", "", false},
        {"1172470", "Apex Legends", "Easy Anti-Cheat", CompatState::Unverified, "", "", "", "", false},
        {"578080", "PUBG: BATTLEGROUNDS", "BattlEye", CompatState::Unverified, "", "", "", "", false},
        {"359550", "Tom Clancy's Rainbow Six Siege", "BattlEye", CompatState::Unverified, "", "", "", "", false},
        {"Fortnite", "Fortnite", "Easy Anti-Cheat", CompatState::Unverified, "", "", "", "", false},
        // T05: Riot (kimlik = product_settings urun adi) ve Call of Duty (Steam kimlikleri; Battle.net kaydi farkli kimlik tasir, eslesmez = canli olcum kapali kalir). Hepsi dogrulanmamis.
        {"valorant.live", "VALORANT", "Riot Vanguard", CompatState::Unverified, "", "", "", "", false},
        {"league_of_legends.live", "League of Legends", "Riot Vanguard", CompatState::Unverified, "", "", "", "", false},
        {"1938090", "Call of Duty", "Ricochet", CompatState::Unverified, "", "", "", "", false},
        {"1962663", "Call of Duty: Warzone", "Ricochet", CompatState::Unverified, "", "", "", "", false},
    };
    return m;
}

std::string_view to_string(ProtectionSystem s) noexcept {
    switch (s) {
        case ProtectionSystem::EasyAntiCheat: return "easy_anticheat";
        case ProtectionSystem::BattlEye: return "battleye";
        case ProtectionSystem::Vanguard: return "vanguard";
    }
    return "unknown";
}

std::vector<ProtectionStatus> protection_status(const ProtectionEvidence& e) {
    if (!e.query_ok) return {};  // okunamadı: "yok" denmez
    return {
        {ProtectionSystem::EasyAntiCheat, e.eac_installed, e.eac_running},
        {ProtectionSystem::BattlEye, e.be_installed, e.be_running},
        {ProtectionSystem::Vanguard, e.vanguard_installed, e.vanguard_running},
    };
}

namespace {
GateDecision deny(std::string k) { return {false, std::move(k)}; }
}  // namespace

std::string observed_collector_commit(std::string_view id) {
    if (id.size() < 7 || id.size() > 40) return {};  // "unknown", "-dirty" ekli veya bos kimlik reddedilir
    for (const char ch : id)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return {};
    return std::string(id);
}

std::string observed_os_driver_scope(std::string_view os, std::string_view drv) {
    if (os.empty() || os.size() > 32 || drv.empty() || drv.size() > 128) return {};
    for (const char ch : os)
        if (!((ch >= '0' && ch <= '9') || ch == '.')) return {};
    if (os.front() == '.' || os.back() == '.') return {};
    for (const char ch : drv)
        if (ch < 0x20 || ch > 0x7E) return {};
    return "os=" + std::string(os) + ";gpu_driver=" + std::string(drv);
}

bool detection_fresh(std::int64_t age_ms) noexcept { return age_ms >= 0 && age_ms <= kMaxDetectionAgeMs; }

GateDecision check(GuardedAction a, const GateContext& ctx, const std::vector<CompatEntry>& matrix) {
    // Oyun durumu guvenilir degilse hicbir korumali islem (canli olcum dahil) baslatilmaz.
    if (!ctx.detection_ok) return deny(ctx.detection_reason_key.empty() ? std::string("gate.detection_failed") : ctx.detection_reason_key);
    // Eski izinle islem baslatilmaz: tarama yasi bilinmiyor veya zaman asimina ugradiysa kapi kapali.
    if (!detection_fresh(ctx.detection_age_ms)) return deny("gate.detection_stale");
    if (a == GuardedAction::LiveCapture) {
        if (ctx.capture_game_id.empty()) return deny("gate.capture_unknown");
        bool any = false;
        for (const auto& e : matrix) {
            if (e.game_id != ctx.capture_game_id) continue;
            any = true;
            // Her koruma kaydi ayri dogrulanmali (FACEIT istege bagli: hangisinin aktif oldugu bilinemez).
            if (e.state == CompatState::Blocked) return deny("gate.capture_blocked");
            if (e.state == CompatState::Stale) return deny("gate.capture_stale");
            if (e.state != CompatState::VerifiedForBuild || !e.live_capture_enabled) return deny("gate.capture_unverified");
            // Kanit kapsami dort alanla birlikte dogrulanir; yalniz oyun surumu yetmez.
            if (e.game_build.empty() || e.protection_build.empty() || e.collector_commit.empty() || e.os_driver_scope.empty())
                return deny("gate.capture_evidence");
            if (ctx.capture_game_build.empty() || ctx.capture_protection_build.empty() || ctx.capture_collector_commit.empty() ||
                ctx.capture_os_driver_scope.empty())
                return deny("gate.capture_evidence");  // gozlenen kimlik eksik: tahmin yok
            if (e.game_build != ctx.capture_game_build || e.protection_build != ctx.capture_protection_build ||
                e.collector_commit != ctx.capture_collector_commit || e.os_driver_scope != ctx.capture_os_driver_scope)
                return deny("gate.capture_stale");  // kanit kapsami ortama uymuyor
        }
        if (!any) return deny("gate.capture_unknown");  // bilinmeyen kombinasyon: kapali
        return {true, {}};
    }
    if (!ctx.running_game_ids.empty()) return deny("gate.game_running");
    return {true, {}};
}

GateDecision capture_source(const std::vector<RunningGameCapture>& running, const std::vector<CompatEntry>& matrix) {
    if (running.empty()) return deny("gate.capture_no_game");  // canli olcum yalniz calisan oyun icin
    GateDecision first_denied{false, "gate.capture_unknown"};
    bool have_denied = false;
    for (const auto& g : running) {
        GateContext c;
        c.detection_ok = true;  // cagiran taraf calisan oyunu guvenilir taramadan verdi
        c.detection_age_ms = 0; // ve bu karar guncel
        c.capture_game_id = g.game_id;
        c.capture_game_build = g.game_build;
        c.capture_protection_build = g.protection_build;
        c.capture_collector_commit = g.collector_commit;
        c.capture_os_driver_scope = g.os_driver_scope;
        auto d = check(GuardedAction::LiveCapture, c, matrix);
        if (d.allowed) return d;
        if (!have_denied) {
            first_denied = std::move(d);
            have_denied = true;
        }
    }
    return first_denied;
}

}  // namespace ef::core::games
