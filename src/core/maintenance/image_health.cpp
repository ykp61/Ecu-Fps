#include "core/maintenance/image_health.h"

#include "core/serialization/json.h"

namespace ef::core::maintenance {

namespace {

constexpr std::uint32_t kSourceMissing = 0x800F081Fu;   // CBS_E_SOURCE_MISSING
constexpr std::uint32_t kRebootRequired = 3010u;        // ERROR_SUCCESS_REBOOT_REQUIRED
constexpr std::uint32_t kRebootRequiredHr = 0x80070BC2u;

MaintOutcome from_health(ImageHealth h) noexcept {
    switch (h) {
        case ImageHealth::Healthy: return MaintOutcome::Healthy;
        case ImageHealth::Repairable: return MaintOutcome::Repairable;
        case ImageHealth::NonRepairable: return MaintOutcome::NonRepairable;
    }
    return MaintOutcome::Failed;
}

// Bekleyen yeniden baslatma: is baslamaz. Okunamazsa da baslamaz (belirsiz servis durumu).
std::optional<MaintRun> blocked_by_reboot(ImageServicingApi& api) {
    const auto pending = api.pending_reboot();
    if (pending == std::optional<bool>(false)) return std::nullopt;
    MaintRun r;
    r.outcome = MaintOutcome::PendingReboot;
    r.detail = pending ? "reboot pending" : "reboot state unknown";
    return r;
}

}  // namespace

std::string_view to_string(MaintOutcome o) noexcept {
    switch (o) {
        case MaintOutcome::Healthy: return "healthy";
        case MaintOutcome::Repairable: return "repairable";
        case MaintOutcome::NonRepairable: return "non_repairable";
        case MaintOutcome::Repaired: return "repaired";
        case MaintOutcome::RepairNotVerified: return "repair_not_verified";
        case MaintOutcome::SourceMissing: return "source_missing";
        case MaintOutcome::RebootRequired: return "reboot_required";
        case MaintOutcome::PendingReboot: return "pending_reboot";
        case MaintOutcome::Unsupported: return "unsupported";
        case MaintOutcome::Failed: return "failed";
        case MaintOutcome::NoRestorePoint: return "no_restore_point";
        case MaintOutcome::Purged: return "purged";
    }
    return "failed";
}

MaintRun run_image_check(ImageServicingApi& api, recovery::JournalFs* jobs) {
    MaintRun r;
    // Onceki onarim yarida kaldiysa kontrol bunu bildirir; kayit bildirildikten sonra kapanir.
    if (jobs && jobs->read(kRepairJobFile).ok()) {
        r.previous_interrupted = true;
        (void)jobs->remove(kRepairJobFile);
    }
    if (auto b = blocked_by_reboot(api)) {
        b->previous_interrupted = r.previous_interrupted;
        return *b;
    }
    const auto h = api.check(false);
    if (!h.ok()) {
        r.outcome = h.code() == contracts::ErrorCode::Unsupported ? MaintOutcome::Unsupported : MaintOutcome::Failed;
        r.detail = h.error().detail;
        return r;
    }
    r.outcome = from_health(h.value());
    return r;
}

MaintRun run_image_repair(ImageServicingApi& api, recovery::JournalFs& jobs, std::int64_t now_unix_ms) {
    MaintRun r;
    r.previous_interrupted = jobs.read(kRepairJobFile).ok();
    if (auto b = blocked_by_reboot(api)) {
        b->previous_interrupted = r.previous_interrupted;
        return *b;
    }
    const auto before = api.check(false);
    if (!before.ok()) {
        r.outcome = before.code() == contracts::ErrorCode::Unsupported ? MaintOutcome::Unsupported : MaintOutcome::Failed;
        r.detail = before.error().detail;
        return r;
    }
    if (before.value() != ImageHealth::Repairable) {  // saglikli veya onarilamaz: onarim baslatilmaz
        r.outcome = from_health(before.value());
        (void)jobs.remove(kRepairJobFile);  // goruntu durumu artik okundu: eski kesik kayit kapanir
        return r;
    }
    if (!api.create_restore_point()) {  // geri donus kosulu yoksa tam geri alinamaz is baslamaz
        r.outcome = MaintOutcome::NoRestorePoint;
        return r;
    }
    auto job = json::Value::object();
    job.set("started_unix_ms", json::Value::integer(now_unix_ms));
    if (!jobs.write_atomic(kRepairJobFile, json::write(job)).ok()) {
        r.detail = "job record failed";  // kalici kayit yoksa onarim baslamaz
        return r;
    }
    const auto hr = api.restore_local_only();
    if (hr == kSourceMissing) {
        r.outcome = MaintOutcome::SourceMissing;
    } else if (hr == kRebootRequired || hr == kRebootRequiredHr) {
        r.outcome = MaintOutcome::RebootRequired;  // yeniden kontrol yeniden baslatmadan sonra: basari denmez
    } else if (hr == 1) {  // DISMAPI_S_RELOAD_IMAGE_SESSION_REQUIRED: oturum yenilenmeden dogrulanamaz
        r.outcome = MaintOutcome::RepairNotVerified;
        r.detail = "session reload required";
    } else if (hr != 0) {
        r.outcome = MaintOutcome::Failed;
        r.detail = "restore " + std::to_string(hr);
    } else {
        const auto after = api.check(true);  // basari yalniz TAM tarama saglikli derse
        r.outcome = after.ok() && after.value() == ImageHealth::Healthy ? MaintOutcome::Repaired : MaintOutcome::RepairNotVerified;
    }
    (void)jobs.remove(kRepairJobFile);
    return r;
}

}  // namespace ef::core::maintenance
