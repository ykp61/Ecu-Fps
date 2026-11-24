// P9 / B4: 0.9.0 yazma adaptorleri icin ORTAK geri alma sozlesmesi + journal adimi basina cokme enjeksiyonu.
// MOCK: her adaptor SAHTE arka uc (bellek ici kayit defteri / SCM / Defender / Appx / surec) ile calisir. Gercek kayit defteri,
// hizmet, Defender, paket veya surec DEGISMEZ. Bu testler gercek cihaz kaniti DEGILDIR (docs/TEST_SAFETY.md).
//
// Sozlesme (her adaptor):
//  C1 capture/read_back salt okumadir (arka uc parmak izi degismez).
//  C2 journal once: apply cagrildiginda adimin Intent kaydi diskte; restore cagrildiginda islem RollingBack olarak diskte.
//  C3 journal yazilamazsa arka uca HIC yazilmaz.
//  C4 uygula -> geri oku = yeni deger, islem Active; geri al -> Restored ve arka uc birebir onceki durum.
//  C5 dis degisiklik: geri almada hedef ne anlik goruntude ne bizim degerimizde ise EZILMEZ; islem RecoveryRequired
//     (RecoveryNeeded paneli), hedef dis degerde kalir. Dis degisiklik hedefi zaten onceki degere dondurduyse yazma yok.
//  K1 cokme: journal'in her yaziminda (yazim diske ulasmadan VE ulastiktan sonra) surec olur; sonraki acilis uzlastirmasi
//     ya islemi Restored yapar (arka uc onceki durumda) ya Active birakir (arka uc uygulanmis; Kapali geri alir).
//     Ara durum veya sessiz kayip yok. Adaptor yazdiktan hemen sonra olum ve geri alma sirasinda olum da denenir.
//  K2 cokme + dis degisiklik: uzlastirma RecoveryRequired raporlar ve RecoveryNeeded kosulu (journal_backend ile ayni
//     kural: RecoveryRequired islem veya bozuk kayit) dogru olur.
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/adapters/startup/tasks_services.h"
#include "core/adapters/windows/appx_remove.h"
#include "core/adapters/windows/background.h"
#include "core/adapters/windows/background_process.h"
#include "core/adapters/windows/defender_exclusion.h"
#include "core/adapters/windows/gaming_toggles.h"
#include "core/adapters/windows/gpu_preference.h"
#include "core/adapters/windows/ifeo_priority.h"
#include "core/adapters/windows/service_start_type.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/adapters/windows/tweak_catalog.h"
#include "core/recovery/reconcile.h"
#include "ef_test.h"

using namespace ef::core::recovery;
using namespace ef::contracts;
namespace cw = ef::core::adapters::windows;
namespace cs = ef::core::adapters::startup;

namespace {

constexpr const char* kMock = "[MOCK]";  // kanit satirlarinda: sahte arka uc, gercek cihaz degil
const std::string kTxId = "0123456789abcdef0123456789abcdef";
const std::string kMachine = "m";

// ---------------------------------------------------------------- sahte arka uclarin parmak izleri

std::string hex(const std::vector<std::uint8_t>& b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (auto c : b) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 15]);
    }
    return s;
}
std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}
std::string fp(const std::map<std::wstring, cw::RegRaw>& v) {
    std::string s;
    for (const auto& [k, r] : v) s += narrow(k) + "=" + std::to_string(r.type) + ":" + hex(r.bytes) + ";";
    return s;
}

// ---------------------------------------------------------------- arka plan sureci sahtesi (adaptor testindekinin esi)

