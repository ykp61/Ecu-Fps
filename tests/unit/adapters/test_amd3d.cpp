#include "core/adapters/amd/amd3d.h"
#include "ef_test.h"

using namespace ef::core::adapters::amd;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Rig {
    FakeAmd3d api;
    Amd3dAdapter adapter{api};
    MemoryJournalFs fs;
    JournalStore store{fs, "m"};
    AdapterRegistry reg;
    TransactionEngine engine{store, reg};
    Rig() { reg.add(OperationId::VendorProfileValue, &adapter); }
    JournalTx tx(const char* target, const char* v) {
        JournalTx t;
        t.id = "0123456789abcdef0123456789abcdef";
        JournalStep s;
        s.op = SettingOperation{OperationId::VendorProfileValue, OperationScope::Device, target, v, false, false};
        t.steps.push_back(s);
        return t;
    }
};
}  // namespace

EF_TEST(amd_targets_parse_strictly) {
    auto t = parse_amd_target("adlx:gpu1:antilag");
    EF_CHECK(t.ok() && t.value().gpu == 1 && t.value().feature == Amd3dFeature::AntiLag);
    EF_CHECK(parse_amd_target("adlx:gpu0:chill").ok());
    EF_CHECK(!parse_amd_target("adlx:gpu0:overclock").ok());
    EF_CHECK(!parse_amd_target("adlx:gpuX:boost").ok());
    EF_CHECK(!parse_amd_target("adlx:gpu:boost").ok());
    EF_CHECK(!parse_amd_target("nvapi:gpu0:boost").ok());
}

EF_TEST(amd_unsupported_interface_is_skipped_without_writes) {
    Rig r;
    r.api.supported[static_cast<int>(Amd3dFeature::AntiLag)] = false;
    auto res = r.engine.apply(r.tx("adlx:gpu1:antilag", "1"));
    EF_CHECK(!res.ok());
    EF_CHECK(res.code() == ErrorCode::Unsupported);
    EF_CHECK(r.fs.files.empty());  // journal acilmadi, yazma denenmedi
    const auto cap = r.adapter.capability({1, Amd3dFeature::AntiLag}, "test");
    EF_CHECK(cap.state == SupportState::Unsupported && cap.feature_id == "G03");
}

EF_TEST(amd_apply_and_restore_old_value) {
    Rig r;
    r.api.supported[static_cast<int>(Amd3dFeature::Chill)] = true;
    r.api.enabled[static_cast<int>(Amd3dFeature::Chill)] = true;  // Chill acik (FPS sinirlar)
    auto active = r.engine.apply(r.tx("adlx:gpu1:chill", "0"));
    EF_CHECK(active.ok());
    EF_CHECK(!r.api.enabled[static_cast<int>(Amd3dFeature::Chill)]);
    auto rb = r.engine.rollback(active.value());
    EF_CHECK(rb.value().state == TxState::Restored);
    EF_CHECK(r.api.enabled[static_cast<int>(Amd3dFeature::Chill)]);
}

EF_TEST(amd_permission_error_is_denied_not_unsupported) {
    Rig r;
    r.api.supported[static_cast<int>(Amd3dFeature::Boost)] = true;
    r.api.deny_writes = true;
    auto res = r.engine.apply(r.tx("adlx:gpu1:boost", "1"));
    EF_CHECK(!res.ok());
    EF_CHECK(!r.api.enabled[static_cast<int>(Amd3dFeature::Boost)]);
    EF_CHECK(r.store.scan().transactions[0].state == TxState::Restored);
}

EF_TEST(amd_cpu_monitoring_never_writes) {
    const auto c = amd_cpu_monitoring_capability();
    EF_CHECK(c.state == SupportState::Unsupported);
    EF_CHECK(c.reason.find("no PBO") != std::string::npos);
    EF_CHECK(capability_is_well_formed(c));
}
