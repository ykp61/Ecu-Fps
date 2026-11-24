#include "contracts/enum_text.h"
#include "contracts/measure.h"
#include "contracts/models.h"
#include "contracts/result.h"
#include "contracts/support.h"

namespace ef::contracts {

namespace {
using namespace std::string_view_literals;

constexpr EnumTable<ErrorCode, 14> kErrorCodes{{
    {ErrorCode::InvalidEnum, "invalid_enum"sv},
    {ErrorCode::UnitMismatch, "unit_mismatch"sv},
    {ErrorCode::MissingValue, "missing_value"sv},
    {ErrorCode::OutOfRange, "out_of_range"sv},
    {ErrorCode::InvalidTransition, "invalid_transition"sv},
    {ErrorCode::SchemaTooNew, "schema_too_new"sv},
    {ErrorCode::ParseLimit, "parse_limit"sv},
    {ErrorCode::ParseError, "parse_error"sv},
    {ErrorCode::NegativeDuration, "negative_duration"sv},
    {ErrorCode::NotAligned, "not_aligned"sv},
    {ErrorCode::IoError, "io_error"sv},
    {ErrorCode::Unsupported, "unsupported"sv},
    {ErrorCode::Denied, "denied"sv},
    {ErrorCode::GateBlocked, "gate_blocked"sv},
}};

constexpr EnumTable<SupportState, 4> kSupport{{
    {SupportState::Supported, "supported"sv},
    {SupportState::Unsupported, "unsupported"sv},
    {SupportState::Unknown, "unknown"sv},
    {SupportState::Denied, "denied"sv},
}};

constexpr EnumTable<Privilege, 3> kPrivilege{{
    {Privilege::User, "user"sv},
    {Privilege::Admin, "admin"sv},
    {Privilege::Reboot, "reboot"sv},
}};

constexpr EnumTable<TaskStatus, 5> kTaskStatus{{
    {TaskStatus::Planned, "planned"sv},
    {TaskStatus::InProgress, "in_progress"sv},
    {TaskStatus::Blocked, "blocked"sv},
    {TaskStatus::Review, "review"sv},
    {TaskStatus::Done, "done"sv},
}};

constexpr EnumTable<FeatureReadiness, 5> kReadiness{{
    {FeatureReadiness::NotStarted, "not_started"sv},
    {FeatureReadiness::Experimental, "experimental"sv},
    {FeatureReadiness::VerifiedSupported, "verified_supported"sv},
    {FeatureReadiness::ReleaseEnabled, "release_enabled"sv},
    {FeatureReadiness::Disabled, "disabled"sv},
}};

constexpr EnumTable<EvidenceLevel, 5> kEvidence{{
    {EvidenceLevel::Mock, "mock"sv},
    {EvidenceLevel::Unit, "unit"sv},
    {EvidenceLevel::WindowsIntegration, "windows_integration"sv},
    {EvidenceLevel::Hardware, "hardware"sv},
    {EvidenceLevel::ReleaseArtifact, "release_artifact"sv},
}};

constexpr EnumTable<Unit, 11> kUnits{{
    {Unit::Fps, "fps"sv},
    {Unit::Milliseconds, "ms"sv},
    {Unit::Percent, "percent"sv},
    {Unit::MiB, "MiB"sv},
    {Unit::Celsius, "C"sv},
    {Unit::Watt, "W"sv},
    {Unit::MHz, "MHz"sv},
    {Unit::BytesPerSecond, "B/s"sv},
    {Unit::Count, "count"sv},
    {Unit::MBps, "MB/s"sv},
    {Unit::Mbps, "Mbit/s"sv},
}};

constexpr EnumTable<MissingReason, 6> kMissing{{
    {MissingReason::None, "none"sv},
    {MissingReason::NotSupported, "not_supported"sv},
    {MissingReason::PermissionDenied, "permission_denied"sv},
    {MissingReason::NotSampled, "not_sampled"sv},
    {MissingReason::SourceError, "source_error"sv},
    {MissingReason::Invalid, "invalid"sv},
}};

constexpr EnumTable<OperationId, 32> kOps{{
    {OperationId::PowerModeSet, "power_mode_set"sv},
    {OperationId::PowerSchemeSet, "power_scheme_set"sv},
    {OperationId::GameModeSet, "game_mode_set"sv},
    {OperationId::GpuPreferenceSet, "gpu_preference_set"sv},
    {OperationId::RefreshRateSet, "refresh_rate_set"sv},
    {OperationId::VisualEffectsSet, "visual_effects_set"sv},
    {OperationId::ProcessPowerThrottling, "process_power_throttling"sv},
    {OperationId::ProcessPriority, "process_priority"sv},
    {OperationId::StartupRunEntry, "startup_run_entry"sv},
    {OperationId::StartupFolderItem, "startup_folder_item"sv},
    {OperationId::StartupTask, "startup_task"sv},
    {OperationId::UpdatePauseSession, "update_pause_session"sv},
    {OperationId::VendorProfileValue, "vendor_profile_value"sv},
    {OperationId::GameConfigField, "game_config_field"sv},
    {OperationId::ServiceSessionStop, "service_session_stop"sv},
    {OperationId::GamingToggleSet, "gaming_toggle_set"sv},
    {OperationId::HagsSet, "hags_set"sv},
    {OperationId::FastStartupSet, "fast_startup_set"sv},
    {OperationId::AppCompatLayerSet, "appcompat_layer_set"sv},
    {OperationId::PowerSettingSet, "power_setting_set"sv},
    {OperationId::RestorePointCreate, "restore_point_create"sv},
    {OperationId::DnsServersSet, "dns_servers_set"sv},
    {OperationId::MaintenanceJob, "maintenance_job"sv},
    {OperationId::DriverInstall, "driver_install"sv},
    {OperationId::VendorDriverInstall, "vendor_driver_install"sv},
    {OperationId::BackgroundProcessLower, "background_process_lower"sv},
    {OperationId::TweakValueSet, "tweak_value_set"sv},
    {OperationId::ServiceStartTypeSet, "service_start_type_set"sv},
    {OperationId::DefenderExclusionSet, "defender_exclusion_set"sv},
    {OperationId::GamePriorityHint, "game_priority_hint"sv},
    {OperationId::AppxPackageRemove, "appx_package_remove"sv},
    {OperationId::BootOptionSet, "boot_option_set"sv},
}};

constexpr EnumTable<OperationScope, 5> kScopes{{
    {OperationScope::CurrentUser, "current_user"sv},
    {OperationScope::Machine, "machine"sv},
    {OperationScope::Process, "process"sv},
    {OperationScope::Device, "device"sv},
    {OperationScope::Game, "game"sv},
}};

constexpr EnumTable<TxState, 8> kTx{{
    {TxState::Prepared, "prepared"sv},
    {TxState::Applying, "applying"sv},
    {TxState::Applied, "applied"sv},
    {TxState::Verifying, "verifying"sv},
    {TxState::Active, "active"sv},
    {TxState::RollingBack, "rolling_back"sv},
    {TxState::Restored, "restored"sv},
    {TxState::RecoveryRequired, "recovery_required"sv},
}};

constexpr EnumTable<RunSource, 2> kRunSource{{
    {RunSource::AppTest, "app_test"sv},
    {RunSource::InGame, "in_game"sv},
}};

constexpr EnumTable<RunValidity, 5> kValidity{{
    {RunValidity::Valid, "valid"sv},
    {RunValidity::InvalidCapped, "invalid_capped"sv},
    {RunValidity::InvalidInterrupted, "invalid_interrupted"sv},
    {RunValidity::InvalidInsufficientSamples, "invalid_insufficient_samples"sv},
    {RunValidity::InvalidClockDiscontinuity, "invalid_clock_discontinuity"sv},
}};

constexpr EnumTable<Confidence, 4> kConfidence{{
    {Confidence::Abstain, "abstain"sv},
    {Confidence::Low, "low"sv},
    {Confidence::Medium, "medium"sv},
    {Confidence::High, "high"sv},
}};

constexpr EnumTable<Bottleneck, 11> kBottleneck{{
    {Bottleneck::Cpu, "cpu"sv},
    {Bottleneck::Gpu, "gpu"sv},
    {Bottleneck::Ram, "ram"sv},
    {Bottleneck::Vram, "vram"sv},
    {Bottleneck::Storage, "storage"sv},
    {Bottleneck::FrameCap, "frame_cap"sv},
    {Bottleneck::Presentation, "presentation"sv},
    {Bottleneck::Thermal, "thermal"sv},
    {Bottleneck::Power, "power"sv},
    {Bottleneck::Background, "background"sv},
    {Bottleneck::Unknown, "unknown"sv},
}};

constexpr EnumTable<ExperimentDecision, 4> kDecision{{
    {ExperimentDecision::Improved, "improved"sv},
    {ExperimentDecision::NoChange, "no_change"sv},
    {ExperimentDecision::Regressed, "regressed"sv},
    {ExperimentDecision::Inconclusive, "inconclusive"sv},
}};
}  // namespace

std::string_view to_string(ErrorCode c) noexcept { return enum_to_text(kErrorCodes, c); }
std::string_view to_string(SupportState s) noexcept { return enum_to_text(kSupport, s); }
std::optional<SupportState> parse_support_state(std::string_view s) noexcept { return enum_from_text(kSupport, s); }
std::string_view to_string(Privilege p) noexcept { return enum_to_text(kPrivilege, p); }
std::optional<Privilege> parse_privilege(std::string_view s) noexcept { return enum_from_text(kPrivilege, s); }
std::string_view to_string(TaskStatus s) noexcept { return enum_to_text(kTaskStatus, s); }
std::optional<TaskStatus> parse_task_status(std::string_view s) noexcept { return enum_from_text(kTaskStatus, s); }
std::string_view to_string(FeatureReadiness r) noexcept { return enum_to_text(kReadiness, r); }
std::optional<FeatureReadiness> parse_feature_readiness(std::string_view s) noexcept { return enum_from_text(kReadiness, s); }
std::string_view to_string(EvidenceLevel e) noexcept { return enum_to_text(kEvidence, e); }
std::optional<EvidenceLevel> parse_evidence_level(std::string_view s) noexcept { return enum_from_text(kEvidence, s); }
std::string_view to_string(Unit u) noexcept { return enum_to_text(kUnits, u); }
std::optional<Unit> parse_unit(std::string_view s) noexcept { return enum_from_text(kUnits, s); }
std::string_view to_string(MissingReason r) noexcept { return enum_to_text(kMissing, r); }
std::optional<MissingReason> parse_missing_reason(std::string_view s) noexcept { return enum_from_text(kMissing, s); }
std::string_view to_string(OperationId id) noexcept { return enum_to_text(kOps, id); }
std::optional<OperationId> parse_operation_id(std::string_view s) noexcept { return enum_from_text(kOps, s); }
std::string_view to_string(OperationScope s) noexcept { return enum_to_text(kScopes, s); }
std::optional<OperationScope> parse_operation_scope(std::string_view s) noexcept { return enum_from_text(kScopes, s); }
std::string_view to_string(TxState s) noexcept { return enum_to_text(kTx, s); }
std::optional<TxState> parse_tx_state(std::string_view s) noexcept { return enum_from_text(kTx, s); }
std::string_view to_string(RunSource s) noexcept { return enum_to_text(kRunSource, s); }
std::string_view to_string(RunValidity v) noexcept { return enum_to_text(kValidity, v); }
std::optional<RunValidity> parse_run_validity(std::string_view s) noexcept { return enum_from_text(kValidity, s); }
std::string_view to_string(Confidence c) noexcept { return enum_to_text(kConfidence, c); }
std::string_view to_string(Bottleneck b) noexcept { return enum_to_text(kBottleneck, b); }
std::string_view to_string(ExperimentDecision d) noexcept { return enum_to_text(kDecision, d); }

bool capability_is_well_formed(const Capability& c) noexcept {
    if (c.feature_id.empty() || c.device_scope.empty()) return false;
    if (c.state != SupportState::Supported && c.reason.empty()) return false;
    // Desteklendi iddiasi saglayici bilgisi olmadan kabul edilmez.
    if (c.state == SupportState::Supported && c.provider.empty()) return false;
    return true;
}

bool may_enable_release(std::string_view feature_id, const std::vector<SupportEvidence>& evidence) noexcept {
    bool real_supported = false;
    for (const auto& e : evidence) {
        if (e.feature_id != feature_id) continue;
        if (!is_real_device_evidence(e.level)) continue;
        if (e.evidence_path.empty() || e.commit.empty()) continue;  // izlenemeyen kanit sayilmaz
        if (e.result == SupportState::Supported) real_supported = true;
        else return false;
    }
    return real_supported;
}

Result<double> Measurement::difference(const Measurement& other) const {
    if (unit_ != other.unit_) return Result<double>::fail(ErrorCode::UnitMismatch);
    if (!value_ || !other.value_) return Result<double>::fail(ErrorCode::MissingValue);
    return *value_ - *other.value_;
}

Result<int> Measurement::compare(const Measurement& other) const {
    auto d = difference(other);
    if (!d) return Result<int>(d.error());
    return d.value() < 0 ? -1 : (d.value() > 0 ? 1 : 0);
}

bool can_transition(TxState from, TxState to) noexcept {
    using S = TxState;
    switch (from) {
        case S::Prepared: return to == S::Applying || to == S::RollingBack;
        case S::Applying: return to == S::Applied || to == S::RollingBack || to == S::RecoveryRequired;
        case S::Applied: return to == S::Verifying || to == S::RollingBack;
        case S::Verifying: return to == S::Active || to == S::RollingBack || to == S::RecoveryRequired;
        case S::Active: return to == S::RollingBack;
        case S::RollingBack: return to == S::Restored || to == S::RecoveryRequired;
        case S::RecoveryRequired: return to == S::RollingBack;
        case S::Restored: return false;
    }
    return false;
}

bool is_terminal(TxState s) noexcept { return s == TxState::Restored; }

Status Transaction::advance(TxState next) {
    if (!can_transition(state, next)) {
        return Status::fail(ErrorCode::InvalidTransition,
                            std::string(to_string(state)) + "->" + std::string(to_string(next)));
    }
    state = next;
    return ok_status();
}

bool diagnosis_is_well_formed(const Diagnosis& d) noexcept {
    if (d.reason.empty()) return false;
    if (d.confidence == Confidence::Abstain) return true;
    return !d.evidence_refs.empty() && d.category != Bottleneck::Unknown;
}

bool experiment_decision_allowed(const Experiment& e, int min_runs_each) noexcept {
    if (e.decision == ExperimentDecision::Inconclusive) return true;
    if (!e.same_conditions) return false;
    const auto n = static_cast<std::size_t>(min_runs_each);
    return e.baseline_runs.size() >= n && e.change_runs.size() >= n;
}

}  // namespace ef::contracts
