// ADR-032: resmi ekran karti paketi kurulumu cekirdegi. Sahte arabirimler; gercek paket indirilmez / kopyalanmaz /
// calistirilmaz, yukseltme yok. Hedef bicimi (klasor disi yol yok), imza + imzalayan, boyut siniri, iptal, cikis kodu
// eslemesi (yeniden baslatma dahil), oyun kapisi, journal kaydi, broker izin listesi ve istemci bildirimi.
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "core/drivers/vendor_install.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::drivers;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::TxState;
using ef::core::security::SignatureStatus;

namespace {

constexpr const char* kId = "0123456789abcdef";

struct Fake final : VendorInstallApi {
    StageResult staged{StageStatus::Ok, 900ull << 20};
    SignatureCheck sig{SignatureStatus::Valid, "NVIDIA Corporation"};
    RestorePointState rp = RestorePointState::Created;
    std::string before = "32.0.15.6094", after = "32.0.15.7000";
    LaunchResult launch{true, false, 0};
    int stages = 0, verifies = 0, runs = 0, discards = 0, versions = 0;
    std::vector<std::wstring> args;
    std::uint32_t timeout = 0;
    std::function<void()> after_rp;
    StageResult stage(const VendorRef&) override {
        ++stages;
        return staged;
    }
    SignatureCheck verify(const VendorRef&) override {
        ++verifies;
        return sig;
    }
    RestorePointState restore_point() override {
        if (after_rp) after_rp();
        return rp;
    }
    std::string installed_version(GpuVendor) override { return versions++ == 0 ? before : after; }
    LaunchResult run_installer(const VendorRef&, const std::vector<std::wstring>& a, std::uint32_t t) override {
        ++runs;
        args = a;
        timeout = t;
        return launch;
    }
    void discard(const VendorRef&) override { ++discards; }
};

struct Rig {
    recovery::MemoryJournalFs fs;
    recovery::JournalStore store{fs, "machine-1"};
    Fake api;
    bool cancel = false, gate = true, no_journal = false;
    int ids = 0;
    VendorInstallDeps deps() {
        VendorInstallDeps d;
        d.api = &api;
        d.journal = no_journal ? nullptr : &store;
        d.new_tx_id = [this] {
            char b[33];
            std::snprintf(b, sizeof b, "%032x", ++ids);
            return std::string(b);
        };
        d.now_unix_ms = [] { return std::int64_t{1000}; };
        d.cancel = [this] { return cancel; };
        d.gate_ok = [this] { return gate; };
        d.actor_sid = d.target_sid = "S-1-5-21-1";
        return d;
    }
    VendorRun run(GpuVendor v = GpuVendor::Nvidia) { return run_vendor_install(deps(), vendor_target(v, kId)); }
    std::vector<recovery::JournalTx> txs() { return store.scan().transactions; }
};

struct CountingSource final : ByteSource {
    std::uint64_t total = 0, given = 0;
    bool fail_at_half = false;
    std::function<void()> on_read;
    std::optional<std::size_t> read(std::uint8_t*, std::size_t n) override {
        if (on_read) on_read();
        if (fail_at_half && given >= total / 2) return std::nullopt;
        const auto left = total - given;
        const auto k = static_cast<std::size_t>(left < n ? left : n);
        given += k;
        return k;
    }
};
struct CountingSink final : ByteSink {
    std::uint64_t got = 0;
    bool fail = false;
    bool write(const std::uint8_t*, std::size_t n) override {
        if (fail) return false;
        got += n;
        return true;
    }
};

}  // namespace

