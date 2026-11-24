#pragma once
// EF-X16: Windows sistem geri yukleme noktasi (saf akis; gercek API platform katmaninda).
//  - API basarisi yetmez: envanterde yeni nokta gorulmeden "olusturuldu" denmez.
//  - Sistem Korumasi kapaliysa ACILMAZ; eski noktalar silinmez; siklik siniri degistirilmez.
//  - Uygulama journal'i geri yukleme noktasi yerine gecmez; ikisi ayri kayittir.
//  - BEGIN sonrasi durum kalici yazilir: END basarisiz olur/islem kesilirse sonraki oturum noktayi iptal eder.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/recovery/journal.h"

namespace ef::core::recovery {

inline constexpr char kRestorePointTarget[] = "restore_point:ecufps";
inline constexpr char kRestorePointValue[] = "create";
inline constexpr char kRestorePointJobFile[] = "rp-job.json";

enum class RestorePointOutcome { Created, SkippedByFrequency, ProtectionOff, DiskFull, Denied, ApiFailed, Unverified };
std::string_view to_string(RestorePointOutcome o) noexcept;

struct RpItem {
    std::uint32_t seq = 0;
    std::string description;
};
struct RpCall {
    std::uint32_t status = 0;  // Win32 hata kodu; 0 = basari
    std::int64_t seq = 0;
};

class RestorePointApi {
public:
    virtual ~RestorePointApi() = default;
    virtual RpCall begin(const std::string& description) = 0;
    virtual std::uint32_t end(std::int64_t seq) = 0;
    virtual std::uint32_t cancel(std::int64_t seq) = 0;
    virtual std::optional<std::vector<RpItem>> inventory() = 0;  // okunamazsa nullopt
};

struct RestorePointRun {
    RestorePointOutcome outcome = RestorePointOutcome::ApiFailed;
    std::int64_t seq = 0;
    std::string detail;
};

// Onceki oturumdan kalan yarim isi iptal eder; yoksa bir sey yapmaz. true: kayit kapatildi.
bool recover_pending_restore_point(RestorePointApi& api, JournalFs& jobs);
RestorePointRun run_restore_point(RestorePointApi& api, JournalFs& jobs, const std::string& description);

}  // namespace ef::core::recovery
