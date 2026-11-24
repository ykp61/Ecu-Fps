#include "core/drivers/driver_install.h"

#include <array>
#include <cstdio>
#include <utility>

namespace ef::core::drivers {

namespace {

using contracts::TxState;

bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = lower(c);
    return out;
}

constexpr std::array<std::pair<InstallOutcome, std::string_view>, 17> kOutcomes{{
    {InstallOutcome::Installed, "installed"},
    {InstallOutcome::InstalledWithErrors, "installed_with_errors"},
    {InstallOutcome::AlreadyInstalled, "already_installed"},
    {InstallOutcome::BadTarget, "bad_target"},
    {InstallOutcome::NotFound, "not_found"},
    {InstallOutcome::Ambiguous, "ambiguous"},
    {InstallOutcome::NotDriver, "not_driver"},
    {InstallOutcome::Firmware, "firmware"},
    {InstallOutcome::NotWindowsUpdate, "not_windows_update"},
    {InstallOutcome::Hidden, "hidden"},
    {InstallOutcome::NeedsUserInput, "needs_user_input"},
    {InstallOutcome::SearchFailed, "search_failed"},
    {InstallOutcome::JournalFailed, "journal_failed"},
    {InstallOutcome::GateBlocked, "gate_blocked"},
    {InstallOutcome::Cancelled, "cancelled"},
    {InstallOutcome::DownloadFailed, "download_failed"},
    {InstallOutcome::InstallFailed, "install_failed"},
}};

constexpr std::array<std::pair<RestorePointState, std::string_view>, 4> kRp{{
    {RestorePointState::NotAttempted, "none"},
    {RestorePointState::Created, "created"},
    {RestorePointState::Recent, "recent"},
    {RestorePointState::Failed, "failed"},
}};

// Yarida kalmis onceki kayitlar: indirme baslamadiysa (Prepared) hicbir sey degismedi -> Restored; aksi halde surucunun
// durumu bilinmiyor -> RecoveryRequired. Kayit silinmez.
bool close_interrupted(recovery::JournalStore& j) {
    bool any = false;
    for (auto& tx : j.scan().transactions) {
        if (tx.state == TxState::Active || tx.state == TxState::Restored || tx.state == TxState::RecoveryRequired) continue;
        any = true;
        const bool untouched = tx.state == TxState::Prepared;
        for (auto& st : tx.steps) {
            st.state = recovery::StepState::Failed;
            st.error = "interrupted";
        }
        tx.state = untouched ? TxState::Restored : TxState::RecoveryRequired;
        (void)j.persist(tx);
    }
    return any;
}

}  // namespace

bool valid_update_id(std::string_view id) {
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? id[i] != '-' : !hex(id[i])) return false;
    }
    return true;
}

std::string install_target(std::string_view update_id, int revision) {
    if (!valid_update_id(update_id) || revision < 1 || revision > 999999999) return {};
    return std::string(kInstallTargetPrefix) + lowered(update_id) + ":" + std::to_string(revision);
}

std::optional<InstallRef> parse_install_target(std::string_view target) {
    if (target.substr(0, kInstallTargetPrefix.size()) != kInstallTargetPrefix) return std::nullopt;
    const auto rest = target.substr(kInstallTargetPrefix.size());
    if (rest.size() < 38 || rest[36] != ':') return std::nullopt;
    const auto id = rest.substr(0, 36);
    const auto rev = rest.substr(37);
    if (!valid_update_id(id) || rev.empty() || rev.size() > 9 || rev[0] == '0') return std::nullopt;
    int n = 0;
    for (const char c : rev) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    return InstallRef{lowered(id), n};
}

std::wstring install_criteria(const InstallRef& ref) {
    std::wstring w = L"UpdateID='";
    for (const char c : ref.update_id) w.push_back(static_cast<wchar_t>(c));
    w += L"' and RevisionNumber=" + std::to_wstring(ref.revision);
    return w;
}

std::string_view to_string(RestorePointState s) noexcept {
    for (const auto& [k, v] : kRp)
        if (k == s) return v;
    return "none";
}

std::string_view to_string(InstallOutcome o) noexcept {
    for (const auto& [k, v] : kOutcomes)
        if (k == o) return v;
    return "install_failed";
}

std::optional<InstallOutcome> parse_install_outcome(std::string_view s) noexcept {
    for (const auto& [k, v] : kOutcomes)
        if (v == s) return k;
    return std::nullopt;
}

bool is_rejection(InstallOutcome o) noexcept {
    switch (o) {
        case InstallOutcome::BadTarget:
        case InstallOutcome::NotFound:
        case InstallOutcome::Ambiguous:
        case InstallOutcome::NotDriver:
        case InstallOutcome::Firmware:
        case InstallOutcome::NotWindowsUpdate:
        case InstallOutcome::Hidden:
        case InstallOutcome::NeedsUserInput: return true;
        default: return false;
    }
}