EF_TEST(vdrv_target_only_names_a_file_inside_the_folder) {
    const auto t = vendor_target(GpuVendor::Nvidia, kId);
    EF_CHECK(t == "vdrv:nvidia:0123456789abcdef");
    const auto p = parse_vendor_target(t);
    EF_CHECK(p && p->vendor == GpuVendor::Nvidia && p->id == kId);
    EF_CHECK(parse_vendor_target("vdrv:amd:fedcba9876543210")->vendor == GpuVendor::Amd);
    EF_CHECK(parse_vendor_target("vdrv:intel:fedcba9876543210")->vendor == GpuVendor::Intel);
    for (const char* bad : {"vdrv:nvidia:..\\..\\Windows\\x", "vdrv:nvidia:C:\\x.exe", "vdrv:nvidia:0123456789ABCDEF", "vdrv:nvidia:0123456789abcde",
                            "vdrv:nvidia:0123456789abcdef0", "vdrv:nvidia:0123456789abcdef.exe", "vdrv:nvidia:../0123456789abcd",
                            "vdrv:nvidia:0123/56789abcdef", "vdrv:qualcomm:0123456789abcdef", "vdrv:0123456789abcdef", "wuinst:nvidia:0123456789abcdef",
                            "vdrv:nvidia:", ""})
        EF_CHECK(!parse_vendor_target(bad));
    EF_CHECK(vendor_target(GpuVendor::Amd, "x.exe").empty() && package_file_name("..").empty());
    EF_CHECK(package_file_name(kId) == "0123456789abcdef.exe" && package_part_name(kId) == "0123456789abcdef.part");
    EF_CHECK(gpu_vendor_from_pci(0x10DE) == GpuVendor::Nvidia && gpu_vendor_from_pci(0x1002) == GpuVendor::Amd &&
              gpu_vendor_from_pci(0x8086) == GpuVendor::Intel && !gpu_vendor_from_pci(0x1234));
}

EF_TEST(vdrv_signer_must_be_valid_and_exactly_the_vendor) {
    EF_CHECK(check_signer(SignatureStatus::Valid, "NVIDIA Corporation", GpuVendor::Nvidia) == SignerVerdict::Ok);
    EF_CHECK(check_signer(SignatureStatus::Valid, "Advanced Micro Devices, Inc.", GpuVendor::Amd) == SignerVerdict::Ok);
    EF_CHECK(check_signer(SignatureStatus::Valid, "Intel Corporation", GpuVendor::Intel) == SignerVerdict::Ok);
    for (const char* wrong : {"Nvidia Corporation", "NVIDIA Corporation ", "NVIDIA Corp", "Intel Corporation", "", "Evil NVIDIA Corporation"})
        EF_CHECK(check_signer(SignatureStatus::Valid, wrong, GpuVendor::Nvidia) == SignerVerdict::WrongSigner);
    EF_CHECK(check_signer(SignatureStatus::Unsigned, "NVIDIA Corporation", GpuVendor::Nvidia) == SignerVerdict::Unsigned);
    EF_CHECK(check_signer(SignatureStatus::Invalid, "NVIDIA Corporation", GpuVendor::Nvidia) == SignerVerdict::Invalid);
    EF_CHECK(check_signer(SignatureStatus::Untrusted, "NVIDIA Corporation", GpuVendor::Nvidia) == SignerVerdict::Untrusted);
    EF_CHECK(check_signer(SignatureStatus::Unknown, "NVIDIA Corporation", GpuVendor::Nvidia) == SignerVerdict::Unknown);
}

// Review fix: "calisiyor" (Applying) kaydi yazilamazsa paket calismaz; kayit "hicbir sey degismedi" ile kapanir.
EF_TEST(vdrv_applying_record_must_persist_before_the_package_runs) {
    Rig rig;
    rig.api.after_rp = [&rig] { rig.fs.fail_only_write = rig.fs.writes + 1; };
    const auto r = rig.run();
    EF_CHECK(r.outcome == VendorOutcome::JournalFailed);
    EF_CHECK(rig.api.runs == 0 && rig.api.discards == 1);
    const auto txs = rig.txs();
    EF_CHECK(txs.size() == 1 && txs.front().state == TxState::Restored);
}