class FakeBgApi final : public cw::BackgroundProcessApi {
public:
    Status verify(const cw::ProcessId& id) override {
        if (!alive || id.pid != pid || id.creation != creation) return Status::fail(ErrorCode::MissingValue, "exited");
        return ok_status();
    }
    Result<std::string> image_path(const cw::ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<std::string>(v.error());
        return path;
    }
    Result<cw::PriorityClass> get_priority(const cw::ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<cw::PriorityClass>(v.error());
        return prio;
    }
    Status set_priority(const cw::ProcessId& id, cw::PriorityClass p) override {
        if (auto v = verify(id); !v.ok()) return v;
        prio = p;
        return ok_status();
    }
    Result<cw::ThrottleState> get_throttling(const cw::ProcessId& id) override {
        if (auto v = verify(id); !v.ok()) return Result<cw::ThrottleState>(v.error());
        return throttle;
    }
    Status set_throttling(const cw::ProcessId& id, cw::ThrottleState s) override {
        if (auto v = verify(id); !v.ok()) return v;
        throttle = s;
        return ok_status();
    }
    bool alive = true;
    std::uint32_t pid = 4242;
    std::uint64_t creation = 133000000000000000ull;
    std::string path = "C:\\Users\\x\\AppData\\Local\\Discord\\Discord.exe";
    cw::PriorityClass prio = cw::PriorityClass::Normal;
    cw::ThrottleState throttle{};
};

// ---------------------------------------------------------------- sozlesme duzenegi

enum class External { Conflict, AlreadyRestored };

// Her adaptor icin: sahte arka uc + adaptor + gecerli islem + arka uc parmak izi + dis degisiklik.
struct Rig {
    virtual ~Rig() = default;
    virtual std::string name() const = 0;
    virtual SettingAdapter& adapter() = 0;
    virtual SettingOperation op() const = 0;
    virtual std::string state() const = 0;  // tum sahte arka uc durumu (sayaclar haric)
    virtual External external_change() = 0;  // hedefi "kullanici" degistirir
};

struct TweakUserRig final : Rig {
    cw::FakeUserRegistry u;
    cw::FakeMachineRegistry m;
    cw::TweakAdapter a{u, &m};
    const cw::TweakEntry* e = cw::find_tweak("privacy.ad_id");
    TweakUserRig() { u.values[cw::FakeUserRegistry::key(e->subkey, e->name)] = cw::dword_raw(1); }
    std::string name() const override { return "TweakAdapter(HKCU privacy.ad_id)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::TweakValueSet, OperationScope::CurrentUser, cw::tweak_target(*e), std::string(e->on_value), false, false};
    }
    std::string state() const override { return fp(u.values) + "|" + fp(m.values); }
    External external_change() override {
        u.values[cw::FakeUserRegistry::key(e->subkey, e->name)] = cw::dword_raw(7);
        return External::Conflict;
    }
};

struct TweakMachineRig final : Rig {
    cw::FakeUserRegistry u;
    cw::FakeMachineRegistry m;
    cw::TweakAdapter a{u, &m};
    const cw::TweakEntry* e = cw::find_tweak("update.no_reboot");
    std::string name() const override { return "TweakAdapter(HKLM update.no_reboot)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::TweakValueSet, OperationScope::Machine, cw::tweak_target(*e), std::string(e->on_value), false, false};
    }
    std::string state() const override {
        std::string k;
        for (const auto& s : m.keys) k += narrow(s) + ",";
        return fp(u.values) + "|" + fp(m.values) + "|" + k;
    }
    External external_change() override {
        m.values[cw::FakeMachineRegistry::key(e->subkey, e->name)] = cw::dword_raw(0);
        return External::Conflict;
    }
};

struct GpuPrefRig final : Rig {
    cw::FakeUserRegistry u;
    cw::GpuPreferenceAdapter a{u};
    std::string exe = "C:\\Games\\Test Game\\game.exe";
    GpuPrefRig() {
        a.set_allowed_roots({"C:\\Games"});
        u.values[cw::FakeUserRegistry::key(L"Other\\Key", L"x")] = cw::dword_raw(3);  // ilgisiz deger korunmali
    }
    std::string name() const override { return "GpuPreferenceAdapter(HKCU UserGpuPreferences)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::GpuPreferenceSet, OperationScope::CurrentUser, cw::gpu_pref_target_for_exe(exe), "2", false, false};
    }
    std::string state() const override { return fp(u.values); }
    External external_change() override {
        u.values[cw::FakeUserRegistry::key(cw::kGpuPrefSubkey, L"c:\\games\\test game\\game.exe")] = cw::sz_raw(L"GpuPreference=1;");
        return External::Conflict;
    }
};

