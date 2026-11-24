#pragma once
// T01: dizustu hibrit GPU icin oyun EXE'si basina Windows "Grafik tercihi" = Yuksek performans (kullanici kapsami, UAC yok).
// Hedef bicimi: "gpupref:<hex>" (hex = kanonik tam .exe yolunun utf-8 baytlari; appcompat ile ayni dogrulama ve sinirlar).
// Kayit yolu SABITTIR: HKCU\Software\Microsoft\DirectX\UserGpuPreferences ; deger adi = tam EXE yolu ; REG_SZ "GpuPreference=2;".
// Uygulama yalniz GpuPreference ogesini yazar; ayni dizedeki baska ogeler ve bilinmeyen jetonlar korunur.
// Geri alma anlik goruntudeki ham deger (tur + bayt) ya da yoklugudur (deger onceden yoksa silinir).
// Guvenlik: YAZMA yalniz kayitli kurulum klasorlerinin altindaki EXE'ler icin (appcompat ile ayni kok kaynagi); geri alma kok istemez.
// Gercek kayit yazimi bu PC'de DOGRULANMADI; birim testleri sahte kayit kullanir.

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/adapters/windows/gaming_toggles.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

inline constexpr std::string_view kGpuPrefPrefix = "gpupref:";
inline constexpr std::wstring_view kGpuPrefSubkey = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
inline constexpr std::wstring_view kGpuPrefKeyName = L"GpuPreference";

std::string gpu_pref_target_for_exe(std::string_view exe_utf8);
std::optional<std::wstring> gpu_pref_exe_from_target(std::string_view target);

class GpuPreferenceAdapter final : public recovery::SettingAdapter {
public:
    explicit GpuPreferenceAdapter(UserRegistryApi& api) : api_(api) {}
    void set_allowed_roots(std::vector<std::string> roots);
    // Plan filtresi: bu EXE yazma kokleri altinda mi (apply ile ayni kural).
    bool exe_writable(std::string_view exe_utf8) const;

    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    bool under_allowed_root(std::string_view canonical_target) const;

    UserRegistryApi& api_;
    mutable std::mutex roots_mu_;
    std::vector<std::string> roots_;
};

}  // namespace ef::core::adapters::windows