std::optional<InstallOutcome> check_candidates(const InstallRef& ref, const std::vector<InstallCandidate>& found, std::size_t* index) {
    std::size_t hits = 0, at = 0;
    for (std::size_t i = 0; i < found.size(); ++i) {
        const auto& u = found[i].update;
        if (lowered(u.update_id) == ref.update_id && u.revision == ref.revision) ++hits, at = i;
    }
    if (hits == 0) return InstallOutcome::NotFound;  // farkli revizyon / kimlik kabul edilmez
    if (hits > 1 || found.size() != 1) return InstallOutcome::Ambiguous;  // yalniz tam o oge
    const auto& c = found[at];
    // Sira onemli: firmware her durumda reddedilir (tur / kaynak dogru gorunse bile).
    if (is_firmware(c.update)) return InstallOutcome::Firmware;
    if (!c.type_driver) return InstallOutcome::NotDriver;
    if (!c.from_windows_update) return InstallOutcome::NotWindowsUpdate;
    if (c.hidden) return InstallOutcome::Hidden;
    if (!c.eula_accepted || c.needs_user_input) return InstallOutcome::NeedsUserInput;
    if (c.installed) return InstallOutcome::AlreadyInstalled;
    if (index) *index = at;
    return std::nullopt;
}

std::optional<InstallOutcome> map_download(const StageOutcome& s) {
    if (s.result_code == kWuaSucceeded || s.result_code == kWuaSucceededWithErrors) return std::nullopt;
    if (s.result_code == kWuaAborted) return InstallOutcome::Cancelled;
    return InstallOutcome::DownloadFailed;
}

InstallOutcome map_install(const StageOutcome& s) {
    switch (s.result_code) {
        case kWuaSucceeded: return InstallOutcome::Installed;
        case kWuaSucceededWithErrors: return InstallOutcome::InstalledWithErrors;
        case kWuaAborted: return InstallOutcome::Cancelled;
        default: return InstallOutcome::InstallFailed;
    }
}

std::string encode_install_detail(const InstallRun& r) {
    char hr[16];
    std::snprintf(hr, sizeof hr, "%08X", static_cast<unsigned>(r.hresult));
    std::string d = "di:" + std::string(to_string(r.outcome)) + ":rc=" + std::to_string(r.result_code) + ":hr=" + hr +
                    ":rp=" + std::string(to_string(r.restore_point));
    if (r.reboot_required) d += ":reboot";
    if (r.previous_interrupted) d += ":prev";
    return d;
}

std::optional<InstallRun> decode_install_detail(std::string_view d) {
    if (d.substr(0, 3) != "di:") return std::nullopt;
    d.remove_prefix(3);
    std::vector<std::string_view> parts;
    for (std::size_t p = 0;;) {
        const auto q = d.find(':', p);
        parts.push_back(d.substr(p, q == std::string_view::npos ? std::string_view::npos : q - p));
        if (q == std::string_view::npos) break;
        p = q + 1;
    }
    if (parts.size() < 4) return std::nullopt;
    InstallRun r;
    const auto o = parse_install_outcome(parts[0]);
    if (!o) return std::nullopt;
    r.outcome = *o;
    if (parts[1].substr(0, 3) != "rc=" || parts[1].size() != 4 || parts[1][3] < '0' || parts[1][3] > '5') return std::nullopt;
    r.result_code = parts[1][3] - '0';
    if (parts[2].substr(0, 3) != "hr=" || parts[2].size() != 11) return std::nullopt;
    unsigned v = 0;
    for (const char c : parts[2].substr(3)) {
        if (!hex(c)) return std::nullopt;
        v = v * 16 + static_cast<unsigned>(c <= '9' ? c - '0' : lower(c) - 'a' + 10);
    }
    r.hresult = static_cast<std::int32_t>(v);
    if (parts[3].substr(0, 3) != "rp=") return std::nullopt;
    bool rp_known = false;
    for (const auto& [k, name] : kRp)
        if (name == parts[3].substr(3)) r.restore_point = k, rp_known = true;
    if (!rp_known) return std::nullopt;
    for (std::size_t i = 4; i < parts.size(); ++i) {
        if (parts[i] == "reboot") r.reboot_required = true;
        else if (parts[i] == "prev") r.previous_interrupted = true;
        else return std::nullopt;
    }
    return r;
}