struct IfeoRig final : Rig {
    cw::FakeMachineRegistry m;
    cw::IfeoPriorityAdapter a{&m};
    std::string name() const override { return "IfeoPriorityAdapter(HKLM IFEO PerfOptions)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::GamePriorityHint, OperationScope::Machine, cw::ifeo_priority_target("cs2.exe"), "above_normal", false, false};
    }
    std::string state() const override {
        std::string k;
        for (const auto& s : m.keys) k += narrow(s) + ",";
        return fp(m.values) + "|" + k;
    }
    External external_change() override {
        m.values[cw::FakeMachineRegistry::key(cw::ifeo_perf_subkey("cs2.exe"), cw::kIfeoValueName)] = cw::dword_raw(3);
        return External::Conflict;
    }
};

struct DefenderRig final : Rig {
    cw::FakeDefenderApi api;
    cw::DefenderExclusionAdapter a{api};
    std::string path = "D:\\SteamLibrary\\steamapps\\common\\Portal";
    DefenderRig() { api.paths = {"C:\\Tools"}; }
    std::string name() const override { return "DefenderExclusionAdapter(Defender exclusion)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::DefenderExclusionSet, OperationScope::Machine, cw::defender_target_for_path(path), "1", false, false};
    }
    std::string state() const override {
        std::set<std::string> s(api.paths.begin(), api.paths.end());  // sira anlamsiz
        std::string o;
        for (const auto& p : s) o += cw::defender_canonical_path(p) + ";";
        return o;
    }
    External external_change() override {  // kullanici dislamayi elle kaldirdi: zaten onceki durum
        std::vector<std::string> keep;
        for (const auto& p : api.paths)
            if (cw::defender_canonical_path(p) != cw::defender_canonical_path(path)) keep.push_back(p);
        api.paths = keep;
        return External::AlreadyRestored;
    }
};

struct AppxRig final : Rig {
    cw::FakeAppxApi api;
    cw::AppxRemoveAdapter a{api};
    const std::string fam = "Microsoft.BingNews_8wekyb3d8bbwe";
    const std::string full = "Microsoft.BingNews_4.55.1_x64__8wekyb3d8bbwe";
    AppxRig() {
        api.installed.emplace_back(fam, full);
        api.provisioned.push_back(fam);
    }
    std::string name() const override { return "AppxRemoveAdapter(Store package per user)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::AppxPackageRemove, OperationScope::CurrentUser, cw::appx_target_for_family(fam), "removed", false, false};
    }
    // Geri alma "aile bu kullanici icin kurulu" durumuna doner; yeniden kayit PROVISIONED surumu getirir, tam ad (surum)
    // onceki kurulu adla ayni olmayabilir. Bu bilinen sinir kanit belgesinde yazilidir; parmak izi aile kumesidir.
    std::string state() const override {
        std::set<std::string> fams;
        for (const auto& [f, n] : api.installed) fams.insert(f);
        std::string o;
        for (const auto& f : fams) o += f + ";";
        o += "|";
        for (const auto& p : api.provisioned) o += p + ";";
        return o;
    }
    External external_change() override {  // kullanici Magaza'dan yeniden kurdu: zaten onceki durum
        api.installed.emplace_back(fam, full);
        return External::AlreadyRestored;
    }
};

cs::ServiceInfo ms_svc(const std::string& name, const std::string& path) {
    cs::ServiceInfo s;
    s.name = name;
    s.display_name = name;
    s.image_path = path;
    s.microsoft = true;
    s.start = cs::StartType::Automatic;
    s.running = true;
    return s;
}

struct ServiceStartRig final : Rig {
    cs::FakeServiceApi scm;
    cw::FakeServiceBusyProbe probe;
    cw::ServiceStartTypeAdapter a{scm, probe};
    ServiceStartRig() { scm.services["bthserv"] = ms_svc("bthserv", "c:\\windows\\system32\\svchost.exe -k bthsvcs"); }
    std::string name() const override { return "ServiceStartTypeAdapter(SCM start type)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::ServiceStartTypeSet, OperationScope::Machine, cw::format_service_start_target("bthserv"), "disabled", false, false};
    }
    std::string state() const override {
        std::string o;
        for (const auto& [n, s] : scm.services) o += n + ":" + std::string(cs::to_string(s.start)) + ":" + (s.running ? "run" : "stop") + ";";
        return o;
    }
    External external_change() override {
        scm.services["bthserv"].start = cs::StartType::Manual;
        return External::Conflict;
    }
};

