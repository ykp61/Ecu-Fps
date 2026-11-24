#include "core/drivers/vendor_install.h"

#include <array>
#include <utility>

namespace ef::core::drivers {

namespace {

using contracts::TxState;
using security::SignatureStatus;

constexpr std::array<std::pair<VendorOutcome, std::string_view>, 21> kOutcomes{{
    {VendorOutcome::Installed, "installed"},
    {VendorOutcome::SameVersion, "same_version"},
    {VendorOutcome::BadTarget, "bad_target"},
    {VendorOutcome::NotFound, "not_found"},
    {VendorOutcome::TooSmall, "too_small"},
    {VendorOutcome::TooLarge, "too_large"},
    {VendorOutcome::Unsigned, "unsigned"},
    {VendorOutcome::BadSignature, "bad_signature"},
    {VendorOutcome::Untrusted, "untrusted"},
    {VendorOutcome::WrongSigner, "wrong_signer"},
    {VendorOutcome::StageFailed, "stage_failed"},
    {VendorOutcome::JournalFailed, "journal_failed"},
    {VendorOutcome::GateBlocked, "gate_blocked"},
    {VendorOutcome::Cancelled, "cancelled"},
    {VendorOutcome::LaunchFailed, "launch_failed"},
    {VendorOutcome::Timeout, "timeout"},
    {VendorOutcome::NotAdmin, "not_admin"},
    {VendorOutcome::NoDevice, "no_device"},
    {VendorOutcome::Downgrade, "downgrade"},
    {VendorOutcome::InstallerCancelled, "installer_cancelled"},
    {VendorOutcome::InstallerFailed, "installer_failed"},
}};

constexpr std::array<std::pair<RestorePointState, std::string_view>, 4> kRp{{
    {RestorePointState::NotAttempted, "none"},
    {RestorePointState::Created, "created"},
    {RestorePointState::Recent, "recent"},
    {RestorePointState::Failed, "failed"},
}};

bool lower_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

bool version_like(std::string_view v) {
    if (v.empty() || v.size() > 32) return false;
    for (const char c : v)
        if (!((c >= '0' && c <= '9') || c == '.')) return false;
    return true;
}

// Yarida kalmis onceki kayitlar: paket calismadan kesildiyse (Prepared) hicbir sey degismedi -> Restored; aksi halde
// surucunun durumu bilinmiyor -> RecoveryRequired. Kayit silinmez.
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

std::optional<GpuVendor> gpu_vendor_from_pci(std::uint32_t id) noexcept {
    switch (id) {
        case 0x10DE: return GpuVendor::Nvidia;
        case 0x1002: return GpuVendor::Amd;
        case 0x8086: return GpuVendor::Intel;
        default: return std::nullopt;
    }
}

std::string_view to_string(GpuVendor v) noexcept {
    switch (v) {
        case GpuVendor::Nvidia: return "nvidia";
        case GpuVendor::Amd: return "amd";
        case GpuVendor::Intel: return "intel";
    }
    return "nvidia";
}

std::optional<GpuVendor> parse_gpu_vendor(std::string_view s) noexcept {
    for (const auto v : {GpuVendor::Nvidia, GpuVendor::Amd, GpuVendor::Intel})
        if (to_string(v) == s) return v;
    return std::nullopt;
}

std::uint32_t pci_vendor_id(GpuVendor v) noexcept {
    switch (v) {
        case GpuVendor::Nvidia: return 0x10DE;
        case GpuVendor::Amd: return 0x1002;
        case GpuVendor::Intel: return 0x8086;
    }
    return 0;
}

std::string_view display_name(GpuVendor v) noexcept {
    switch (v) {
        case GpuVendor::Nvidia: return "NVIDIA";
        case GpuVendor::Amd: return "AMD";
        case GpuVendor::Intel: return "Intel";
    }
    return "";
}

std::string_view vendor_signer(GpuVendor v) noexcept {
    switch (v) {
        case GpuVendor::Nvidia: return "NVIDIA Corporation";
        case GpuVendor::Amd: return "Advanced Micro Devices, Inc.";
        case GpuVendor::Intel: return "Intel Corporation";
    }
    return "";
}

std::vector<std::wstring> silent_args(GpuVendor v) {
    switch (v) {
        case GpuVendor::Nvidia: return {L"-s", L"-n", L"Display.Driver"};
        case GpuVendor::Amd: return {L"-INSTALL"};
        case GpuVendor::Intel: return {L"-s"};
    }
    return {};
}

bool package_size_ok(std::uint64_t bytes) noexcept { return bytes >= kPackageMinBytes && bytes <= kPackageMaxBytes; }

bool valid_package_id(std::string_view id) noexcept {
    if (id.size() != 16) return false;
    for (const char c : id)
        if (!lower_hex(c)) return false;
    return true;
}

std::string package_file_name(std::string_view id) { return valid_package_id(id) ? std::string(id) + ".exe" : std::string(); }
std::string package_part_name(std::string_view id) { return valid_package_id(id) ? std::string(id) + ".part" : std::string(); }

std::string vendor_target(GpuVendor v, std::string_view id) {
    if (!valid_package_id(id)) return {};
    return std::string(kVendorTargetPrefix) + std::string(to_string(v)) + ":" + std::string(id);
}

std::optional<VendorRef> parse_vendor_target(std::string_view t) {
    if (t.substr(0, kVendorTargetPrefix.size()) != kVendorTargetPrefix) return std::nullopt;
    t.remove_prefix(kVendorTargetPrefix.size());
    const auto colon = t.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const auto v = parse_gpu_vendor(t.substr(0, colon));
    const auto id = t.substr(colon + 1);
    if (!v || !valid_package_id(id)) return std::nullopt;
    return VendorRef{*v, std::string(id)};
}

SignerVerdict check_signer(SignatureStatus status, std::string_view signer, GpuVendor v) noexcept {
    switch (status) {
        case SignatureStatus::Valid: return signer == vendor_signer(v) ? SignerVerdict::Ok : SignerVerdict::WrongSigner;
        case SignatureStatus::Unsigned: return SignerVerdict::Unsigned;
        case SignatureStatus::Invalid: return SignerVerdict::Invalid;
        case SignatureStatus::Untrusted: return SignerVerdict::Untrusted;
        case SignatureStatus::Unknown: break;
    }
    return SignerVerdict::Unknown;
}

InstallerExit map_installer_exit(GpuVendor v, std::uint32_t code) noexcept {
    if (code == 0) return InstallerExit::Ok;
    switch (v) {
        case GpuVendor::Nvidia: return code == 1 ? InstallerExit::OkReboot : InstallerExit::Failed;
        case GpuVendor::Intel:
            switch (code) {
                case 3010:
                case 14: return InstallerExit::OkReboot;
                case 2: return InstallerExit::NotAdmin;
                case 5: return InstallerExit::NoDevice;
                case 7: return InstallerExit::Downgrade;
                case 9: return InstallerExit::Cancelled;
                default: return InstallerExit::Failed;
            }
        case GpuVendor::Amd: return code == 3010 ? InstallerExit::OkReboot : InstallerExit::Failed;
    }
    return InstallerExit::Failed;
}

CopyRun copy_package(ByteSource& src, ByteSink& dst, std::uint64_t expected_total, const std::function<bool()>& stop,
                     const std::function<void(std::uint64_t, std::uint64_t)>& progress, std::size_t block) {
    CopyRun r;
    if (expected_total > kPackageMaxBytes) {  // bilinen boyut sinirin ustunde: tek bayt kopyalanmaz
        r.outcome = CopyOutcome::TooLarge;
        return r;
    }
    if (block == 0) block = 1u << 20;
    std::vector<std::uint8_t> buf(block);
    for (;;) {
        if (stop && stop()) {
            r.outcome = CopyOutcome::Cancelled;
            return r;
        }
        const auto n = src.read(buf.data(), buf.size());
        if (!n) {
            r.outcome = CopyOutcome::ReadFailed;
            return r;
        }
        if (*n == 0) break;
        if (r.bytes + *n > kPackageMaxBytes) {  // fazlasi yazilmaz
            r.outcome = CopyOutcome::TooLarge;
            return r;
        }
        if (!dst.write(buf.data(), *n)) {
            r.outcome = CopyOutcome::WriteFailed;
            return r;
        }
        r.bytes += *n;
        if (progress) progress(r.bytes, expected_total);
    }
    r.outcome = r.bytes < kPackageMinBytes ? CopyOutcome::TooSmall : CopyOutcome::Ok;
    return r;
}

std::string_view to_string(VendorOutcome o) noexcept {
    for (const auto& [k, v] : kOutcomes)
        if (k == o) return v;
    return "installer_failed";
}

std::optional<VendorOutcome> parse_vendor_outcome(std::string_view s) noexcept {
    for (const auto& [k, v] : kOutcomes)
        if (v == s) return k;
    return std::nullopt;
}

bool is_rejection(VendorOutcome o) noexcept {
    switch (o) {
        case VendorOutcome::BadTarget:
        case VendorOutcome::NotFound:
        case VendorOutcome::TooSmall:
        case VendorOutcome::TooLarge:
        case VendorOutcome::Unsigned:
        case VendorOutcome::BadSignature:
        case VendorOutcome::Untrusted:
        case VendorOutcome::WrongSigner: return true;
        default: return false;
    }
}

std::string encode_vendor_detail(const VendorRun& r) {
    std::string rp = "none";
    for (const auto& [k, v] : kRp)
        if (k == r.restore_point) rp = std::string(v);
    std::string d = "vd:" + std::string(to_string(r.outcome)) + ":ec=" + std::to_string(r.exit_code) + ":rp=" + rp +
                    ":v=" + (version_like(r.version_after) ? r.version_after : std::string());
    if (r.reboot_required) d += ":reboot";
    if (r.previous_interrupted) d += ":prev";
    return d;
}

std::optional<VendorRun> decode_vendor_detail(std::string_view d) {
    if (d.substr(0, 3) != "vd:") return std::nullopt;
    d.remove_prefix(3);
    std::vector<std::string_view> parts;
    for (std::size_t p = 0;;) {
        const auto q = d.find(':', p);
        parts.push_back(d.substr(p, q == std::string_view::npos ? std::string_view::npos : q - p));
        if (q == std::string_view::npos) break;
        p = q + 1;
    }
    if (parts.size() < 4) return std::nullopt;
    VendorRun r;
    const auto o = parse_vendor_outcome(parts[0]);
    if (!o) return std::nullopt;
    r.outcome = *o;
    if (parts[1].substr(0, 3) != "ec=" || parts[1].size() < 4 || parts[1].size() > 13) return std::nullopt;
    std::uint64_t ec = 0;
    for (const char c : parts[1].substr(3)) {
        if (c < '0' || c > '9') return std::nullopt;
        ec = ec * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (ec > 0xFFFFFFFFull) return std::nullopt;
    r.exit_code = static_cast<std::uint32_t>(ec);
    if (parts[2].substr(0, 3) != "rp=") return std::nullopt;
    bool rp_known = false;
    for (const auto& [k, name] : kRp)
        if (name == parts[2].substr(3)) r.restore_point = k, rp_known = true;
    if (!rp_known) return std::nullopt;
    if (parts[3].substr(0, 2) != "v=") return std::nullopt;
    const auto ver = parts[3].substr(2);
    if (!ver.empty() && !version_like(ver)) return std::nullopt;
    r.version_after = std::string(ver);
    for (std::size_t i = 4; i < parts.size(); ++i) {
        if (parts[i] == "reboot") r.reboot_required = true;
        else if (parts[i] == "prev") r.previous_interrupted = true;
        else return std::nullopt;
    }
    return r;
}

VendorRun run_vendor_install(const VendorInstallDeps& d, std::string_view target) {
    VendorRun r;
    const auto stop = [&d] { return d.cancel && d.cancel(); };
    const auto ref = parse_vendor_target(target);
    if (!ref || !d.api) {
        r.outcome = VendorOutcome::BadTarget;
        return r;
    }
    if (!d.journal || !d.new_tx_id || !d.now_unix_ms) {  // kalici kayit yoksa hicbir sey baslamaz
        r.outcome = VendorOutcome::JournalFailed;
        return r;
    }
    r.previous_interrupted = close_interrupted(*d.journal);
    auto& api = *d.api;
    const auto finish = [&](VendorOutcome o) {
        r.outcome = o;
        api.discard(*ref);
        return r;
    };
    if (stop()) return finish(VendorOutcome::Cancelled);
    // 1. Kopya: yalniz kimlikten kurulan ad, yonetici ACL'li makine klasorune.
    const auto st = api.stage(*ref);
    switch (st.status) {
        case StageStatus::Ok: break;
        case StageStatus::NotFound: return finish(VendorOutcome::NotFound);
        case StageStatus::TooSmall: return finish(VendorOutcome::TooSmall);
        case StageStatus::TooLarge: return finish(VendorOutcome::TooLarge);
        case StageStatus::IoError: return finish(VendorOutcome::StageFailed);
    }
    if (!package_size_ok(st.bytes)) return finish(st.bytes > kPackageMaxBytes ? VendorOutcome::TooLarge : VendorOutcome::TooSmall);
    // 2. Imza + imzalayan KOPYA uzerinde (UI'nin on denetimine guvenilmez).
    const auto sig = api.verify(*ref);
    switch (check_signer(sig.status, sig.signer, ref->vendor)) {
        case SignerVerdict::Ok: break;
        case SignerVerdict::Unsigned: return finish(VendorOutcome::Unsigned);
        case SignerVerdict::Invalid:
        case SignerVerdict::Unknown: return finish(VendorOutcome::BadSignature);
        case SignerVerdict::Untrusted: return finish(VendorOutcome::Untrusted);
        case SignerVerdict::WrongSigner: return finish(VendorOutcome::WrongSigner);
    }
    if (d.gate_ok && !d.gate_ok()) return finish(VendorOutcome::GateBlocked);
    // 3. Kalici baslangic kaydi (yazilamazsa paket calismaz).
    recovery::JournalTx tx;
    tx.id = d.new_tx_id();
    tx.actor_sid = d.actor_sid;
    tx.target_sid = d.target_sid;
    tx.created_unix_ms = d.now_unix_ms();
    recovery::JournalStep step;
    step.op = {contracts::OperationId::VendorDriverInstall, contracts::OperationScope::Machine, std::string(target),
               std::string(kVendorInstallValue), false, false};
    step.state = recovery::StepState::Intent;
    tx.steps.push_back(std::move(step));
    if (!d.journal->begin(tx).ok()) return finish(VendorOutcome::JournalFailed);
    auto& js = tx.steps.front();
    const auto close = [&](TxState state, recovery::StepState step_state, std::string error) {
        if (state == TxState::Restored) {
            tx.state = TxState::RollingBack;
            (void)d.journal->persist(tx);
        }
        js.state = step_state;
        js.error = std::move(error);
        tx.state = state;
        (void)d.journal->persist(tx);
    };
    // 4. Geri yukleme noktasi (olusmadiysa kurulum surer, sonuc bunu acikca tasir).
    r.restore_point = api.restore_point();
    if (stop()) {
        close(TxState::Restored, recovery::StepState::Failed, "cancelled");
        return finish(VendorOutcome::Cancelled);
    }
    if (d.gate_ok && !d.gate_ok()) {
        close(TxState::Restored, recovery::StepState::Failed, "gate");
        return finish(VendorOutcome::GateBlocked);
    }
    r.version_before = api.installed_version(ref->vendor);
    tx.state = TxState::Applying;
    // "Paket calisiyor" kaydi diske yazilamazsa paket calismaz: yoksa yarida kesilen kurulum sonraki acilista
    // "Prepared = hicbir sey degismedi" diye yanlis kapatilirdi.
    if (!d.journal->persist(tx).ok()) {
        tx.state = TxState::Prepared;
        close(TxState::Restored, recovery::StepState::Failed, "journal");
        return finish(VendorOutcome::JournalFailed);
    }
    // 5. Resmi sessiz anahtarlarla calistir (baslamis kurulum yarida kesilmez).
    const auto run = api.run_installer(*ref, silent_args(ref->vendor), kInstallerTimeoutMs);
    if (!run.started) {
        close(TxState::Restored, recovery::StepState::Failed, "launch_failed");  // paket hic calismadi
        return finish(VendorOutcome::LaunchFailed);
    }
    r.exit_code = run.exit_code;
    if (run.timed_out) {
        close(TxState::RecoveryRequired, recovery::StepState::Failed, "timeout");
        return finish(VendorOutcome::Timeout);
    }
    const auto ex = map_installer_exit(ref->vendor, run.exit_code);
    r.version_after = api.installed_version(ref->vendor);
    if (ex == InstallerExit::Ok || ex == InstallerExit::OkReboot) {
        r.reboot_required = ex == InstallerExit::OkReboot;
        const bool same = !r.version_before.empty() && r.version_before == r.version_after;
        // Yeniden baslatma gerekiyorsa surum ancak sonra degisir: "ayni surum" denmez.
        const auto o = same && !r.reboot_required ? VendorOutcome::SameVersion : VendorOutcome::Installed;
        js.last_written = r.version_after.empty() ? std::string("installed") : r.version_after;
        tx.state = TxState::Applied;
        js.state = recovery::StepState::Applied;
        (void)d.journal->persist(tx);
        tx.state = TxState::Verifying;
        (void)d.journal->persist(tx);
        close(TxState::Active, recovery::StepState::Verified, r.reboot_required ? "reboot" : "");
        return finish(o);
    }
    VendorOutcome o = VendorOutcome::InstallerFailed;
    switch (ex) {
        case InstallerExit::NotAdmin: o = VendorOutcome::NotAdmin; break;
        case InstallerExit::NoDevice: o = VendorOutcome::NoDevice; break;
        case InstallerExit::Downgrade: o = VendorOutcome::Downgrade; break;
        case InstallerExit::Cancelled: o = VendorOutcome::InstallerCancelled; break;
        default: break;
    }
    // Paket calisti ve basarisiz dondu: aygitin son durumu dogrulanmadi.
    close(TxState::RecoveryRequired, recovery::StepState::Failed, std::string(to_string(o)));
    return finish(o);
}

}  // namespace ef::core::drivers