InstallRun run_driver_install(const InstallDeps& d, std::string_view target) {
    InstallRun r;
    const auto stop = [&d] { return d.cancel && d.cancel(); };
    const auto ref = parse_install_target(target);
    if (!ref || !d.api) {
        r.outcome = InstallOutcome::BadTarget;
        return r;
    }
    if (!d.journal || !d.new_tx_id || !d.now_unix_ms) {  // kalici kayit yoksa hicbir sey baslamaz
        r.outcome = InstallOutcome::JournalFailed;
        return r;
    }
    r.previous_interrupted = close_interrupted(*d.journal);
    if (stop()) {
        r.outcome = InstallOutcome::Cancelled;
        return r;
    }
    // Arayuzden gelen baslik / sinif kullanilmaz: oge yeniden aranir.
    auto f = d.api->find(*ref, stop);
    if (f.cancelled || (!f.ok && stop())) {
        r.outcome = InstallOutcome::Cancelled;
        r.hresult = f.hresult;
        return r;
    }
    if (!f.ok) {
        r.outcome = InstallOutcome::SearchFailed;
        r.hresult = f.hresult;
        return r;
    }
    std::size_t at = 0;
    if (const auto rej = check_candidates(*ref, f.found, &at)) {
        r.outcome = *rej;
        if (!f.found.empty()) r.title = f.found.front().update.title;
        return r;
    }
    r.title = f.found[at].update.title;
    if (!d.api->select(at)) {
        r.outcome = InstallOutcome::NotFound;
        return r;
    }
    if (d.gate_ok && !d.gate_ok()) {
        r.outcome = InstallOutcome::GateBlocked;
        return r;
    }
    // Kalici baslangic kaydi (yazilamazsa indirme / kurulum yok).
    recovery::JournalTx tx;
    tx.id = d.new_tx_id();
    tx.actor_sid = d.actor_sid;
    tx.target_sid = d.target_sid;
    tx.created_unix_ms = d.now_unix_ms();
    recovery::JournalStep step;
    step.op = {contracts::OperationId::DriverInstall, contracts::OperationScope::Machine, std::string(target), std::string(kInstallValue), false, false};
    step.state = recovery::StepState::Intent;
    tx.steps.push_back(std::move(step));
    if (!d.journal->begin(tx).ok()) {
        r.outcome = InstallOutcome::JournalFailed;
        return r;
    }
    auto& st = tx.steps.front();
    const auto close = [&](TxState state, recovery::StepState step_state, std::string error) {
        if (state == TxState::Restored) {
            tx.state = TxState::RollingBack;
            (void)d.journal->persist(tx);
        }
        st.state = step_state;
        st.error = std::move(error);
        tx.state = state;
        (void)d.journal->persist(tx);
    };
    // Geri yukleme noktasi istenir; olusmadiysa kurulum surer ama sonuc bunu acikca tasir.
    r.restore_point = d.api->restore_point();
    if (stop()) {
        r.outcome = InstallOutcome::Cancelled;
        close(TxState::Restored, recovery::StepState::Failed, "cancelled");
        return r;
    }
    if (d.gate_ok && !d.gate_ok()) {
        r.outcome = InstallOutcome::GateBlocked;
        close(TxState::Restored, recovery::StepState::Failed, "gate");
        return r;
    }
    tx.state = TxState::Applying;
    (void)d.journal->persist(tx);
    const auto dl = d.api->download(stop);
    r.result_code = dl.result_code;
    r.hresult = dl.hresult;
    if (const auto bad = map_download(dl)) {
        r.outcome = stop() ? InstallOutcome::Cancelled : *bad;
        close(TxState::Restored, recovery::StepState::Failed, std::string(to_string(r.outcome)));  // kurulum baslamadi
        return r;
    }
    if (stop()) {  // indirildi ama kurulum baslamadi: surucu degismedi
        r.outcome = InstallOutcome::Cancelled;
        close(TxState::Restored, recovery::StepState::Failed, "cancelled");
        return r;
    }
    if (d.gate_ok && !d.gate_ok()) {
        r.outcome = InstallOutcome::GateBlocked;
        close(TxState::Restored, recovery::StepState::Failed, "gate");
        return r;
    }
    const auto in = d.api->install();  // baslamis kurulum yarida kesilmez
    r.result_code = in.result_code;
    r.hresult = in.hresult;
    r.reboot_required = in.reboot_required;
    r.outcome = map_install(in);
    st.last_written = r.reboot_required ? "installed:reboot" : "installed";
    if (r.outcome == InstallOutcome::Installed || r.outcome == InstallOutcome::InstalledWithErrors) {
        tx.state = TxState::Applied;
        st.state = recovery::StepState::Applied;
        (void)d.journal->persist(tx);
        tx.state = TxState::Verifying;
        (void)d.journal->persist(tx);
        close(TxState::Active, recovery::StepState::Verified, r.outcome == InstallOutcome::InstalledWithErrors ? "with_errors" : "");
    } else {
        // Kurulum denendi ve basarisiz / kesildi: aygitin son durumu dogrulanmadi.
        st.last_written.clear();
        close(TxState::RecoveryRequired, recovery::StepState::Failed, std::string(to_string(r.outcome)));
    }
    return r;
}

}  // namespace ef::core::drivers