struct BgProcessRig final : Rig {
    FakeBgApi api;
    cw::BackgroundProcessAdapter a{api};
    std::string name() const override { return "BackgroundProcessAdapter(priority + EcoQoS)"; }
    SettingAdapter& adapter() override { return a; }
    SettingOperation op() const override {
        return {OperationId::BackgroundProcessLower, OperationScope::Process,
                cw::format_bg_process_target({{api.pid, api.creation}, cw::bg_path_hash(api.path)}), "low", false, false};
    }
    std::string state() const override {
        return std::string(cw::to_string(api.prio)) + ":" + std::to_string(api.throttle.control) + ":" + std::to_string(api.throttle.state);
    }
    External external_change() override {
        api.prio = cw::PriorityClass::High;
        api.throttle = {};
        return External::Conflict;
    }
};

using Factory = std::function<std::unique_ptr<Rig>()>;
std::vector<Factory> all_rigs() {
    return {
        [] { return std::unique_ptr<Rig>(new TweakUserRig); },    [] { return std::unique_ptr<Rig>(new TweakMachineRig); },
        [] { return std::unique_ptr<Rig>(new GpuPrefRig); },      [] { return std::unique_ptr<Rig>(new IfeoRig); },
        [] { return std::unique_ptr<Rig>(new DefenderRig); },     [] { return std::unique_ptr<Rig>(new AppxRig); },
        [] { return std::unique_ptr<Rig>(new ServiceStartRig); }, [] { return std::unique_ptr<Rig>(new BgProcessRig); },
    };
}

struct ProcessCrash : std::runtime_error {
    ProcessCrash() : std::runtime_error("simulated process death") {}
};

// Journal FS: N. yazimda surec olur (yazim diske ulasmadan ya da ulastiktan sonra).
class CrashingFs final : public JournalFs {
public:
    explicit CrashingFs(MemoryJournalFs& inner) : in(inner) {}
    Status write_atomic(const std::string& n, const std::string& b) override {
        const int k = count++;
        if (k == crash_at && !after_write) throw ProcessCrash();
        auto st = in.write_atomic(n, b);
        if (k == crash_at && after_write) throw ProcessCrash();
        return st;
    }
    Result<std::string> read(const std::string& n) override { return in.read(n); }
    Result<std::vector<std::string>> list() override { return in.list(); }
    Status remove(const std::string& n) override { return in.remove(n); }
    MemoryJournalFs& in;
    int crash_at = -1;
    bool after_write = false;
    int count = 0;
};

// Gercek adaptoru saran gozlemci: sozlesme ihlallerini kaydeder; istenirse yazdiktan hemen sonra "olur".
class Observer final : public SettingAdapter {
public:
    Observer(Rig& r, JournalFs& fs) : rig(r), fs_(fs) {}
    Result<Snapshot> capture(const SettingOperation& op) override {
        const auto before = rig.state();
        auto s = rig.adapter().capture(op);
        if (rig.state() != before) violations.push_back("C1 capture wrote the backend");
        return s;
    }
    Status apply(const SettingOperation& op) override {
        ++applies;
        if (!persisted_step_is(StepState::Intent)) violations.push_back("C2 apply before durable Intent record");
        auto st = rig.adapter().apply(op);
        if (crash_after_apply) throw ProcessCrash();
        return st;
    }
    Result<std::optional<std::string>> read_back(const SettingOperation& op) override {
        const auto before = rig.state();
        auto v = rig.adapter().read_back(op);
        if (rig.state() != before) violations.push_back("C1 read_back wrote the backend");
        return v;
    }
    Status restore(const SettingOperation& op, const Snapshot& snap) override {
        ++restores;
        if (!persisted_tx_is(TxState::RollingBack)) violations.push_back("C2 restore before durable RollingBack record");
        auto st = rig.adapter().restore(op, snap);
        if (crash_after_restore) throw ProcessCrash();
        return st;
    }
    bool target_gone(const SettingOperation& op) override { return rig.adapter().target_gone(op); }
    Result<std::optional<std::string>> read_for_restore(const SettingOperation& op, const Snapshot& s) override {
        const auto before = rig.state();
        auto v = rig.adapter().read_for_restore(op, s);
        if (rig.state() != before) violations.push_back("C1 read_for_restore wrote the backend");
        return v;
    }
    bool matches_snapshot(const SettingOperation& op, const Snapshot& s, const std::optional<std::string>& v) override {
        return rig.adapter().matches_snapshot(op, s, v);
    }

