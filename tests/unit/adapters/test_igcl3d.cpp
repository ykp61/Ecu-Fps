// GPU-04 sozlesmesi: sahte IGCL ile hata esleme ve geri alma. Gercek Intel donanim testi DEGILDIR (B-004).
#include "core/adapters/intel/igcl3d.h"
#include "ef_test.h"

using namespace ef::core::adapters::intel;
using namespace ef::core::recovery;
using namespace ef::contracts;

namespace {
struct Rig {
    FakeIgcl3d api;
    Igcl3dAdapter adapter{api};
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
    int low() const { return static_cast<int>(Igcl3dFeature::LowLatency); }
};
}  // namespace

EF_TEST(igcl_targets_parse_strictly) {
    auto t = parse_igcl_target("igcl:gpu2:endurance_gaming");
    EF_CHECK(t.ok() && t.value().gpu == 2 && t.value().feature == Igcl3dFeature::EnduranceGaming);
    EF_CHECK(!parse_igcl_target("igcl:gpu0:voltage").ok());
    EF_CHECK(!parse_igcl_target("igcl:gpuX:low_latency").ok());
    EF_CHECK(!parse_igcl_target("adlx:gpu0:low_latency").ok());
}

EF_TEST(igcl_unsupported_never_writes_and_is_not_success) {
    Rig r;
    r.api.supported[r.low()] = false;
    auto res = r.engine.apply(r.tx("igcl:gpu0:low_latency", "1"));
    EF_CHECK(!res.ok() && res.code() == ErrorCode::Unsupported && r.api.writes == 0 && r.fs.files.empty());
    EF_CHECK(r.adapter.capability({0, Igcl3dFeature::LowLatency}, "t").state == SupportState::Unsupported);
}

EF_TEST(igcl_permission_is_denied_not_unsupported) {
    Rig r;
    r.api.support_result = IgclResult::InsufficientPermissions;
    auto res = r.engine.apply(r.tx("igcl:gpu0:low_latency", "1"));
    EF_CHECK(!res.ok() && res.code() == ErrorCode::Denied);
    EF_CHECK(r.adapter.capability({0, Igcl3dFeature::LowLatency}, "t").state == SupportState::Denied);
}

EF_TEST(igcl_apply_restores_exact_old_value_evidence_is_mock) {
    Rig r;
    r.api.supported[r.low()] = true;
    r.api.enabled[r.low()] = false;
    auto res = r.engine.apply(r.tx("igcl:gpu0:low_latency", "1"));
    EF_CHECK(res.ok() && res.value().state == TxState::Active && r.api.enabled[r.low()]);
    auto back = r.engine.rollback(res.value());
    EF_CHECK(back.ok() && back.value().state == TxState::Restored && !r.api.enabled[r.low()]);
    EF_CHECK(r.adapter.capability({0, Igcl3dFeature::LowLatency}, "t").evidence == EvidenceLevel::Mock);
}

EF_TEST(igcl_write_failure_rolls_back_no_silent_success) {
    Rig r;
    r.api.supported[r.low()] = true;
    r.api.write_result = IgclResult::DeviceLost;
    auto res = r.engine.apply(r.tx("igcl:gpu0:low_latency", "1"));
    EF_CHECK(!res.ok() || res.value().state != TxState::Active);
}