EF_TEST(vdrv_silent_switches_and_exit_codes_follow_vendor_docs) {
    EF_CHECK(silent_args(GpuVendor::Nvidia) == (std::vector<std::wstring>{L"-s", L"-n", L"Display.Driver"}));
    EF_CHECK(silent_args(GpuVendor::Amd) == (std::vector<std::wstring>{L"-INSTALL"}));  // -boot (otomatik yeniden baslatma) yok
    EF_CHECK(silent_args(GpuVendor::Intel) == (std::vector<std::wstring>{L"-s"}));      // --overwrite (surum dusurme) yok
    EF_CHECK(map_installer_exit(GpuVendor::Nvidia, 0) == InstallerExit::Ok);
    EF_CHECK(map_installer_exit(GpuVendor::Nvidia, 1) == InstallerExit::OkReboot);
    EF_CHECK(map_installer_exit(GpuVendor::Nvidia, 3010) == InstallerExit::Failed);
    EF_CHECK(map_installer_exit(GpuVendor::Intel, 0) == InstallerExit::Ok);
    EF_CHECK(map_installer_exit(GpuVendor::Intel, 3010) == InstallerExit::OkReboot && map_installer_exit(GpuVendor::Intel, 14) == InstallerExit::OkReboot);
    EF_CHECK(map_installer_exit(GpuVendor::Intel, 2) == InstallerExit::NotAdmin && map_installer_exit(GpuVendor::Intel, 5) == InstallerExit::NoDevice);
    EF_CHECK(map_installer_exit(GpuVendor::Intel, 7) == InstallerExit::Downgrade && map_installer_exit(GpuVendor::Intel, 9) == InstallerExit::Cancelled);
    EF_CHECK(map_installer_exit(GpuVendor::Intel, 1) == InstallerExit::Failed && map_installer_exit(GpuVendor::Intel, 17) == InstallerExit::Failed &&
              map_installer_exit(GpuVendor::Intel, 255) == InstallerExit::Failed);
    EF_CHECK(map_installer_exit(GpuVendor::Amd, 0) == InstallerExit::Ok && map_installer_exit(GpuVendor::Amd, 3010) == InstallerExit::OkReboot);
    EF_CHECK(map_installer_exit(GpuVendor::Amd, 1) == InstallerExit::Failed);
}

EF_TEST(vdrv_copy_enforces_size_limit_and_cancel) {
    // Bilinen boyut sinirin ustunde: tek bayt okunmaz / yazilmaz.
    {
        CountingSource src;
        src.total = kPackageMaxBytes + 1;
        CountingSink dst;
        const auto r = copy_package(src, dst, src.total, {}, {});
        EF_CHECK(r.outcome == CopyOutcome::TooLarge && dst.got == 0 && src.given == 0);
    }
    // Boyut bilinmiyor (0) ama akis sinirin ustune cikiyor: sinirda durur, fazlasi yazilmaz.
    {
        CountingSource src;
        src.total = kPackageMaxBytes + (8u << 20);
        CountingSink dst;
        const auto r = copy_package(src, dst, 0, {}, {}, 64u << 20);
        EF_CHECK(r.outcome == CopyOutcome::TooLarge && dst.got <= kPackageMaxBytes);
    }
    {
        CountingSource src;
        src.total = 1000;
        CountingSink dst;
        EF_CHECK(copy_package(src, dst, 1000, {}, {}).outcome == CopyOutcome::TooSmall);
    }
    {
        CountingSource src;
        src.total = 5u << 20;
        CountingSink dst;
        std::vector<std::uint64_t> seen;
        const auto r = copy_package(src, dst, src.total, {}, [&](std::uint64_t d, std::uint64_t t) {
            EF_CHECK(t == src.total);
            seen.push_back(d);
        });
        EF_CHECK(r.outcome == CopyOutcome::Ok && r.bytes == src.total && dst.got == src.total);
        EF_CHECK(!seen.empty() && seen.back() == src.total);
    }
    {
        CountingSource src;
        src.total = 10u << 20;
        CountingSink dst;
        int reads = 0;
        src.on_read = [&] { ++reads; };
        bool stop = false;
        const auto r = copy_package(src, dst, src.total, [&] { return stop; }, [&](std::uint64_t d, std::uint64_t) { stop = d >= (3u << 20); });
        EF_CHECK(r.outcome == CopyOutcome::Cancelled && dst.got == (3u << 20) && reads == 3);
    }
    {
        CountingSource src;
        src.total = 4u << 20;
        src.fail_at_half = true;
        CountingSink dst;
        EF_CHECK(copy_package(src, dst, src.total, {}, {}).outcome == CopyOutcome::ReadFailed);
        CountingSource s2;
        s2.total = 4u << 20;
        CountingSink bad;
        bad.fail = true;
        EF_CHECK(copy_package(s2, bad, s2.total, {}, {}).outcome == CopyOutcome::WriteFailed);
    }
    EF_CHECK(package_size_ok(kPackageMinBytes) && package_size_ok(kPackageMaxBytes) && !package_size_ok(kPackageMinBytes - 1) &&
              !package_size_ok(kPackageMaxBytes + 1));
}