    Rig& rig;
    std::vector<std::string> violations;
    int applies = 0, restores = 0;
    bool crash_after_apply = false, crash_after_restore = false;

private:
    std::optional<JournalTx> persisted() {
        auto bytes = fs_.read(JournalStore::file_name(kTxId));
        if (!bytes.ok()) return std::nullopt;
        auto tx = deserialize(bytes.value());
        if (!tx.ok()) return std::nullopt;
        return tx.value();
    }
    bool persisted_step_is(StepState s) {
        auto tx = persisted();
        return tx && tx->steps.size() == 1 && tx->steps[0].state == s;
    }
    bool persisted_tx_is(TxState s) {
        auto tx = persisted();
        return tx && tx->state == s;
    }
    JournalFs& fs_;
};

JournalTx make_tx(const Rig& r) {
    JournalTx t;
    t.id = kTxId;
    JournalStep s;
    s.op = r.op();
    t.steps.push_back(s);
    return t;
}

// journal_backend.cpp JournalBackend::recovery_needed ile ayni kural (RecoveryNeeded asamasi).
bool recovery_needed(JournalStore& store) {
    const auto l = store.scan();
    if (!l.corrupt.empty()) return true;
    for (const auto& tx : l.transactions)
        if (tx.state == TxState::RecoveryRequired) return true;
    return false;
}

void report(const Rig& r, const char* what, bool ok) {
    if (!ok) std::fprintf(stderr, "  %s contract %s: %s\n", kMock, r.name().c_str(), what);
}
#define CONTRACT(rig, what, expr)       \
    do {                                \
        const bool ok_ = (expr);        \
        report(rig, what, ok_);         \
        EF_CHECK(ok_ && what);         \
    } while (0)

// Sayilan journal yazim sayisi (cokme noktalari bunun uzerinden taranir).
int count_apply_writes(const Factory& f) {
    auto r = f();
    MemoryJournalFs mem;
    JournalStore store(mem, kMachine);
    AdapterRegistry reg;
    reg.add(r->op().id, &r->adapter());
    TransactionEngine eng(store, reg);
    (void)eng.apply(make_tx(*r));
    return mem.writes;
}

}  // namespace

// C1-C4: uygula / geri oku / geri al, journal once.
EF_TEST(contract_090_adapters_apply_readback_restore_journal_first) {
    for (const auto& f : all_rigs()) {
        auto r = f();
        MemoryJournalFs mem;
        JournalStore store(mem, kMachine);
        Observer obs(*r, mem);
        AdapterRegistry reg;
        reg.add(r->op().id, &obs);
        TransactionEngine eng(store, reg);
        const auto before = r->state();
        auto a = eng.apply(make_tx(*r));
        CONTRACT(*r, "C4 apply -> Active", a.ok() && a.value().state == TxState::Active);
        if (!a.ok()) continue;
        CONTRACT(*r, "C4 apply changed the backend", r->state() != before);
        auto rb = r->adapter().read_back(r->op());
        CONTRACT(*r, "C4 read_back == new value", rb.ok() && rb.value() && *rb.value() == r->op().new_value);
        CONTRACT(*r, "C4 snapshot stored in journal", store.load(kTxId).ok() && store.load(kTxId).value().steps[0].state == StepState::Verified);
        auto back = eng.rollback(a.value());
        CONTRACT(*r, "C4 rollback -> Restored", back.ok() && back.value().state == TxState::Restored);
        CONTRACT(*r, "C4 backend byte-identical to before", r->state() == before);
        CONTRACT(*r, "C4 exactly one apply and one restore", obs.applies == 1 && obs.restores == 1);
        CONTRACT(*r, "C1/C2 no violations", obs.violations.empty());
        for (const auto& v : obs.violations) std::fprintf(stderr, "  %s %s: %s\n", kMock, r->name().c_str(), v.c_str());
        // Idempotent: Restored islemde ikinci geri alma yazmaz.
        auto again = eng.rollback(back.value());
        CONTRACT(*r, "C4 second rollback is a no-op", again.ok() && obs.restores == 1 && r->state() == before);
    }
}

