#include "core/adapters/nvidia/drs.h"

#include <cstdio>

#include "core/recovery/engine.h"
#include "ef_test.h"

using namespace ef::core::adapters::nvidia;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
constexpr std::uint32_t kPower = static_cast<std::uint32_t>(DrsSetting::PowerMode);
struct Rig {
    FakeDrs drs;
    DrsAdapter adapter{drs};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() {
        reg.add(OperationId::VendorProfileValue, &adapter);
        drs.defaults[kPower] = 0;  // optimal power
        drs.apps["cs2.exe"] = {};
    }
    JournalTx tx(const std::string& target, std::uint32_t value) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcdef";
        JournalStep s;
        s.op = SettingOperation{OperationId::VendorProfileValue, OperationScope::Device, target, DrsAdapter::encode(value), false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(drs_targets_are_allowlisted) {
    EF_CHECK(parse_drs_target("drs:global:1057EB71").ok());
    auto app = parse_drs_target("drs:app:cs2.exe:1057EB71");
    EF_CHECK(app.ok() && !app.value().global && app.value().app == "cs2.exe");
    EF_CHECK(parse_drs_target("drs:global:12345678").code() == ErrorCode::Denied);   // izin listesinde yok
    EF_CHECK(!parse_drs_target("drs:app:..\\x.exe:1057EB71").ok());
    EF_CHECK(!parse_drs_target("drs:app:cs2:1057EB71").ok());
    EF_CHECK(!parse_drs_target("reg:global:1057EB71").ok());
    EF_CHECK(format_drs_target(app.value()) == "drs:app:cs2.exe:1057EB71");
}

EF_TEST(drs_global_restore_fixture) {
    Rig r;
    r.drs.global[kPower] = 0;  // global profilde acik "optimal"
    auto active = r.engine.apply(r.tx("drs:global:1057EB71", 1));
    EF_CHECK(active.ok());
    EF_CHECK(r.drs.global[kPower] == 1);
    EF_CHECK(active.value().steps[0].snapshot.existed);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
    EF_CHECK(r.drs.global.count(kPower) == 1 && r.drs.global[kPower] == 0);
}

EF_TEST(drs_game_specific_inherited_restore_fixture) {
    Rig r;
    r.drs.global[kPower] = 0;  // oyun profili globalden miras aliyor
    auto active = r.engine.apply(r.tx("drs:app:cs2.exe:1057EB71", 1));
    EF_CHECK(active.ok());
    const auto& snap = active.value().steps[0].snapshot;
    EF_CHECK(!snap.existed && snap.inherited);
    EF_CHECK(snap.source_version.find("global=") == 0);
    EF_CHECK(r.drs.apps["cs2.exe"][kPower] == 1);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK(r.drs.apps["cs2.exe"].count(kPower) == 0);  // override silindi; global 0 yazilmadi
    // Miras etkisi: global degisirse oyun profili onu izler
    r.drs.global[kPower] = 2;
    EF_CHECK(r.drs.get(parse_drs_target("drs:app:cs2.exe:1057EB71").value()).value().value == 2);
}

EF_TEST(drs_missing_app_profile_is_not_created_silently) {
    Rig r;
    auto res = r.engine.apply(r.tx("drs:app:valorant.exe:1057EB71", 1));
    EF_CHECK(!res.ok());
    EF_CHECK(res.code() == ErrorCode::MissingValue);
    EF_CHECK(r.drs.apps.count("valorant.exe") == 0);
    EF_CHECK(r.fs.files.empty());  // capture basarisiz: journal bile acilmadi
}

EF_TEST(drs_external_change_is_kept) {
    Rig r;
    auto active = r.engine.apply(r.tx("drs:global:1057EB71", 1));
    r.drs.global[kPower] = 2;  // kullanici NVIDIA panelinden degistirdi
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::RecoveryRequired);
    EF_CHECK(r.drs.global[kPower] == 2);
}

// ---- B6 (ADR-040 m.8): G13 is parcacikli iyilestirme, G14 FXAA. Tum DRS SAHTE (MOCK FakeDrs); gercek GPU testi DEGIL. ----
namespace {
constexpr std::uint32_t kThreaded = 0x20C1221E;  // NvApiDriverSettings.h OGL_THREAD_CONTROL_ID
constexpr std::uint32_t kFxaa = 0x1074C972;      // NvApiDriverSettings.h FXAA_ENABLE_ID
// Geri okuma uyusmazligi ureten sahte (MOCK).
//  BadRead   : ilk yazmadan sonraki ILK okuma yanlis doner (gecici okuma hatasi; hedefte bizim degerimiz var).
//  WrongWrite: ilk yazma hedefe baska deger birakir (surucu degeri degistirdi; deger bizim degil).
class LyingDrs final : public DrsApi {
public:
    enum class Mode { BadRead, WrongWrite };
    LyingDrs(FakeDrs& inner, Mode m) : inner_(inner), mode_(m) {}
    ef::contracts::Result<DrsValue> get(const DrsTarget& t) override {
        auto v = inner_.get(t);
        if (bad_read_pending_ && v.ok()) {
            bad_read_pending_ = false;
            return DrsValue{v.value().value + 7, v.value().location};
        }
        return v;
    }
    Status set(const DrsTarget& t, std::uint32_t v) override {
        const bool first = !wrote_;
        wrote_ = true;
        if (first && mode_ == Mode::BadRead) bad_read_pending_ = true;
        return inner_.set(t, first && mode_ == Mode::WrongWrite ? v + 7 : v);
    }
    Status remove(const DrsTarget& t) override { return inner_.remove(t); }
    Status save() override { return inner_.save(); }

private:
    FakeDrs& inner_;
    Mode mode_;
    bool wrote_ = false;
    bool bad_read_pending_ = false;
};
}  // namespace

EF_TEST(drs_new_settings_parse) {
    // Kimlikler resmi NVAPI basligiyla ayni (enum degeri derlemede sabitlenir).
    static_assert(static_cast<std::uint32_t>(DrsSetting::ThreadedOptimization) == kThreaded, "OGL_THREAD_CONTROL_ID");
    static_assert(static_cast<std::uint32_t>(DrsSetting::FxaaEnable) == kFxaa, "FXAA_ENABLE_ID");
    EF_CHECK(catalog_id(DrsSetting::ThreadedOptimization) == "G13" && catalog_id(DrsSetting::FxaaEnable) == "G14");
    for (const char* t : {"drs:global:20C1221E", "drs:global:1074C972", "drs:global:20c1221e"}) {
        auto p = parse_drs_target(t);
        EF_CHECK(p.ok() && p.value().global);
    }
    EF_CHECK(parse_drs_target("drs:global:20C1221E").value().setting == kThreaded);
    // Tabloda olmayan komsu kimlikler (FXAA_ALLOW 0x1034CB89, FXAA_INDICATOR 0x1068FB9C) reddedilir.
    EF_CHECK(parse_drs_target("drs:global:1034CB89").code() == ErrorCode::Denied);
    EF_CHECK(parse_drs_target("drs:global:1068FB9C").code() == ErrorCode::Denied);
    EF_CHECK(parse_drs_target("drs:global:20C1221F").code() == ErrorCode::Denied);
}

EF_TEST(drs_new_restore_inherit) {  // MOCK
    for (const auto id : {kThreaded, kFxaa}) {
        char target[32];
        std::snprintf(target, sizeof target, "drs:global:%08X", id);
        const std::uint32_t want = id == kThreaded ? 1u : 0u;
        // (a) miras: global profilde acik deger yok -> uygula -> geri al -> acik override silinir (varsayilan YAZILMAZ)
        {
            Rig r;
            r.drs.defaults[id] = id == kThreaded ? 0u : 1u;
            auto active = r.engine.apply(r.tx(target, want));
            EF_CHECK(active.ok());
            if (!active.ok()) continue;
            EF_CHECK(r.drs.global.count(id) == 1 && r.drs.global[id] == want);
            EF_CHECK(!active.value().steps[0].snapshot.existed && active.value().steps[0].snapshot.inherited);
            auto rb = r.engine.rollback(active.value());
            EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
            EF_CHECK(r.drs.global.count(id) == 0);
        }
        // (b) acik deger: eski deger aynen geri yazilir
        {
            Rig r;
            r.drs.global[id] = id == kThreaded ? 2u : 1u;
            auto active = r.engine.apply(r.tx(target, want));
            EF_CHECK(active.ok());
            if (!active.ok()) continue;
            EF_CHECK(active.value().steps[0].snapshot.existed);
            auto rb = r.engine.rollback(active.value());
            EF_CHECK(rb.ok() && rb.value().state == TxState::Restored);
            EF_CHECK(r.drs.global.count(id) == 1 && r.drs.global[id] == (id == kThreaded ? 2u : 1u));
        }
    }
}

EF_TEST(drs_new_readback_mismatch_fails_and_rolls_back) {  // MOCK
    for (const auto mode : {LyingDrs::Mode::BadRead, LyingDrs::Mode::WrongWrite})
        for (const bool had_value : {false, true}) {
            FakeDrs inner;
            if (had_value) inner.global[kThreaded] = 2;
            LyingDrs lying(inner, mode);
            DrsAdapter adapter(lying);
            MemoryJournalFs fs;
            JournalStore store(fs, "m");
            AdapterRegistry reg;
            reg.add(OperationId::VendorProfileValue, &adapter);
            TransactionEngine engine(store, reg);
            JournalTx t;
            t.id = "0123456789abcdef0123456789abcdef";
            JournalStep s;
            s.op = SettingOperation{OperationId::VendorProfileValue, OperationScope::Machine, "drs:global:20C1221E", DrsAdapter::encode(1),
                                    false, false};
            t.steps.push_back(s);
            const auto res = engine.apply(t);
            if (mode == LyingDrs::Mode::BadRead) {
                // Geri okunan deger hedef degil -> hata (Active/Applied degil); hedefte bizim degerimiz oldugu icin ayni
                // adimda geri alindi: miras ise acik deger silindi, acik ise eski deger birebir geri yazildi.
                EF_CHECK(!res.ok());
                if (had_value) EF_CHECK(inner.global.count(kThreaded) == 1 && inner.global[kThreaded] == 2);
                else EF_CHECK(inner.global.count(kThreaded) == 0);
            } else {
                // Hedefte bizim yazmadigimiz deger: Active DEGIL, ezilmez, RecoveryRequired (dis degisiklik, REC-04).
                EF_CHECK(res.ok() && res.value().state == TxState::RecoveryRequired);
                EF_CHECK(inner.global.count(kThreaded) == 1 && inner.global[kThreaded] == 8);
            }
        }
}