EF_TEST(vdrv_install_runs_official_switches_and_reads_version_back) {
    Rig g;
    const auto r = g.run();
    EF_CHECK(r.outcome == VendorOutcome::Installed && !r.reboot_required && r.exit_code == 0);
    EF_CHECK(r.version_before == "32.0.15.6094" && r.version_after == "32.0.15.7000");
    EF_CHECK(g.api.stages == 1 && g.api.verifies == 1 && g.api.runs == 1 && g.api.discards == 1);
    EF_CHECK(g.api.args == silent_args(GpuVendor::Nvidia) && g.api.timeout == kInstallerTimeoutMs);
    EF_CHECK(r.restore_point == RestorePointState::Created);
    const auto t = g.txs();
    EF_CHECK(t.size() == 1 && t[0].state == TxState::Active && t[0].steps.size() == 1);
    EF_CHECK(t.size() == 1 && t[0].steps[0].op.id == OperationId::VendorDriverInstall && t[0].steps[0].last_written == "32.0.15.7000");
    // NVIDIA cikis 1 = basari + yeniden baslatma; surum yeniden baslatmadan sonra degisir: "ayni surum" denmez.
    Rig rb;
    rb.api.launch.exit_code = 1;
    rb.api.after = rb.api.before;
    const auto r2 = rb.run();
    EF_CHECK(r2.outcome == VendorOutcome::Installed && r2.reboot_required);
    Rig same;
    same.api.after = same.api.before;
    EF_CHECK(same.run().outcome == VendorOutcome::SameVersion);
    Rig intel;
    intel.api.sig.signer = "Intel Corporation";
    intel.api.launch.exit_code = 7;
    const auto r3 = intel.run(GpuVendor::Intel);
    EF_CHECK(r3.outcome == VendorOutcome::Downgrade && intel.api.args == silent_args(GpuVendor::Intel));
    EF_CHECK(intel.txs().size() == 1 && intel.txs()[0].state == TxState::RecoveryRequired);
}

EF_TEST(vdrv_bad_signature_or_wrong_signer_never_runs_the_package) {
    for (const auto st : {SignatureStatus::Unsigned, SignatureStatus::Invalid, SignatureStatus::Untrusted, SignatureStatus::Unknown}) {
        Rig g;
        g.api.sig.status = st;
        const auto r = g.run();
        EF_CHECK(is_rejection(r.outcome) && g.api.runs == 0 && g.api.discards == 1 && g.txs().empty());
    }
    Rig w;
    w.api.sig.signer = "Advanced Micro Devices, Inc.";  // gecerli imza ama baska uretici
    EF_CHECK(w.run().outcome == VendorOutcome::WrongSigner && w.api.runs == 0 && w.txs().empty());
    Rig big;
    big.api.staged = {StageStatus::Ok, kPackageMaxBytes + 1};
    EF_CHECK(big.run().outcome == VendorOutcome::TooLarge && big.api.verifies == 0 && big.api.runs == 0);
    Rig gone;
    gone.api.staged = {StageStatus::NotFound, 0};
    EF_CHECK(gone.run().outcome == VendorOutcome::NotFound && gone.api.runs == 0);
    Rig bad;
    EF_CHECK(run_vendor_install(bad.deps(), "vdrv:nvidia:..\\x").outcome == VendorOutcome::BadTarget && bad.api.stages == 0);
}