// C3: journal yazilamazsa hicbir sey uygulanmaz (begin ve Intent kaydi ayri ayri).
EF_TEST(contract_090_adapters_no_write_without_journal) {
    for (const auto& f : all_rigs()) {
        for (int fail_at : {0, 1, 2}) {  // begin, Applying gecisi, Intent kaydi
            auto r = f();
            MemoryJournalFs mem;
            mem.fail_write_after = fail_at;
            JournalStore store(mem, kMachine);
            Observer obs(*r, mem);
            AdapterRegistry reg;
            reg.add(r->op().id, &obs);
            TransactionEngine eng(store, reg);
            const auto before = r->state();
            auto a = eng.apply(make_tx(*r));
            CONTRACT(*r, "C3 apply fails when journal unwritable", !a.ok() || a.value().state != TxState::Active);
            CONTRACT(*r, "C3 backend untouched when journal unwritable", obs.applies == 0 && r->state() == before);
        }
    }
}

// C5: dis degisiklik ezilmez.
EF_TEST(contract_090_adapters_external_change_is_not_overwritten) {
    for (const auto& f : all_rigs()) {
        auto r = f();
        MemoryJournalFs mem;
        JournalStore store(mem, kMachine);
        Observer obs(*r, mem);
        AdapterRegistry reg;
        reg.add(r->op().id, &obs);
        TransactionEngine eng(store, reg);
        const auto before = r->state();
        auto a = eng.apply(make_tx(*r));
        CONTRACT(*r, "C5 apply -> Active", a.ok() && a.value().state == TxState::Active);
        if (!a.ok()) continue;
        const External kind = r->external_change();
        const auto external = r->state();
        auto back = eng.rollback(a.value());
        CONTRACT(*r, "C5 rollback returns a record", back.ok());
        if (!back.ok()) continue;
        if (kind == External::Conflict) {
            CONTRACT(*r, "C5 conflict -> RecoveryRequired", back.value().state == TxState::RecoveryRequired);
            CONTRACT(*r, "C5 conflict step marked external", back.value().steps[0].state == StepState::Failed &&
                                                                 back.value().steps[0].error == kExternalChange);
            CONTRACT(*r, "C5 external value kept (no restore write)", obs.restores == 0 && r->state() == external);
            CONTRACT(*r, "C5 RecoveryNeeded raised", recovery_needed(store));
        } else {
            CONTRACT(*r, "C5 already-restored -> Restored without write",
                     back.value().state == TxState::Restored && obs.restores == 0 && r->state() == before);
            CONTRACT(*r, "C5 no RecoveryNeeded", !recovery_needed(store));
        }
        CONTRACT(*r, "C5 no violations", obs.violations.empty());
    }
}

