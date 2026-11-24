// R-F2 (ADR-043): eski adli kurulumdan yeni markaya yukseltme. Yalniz sahte SCM / kayit defteri / kisayol / ACL ve
// bellek ici dosya sistemi; gercek Program Files / ProgramData / hizmet / kayit defterine dokunulmaz. Eski ad yalniz
// src/core/legacy ve legacy testlerinde gecer.
#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "core/legacy/legacy_entry.h"
#include "core/legacy/legacy_install.h"
#include "core/legacy/legacy_names.h"
#include "core/legacy/legacy_upgrade.h"
#include "core/legacy/machine_journal_migration.h"
#include "core/recovery/engine.h"
#include "core/recovery/journal.h"
#include "core/recovery/sha256.h"
#include "core/roles.h"
#include "core/setup/layout.h"
#include "ef_test.h"

using namespace ef::core::legacy;
using namespace ef::core::recovery;
using namespace ef::contracts;
namespace setup = ef::core::setup;

namespace {

setup::KnownRoots roots() {
    setup::KnownRoots r;
    r.program_files = L"C:\\Program Files";
    r.program_data = L"C:\\ProgramData";
    r.common_programs = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    r.local_app_data = L"C:\\Users\\oyuncu\\AppData\\Local";
    r.common_desktop = L"C:\\Users\\Public\\Desktop";
    return r;
}

const std::string kMachine = "m-test-1";
const std::string kNewBinary = "MZ-new-brand-binary-0.9.6";
const std::string kOldBinary = "MZ-old-brand-binary-0.9.5";

std::string ascii(std::wstring_view w) {
    std::string s;
    for (wchar_t ch : w) s += static_cast<char>(ch);
    return s;
}

bool under(const std::string& p, const std::string& root) {
    return !root.empty() && (p == root || (p.size() > root.size() && p.compare(0, root.size(), root) == 0 && p[root.size()] == '\\'));
}

// MemoryMigrationFs'i sarar: kilitli dosya enjeksiyonu + "eski artefakt Healthy'den once degisti mi" denetimi.
class GuardFs final : public MigrationFs {
public:
    GuardFs(MemoryMigrationFs& m, const UpgradePaths& p) : m_(m), p_(p) {}
    Kind kind(const std::string& path) override { return m_.kind(path); }
    FsStatus list_files(const std::string& d, std::vector<FileInfo>& o) override { return m_.list_files(d, o); }
    FsStatus hash_file(const std::string& path, FileDigest& o) override { return m_.hash_file(path, o); }
    FsStatus copy_file(const std::string& f, const std::string& t) override { return guard(t), m_.copy_file(f, t); }
    FsStatus read_file(const std::string& path, std::size_t mx, std::string& o) override { return m_.read_file(path, mx, o); }
    FsStatus write_file(const std::string& path, const std::string& b) override {
        guard(path);
        if (fail_record_write && path == p_.record_file()) return FsStatus::fail(FsCode::Denied, "injected");
        return m_.write_file(path, b);
    }
    FsStatus make_dirs(const std::string& path) override { return guard(path), m_.make_dirs(path); }
    FsStatus rename_dir(const std::string& f, const std::string& t) override { return guard(f), guard(t), m_.rename_dir(f, t); }
    FsStatus move_file(const std::string& f, const std::string& t) override { return guard(f), guard(t), m_.move_file(f, t); }
    FsStatus remove_file(const std::string& path) override {
        guard(path);
        if (locked.count(path)) return FsStatus::fail(FsCode::Busy, "sharing violation");
        return m_.remove_file(path);
    }
    FsStatus remove_tree(const std::string& path) override { return guard(path), m_.remove_tree(path); }
    std::optional<std::uint64_t> free_bytes(const std::string& path) override { return m_.free_bytes(path); }

    std::set<std::string> locked;
    bool fail_record_write = false;
    int violations = 0;  // eski kuruluma/eski veriye Healthy oncesi degistirici islem
    int old_data_mutations = 0;  // eski ProgramData / LocalAppData degisikligi (hicbir zaman olmamali)

    std::string phase_now() {
        std::string t;
        if (!m_.read_file(p_.record_file(), 1 << 20, t).ok()) return "none";
        const auto r = decode_record(t);
        return r ? std::string(to_string(r->phase)) : "corrupt";
    }

private:
    void guard(const std::string& path) {
        if (under(path, p_.old_machine_root) || under(path, old_user_root())) ++old_data_mutations;
        if (under(path, p_.old_dir) && phase_now() != "healthy" && phase_now() != "done") ++violations;
    }
    static std::string old_user_root() { return "C:\\Users\\oyuncu\\AppData\\Local\\" + ascii(kOldDataDirName); }
    MemoryMigrationFs& m_;
    const UpgradePaths& p_;
};

class FakeSys final : public UpgradeSystem {
public:
    struct Svc {
        std::string exe;
        SvcState state = SvcState::Stopped;
    };
    FakeSys(GuardFs& fs, MemoryMigrationFs& mem, const UpgradePaths& p, const UpgradeNames& n) : fs_(fs), mem_(mem), p_(p), n_(n) {}

