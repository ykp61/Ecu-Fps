#pragma once
// Cihaz baglami (docs/16 DeviceContext). Her alan ya kaynakli deger ya da gerekceli "bilinmiyor".

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::contracts {

enum class Provenance { None, Api, Registry, Wmi, Smbios, Derived };
std::string_view to_string(Provenance p) noexcept;

template <typename T>
struct Field {
    std::optional<T> value;
    Provenance source = Provenance::None;
    std::string reason;  // value yoksa neden (izin, desteklenmiyor, okunamadi)

    static Field known(T v, Provenance p) { return Field{std::move(v), p, {}}; }
    static Field unknown(std::string why) { return Field{std::nullopt, Provenance::None, std::move(why)}; }
    bool has() const noexcept { return value.has_value(); }
};

struct OsContext {
    Field<std::string> product_name;     // "Windows 11 Pro"
    Field<std::string> display_version;  // "26H2"
    Field<std::string> edition_id;       // "Professional"
    Field<std::uint32_t> major, minor, build, ubr;
    Field<std::string> native_arch;      // "x64", "arm64"
    Field<std::string> process_arch;
};

// Hedef kullanici secimi: islem farkli bir yonetici hesabiyla yukseltildiyse ayarlarin hedefi
// oturumdaki etkilesimli kullanicidir (ozgun oyuncu), yukselten hesap degil.
struct UserContext {
    Field<std::string> process_user_sid;   // bu islemin token kullanicisi
    Field<std::string> session_user_sid;   // oturumun etkilesimli kullanicisi
    Field<std::uint32_t> session_id;
    Field<bool> elevated;
    Field<bool> remote_session;
    Field<std::string> target_user_sid;    // turetilmis: kimin ayarlari degisecek
    Field<bool> different_admin;           // UAC baska hesapla mi onaylandi
};

struct PowerContext {
    Field<bool> on_ac;
    Field<bool> has_battery;
    Field<int> battery_percent;
    Field<bool> battery_saver;
};

enum class PolicyState { Absent, Present, Unknown };
std::string_view to_string(PolicyState s) noexcept;

struct PolicyEntry {
    std::string id;       // "windows_update", "game_dvr", "power"
    PolicyState state = PolicyState::Unknown;
    std::string reason;
};

struct PolicyContext {
    Field<bool> domain_joined;
    Field<bool> mdm_enrolled;
    std::vector<PolicyEntry> policies;
};

// Saf karar: hedef kullanici ve "farkli yonetici" tespiti. Oturum kullanicisi bilinmiyorsa
// hedef de bilinmez (yanlis kullaniciya yazmaktansa durmak).
void resolve_target_user(UserContext& u);

}  // namespace ef::contracts