EF_TEST(vdrv_game_gate_cancel_and_journal_block_the_install) {
    Rig g;
    g.gate = false;
    EF_CHECK(g.run().outcome == VendorOutcome::GateBlocked && g.api.runs == 0 && g.txs().empty());
    Rig late;  // geri yukleme noktasi sirasinda oyun acildi
    late.api.after_rp = [&late] { late.gate = false; };
    EF_CHECK(late.run().outcome == VendorOutcome::GateBlocked && late.api.runs == 0);
    EF_CHECK(late.txs().size() == 1 && late.txs()[0].state == TxState::Restored);
    Rig c;
    c.cancel = true;
    EF_CHECK(c.run().outcome == VendorOutcome::Cancelled && c.api.stages == 0 && c.api.runs == 0);
    Rig j;
    j.no_journal = true;
    EF_CHECK(j.run().outcome == VendorOutcome::JournalFailed && j.api.stages == 0 && j.api.runs == 0);
    Rig l;
    l.api.launch = {false, false, 0};
    EF_CHECK(l.run().outcome == VendorOutcome::LaunchFailed && l.txs().size() == 1 && l.txs()[0].state == TxState::Restored);
    Rig to;
    to.api.launch = {true, true, 0};
    EF_CHECK(to.run().outcome == VendorOutcome::Timeout && to.txs().size() == 1 && to.txs()[0].state == TxState::RecoveryRequired);
}

EF_TEST(vdrv_detail_round_trip) {
    VendorRun r;
    r.outcome = VendorOutcome::Installed;
    r.exit_code = 1;
    r.reboot_required = true;
    r.restore_point = RestorePointState::Recent;
    r.version_after = "32.0.15.7000";
    const auto d = encode_vendor_detail(r);
    EF_CHECK(d == "vd:installed:ec=1:rp=recent:v=32.0.15.7000:reboot");
    const auto back = decode_vendor_detail(d);
    EF_CHECK(back && back->outcome == VendorOutcome::Installed && back->exit_code == 1 && back->reboot_required &&
              back->restore_point == RestorePointState::Recent && back->version_after == "32.0.15.7000");
    r.version_after = "x; rm";  // surum gibi degil: tasinmaz
    EF_CHECK(encode_vendor_detail(r) == "vd:installed:ec=1:rp=recent:v=:reboot");
    for (const char* bad : {"di:installed:rc=2:hr=00000000:rp=recent", "vd:what:ec=0:rp=none:v=", "vd:installed:ec=x:rp=none:v=",
                            "vd:installed:ec=0:rp=maybe:v=", "vd:installed:ec=0:rp=none:v=1.2:extra", "vd:installed:ec=99999999999:rp=none:v="})
        EF_CHECK(!decode_vendor_detail(bad));
}

EF_TEST(vdrv_broker_allowlist_action_and_gate) {
    const ef::contracts::SettingOperation op{OperationId::VendorDriverInstall, OperationScope::Machine, vendor_target(GpuVendor::Nvidia, kId),
                                              std::string(kVendorInstallValue), false, false};
    EF_CHECK(broker::admin_operation_allowed(op) && broker::admin_apply_allowed(op));
    auto bad = op;
    bad.target = "vdrv:nvidia:C:\\Users\\x\\Downloads\\driver.exe";  // klasor disi yol
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad = op;
    bad.new_value = "run";
    EF_CHECK(!broker::admin_operation_allowed(bad));
    bad = op;
    bad.scope = OperationScope::CurrentUser;
    EF_CHECK(!broker::admin_operation_allowed(bad));

    broker::ExecutorDeps d;
    d.ctx.elevated = true;
    d.ctx.machine_lock_held = true;
    d.ctx.broker_user_sid = d.ctx.target_user_sid = "S-1-5-21-1";
    int calls = 0;
    d.vendor_install = [&](std::string_view t) {
        ++calls;
        EF_CHECK(t == op.target);
        VendorRun r;
        r.outcome = VendorOutcome::Installed;
        r.reboot_required = true;
        r.version_after = "32.0.15.7000";
        return r;
    };
    broker::AdminExecutor ex(d);
    const auto r = ex.handle(ipc::RequestAction::VendorInstall, op);
    EF_CHECK(calls == 1 && r.state == broker::StepState::Applied && r.pending_reboot);
    EF_CHECK(ex.handle(ipc::RequestAction::DriverInstall, op).state == broker::StepState::Rejected && calls == 1);  // eylem / islem eslesmeli
    EF_CHECK(ex.handle(ipc::RequestAction::Fix, op).state == broker::StepState::Rejected && calls == 1);
    EF_CHECK(ex.handle(ipc::RequestAction::VendorInstall, bad).state == broker::StepState::Rejected && calls == 1);
    d.permit = []() -> recovery::WriteDecision { return {false, "gate.game_running"}; };  // oyun acik
    broker::AdminExecutor closed(d);
    EF_CHECK(closed.handle(ipc::RequestAction::VendorInstall, op).state == broker::StepState::GateBlocked && calls == 1);
    d.permit = nullptr;
    d.vendor_install = [](std::string_view) {
        VendorRun x;
        x.outcome = VendorOutcome::WrongSigner;
        return x;
    };
    broker::AdminExecutor ws(d);
    EF_CHECK(ws.handle(ipc::RequestAction::VendorInstall, op).state == broker::StepState::Rejected);
    EF_CHECK(ipc::to_string(ipc::RequestAction::VendorInstall) == "vendor_install");
    EF_CHECK(ef::contracts::to_string(OperationId::VendorDriverInstall) == "vendor_driver_install");
    EF_CHECK(ef::contracts::parse_operation_id("vendor_driver_install") == OperationId::VendorDriverInstall);
}

