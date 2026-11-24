#include "contracts/measure.h"
#include "contracts/models.h"
#include "contracts/support.h"
#include "ef_test.h"

using namespace ef::contracts;

EF_TEST(missing_sensor_is_not_zero) {
    const auto m = Measurement::missing(Unit::Celsius, MissingReason::NotSupported);
    EF_CHECK(!m.has_value());
    EF_CHECK(!m.value().has_value());
    EF_CHECK(m.missing_reason() == MissingReason::NotSupported);
    const auto zero = Measurement::of(0.0, Unit::Celsius);
    EF_CHECK(zero.has_value());
    // Eksik ile 0 karsilastirilamaz; sonuc hata olmali.
    auto c = m.compare(zero);
    EF_CHECK(!c.ok());
    EF_CHECK(c.code() == ErrorCode::MissingValue);
}

EF_TEST(missing_with_none_reason_becomes_not_sampled) {
    const auto m = Measurement::missing(Unit::Fps, MissingReason::None);
    EF_CHECK(m.missing_reason() == MissingReason::NotSampled);
}

EF_TEST(unit_mismatch_is_rejected) {
    const auto fps = Measurement::of(120.0, Unit::Fps);
    const auto ms = Measurement::of(8.3, Unit::Milliseconds);
    auto d = fps.difference(ms);
    EF_CHECK(!d.ok());
    EF_CHECK(d.code() == ErrorCode::UnitMismatch);
    auto same = fps.difference(Measurement::of(100.0, Unit::Fps));
    EF_CHECK(same.ok());
    EF_CHECK(same.value() == 20.0);
}

EF_TEST(invalid_enum_text_is_rejected) {
    EF_CHECK(!parse_support_state("Supported").has_value());  // buyuk/kucuk harf duyarli
    EF_CHECK(!parse_support_state("").has_value());
    EF_CHECK(!parse_support_state("yes").has_value());
    EF_CHECK(parse_support_state("denied") == SupportState::Denied);
    EF_CHECK(!parse_unit("FPS").has_value());
    EF_CHECK(parse_unit("fps") == Unit::Fps);
    EF_CHECK(!parse_operation_id("rm -rf").has_value());
    EF_CHECK(!parse_operation_id("C:\\Windows\\system32\\cmd.exe").has_value());
    EF_CHECK(parse_operation_id("power_mode_set") == OperationId::PowerModeSet);
    EF_CHECK(!parse_tx_state("done").has_value());
    EF_CHECK(!parse_task_status("finished").has_value());
}

EF_TEST(enum_text_round_trip) {
    for (auto s : {SupportState::Supported, SupportState::Unsupported, SupportState::Unknown, SupportState::Denied})
        EF_CHECK(parse_support_state(to_string(s)) == s);
    for (auto s : {TxState::Prepared, TxState::Applying, TxState::Applied, TxState::Verifying, TxState::Active,
                   TxState::RollingBack, TxState::Restored, TxState::RecoveryRequired})
        EF_CHECK(parse_tx_state(to_string(s)) == s);
    for (auto e : {EvidenceLevel::Mock, EvidenceLevel::Unit, EvidenceLevel::WindowsIntegration,
                   EvidenceLevel::Hardware, EvidenceLevel::ReleaseArtifact})
        EF_CHECK(parse_evidence_level(to_string(e)) == e);
}

EF_TEST(status_enums_are_distinct_types) {
    // Gorev done != ozellik dogrulandi; ayri parser'lar ayri kume kabul eder.
    EF_CHECK(parse_task_status("done").has_value());
    EF_CHECK(!parse_feature_readiness("done").has_value());
    EF_CHECK(!parse_run_validity("done").has_value());
    EF_CHECK(!is_real_device_evidence(EvidenceLevel::Mock));
    EF_CHECK(!is_real_device_evidence(EvidenceLevel::WindowsIntegration));
    EF_CHECK(is_real_device_evidence(EvidenceLevel::Hardware));
}

EF_TEST(capability_requires_reason_and_provider) {
    Capability c{"W01", "system", SupportState::Unknown, "", "", "", Privilege::User, EvidenceLevel::Mock};
    EF_CHECK(!capability_is_well_formed(c));
    c.reason = "power API not queried yet";
    EF_CHECK(capability_is_well_formed(c));
    c.state = SupportState::Supported;
    EF_CHECK(!capability_is_well_formed(c));  // saglayici yok
    c.provider = "PowerSetActiveOverlayScheme";
    EF_CHECK(capability_is_well_formed(c));
}

EF_TEST(transaction_happy_path) {
    Transaction tx;
    EF_CHECK(tx.advance(TxState::Applying).ok());
    EF_CHECK(tx.advance(TxState::Applied).ok());
    EF_CHECK(tx.advance(TxState::Verifying).ok());
    EF_CHECK(tx.advance(TxState::Active).ok());
    EF_CHECK(tx.advance(TxState::RollingBack).ok());
    EF_CHECK(tx.advance(TxState::Restored).ok());
    EF_CHECK(is_terminal(tx.state));
}

EF_TEST(transaction_invalid_transitions_rejected) {
    Transaction tx;
    auto r = tx.advance(TxState::Active);  // prepared -> active atlanamaz
    EF_CHECK(!r.ok());
    EF_CHECK(r.code() == ErrorCode::InvalidTransition);
    EF_CHECK(tx.state == TxState::Prepared);
    EF_CHECK(!can_transition(TxState::Restored, TxState::Applying));
    EF_CHECK(!can_transition(TxState::Active, TxState::Applied));
    EF_CHECK(can_transition(TxState::RecoveryRequired, TxState::RollingBack));
    EF_CHECK(can_transition(TxState::Applying, TxState::RecoveryRequired));
}

EF_TEST(diagnosis_needs_evidence_unless_abstain) {
    Diagnosis d;
    EF_CHECK(!diagnosis_is_well_formed(d));  // gerekce yok
    d.reason = "veri yetersiz";
    EF_CHECK(diagnosis_is_well_formed(d));   // abstain + gerekce
    d.confidence = Confidence::High;
    d.category = Bottleneck::Gpu;
    EF_CHECK(!diagnosis_is_well_formed(d));  // kanit yok
    d.evidence_refs.push_back("run:1#gpu_busy");
    EF_CHECK(diagnosis_is_well_formed(d));
}

EF_TEST(experiment_decision_requires_same_conditions_and_runs) {
    Experiment e;
    e.decision = ExperimentDecision::Improved;
    e.baseline_runs = {"a1", "a2", "a3", "a4", "a5"};
    e.change_runs = {"b1", "b2", "b3", "b4", "b5"};
    EF_CHECK(!experiment_decision_allowed(e, 5));  // es kosul yok
    e.same_conditions = true;
    EF_CHECK(experiment_decision_allowed(e, 5));
    e.change_runs.pop_back();
    EF_CHECK(!experiment_decision_allowed(e, 5));
    e.decision = ExperimentDecision::Inconclusive;
    EF_CHECK(experiment_decision_allowed(e, 5));
}

EF_TEST(benchmark_run_defaults_are_not_fake_values) {
    BenchmarkRun r;
    EF_CHECK(!r.avg_fps.has_value());
    EF_CHECK(r.validity != RunValidity::Valid);
}
