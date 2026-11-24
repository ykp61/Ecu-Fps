#include "core/recovery/restore_point.h"

#include <algorithm>

#include "core/serialization/json.h"

namespace ef::core::recovery {

namespace {

constexpr std::uint32_t kErrAccessDenied = 5;
constexpr std::uint32_t kErrDiskFull = 112;
constexpr std::uint32_t kErrServiceDisabled = 1058;
constexpr std::uint32_t kErrInvalidData = 13;  // sira numarasi artik yok
constexpr std::int64_t kMaxCancelTries = 3;

RestorePointOutcome classify_status(std::uint32_t s) noexcept {
    switch (s) {
        case kErrServiceDisabled: return RestorePointOutcome::ProtectionOff;
        case kErrDiskFull: return RestorePointOutcome::DiskFull;
        case kErrAccessDenied: return RestorePointOutcome::Denied;
        default: return RestorePointOutcome::ApiFailed;
    }
}

struct PendingJob {
    std::int64_t seq = 0;  // 0: bozuk kayit, iptal edilecek sey yok
    std::int64_t tries = 0;
};

std::optional<PendingJob> pending_job(JournalFs& jobs) {
    const auto text = jobs.read(kRestorePointJobFile);
    if (!text.ok()) return std::nullopt;
    PendingJob p;
    const auto doc = json::parse(text.value());
    if (!doc.ok()) return p;
    if (const auto* seq = doc.value().find("seq"); seq && seq->type() == json::Value::Type::Int) p.seq = seq->as_int();
    if (const auto* t = doc.value().find("tries"); t && t->type() == json::Value::Type::Int) p.tries = t->as_int();
    return p;
}

contracts::Status write_job(JournalFs& jobs, std::int64_t seq, std::int64_t tries) {
    auto job = json::Value::object();
    job.set("seq", json::Value::integer(seq));
    job.set("tries", json::Value::integer(tries));
    return jobs.write_atomic(kRestorePointJobFile, json::write(job));
}

}  // namespace

std::string_view to_string(RestorePointOutcome o) noexcept {
    switch (o) {
        case RestorePointOutcome::Created: return "created";
        case RestorePointOutcome::SkippedByFrequency: return "skipped_frequency";
        case RestorePointOutcome::ProtectionOff: return "protection_off";
        case RestorePointOutcome::DiskFull: return "disk_full";
        case RestorePointOutcome::Denied: return "denied";
        case RestorePointOutcome::ApiFailed: return "api_failed";
        case RestorePointOutcome::Unverified: return "unverified";
    }
    return "api_failed";
}

bool recover_pending_restore_point(RestorePointApi& api, JournalFs& jobs) {
    const auto job = pending_job(jobs);
    if (!job) return false;
    if (job->seq > 0) {
        const auto st = api.cancel(job->seq);
        // Nokta artik yoksa veya koruma kapandiysa acik kalan is de yoktur; gecici hatada sinirli yeniden deneme.
        if (st != 0 && st != kErrInvalidData && st != kErrServiceDisabled && job->tries + 1 < kMaxCancelTries) {
            (void)write_job(jobs, job->seq, job->tries + 1);
            return false;
        }
    }
    return jobs.remove(kRestorePointJobFile).ok();
}

RestorePointRun run_restore_point(RestorePointApi& api, JournalFs& jobs, const std::string& description) {
    RestorePointRun r;
    (void)recover_pending_restore_point(api, jobs);
    if (pending_job(jobs)) {  // eski is kapatilamadi: ust uste ikinci BEGIN yapilmaz
        r.detail = "previous job unresolved";
        return r;
    }
    const auto before = api.inventory();
    const auto call = api.begin(description);
    if (call.status != 0) {
        r.outcome = classify_status(call.status);
        r.detail = "begin " + std::to_string(call.status);
        return r;
    }
    r.seq = call.seq;
    // Siklik siniri: Windows BEGIN'de ONCEKI noktanin numarasini dondurur. O nokta iptal EDILMEZ (silinirdi).
    if (before && std::any_of(before->begin(), before->end(), [&](const RpItem& b) { return static_cast<std::int64_t>(b.seq) == call.seq; })) {
        r.outcome = RestorePointOutcome::SkippedByFrequency;
        return r;
    }
    if (!before) {  // numaranin yeni oldugu kanitlanamaz: iptal kaydi yazilmaz
        (void)api.end(call.seq);
        r.outcome = RestorePointOutcome::Unverified;
        r.detail = "inventory unavailable before begin";
        return r;
    }
    if (const auto st = write_job(jobs, call.seq, 0); !st.ok()) {
        (void)api.cancel(call.seq);  // kalici kayit yoksa acik birakilmaz
        r.detail = "job record failed";
        return r;
    }
    if (const auto s = api.end(call.seq); s != 0) {
        r.outcome = classify_status(s);  // kayit kalir: sonraki oturum iptal eder
        r.detail = "end " + std::to_string(s);
        return r;
    }
    (void)jobs.remove(kRestorePointJobFile);
    const auto after = api.inventory();
    if (!after) {
        r.outcome = RestorePointOutcome::Unverified;
        r.detail = "inventory unavailable";
        return r;
    }
    const bool created = std::any_of(after->begin(), after->end(), [&](const RpItem& a) {
        return a.description == description &&
               std::none_of(before->begin(), before->end(), [&](const RpItem& b) { return b.seq == a.seq; });
    });
    r.outcome = created ? RestorePointOutcome::Created : RestorePointOutcome::SkippedByFrequency;
    return r;
}

}  // namespace ef::core::recovery