EF_TEST(vdrv_client_notice_mapping) {
    broker::BatchResult b;
    b.elevation = ElevationOutcome::Denied;
    EF_CHECK(broker::vendor_install_notice(b).key == "updates.install.denied");
    b.elevation = ElevationOutcome::Failed;
    EF_CHECK(broker::vendor_install_notice(b).key == "updates.install.start_failed");
    b.elevation = ElevationOutcome::Started;
    EF_CHECK(broker::vendor_install_notice(b).key == "updates.install.unknown");
    const auto with = [&](VendorOutcome o, bool reboot, std::string ver, RestorePointState rp = RestorePointState::Created, std::uint32_t ec = 0) {
        VendorRun r;
        r.outcome = o, r.reboot_required = reboot, r.version_after = std::move(ver), r.restore_point = rp, r.exit_code = ec;
        b.steps = {broker::StepResult{vendor_target(GpuVendor::Nvidia, kId), broker::StepState::Applied, reboot, encode_vendor_detail(r)}};
        return broker::vendor_install_notice(b);
    };
    auto n = with(VendorOutcome::Installed, false, "32.0.15.7000");
    EF_CHECK(n.key == "updates.local.done" && n.installed && !n.warn && n.version == "32.0.15.7000" && !n.reboot);
    n = with(VendorOutcome::Installed, true, "32.0.15.6094");
    EF_CHECK(n.key == "updates.local.done_unread" && n.reboot && n.installed);  // yeniden baslatmadan once surum iddia edilmez
    n = with(VendorOutcome::Installed, false, "", RestorePointState::Failed);
    EF_CHECK(n.key == "updates.local.done_unread" && n.no_restore_point && n.warn);
    EF_CHECK(with(VendorOutcome::SameVersion, false, "32.0.15.6094").key == "updates.local.same_version");
    EF_CHECK(with(VendorOutcome::WrongSigner, false, "").key == "updates.local.wrong_signer");
    EF_CHECK(with(VendorOutcome::Unsigned, false, "").key == "updates.local.bad_signature");
    EF_CHECK(with(VendorOutcome::BadSignature, false, "").key == "updates.local.bad_signature");
    EF_CHECK(with(VendorOutcome::TooLarge, false, "").key == "updates.local.size");
    EF_CHECK(with(VendorOutcome::GateBlocked, false, "").key == "updates.install.gate");
    n = with(VendorOutcome::InstallerFailed, false, "", RestorePointState::Created, 17);
    EF_CHECK(n.key == "updates.local.failed" && n.exit_code == 17 && !n.installed && n.warn);
    EF_CHECK(with(VendorOutcome::Downgrade, false, "").key == "updates.local.downgrade");
    b.steps = {broker::StepResult{"x", broker::StepState::Rejected, false, "operation not in admin allowlist"}};
    EF_CHECK(broker::vendor_install_notice(b).key == "updates.local.rejected");
}