// K1: her journal yaziminda cokme (yazim oncesi/sonrasi) -> uzlastirma Restored ya da Active; ara durum yok.
EF_TEST(crash_090_adapters_every_journal_write_reconciles) {
    for (const auto& f : all_rigs()) {
        const int writes = count_apply_writes(f);
        EF_CHECK(writes >= 6);  // begin, Applying, Intent, Verified, Applied, Verifying, Active
        for (int k = 0; k < writes; ++k) {
            for (bool after : {false, true}) {
                auto r = f();
                MemoryJournalFs mem;
                const auto before = r->state();
                std::string applied;
                {
                    CrashingFs cfs(mem);
                    cfs.crash_at = k;
                    cfs.after_write = after;
                    JournalStore store(cfs, kMachine);
                    Observer obs(*r, cfs);
                    AdapterRegistry reg;
                    reg.add(r->op().id, &obs);
                    TransactionEngine eng(store, reg);
                    bool crashed = false;
                    try {
                        (void)eng.apply(make_tx(*r));
                    } catch (const ProcessCrash&) {
                        crashed = true;
                    }
                    CONTRACT(*r, "K1 crash injected", crashed);
                    CONTRACT(*r, "K1 no violations before crash", obs.violations.empty());
                }
                // Sonraki acilis: yeni surec, ayni disk ve ayni (sahte) sistem durumu.
                JournalStore store2(mem, kMachine);
                Observer obs2(*r, mem);
                AdapterRegistry reg2;
                reg2.add(r->op().id, &obs2);
                TransactionEngine eng2(store2, reg2);
                const auto rep = reconcile(store2, eng2);
                CONTRACT(*r, "K1 reconcile clean", rep.clean() && rep.recovery_required.empty());
                CONTRACT(*r, "K1 no RecoveryNeeded without external change", !recovery_needed(store2));
                if (!rep.active.empty()) {
                    // Active kaldi: hedef uygulanmis olmali ve Kapali (rollback) birebir geri getirmeli.
                    auto rb = r->adapter().read_back(r->op());
                    CONTRACT(*r, "K1 Active means applied", rb.ok() && rb.value() && *rb.value() == r->op().new_value);
                    auto tx = store2.load(kTxId);
                    auto back = tx.ok() ? eng2.rollback(tx.value()) : Result<JournalTx>::fail(ErrorCode::IoError, "load");
                    CONTRACT(*r, "K1 Kapali after crash restores", back.ok() && back.value().state == TxState::Restored);
                }
                CONTRACT(*r, "K1 backend back to original after recovery", r->state() == before);
                CONTRACT(*r, "K1 no violations during recovery", obs2.violations.empty());
            }
        }
    }
}

// K1: adaptor yazdiktan hemen sonra (Applied kaydindan once) ve geri alma yazimindan hemen sonra olum.
EF_TEST(crash_090_adapters_inside_adapter_write_and_restore) {
    for (const auto& f : all_rigs()) {
        for (bool in_restore : {false, true}) {
            auto r = f();
            MemoryJournalFs mem;
            const auto before = r->state();
            {
                JournalStore store(mem, kMachine);
                Observer obs(*r, mem);
                AdapterRegistry reg;
                reg.add(r->op().id, &obs);
                TransactionEngine eng(store, reg);
                bool crashed = false;
                try {
                    if (!in_restore) {
                        obs.crash_after_apply = true;
                        (void)eng.apply(make_tx(*r));
                    } else {
                        auto a = eng.apply(make_tx(*r));
                        CONTRACT(*r, "K1 apply -> Active", a.ok() && a.value().state == TxState::Active);
                        obs.crash_after_restore = true;
                        if (a.ok()) (void)eng.rollback(a.value());
                    }
                } catch (const ProcessCrash&) {
                    crashed = true;
                }
                CONTRACT(*r, "K1 crash injected in adapter", crashed);
            }
            JournalStore store2(mem, kMachine);
            Observer obs2(*r, mem);
            AdapterRegistry reg2;
            reg2.add(r->op().id, &obs2);
            TransactionEngine eng2(store2, reg2);
            const auto rep = reconcile(store2, eng2);
            CONTRACT(*r, "K1 adapter-crash reconcile restores", rep.restored.size() == 1 && rep.clean() && rep.active.empty());
            CONTRACT(*r, "K1 adapter-crash backend original", r->state() == before && !recovery_needed(store2));
        }
    }
}

