#pragma once
// F-48 (windows_write): Hizli baslatma (Fast Startup) tercihi.
// Tek SABIT hedef: HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Power  HiberbootEnabled (REG_DWORD)
//   1 = acik, 0 = kapali (Windows'un yazdigi deger).
// Yazma yalniz yukseltilmis broker'da (makine kapsami, izin listesi). Deger bir sonraki KAPANISIN davranisini
// degistirir; yeniden baslatma gerekmez, bu yuzden op.needs_reboot = false.
// Geri alma: anlik goruntu degeri aynen yazilir; onceden yoksa deger SILINIR (varsayilan uydurulmaz).
// Hiberfil.sys / powercfg /h silme bu adaptorun kapsaminda DEGILDIR: sistem dosyasi ve hazirda bekletme
// davranisini degistirir; sessiz disk temizligi yapilmaz.
// Arayuz yol/ad almaz: platform uygulamasi yalniz bu tek deger dokunabilir (serbest kayit yolu yok).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kFastStartupTarget = "faststartup:hiberboot";

class FastStartupApi {
public:
    virtual ~FastStartupApi() = default;
    // HiberbootEnabled degeri; yoksa nullopt. DWORD disinda bir tur bulunursa hata (dokunulmaz).
    virtual contracts::Result<std::optional<std::uint32_t>> read_pref() = 0;
    virtual contracts::Status write_pref(std::uint32_t v) = 0;
    virtual contracts::Status remove_pref() = 0;
};

// Islem: OperationId::FastStartupSet, scope Machine, target kFastStartupTarget, value "0" | "1".
// Uygulama yolu (broker izin listesi) yalniz "0" (kapat) gonderir; "1" kod korunur ve geri okuma icindir.
class FastStartupAdapter final : public recovery::SettingAdapter {
public:
    explicit FastStartupAdapter(FastStartupApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

    FastStartupApi& api() noexcept { return api_; }
    static bool valid_value(std::string_view v) noexcept { return v == "0" || v == "1"; }

private:
    FastStartupApi& api_;
};

class FakeFastStartup final : public FastStartupApi {
public:
    contracts::Result<std::optional<std::uint32_t>> read_pref() override;
    contracts::Status write_pref(std::uint32_t v) override;
    contracts::Status remove_pref() override;

    std::optional<std::uint32_t> value;
    bool fail_write = false;
    int writes = 0, removes = 0;
};

}  // namespace ef::core::adapters::windows
