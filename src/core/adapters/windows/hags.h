#pragma once
// W06 / OPT-10 (plan v3.5): Donanim hizlandirmali GPU zamanlamasi (HAGS) tercihi.
// Tek SABIT hedef: HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers  HwSchMode (REG_DWORD)
//   2 = acik, 1 = kapali (Windows Ayarlar > Grafik sayfasinin yazdigi deger; ADR-022/ADR-023).
// Yazma yalniz yukseltilmis broker'da (makine kapsami, izin listesi). Etkin durum yeniden baslatmadan
// sonra degisir: uygulama/geri alma sonucu "yeniden baslatma bekliyor" (pending_reboot) olarak raporlanir.
// Geri alma: anlik goruntu degeri aynen yazilir; onceden yoksa deger SILINIR (varsayilan uydurulmaz).
// Arayuz yol/ad almaz: platform uygulamasi yalniz bu tek degere dokunabilir (serbest kayit yolu yok).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kHagsTarget = "hags:hwschmode";
inline constexpr std::uint32_t kHagsOn = 2;
inline constexpr std::uint32_t kHagsOff = 1;

class HagsApi {
public:
    virtual ~HagsApi() = default;
    // HwSchMode degeri; yoksa nullopt. DWORD disinda bir tur bulunursa hata (dokunulmaz).
    virtual contracts::Result<std::optional<std::uint32_t>> read_pref() = 0;
    virtual contracts::Status write_pref(std::uint32_t v) = 0;
    virtual contracts::Status remove_pref() = 0;
    // GPU/surucu HAGS destekliyor mu (D3DKMT WDDM 2.7 caps). Bilinmiyorsa nullopt.
    virtual std::optional<bool> supported() = 0;
};

// Islem: OperationId::HagsSet, scope Machine, target kHagsTarget, value "1" | "2".
class HagsAdapter final : public recovery::SettingAdapter {
public:
    explicit HagsAdapter(HagsApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

    HagsApi& api() noexcept { return api_; }
    static bool valid_value(std::string_view v) noexcept { return v == "1" || v == "2"; }

private:
    HagsApi& api_;
};

class FakeHags final : public HagsApi {
public:
    contracts::Result<std::optional<std::uint32_t>> read_pref() override;
    contracts::Status write_pref(std::uint32_t v) override;
    contracts::Status remove_pref() override;
    std::optional<bool> supported() override { return is_supported; }

    std::optional<std::uint32_t> value;
    std::optional<bool> is_supported = true;
    bool fail_write = false;
    int writes = 0, removes = 0;
};

}  // namespace ef::core::adapters::windows