// K1: geri alma (Kapali) sirasinda her journal yaziminda cokme -> sonraki acilis ya geri alir ya Active birakir; sonunda temiz.
EF_TEST(crash_090_adapters_every_rollback_write_reconciles) {
    for (const auto& f : all_rigs()) {
        for (int k = 0; k < 4; ++k) {  // RollingBack, adim RolledBack, Restored (+1 fazla: cokme yok)
            for (bool after : {false, true}) {
                auto r = f();
                MemoryJournalFs mem;
                const auto before = r->state();
                JournalTx active;
                {
                    JournalStore s0(mem, kMachine);
                    AdapterRegistry reg0;
                    reg0.add(r->op().id, &r->adapter());
                    TransactionEngine e0(s0, reg0);
                    auto a = e0.apply(make_tx(*r));
                    CONTRACT(*r, "K1 rollback-crash setup Active", a.ok() && a.value().state == TxState::Active);
                    if (!a.ok()) continue;
                    active = a.value();
                }
                {
                    CrashingFs cfs(mem);
                    cfs.crash_at = k;
                    cfs.after_write = after;
                    JournalStore store(cfs, kMachine);
                    AdapterRegistry reg;
                    reg.add(r->op().id, &r->adapter());
                    TransactionEngine eng(store, reg);
                    try {
                        (void)eng.rollback(active);
                    } catch (const ProcessCrash&) {
                    }
                }
                JournalStore store2(mem, kMachine);
                Observer obs2(*r, mem);
                AdapterRegistry reg2;
                reg2.add(r->op().id, &obs2);
                TransactionEngine eng2(store2, reg2);
                const auto rep = reconcile(store2, eng2);
                CONTRACT(*r, "K1 rollback-crash reconcile clean", rep.clean());
                for (const auto& id : rep.active) {  // RollingBack kaydi yazilmadan oldu: Kapali yeniden denenir
                    auto tx = store2.load(id);
                    auto back = tx.ok() ? eng2.rollback(tx.value()) : Result<JournalTx>::fail(ErrorCode::IoError, "load");
                    CONTRACT(*r, "K1 retried Kapali restores", back.ok() && back.value().state == TxState::Restored);
                }
                CONTRACT(*r, "K1 rollback-crash backend original", r->state() == before && !recovery_needed(store2));
            }
        }
    }
}

// K2: cokme + dis degisiklik -> RecoveryRequired raporlanir ve RecoveryNeeded kosulu dogru (dis deger ezilmez).
EF_TEST(crash_090_adapters_external_change_after_crash_raises_recovery_needed) {
    for (const auto& f : all_rigs()) {
        auto r = f();
        MemoryJournalFs mem;
        const auto before = r->state();
        {
            CrashingFs cfs(mem);
            cfs.crash_at = 3;  // Verified kaydi diske ulasmadan (adim Intent, hedef uygulanmis)
            JournalStore store(cfs, kMachine);
            AdapterRegistry reg;
            reg.add(r->op().id, &r->adapter());
            TransactionEngine eng(store, reg);
            try {
                (void)eng.apply(make_tx(*r));
            } catch (const ProcessCrash&) {
            }
        }
        const External kind = r->external_change();
        const auto external = r->state();
        JournalStore store2(mem, kMachine);
        Observer obs2(*r, mem);
        AdapterRegistry reg2;
        reg2.add(r->op().id, &obs2);
        TransactionEngine eng2(store2, reg2);
        const auto rep = reconcile(store2, eng2);
        if (kind == External::Conflict) {
            CONTRACT(*r, "K2 reconcile reports recovery_required", rep.recovery_required.size() == 1 && !rep.clean());
            CONTRACT(*r, "K2 RecoveryNeeded raised", recovery_needed(store2));
            CONTRACT(*r, "K2 external value kept", obs2.restores == 0 && r->state() == external);
        } else {
            CONTRACT(*r, "K2 already restored -> clean", rep.clean() && rep.restored.size() == 1 && r->state() == before);
            CONTRACT(*r, "K2 no RecoveryNeeded", !recovery_needed(store2));
        }
    }
}

// Kanit satiri: kapsanan adaptorler (MOCK etiketi). Liste degisirse kanit belgesi de guncellenmeli.
EF_TEST(contract_090_adapters_inventory_is_mock_labelled) {
    std::vector<std::string> names;
    for (const auto& f : all_rigs()) names.push_back(f()->name());
    EF_CHECK(names.size() == 8);
    for (const auto& n : names) std::printf("  %s covered: %s\n", kMock, n.c_str());
}
