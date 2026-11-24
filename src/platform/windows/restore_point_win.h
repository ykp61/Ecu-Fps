#pragma once
// EF-X16: SRSetRestorePointW + root\default:SystemRestore envanteri. Yalniz yukseltilmis broker cagirir.
#include <optional>

#include "core/recovery/restore_point.h"

namespace ef::platform {

// T08 (salt okuma, UAC yok): Sistem Geri Yukleme ilke ile KAPATILMISSA false; aksi halde bilinmiyor (nullopt). Kesin "acik"
// iddia edilmez ve yonetici gerektiren WMI sorgusu yapilmaz; kapali ise broker zaten nokta olusturmaz/acmaz (ProtectionOff).
std::optional<bool> system_restore_enabled();

class WinRestorePointApi final : public core::recovery::RestorePointApi {
public:
    core::recovery::RpCall begin(const std::string& description) override;
    std::uint32_t end(std::int64_t seq) override;
    std::uint32_t cancel(std::int64_t seq) override;
    std::optional<std::vector<core::recovery::RpItem>> inventory() override;
};

}  // namespace ef::platform