    SvcState service_state(std::wstring_view name) override {
        const auto it = services.find(std::wstring(name));
        return it == services.end() ? SvcState::Missing : it->second.state;
    }
    std::optional<std::string> service_exe(std::wstring_view name) override {
        const auto it = services.find(std::wstring(name));
        if (it == services.end()) return std::nullopt;
        return it->second.exe;
    }
    FsStatus create_service(std::wstring_view name, const std::string& exe) override {
        nm(SysOp::CreateService, name);
        note("create_service", name);
        if (services.count(std::wstring(name))) return FsStatus::fail(FsCode::Exists, "exists");
        services[std::wstring(name)] = {exe, SvcState::Stopped};
        ++creates;
        return {};
    }
    FsStatus start_service(std::wstring_view name) override {
        nm(SysOp::StartService, name);
        note("start_service", name);
        if (fail_start_new && name == n_.new_service) return FsStatus::fail(FsCode::Io, "1053");
        services[std::wstring(name)].state = SvcState::Running;
        return {};
    }
    FsStatus stop_service(std::wstring_view name) override {
        nm(SysOp::StopService, name);
        note("stop_service", name);
        if (name == n_.old_service) old_guard();
        if (old_wont_stop && name == n_.old_service) return FsStatus::fail(FsCode::Busy, "still running");
        if (auto it = services.find(std::wstring(name)); it != services.end()) it->second.state = SvcState::Stopped;
        return {};
    }
    FsStatus delete_service(std::wstring_view name) override {
        nm(SysOp::DeleteService, name);
        note("delete_service", name);
        if (name == n_.old_service) old_guard();
        services.erase(std::wstring(name));
        return {};
    }
    bool service_healthy(std::wstring_view name, std::int64_t) override {
        note("health", name);
        return !unhealthy && service_state(name) == SvcState::Running;
    }
    std::optional<std::string> arp_install_location(std::wstring_view key) override {
        const auto it = arp.find(std::wstring(key));
        if (it == arp.end()) return std::nullopt;
        return it->second;
    }
    FsStatus write_arp(std::wstring_view key) override {
        nm(SysOp::WriteArp, key);
        note("write_arp", key);
        if (fail_arp) return FsStatus::fail(FsCode::Denied, "injected");
        arp[std::wstring(key)] = p_.new_dir;
        return {};
    }
    FsStatus delete_arp(std::wstring_view key) override {
        nm(SysOp::DeleteArp, key);
        note("delete_arp", key);
        if (key == n_.old_arp_key) old_guard();
        arp.erase(std::wstring(key));
        return {};
    }
    bool shortcut_present(const std::string& lnk) override { return links.count(lnk) != 0; }
    std::optional<std::string> shortcut_target(const std::string& lnk) override {
        const auto it = links.find(lnk);
        if (it == links.end()) return std::nullopt;
        return it->second;
    }
    FsStatus write_shortcut(const std::string& lnk, const std::string& target) override {
        pa(SysOp::WriteShortcut, lnk);
        log.push_back("write_lnk " + lnk);
        links[lnk] = target;
        return {};
    }
    FsStatus delete_shortcut(const std::string& lnk) override {
        pa(SysOp::DeleteShortcut, lnk);
        log.push_back("delete_lnk " + lnk);
        if (lnk == p_.old_start_lnk || lnk == p_.old_desktop_lnk) old_guard();
        links.erase(lnk);
        return {};
    }
    FsStatus apply_install_acl(const std::string& dir) override {
        pa(SysOp::InstallAcl, dir);
        log.push_back("install_acl " + dir);
        return {};
    }
    FsStatus apply_machine_acl(const std::string& dir) override {
        pa(SysOp::MachineAcl, dir);
        log.push_back("machine_acl_apply " + dir);
        return {};
    }
    FsStatus verify_machine_acl(const std::string& dir) override {
        log.push_back("machine_acl_verify " + dir);
        if (acl_bad) return FsStatus::fail(FsCode::Denied, "user writable");
        return {};
    }
    FsStatus schedule_delete_on_reboot(const std::string& path) override {
        pa(SysOp::RebootDelete, path);
        log.push_back("reboot_delete " + path);
        if (under(path, p_.old_dir)) old_guard();
        reboot.insert(path);
        return {};
    }
    FsStatus restore_legacy_exe() override {
        log.push_back("restore_legacy_exe");
        if (fail_restore) return FsStatus::fail(FsCode::Busy, "exe in use");
        const auto backup = p_.old_exe + ".old";
        if (!mem_.files.count(backup)) return mem_.files.count(p_.old_exe) ? FsStatus{} : FsStatus::fail(FsCode::NotFound, "no backup");
        mem_.files[p_.old_exe] = mem_.files[backup];
        mem_.files.erase(backup);
        return {};
    }

    std::map<std::wstring, Svc> services;
    std::map<std::wstring, std::string> arp;
    std::map<std::string, std::string> links;
    std::set<std::string> reboot;
    std::vector<std::string> log;
    int creates = 0;
    bool fail_start_new = false, unhealthy = false, old_wont_stop = false, fail_restore = false, fail_arp = false, acl_bad = false;
    int violations = 0;
    int disallowed = 0;  // T3b: platform izin listesinin reddedecegi cagri (WinUpgradeSystem Win32'ye ulastirmaz)

private:
    void nm(SysOp op, std::wstring_view name) { disallowed += upgrade_name_allowed(op, name, n_) ? 0 : 1; }
    void pa(SysOp op, const std::string& path) { disallowed += upgrade_path_allowed(op, path, p_) ? 0 : 1; }
    void note(const char* op, std::wstring_view name) { log.push_back(std::string(op) + " " + ascii(name)); }
    void old_guard() {
        const auto ph = fs_.phase_now();
        if (ph != "healthy" && ph != "done") ++violations;
    }
    GuardFs& fs_;
    MemoryMigrationFs& mem_;
    const UpgradePaths& p_;
    const UpgradeNames& n_;
};

JournalTx tx3(const std::string& id) {
    JournalTx tx;
    tx.id = id;
    tx.target_sid = "S-1-5-21-1-2-3-1001";
    for (const char* t : {"hags", "power", "drs"}) {
        JournalStep s;
        s.op = SettingOperation{OperationId::GameModeSet, OperationScope::Machine, t, std::string("new-") + t, false, false};
        tx.steps.push_back(s);
    }
    return tx;
}

// Eski surumun makine journal'i: proje JournalStore'u ile yazilmis Active islem (3 adim).
struct MachineFixture {
    MemoryJournalFs jfs;
    JournalStore store{jfs, kMachine};
    FakeAdapter fake;
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    std::string active_id = "00112233445566778899aabbccddeeff";
    MachineFixture() {
        reg.add(OperationId::GameModeSet, &fake);
        fake.values = {{"hags", "2"}, {"power", "balanced"}};  // "drs" onceden yok
        auto r = engine.apply(tx3(active_id));
        EF_CHECK(r.ok() && r.value().state == TxState::Active);
    }
};

// Gercekci 0.9.5 makinesi: degisim yapilmis (eski yolda yeni ikili + exe.old), eski hizmet calisiyor (ev sahibi biziz),
// eski ARP, eski Baslat + masaustu kisayolu, eski makine verisi (journal + fixes + updates + setup), eski kullanici verisi.
struct World {
    UpgradePaths p = make_upgrade_paths(roots(), legacy_layout(roots()).exe);
    UpgradeNames n = default_upgrade_names();
    MemoryMigrationFs mem;
    GuardFs fs{mem, p};
    FakeSys sys{fs, mem, p, n};
    MachineFixture mx;
    UpgradeEnv env;

