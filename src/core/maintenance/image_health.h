#pragma once
// EF-X02: Windows goruntu sagligi kontrolu ve onarimi (bakim isi; ayar degil, tam geri alinamaz).
//  - Once kontrol. Saglikli goruntude onarim BASLATILMAZ. Onarilamaz goruntude "duzeltildi" yazilmaz.
//  - Bekleyen yeniden baslatma varsa is baslamaz. Kaynak yok, yeniden baslatma gerekli, reddedildi ayri sonuclardir.
//  - Onarim sonrasi yeniden kontrol saglikli demeden basari yazilmaz.
//  - Windows Update'ten indirme ayri onay kapsamidir; bu surumde yalniz yerel kaynak (LimitAccess) kullanilir.
//  - Onarim basinda kalici kayit yazilir; yarida kalirsa sonraki calismada bildirilir (servis islemi zorla oldurulmez).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/recovery/journal.h"

namespace ef::core::maintenance {

inline constexpr char kImageCheckTarget[] = "maint:image_check";
inline constexpr char kImageRepairTarget[] = "maint:image_repair";
inline constexpr char kMaintenanceValue[] = "run";
inline constexpr char kRepairJobFile[] = "image-repair-job.json";

enum class ImageHealth { Healthy, Repairable, NonRepairable };

enum class MaintOutcome {
    Healthy, Repairable, NonRepairable,  // kontrol sonucu
    Repaired,                            // onarim + yeniden kontrol saglikli
    RepairNotVerified,                   // onarim dondu ama yeniden kontrol saglikli demedi
    SourceMissing, RebootRequired, PendingReboot, Unsupported, Failed,
    NoRestorePoint,                      // geri yukleme noktasi olmadan onarim baslatilmadi
    Purged,                              // T15: bekleme bellegi bosaltildi (standby_purge.h)
};
std::string_view to_string(MaintOutcome o) noexcept;

class ImageServicingApi {
public:
    virtual ~ImageServicingApi() = default;
    virtual std::optional<bool> pending_reboot() = 0;           // okunamazsa nullopt
    // scan=false: CBS'nin isaretledigi bozulma (hizli); scan=true: tam tarama. Unsupported yalniz API yoksa.
    virtual contracts::Result<ImageHealth> check(bool scan) = 0;
    virtual std::uint32_t restore_local_only() = 0;              // HRESULT
    virtual bool create_restore_point() = 0;                     // onarim on kosulu: nokta olustu veya yakin zamanda var
};

struct MaintRun {
    MaintOutcome outcome = MaintOutcome::Failed;
    bool previous_interrupted = false;  // onceki onarim yarida kalmisti
    std::string detail;
};

MaintRun run_image_check(ImageServicingApi& api, recovery::JournalFs* jobs = nullptr);
MaintRun run_image_repair(ImageServicingApi& api, recovery::JournalFs& jobs, std::int64_t now_unix_ms);

}  // namespace ef::core::maintenance
