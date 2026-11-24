#pragma once
// T15: bekleme (standby) bellegi bosaltma. Tek seferlik bakim isi; ayar degil, geri alinacak bir sey yok, kullanici
// verisi silinmez. Sabit is kimligi + "run" degeri; yalniz yukseltilmis broker calistirir.
//  - Once ve sonra bekleme belleginin OLCULEN boyutu okunur. Biri okunamazsa sayi gosterilmez (tahmin yok).
//  - Fayda kanitli degildir (modern Windows bekleme bellegini kendisi yonetir); sonuc yalniz olculen farktir.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"

namespace ef::core::maintenance {

inline constexpr char kStandbyPurgeTarget[] = "maint:standby_purge";

class MemoryListApi {
public:
    virtual ~MemoryListApi() = default;
    virtual contracts::Result<std::uint64_t> standby_bytes() = 0;  // bekleme listesi toplam boyutu
    virtual contracts::Status purge_standby() = 0;
};

struct PurgeRun {
    bool ok = false;  // bosaltma cagrisi basarili dondu
    std::optional<std::uint64_t> before_bytes, after_bytes;  // yalniz okuma basarili ise dolu
    std::string error_key;                                   // ok=false iken: "purge" (cagri/yetki basarisiz)
    bool measured() const noexcept { return ok && before_bytes && after_bytes; }
};

PurgeRun run_standby_purge(MemoryListApi& api);

// Broker adim ayrintisi: "mj:purged:b=<bayt|?>:a=<bayt|?>" (basari) | "mj:failed:purge" (basarisiz).
std::string encode_purge_detail(const PurgeRun& r);
// Broker adim ayrintisinden (StepResult::detail) cozer; taninmayan/bozuk -> ok=false, sayi yok.
PurgeRun parse_purge_detail(std::string_view detail);

}  // namespace ef::core::maintenance