    World() {
        mem.put(p.old_exe, kNewBinary);
        mem.put(p.old_exe + ".old", kOldBinary);
        for (const auto& [name, bytes] : mx.jfs.files) mem.put(join_path(p.old_machine_root, "journal/" + name), bytes);
        mem.put(join_path(p.old_machine_root, "journal/fixes/tx-ffeeddccbbaa99887766554433221100.json"), "{\"fix\":1}");
        mem.put(join_path(p.old_machine_root, "updates/state.json"), "{\"phase\":\"ui_healthy\"}");
        mem.put(join_path(p.old_machine_root, "updates/0.9.6.part"), std::string(2048, 'x'));
        mem.put(join_path(p.old_machine_root, "setup/setup.journal"), "");
        mem.put("C:\\Users\\oyuncu\\AppData\\Local\\" + ascii(kOldDataDirName) + "\\preferences.json", "{}");
        sys.services[n.old_service] = {p.old_exe, SvcState::Running};
        sys.arp[n.old_arp_key] = p.old_dir;
        sys.links[p.old_start_lnk] = p.old_exe;
        sys.links[p.old_desktop_lnk] = p.old_exe;
        env.role = UpgradeRole::LegacyHost;
        env.version = "0.9.6";
        env.now_unix_ms = 1795000000000;
        env.machine_id = kMachine;
        env.sleep = [](int) {};
        mem.mutations.clear();
    }
    UpgradeResult host() {
        env.role = UpgradeRole::LegacyHost;
        return run_legacy_upgrade(fs, sys, p, n, env);
    }
    UpgradeResult newsvc() {
        env.role = UpgradeRole::NewService;
        p.self_exe = p.new_exe;
        auto r = run_legacy_upgrade(fs, sys, p, n, env);
        p.self_exe = p.old_exe;
        return r;
    }
    // Ev sahibi cikis 0 ile durdu (SCM yeniden baslatmaz).
    void host_exited() { sys.services[n.old_service].state = SvcState::Stopped; }
    std::optional<UpgradeRecord> record() {
        std::string t;
        if (!mem.read_file(p.record_file(), 1 << 20, t).ok()) return std::nullopt;
        return decode_record(t);
    }
    bool has_file(const std::string& path) const { return mem.files.count(path) != 0; }
    bool any_under(const std::string& root) const {
        for (const auto& [path, f] : mem.files)
            if (under(path, root)) return true;
        return false;
    }
    std::size_t log_index(const std::string& prefix) const {
        for (std::size_t i = 0; i < sys.log.size(); ++i)
            if (sys.log[i].rfind(prefix, 0) == 0) return i;
        return static_cast<std::size_t>(-1);
    }
    void expect_old_install_intact(const char* what) {
        (void)what;
        EF_CHECK(sys.service_state(n.old_service) != SvcState::Missing);
        EF_CHECK(sys.arp.count(n.old_arp_key) == 1);
        EF_CHECK(sys.links.count(p.old_start_lnk) == 1 && sys.links.count(p.old_desktop_lnk) == 1);
        EF_CHECK(has_file(p.old_exe));
        EF_CHECK(has_file(join_path(p.old_machine_root, "updates/state.json")));
        EF_CHECK(fs.old_data_mutations == 0);
    }
    void expect_no_new_artefacts() {
        EF_CHECK(sys.service_state(n.new_service) == SvcState::Missing);
        EF_CHECK(sys.arp.count(n.new_arp_key) == 0);
        EF_CHECK(sys.links.count(p.new_start_lnk) == 0 && sys.links.count(p.new_desktop_lnk) == 0);
        EF_CHECK(!has_file(p.new_exe));
        EF_CHECK(!any_under(join_path(p.new_machine_root, "journal")));
        EF_CHECK(!has_file(join_path(p.new_machine_root, std::string(kMachineMarkerFile))));
    }
};

std::string utf8(const std::filesystem::path& x) {
    const auto u = x.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace

// ---- 1. Eski cagri bicimleri ----
EF_TEST(legacy_args_old_service_and_client_invocations_map_to_upgrade) {
    const auto legacy = legacy_layout(roots());
    const auto current = setup::make_layout(roots());
    // Eski ImagePath tam olarak "\"<eski exe>\" --service": arguman kismi gozetimsiz yukseltmeye esler.
    EF_CHECK(setup::service_image_path(legacy) == L"\"" + legacy.exe.native() + L"\" --service");
    EF_CHECK(legacy.exe.filename().native() == kOldExeName);
    EF_CHECK(legacy.dir.filename().native() == kOldInstallDirName);
    for (const auto& l : kLegacyLaunches) EF_CHECK(classify_legacy_invocation(l.spelling, legacy.exe, roots()) == l.kind);
    EF_CHECK(classify_legacy_invocation(L"--service", legacy.exe, roots()) == LegacyInvocation::ServiceHost);
    EF_CHECK(legacy_action(LegacyInvocation::ServiceHost) == LegacyAction::UnattendedUpgrade);
    EF_CHECK(legacy_action(LegacyInvocation::HealthHandshake) == LegacyAction::UiSignalOldService);
    EF_CHECK(legacy_action(LegacyInvocation::Shortcut) == LegacyAction::Ui);  // Setup penceresine yonlenmez
    EF_CHECK(legacy_action(LegacyInvocation::Uninstall) == LegacyAction::RefuseUpgradePending);
    // Buyuk/kucuk harf ve \\?\ oneki eski yol taninmasini bozmaz.
    EF_CHECK(classify_legacy_invocation(L"--service", std::filesystem::path(L"\\\\?\\c:\\program files\\" + std::wstring(kOldInstallDirName) + L"\\" +
                                                                             std::wstring(kOldExeName)),
                                         roots()) == LegacyInvocation::ServiceHost);
    // Yeni kurulum yolu / tasinabilir kopya eski cagri degildir.
    EF_CHECK(classify_legacy_invocation(L"--service", current.exe, roots()) == LegacyInvocation::None);
    EF_CHECK(classify_legacy_invocation(L"--updated-from=0.9.5", L"D:\\Downloads\\x.exe", roots()) == LegacyInvocation::None);
    EF_CHECK(classify_legacy_invocation(L"--service --tray", legacy.exe, roots()) == LegacyInvocation::Other);
    // Saglik sinyali eski pipe ve eski hizmet adina gider; cikis kodlari 0.9.5 SCM sozlesmesiyle ayni.
    EF_CHECK(legacy_health_pipe() == kOldHelperPipeName);
    EF_CHECK(legacy_health_service() == kOldServiceName);
    static_assert(kLegacyHostExitHandedOff == 0 && kLegacyHostExitRolledBack == 5 && kLegacyHostExitRetry != 0);
    static_assert(ef::core::kExitRecoveryNeedsUser != kExitLegacyUpgradePending);
    // Sahibi olunan eski dosyalar: yalniz EXE ve degisim kardesleri.
    const auto owned = legacy_owned_files(legacy);
    EF_CHECK(owned.size() == 5 && owned[0] == legacy.exe && owned[2].native() == legacy.exe.native() + L".old");
}

// T3c (ADR-044): wWinMain giris yonlendirmesi. main.cpp ayni bilesimi kullanir: RefuseUpgradePending -> cikis 21;
// ForceUi -> effective_role(a, true) (kurulum penceresi yok).
EF_TEST(legacy_launch_route_bare_tray_ui_and_uninstall_refused_with_record) {
    const auto legacy = legacy_layout(roots());
    const auto current = setup::make_layout(roots());
    const auto route_for = [&](std::wstring_view cmd, const std::filesystem::path& exe, bool record) {
        return legacy_launch_route(classify_legacy_invocation(cmd, exe, roots()), record);
    };
    const auto role_for = [&](std::wstring_view cmd, const std::filesystem::path& exe, bool record) {
        const auto a = ef::core::parse_launch_args(cmd);
        EF_CHECK(a.ok());
        const auto r = route_for(cmd, exe, record);
        const bool from_install = !a.value().bare || r == LegacyRoute::ForceUi || setup::is_installed_location(exe, current);
        return ef::core::effective_role(a.value(), from_install);
    };
    for (bool record : {false, true}) {
        // Eski kisayol (bos) ve eski Run degeri (--tray): Ui, kurulum penceresi degil.
        EF_CHECK(route_for(L"", legacy.exe, record) == LegacyRoute::ForceUi);
        EF_CHECK(route_for(L"--tray", legacy.exe, record) == LegacyRoute::ForceUi);
        EF_CHECK(role_for(L"", legacy.exe, record) == ef::core::Role::Ui);
        EF_CHECK(role_for(L"--tray", legacy.exe, record) == ef::core::Role::Ui);
        // Eski hizmet / saglik / kurtarma girisleri kendi yollarinda (yonlendirme yok).
        EF_CHECK(route_for(L"--service", legacy.exe, record) == LegacyRoute::Normal);
        EF_CHECK(route_for(L"--updated-from=0.9.5", legacy.exe, record) == LegacyRoute::Normal);
        EF_CHECK(route_for(L"--recovery", legacy.exe, record) == LegacyRoute::Normal);
        EF_CHECK(role_for(L"--service", legacy.exe, record) == ef::core::Role::Service);
    }
    // --uninstall eski yoldan: kayit varken reddedilir (21), kayit yokken normal Ui kaldirma onayi.
    EF_CHECK(route_for(L"--uninstall", legacy.exe, true) == LegacyRoute::RefuseUpgradePending);
    EF_CHECK(route_for(L"--uninstall", legacy.exe, false) == LegacyRoute::Normal);
    static_assert(kExitLegacyUpgradePending == 21);
    // Eski yol disinda yonlendirme yok: tasinabilir kopya bos acilista yine kurulum penceresi, yeni kurulum Ui.
    EF_CHECK(route_for(L"--uninstall", current.exe, true) == LegacyRoute::Normal);
    EF_CHECK(route_for(L"", L"D:\\Downloads\\x.exe", true) == LegacyRoute::Normal);
    EF_CHECK(role_for(L"", L"D:\\Downloads\\x.exe", true) == ef::core::Role::Setup);
    EF_CHECK(role_for(L"", current.exe, true) == ef::core::Role::Ui);
    // Tablonun her satiri bir yonlendirme verir; yalniz Shortcut/Tray ForceUi, yalniz Uninstall/Other reddedilebilir.
    for (const auto& l : kLegacyLaunches) {
        const auto r = legacy_launch_route(l.kind, true);
        const bool force = l.kind == LegacyInvocation::Shortcut || l.kind == LegacyInvocation::Tray;
        EF_CHECK((r == LegacyRoute::ForceUi) == force);
        EF_CHECK((r == LegacyRoute::RefuseUpgradePending) == (l.kind == LegacyInvocation::Uninstall));
        EF_CHECK(!to_string(r).empty());
    }
    EF_CHECK(legacy_launch_route(LegacyInvocation::Other, true) == LegacyRoute::RefuseUpgradePending);
    EF_CHECK(legacy_launch_route(LegacyInvocation::None, true) == LegacyRoute::Normal);
}

EF_TEST(legacy_upgrade_paths_and_names_split_old_and_new) {
    const auto p = make_upgrade_paths(roots(), legacy_layout(roots()).exe);
    const auto n = default_upgrade_names();
    EF_CHECK(p.old_exe == utf8(legacy_layout(roots()).exe));
    EF_CHECK(p.new_exe == utf8(setup::make_layout(roots()).exe));
    EF_CHECK(p.self_exe == p.old_exe);
    EF_CHECK(p.old_machine_root != p.new_machine_root && !p.old_machine_root.empty());
    EF_CHECK(p.old_owned_files.size() == 5);
    EF_CHECK(n.old_service == kOldServiceName && n.new_service == setup::kServiceName);
    EF_CHECK(n.old_arp_key == legacy_arp_key() && n.new_arp_key == setup::kArpKey);
    UpgradeRecord r;
    r.phase = UpgradePhase::Registered;
    r.attempt = 2;
    r.version = "0.9.6";
    r.created_service = r.created_arp = r.migrated_journals = true;
    r.stage = "x";
    r.error = "y";
    r.updated_unix_ms = 7;
    const auto back = decode_record(encode_record(r));
    EF_CHECK(back && *back == r);
    EF_CHECK(!decode_record("{\"schema_version\":99}"));
    EF_CHECK(!decode_record("{bozuk"));
}

// ---- 2. Anahtar test (once bos uygulamaya karsi kirmizi goruldu) ----
EF_TEST(legacy_upgrade_happy_path_side_by_side_then_cleanup_only_after_healthy) {
    World w;
    const auto h = w.host();
    EF_CHECK(h.outcome == UpgradeOutcome::HandedOff);
    EF_CHECK(h.exit_code == kLegacyHostExitHandedOff);
    // Yeni kurulum yan yana: EXE bayt bayt kendi goruntusu, hizmet yeni adla ve yeni EXE ile calisiyor.
    EF_CHECK(w.has_file(w.p.new_exe) && w.mem.files[w.p.new_exe].bytes == kNewBinary);
    EF_CHECK(w.sys.service_state(w.n.new_service) == SvcState::Running);
    EF_CHECK(w.sys.service_exe(w.n.new_service) == w.p.new_exe);
    EF_CHECK(w.sys.arp[w.n.new_arp_key] == w.p.new_dir);
    EF_CHECK(w.sys.links[w.p.new_start_lnk] == w.p.new_exe);
    EF_CHECK(w.sys.links[w.p.new_desktop_lnk] == w.p.new_exe);  // eski masaustu kisayolu bizimdi
    EF_CHECK(w.log_index("install_acl " + w.p.new_dir) != static_cast<std::size_t>(-1));
    // Healthy'ye kadar eski kurulumdan hicbir sey silinmedi / degismedi.
    w.expect_old_install_intact("after host");
    EF_CHECK(w.has_file(w.p.old_exe + ".old"));
    EF_CHECK(w.record() && w.record()->phase == UpgradePhase::Healthy);
    // Makine journal'i: korumali kok ACL'i KOPYADAN ONCE uygulandi; journal + alt klasor ACL'i devirden sonra geri okundu.
    EF_CHECK(h.journals.outcome == MigrationOutcome::Migrated && h.journals.acl_verified);
    const auto apply_at = w.log_index("machine_acl_apply " + w.p.new_machine_root);
    EF_CHECK(apply_at != static_cast<std::size_t>(-1) && apply_at < w.log_index("machine_acl_verify"));
    EF_CHECK(w.log_index("machine_acl_verify " + join_path(w.p.new_machine_root, "journal\\fixes")) != static_cast<std::size_t>(-1));
    for (const auto& [name, bytes] : w.mx.jfs.files) EF_CHECK(w.mem.files[join_path(w.p.new_machine_root, "journal/" + name)].bytes == bytes);
    EF_CHECK(!w.any_under(join_path(w.p.new_machine_root, "updates")));  // degisim hazirligi kopyalanmadi

    // Yeni hizmet ilk acilisi: eski temizlik.
    w.host_exited();
    const auto c = w.newsvc();
    EF_CHECK(c.outcome == UpgradeOutcome::Completed);
    EF_CHECK(!c.reboot_pending);
    EF_CHECK(w.sys.service_state(w.n.old_service) == SvcState::Missing);
    EF_CHECK(w.sys.arp.count(w.n.old_arp_key) == 0);
    EF_CHECK(w.sys.links.count(w.p.old_start_lnk) == 0 && w.sys.links.count(w.p.old_desktop_lnk) == 0);
    EF_CHECK(!w.has_file(w.p.old_exe) && !w.has_file(w.p.old_exe + ".old"));
    EF_CHECK(w.mem.kind(w.p.old_dir) == MigrationFs::Kind::Missing);
    // Yeni kurulum dokunulmadi; eski ProgramData / LocalAppData yedek olarak duruyor.
    EF_CHECK(w.sys.service_state(w.n.new_service) == SvcState::Running && w.has_file(w.p.new_exe));
    EF_CHECK(w.has_file(join_path(w.p.old_machine_root, "updates/state.json")));
    for (const auto& [name, bytes] : w.mx.jfs.files) EF_CHECK(w.mem.files[join_path(w.p.old_machine_root, "journal/" + name)].bytes == bytes);
    EF_CHECK(w.fs.old_data_mutations == 0);
    EF_CHECK(w.record() && w.record()->phase == UpgradePhase::Done);
    EF_CHECK(w.fs.violations == 0 && w.sys.violations == 0);  // hicbir eski silme Healthy'den once olmadi
}

// ---- 3. Hata durumlari: eski kurulum calismaya devam eder ----
EF_TEST(legacy_upgrade_new_service_start_failure_rolls_back_to_old_exe) {
    World w;
    w.sys.fail_start_new = true;
    const auto r = w.host();
    EF_CHECK(r.outcome == UpgradeOutcome::RolledBack);
    EF_CHECK(r.exit_code == kLegacyHostExitRolledBack);
    EF_CHECK(r.stage == "new_service");
    w.expect_no_new_artefacts();
    w.expect_old_install_intact("rollback");
    EF_CHECK(w.mem.files[w.p.old_exe].bytes == kOldBinary && !w.has_file(w.p.old_exe + ".old"));  // eski EXE geri geldi
    EF_CHECK(w.record() && w.record()->phase == UpgradePhase::RolledBack);
    EF_CHECK(w.fs.violations == 0 && w.sys.violations == 0);
}

EF_TEST(legacy_upgrade_unhealthy_new_service_rolls_back) {
    World w;
    w.sys.unhealthy = true;
    const auto r = w.host();
    EF_CHECK(r.outcome == UpgradeOutcome::RolledBack && r.exit_code == kLegacyHostExitRolledBack);
    w.expect_no_new_artefacts();
    w.expect_old_install_intact("unhealthy");
    EF_CHECK(w.sys.violations == 0);
}

EF_TEST(legacy_upgrade_journal_migration_failure_rolls_back) {
    World w;
    w.mem.corrupt_copy_of = "fixes\\tx-ffeeddccbbaa99887766554433221100.json";
    const auto r = w.host();
    EF_CHECK(r.outcome == UpgradeOutcome::RolledBack);
    EF_CHECK(r.stage == "journals" && r.journals.outcome == MigrationOutcome::Failed);
    w.expect_no_new_artefacts();
    w.expect_old_install_intact("journals");
    EF_CHECK(!w.any_under(join_path(w.p.new_machine_root, "journal.migrating")));
}

EF_TEST(legacy_upgrade_disk_full_and_arp_failure_roll_back) {
    {
        World w;
        w.mem.files[w.p.old_exe].bytes = std::string(1 << 20, 'M');  // yeni EXE kopyasi sigmaz
        w.mem.capacity = 64 * 1024;
        const auto r = w.host();
        EF_CHECK(r.outcome == UpgradeOutcome::RolledBack && r.stage == "new_files");
        w.expect_no_new_artefacts();
        w.expect_old_install_intact("disk full");
    }
    {
        World w;
        w.sys.fail_arp = true;  // hizmet olustu + calisti, sonra ARP yazilamadi: hizmet de geri alinir
        const auto r = w.host();
        EF_CHECK(r.outcome == UpgradeOutcome::RolledBack && r.stage == "register");
        w.expect_no_new_artefacts();
        w.expect_old_install_intact("arp");
        EF_CHECK(w.log_index("delete_service " + ascii(w.n.new_service)) != static_cast<std::size_t>(-1));
    }
}

EF_TEST(legacy_upgrade_unwritable_record_changes_nothing_and_restores_old_exe) {
    World w;
    w.fs.fail_record_write = true;
    const auto r = w.host();
    EF_CHECK(r.outcome == UpgradeOutcome::RolledBack && r.stage == "begin");
    w.expect_no_new_artefacts();
    EF_CHECK(!w.has_file(w.p.new_exe) && w.sys.creates == 0);
    EF_CHECK(w.mem.files[w.p.old_exe].bytes == kOldBinary);
}

EF_TEST(legacy_upgrade_locked_old_exe_rollback_is_retried) {
    World w;
    w.sys.fail_start_new = true;
    w.sys.fail_restore = true;  // yeni UI eski yoldan hala calisiyor
    auto r = w.host();
    EF_CHECK(r.outcome == UpgradeOutcome::RollbackPending && r.exit_code == kLegacyHostExitRetry);
    EF_CHECK(w.record() && w.record()->phase == UpgradePhase::RollingBack);
    w.expect_no_new_artefacts();
    // SCM 5 sn sonra ayni ImagePath'i yeniden baslatir: kayit RollingBack -> yalniz geri alma tamamlanir.
    w.sys.fail_restore = false;
    w.sys.fail_start_new = false;
    r = w.host();
    EF_CHECK(r.outcome == UpgradeOutcome::RolledBack && r.exit_code == kLegacyHostExitRolledBack);
    EF_CHECK(w.mem.files[w.p.old_exe].bytes == kOldBinary);
    EF_CHECK(w.sys.service_state(w.n.new_service) == SvcState::Missing);
}

// ---- 4. Idempotent yeniden calistirma ----
EF_TEST(legacy_upgrade_is_idempotent_and_retries_after_rollback) {
    World w;
    EF_CHECK(w.host().outcome == UpgradeOutcome::HandedOff);
    // Ev sahibi yeniden baslarsa (ornek: yeniden baslatma) hicbir sey yeniden yapilmaz, hizmet sunmaz.
    const auto before = w.sys.log.size();
    const auto again = w.host();
    EF_CHECK(again.outcome == UpgradeOutcome::HandedOff && again.exit_code == 0);
    EF_CHECK(w.sys.creates == 1 && w.sys.log.size() == before);
    // Yeni hizmet Healthy'den once acilirsa dokunmaz.
    {
        World x;
        x.sys.fail_start_new = false;
        EF_CHECK(x.newsvc().outcome == UpgradeOutcome::NotApplicable);
    }
    w.host_exited();
    EF_CHECK(w.newsvc().outcome == UpgradeOutcome::Completed);
    const auto muts = w.mem.mutations.size();
    const auto logs = w.sys.log.size();
    EF_CHECK(w.newsvc().outcome == UpgradeOutcome::Completed);
    EF_CHECK(w.mem.mutations.size() == muts && w.sys.log.size() == logs);  // ikinci kez hicbir degisiklik

    // Geri alinmis denemeden sonra eski istemci yeniden degisim yapar: yeni deneme bastan ve basarili.
    World y;
    y.sys.fail_start_new = true;
    EF_CHECK(y.host().outcome == UpgradeOutcome::RolledBack);
    y.mem.files[y.p.old_exe + ".old"] = y.mem.files[y.p.old_exe];  // eski hizmet yeniden degistirdi
    y.mem.files[y.p.old_exe].bytes = kNewBinary;
    y.sys.fail_start_new = false;
    const auto r2 = y.host();
    EF_CHECK(r2.outcome == UpgradeOutcome::HandedOff);
    EF_CHECK(y.record() && y.record()->attempt == 2);
    EF_CHECK(y.sys.violations == 0 && y.fs.violations == 0);
}

EF_TEST(legacy_upgrade_new_service_waits_while_host_upgrade_incomplete) {
    World w;
    UpgradeRecord r;
    r.phase = UpgradePhase::NewService;
    r.attempt = 1;
    w.mem.put(w.p.record_file(), encode_record(r));
    w.mem.mutations.clear();
    const auto res = w.newsvc();
    EF_CHECK(res.outcome == UpgradeOutcome::WaitingForHost);
    EF_CHECK(w.mem.mutations.empty());
    w.expect_old_install_intact("waiting");
}

// ---- 5. Temizlik kenar durumlari ----
EF_TEST(legacy_upgrade_old_service_still_running_defers_cleanup) {
    World w;
    EF_CHECK(w.host().outcome == UpgradeOutcome::HandedOff);
    w.sys.old_wont_stop = true;  // ev sahibi henuz durmadi
    const auto r = w.newsvc();
    EF_CHECK(r.outcome == UpgradeOutcome::CleanupPending && r.stage == "old_service");
    w.expect_old_install_intact("old running");
    EF_CHECK(w.record() && w.record()->phase == UpgradePhase::Healthy);
    w.sys.old_wont_stop = false;
    w.host_exited();
    EF_CHECK(w.newsvc().outcome == UpgradeOutcome::Completed);
}

EF_TEST(legacy_upgrade_locked_old_files_scheduled_for_reboot_delete) {
    World w;
    EF_CHECK(w.host().outcome == UpgradeOutcome::HandedOff);
    w.host_exited();
    w.fs.locked.insert(w.p.old_exe);  // ornek: antivirus tarayici hala acik tutuyor
    const auto r = w.newsvc();
    EF_CHECK(r.outcome == UpgradeOutcome::Completed);
    EF_CHECK(r.reboot_pending);
    EF_CHECK(w.sys.reboot.count(w.p.old_exe) == 1);
    EF_CHECK(w.sys.reboot.count(w.p.old_dir) == 1);  // dosyadan sonra klasor
    {
        const auto& lg = w.sys.log;
        const auto ie = std::find(lg.begin(), lg.end(), "reboot_delete " + w.p.old_exe);
        const auto id = std::find(lg.begin(), lg.end(), "reboot_delete " + w.p.old_dir);
        EF_CHECK(ie != lg.end() && id != lg.end() && ie < id);
    }
    EF_CHECK(!w.has_file(w.p.old_exe + ".old"));  // kilitli olmayan silindi
    EF_CHECK(w.record() && w.record()->phase == UpgradePhase::Done && w.record()->reboot_pending);
    EF_CHECK(w.sys.violations == 0);
}

EF_TEST(legacy_upgrade_keeps_foreign_shortcuts_arp_and_files) {
    World w;
    w.sys.links[w.p.old_desktop_lnk] = "C:\\Tools\\other.exe";  // kullanici kendi kisayolunu ayni adla koymus
    w.sys.arp[w.n.old_arp_key] = "D:\\Elsewhere";              // baska konumu gosteren kayit
    w.mem.put(join_path(w.p.old_dir, "notes.txt"), "kullanici dosyasi");
    w.mem.mutations.clear();
    EF_CHECK(w.host().outcome == UpgradeOutcome::HandedOff);
    EF_CHECK(w.sys.links.count(w.p.new_desktop_lnk) == 0);  // eski masaustu bizim degildi: yeni olusturulmadi
    w.host_exited();
    EF_CHECK(w.newsvc().outcome == UpgradeOutcome::Completed);
    EF_CHECK(w.sys.links[w.p.old_desktop_lnk] == "C:\\Tools\\other.exe");
    EF_CHECK(w.sys.arp.count(w.n.old_arp_key) == 1);
    EF_CHECK(w.has_file(join_path(w.p.old_dir, "notes.txt")));
    EF_CHECK(!w.has_file(w.p.old_exe));
    EF_CHECK(w.sys.reboot.count(w.p.old_dir) == 0);  // klasor bos degil: dokunulmadi
}

// ---- T3b: Win32 baglamasinin saf sozlesmeleri ----
// SCM cikis sozlesmesi: her senaryonun cikis kodu tabloyla ayni (0 devraldi, 1 yanlis konum, 5 geri alindi, 6 yeniden).
EF_TEST(legacy_upgrade_exit_code_contract) {
    {
        World w;
        const auto r = w.host();
        EF_CHECK(r.outcome == UpgradeOutcome::HandedOff && r.exit_code == 0);
    }
    {
        World w;
        w.p.self_exe = w.p.new_exe;  // yeni yoldan LegacyHost: yukseltme yok
        const auto r = w.host();
        EF_CHECK(r.outcome == UpgradeOutcome::NotApplicable && r.exit_code == kLegacyHostExitWrongLocation && r.exit_code == 1);
        EF_CHECK(w.sys.log.empty() && w.mem.mutations.empty());
    }
    {
        World w;
        w.sys.unhealthy = true;
        const auto r = w.host();
        EF_CHECK(r.outcome == UpgradeOutcome::RolledBack && r.exit_code == 5);
    }
    {
        World w;
        w.sys.unhealthy = true;
        w.sys.fail_restore = true;
        const auto r = w.host();
        EF_CHECK(r.outcome == UpgradeOutcome::RollbackPending && r.exit_code == 6);
    }
    {  // NewService rolu SCM'ye kod bildirmez (yeni hizmet normal calisir)
        World w;
        EF_CHECK(w.host().exit_code == 0);
        w.host_exited();
        EF_CHECK(w.newsvc().exit_code == 0);
    }
}

// Temizlik YALNIZ kalici "healthy" kaydiyla: diger her kayit durumu (eksik, bozuk, suren, geri alinmis) eski kuruluma
// dokunmaz; "done" yeniden calistirildiginda hicbir sey degismez.
EF_TEST(legacy_upgrade_cleanup_gated_on_persisted_healthy) {
    const UpgradePhase not_healthy[] = {UpgradePhase::None,       UpgradePhase::Begun,      UpgradePhase::NewFiles,  UpgradePhase::Journals,
                                        UpgradePhase::NewService, UpgradePhase::Registered, UpgradePhase::RollingBack, UpgradePhase::RolledBack};
    for (const auto ph : not_healthy) {
        World w;
        w.host_exited();
        UpgradeRecord r;
        r.phase = ph;
        r.attempt = 1;
        w.mem.put(w.p.record_file(), encode_record(r));
        w.mem.mutations.clear();
        const auto res = w.newsvc();
        EF_CHECK(res.outcome == UpgradeOutcome::WaitingForHost || res.outcome == UpgradeOutcome::NotApplicable);
        EF_CHECK(w.mem.mutations.empty());
        EF_CHECK(w.sys.log.empty());
        w.expect_old_install_intact("not healthy");
    }
    for (const char* bad : {"", "{", "{\"schema_version\":1,\"phase\":\"healthy\"}", "{\"schema_version\":2,\"phase\":\"healthy\"}"}) {
        World w;
        w.host_exited();
        w.mem.put(w.p.record_file(), bad);
        w.mem.mutations.clear();
        EF_CHECK(w.newsvc().outcome == UpgradeOutcome::NotApplicable);
        EF_CHECK(w.mem.mutations.empty() && w.sys.log.empty());
    }
    {  // kayit hic yok (normal yeni kurulum): hicbir sey
        World w;
        EF_CHECK(w.newsvc().outcome == UpgradeOutcome::NotApplicable);
        EF_CHECK(w.sys.log.empty());
        EF_CHECK(!legacy_cleanup_retry_ms(UpgradeOutcome::NotApplicable, 0));
    }
}

// Durum makinesinin UpgradeSystem'e verdigi HER hedef platform izin listesinden gecer (WinUpgradeSystem reddetmez);
// liste disi hedefler reddedilir.
EF_TEST(legacy_upgrade_all_system_targets_on_allow_list) {
    {
        World w;
        EF_CHECK(w.host().outcome == UpgradeOutcome::HandedOff);
        w.host_exited();
        w.fs.locked.insert(w.p.old_exe);  // yeniden baslatmada silme yolu da
        EF_CHECK(w.newsvc().outcome == UpgradeOutcome::Completed);
        EF_CHECK(w.sys.disallowed == 0);
    }
    {  // geri alma yolu (olusturulan her sey silinir)
        World w;
        w.sys.unhealthy = true;
        EF_CHECK(w.host().outcome == UpgradeOutcome::RolledBack);
        EF_CHECK(w.sys.disallowed == 0);
    }
    const auto p = make_upgrade_paths(roots(), legacy_layout(roots()).exe);
    const auto n = default_upgrade_names();
    EF_CHECK(!upgrade_name_allowed(SysOp::CreateService, n.old_service, n));  // eski adla hizmet OLUSTURULMAZ
    EF_CHECK(!upgrade_name_allowed(SysOp::StartService, n.old_service, n));
    EF_CHECK(!upgrade_name_allowed(SysOp::DeleteService, L"WinDefend", n));
    EF_CHECK(!upgrade_name_allowed(SysOp::WriteArp, n.old_arp_key, n));
    EF_CHECK(!upgrade_name_allowed(SysOp::DeleteArp, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Other", n));
    EF_CHECK(!upgrade_name_allowed(SysOp::DeleteService, L"", n));
    EF_CHECK(!upgrade_path_allowed(SysOp::WriteShortcut, p.old_start_lnk, p));
    EF_CHECK(!upgrade_path_allowed(SysOp::DeleteShortcut, "C:\\Users\\Public\\Desktop\\Other.lnk", p));
    EF_CHECK(!upgrade_path_allowed(SysOp::RebootDelete, "C:\\Windows\\System32\\kernel32.dll", p));
    EF_CHECK(!upgrade_path_allowed(SysOp::RebootDelete, p.old_dir + "X\\a.exe", p));  // ayni onekli kardes klasor
    EF_CHECK(!upgrade_path_allowed(SysOp::RebootDelete, p.old_dir + "\\..\\Other\\a.exe", p));
    EF_CHECK(!upgrade_path_allowed(SysOp::RebootDelete, p.new_exe, p));
    EF_CHECK(upgrade_path_allowed(SysOp::RebootDelete, p.old_dir, p));
    EF_CHECK(!upgrade_path_allowed(SysOp::InstallAcl, p.old_dir, p));
    EF_CHECK(!upgrade_path_allowed(SysOp::MachineAcl, p.old_machine_root, p));  // eski veri klasorune ASLA yazilmaz
    EF_CHECK(!path_under("C:\\", "C:\\"));
}

// ---- 6. Makine journal'i: Etkin islem yeni surumde Kapali ile birebir geri alinir ----
EF_TEST(machine_journal_migration_active_tx_restorable_with_identical_bytes) {
    MemoryMigrationFs fs;
    MachineFixture mx;
    const auto p = make_upgrade_paths(roots(), legacy_layout(roots()).exe);
    const MachineMigrationPaths mp{p.old_machine_root, p.new_machine_root};
    for (const auto& [name, bytes] : mx.jfs.files) fs.put(join_path(mp.old_root, "journal/" + name), bytes);
    struct Acl final : MachineAcl {
        std::vector<std::string> calls;
        FsStatus apply_machine_acl(const std::string& d) override { return calls.push_back("apply " + d), FsStatus{}; }
        FsStatus verify_machine_acl(const std::string& d) override { return calls.push_back("verify " + d), FsStatus{}; }
    } acl;
    MigrationEnv env;
    env.machine_id = kMachine;
    env.app_version = "0.9.6";
    env.sleep = [](int) {};
    // Gocten once: kullanici sureci eski bekleyen kaydi gorur (durust RecoveryNeeded).
    EF_CHECK(machine_legacy_after_upgrade(fs, mp, kMachine).unresolved == 1);
    const auto r = migrate_machine_journals(fs, acl, mp, env);
    EF_CHECK(r.outcome == MigrationOutcome::Migrated && r.acl_verified && r.legacy.unresolved == 1);
    EF_CHECK(acl.calls.size() >= 2 && acl.calls[0] == "apply " + mp.new_root);
    EF_CHECK(machine_legacy_after_upgrade(fs, mp, kMachine).unresolved == 0);  // aktarildi: bildirim temizlenir
    // Yeni broker'in okudugu klasor (<yeni>\journal) eski baytlari tasir; isaret journal klasorunun disinda.
    MemoryJournalFs njfs;
    const std::string jd = join_path(mp.new_root, "journal") + "\\";
    for (const auto& [path, f] : fs.files)
        if (path.rfind(jd, 0) == 0 && path.find('\\', jd.size()) == std::string::npos) njfs.files.emplace_back(path.substr(jd.size()), f.bytes);
    EF_CHECK(njfs.files.size() == mx.jfs.files.size());
    for (const auto& [name, bytes] : mx.jfs.files) EF_CHECK(fs.files.at(join_path(mp.new_root, "journal/" + name)).bytes == bytes);
    JournalStore nstore(njfs, kMachine);
    const auto l = nstore.scan();
    EF_CHECK(l.corrupt.empty() && l.transactions.size() == 1);
    if (l.transactions.size() != 1) return;
    EF_CHECK(l.transactions[0].state == TxState::Active && l.transactions[0].id == mx.active_id);
    EF_CHECK(mx.fake.values["hags"] == "new-hags" && mx.fake.values["drs"] == "new-drs");
    AdapterRegistry reg;
    reg.add(OperationId::GameModeSet, &mx.fake);
    TransactionEngine eng(nstore, reg);
    const auto back = eng.rollback(l.transactions[0]);
    EF_CHECK(back.ok() && back.value().state == TxState::Restored);
    EF_CHECK(mx.fake.values["hags"] == "2" && mx.fake.values["power"] == "balanced" && mx.fake.values.count("drs") == 0);
    // Yeniden calistirma: zaten aktarildi (ACL yine geri okunur).
    acl.calls.clear();
    EF_CHECK(migrate_machine_journals(fs, acl, mp, env).outcome == MigrationOutcome::AlreadyMigrated);
    EF_CHECK(!acl.calls.empty() && acl.calls[0].rfind("verify ", 0) == 0);
}

EF_TEST(machine_journal_migration_acl_failure_conflict_and_undo) {
    const auto p = make_upgrade_paths(roots(), legacy_layout(roots()).exe);
    const MachineMigrationPaths mp{p.old_machine_root, p.new_machine_root};
    MigrationEnv env;
    env.machine_id = kMachine;
    env.sleep = [](int) {};
    struct Acl final : MachineAcl {
        bool bad = false;
        FsStatus apply_machine_acl(const std::string&) override { return {}; }
        FsStatus verify_machine_acl(const std::string&) override { return bad ? FsStatus::fail(FsCode::Denied, "x") : FsStatus{}; }
    } acl;
    {  // ACL geri okumasi basarisiz -> Failed (veri isaretli kopya olarak duruyor; geri alma isaretle siler)
        MemoryMigrationFs fs;
        fs.put(join_path(mp.old_root, "journal/tx-a.json"), "A");
        acl.bad = true;
        const auto r = migrate_machine_journals(fs, acl, mp, env);
        EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "acl" && !r.acl_verified);
        std::size_t kept = 9;
        EF_CHECK(undo_machine_migration(fs, mp, &kept).ok() && kept == 0);
        EF_CHECK(fs.files.count(join_path(mp.new_root, "journal/tx-a.json")) == 0);
        EF_CHECK(fs.files.at(join_path(mp.old_root, "journal/tx-a.json")).bytes == "A");
        acl.bad = false;
    }
    {  // Farkli icerikli ayni adli journal kaydi: cakisma, uzerine yazilmaz
        MemoryMigrationFs fs;
        fs.put(join_path(mp.old_root, "journal/tx-a.json"), "A");
        fs.put(join_path(mp.new_root, "journal/tx-a.json"), "B");
        const auto r = migrate_machine_journals(fs, acl, mp, env);
        EF_CHECK(r.outcome == MigrationOutcome::Failed && r.stage == "commit");
        EF_CHECK(fs.files.at(join_path(mp.new_root, "journal/tx-a.json")).bytes == "B");
    }
    {  // Undo yeni surumun degistirdigi kaydi korur
        MemoryMigrationFs fs;
        fs.put(join_path(mp.old_root, "journal/tx-a.json"), "A");
        fs.put(join_path(mp.old_root, "journal/drivers/tx-b.json"), "Bd");
        fs.put(join_path(mp.old_root, "journal/tx-c.json.tmp"), "T");
        EF_CHECK(migrate_machine_journals(fs, acl, mp, env).outcome == MigrationOutcome::Migrated);
        EF_CHECK(fs.files.count(join_path(mp.new_root, "journal/drivers/tx-b.json")) == 1);
        EF_CHECK(fs.files.count(join_path(mp.new_root, "journal/tx-c.json.tmp")) == 0);
        fs.files[join_path(mp.new_root, "journal/tx-a.json")].bytes = "A-restored";
        std::size_t kept = 0;
        EF_CHECK(undo_machine_migration(fs, mp, &kept).ok() && kept == 1);
        EF_CHECK(fs.files.at(join_path(mp.new_root, "journal/tx-a.json")).bytes == "A-restored");
        EF_CHECK(fs.files.count(join_path(mp.new_root, "journal/drivers/tx-b.json")) == 0);
    }
}
