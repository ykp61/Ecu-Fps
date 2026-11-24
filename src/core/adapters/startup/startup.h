#pragma once
// WIN-03: B01 kullanici Run, B02 makine Run (64/32 bit gorunum), B03 Startup klasoru.
// Yontem: Gorev Yoneticisi'nin kullandigi StartupApproved degeri (02 = acik, 03 = kapali + FILETIME).
// Run kayitlari ve kisayol dosyalari SILINMEZ/TASINMAZ; RunOnce kapsam disi; calisan uygulama oldurulmez.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::startup {

enum class Location { UserRun, MachineRun64, MachineRun32, UserFolder, CommonFolder };
std::string_view to_string(Location l) noexcept;
std::optional<Location> parse_location(std::string_view s) noexcept;
bool requires_admin(Location l) noexcept;

enum class Approval { Enabled, Disabled };

struct Entry {
    Location location = Location::UserRun;
    std::string name;     // UTF-8 deger adi veya kisayol dosya adi
    std::string command;  // bilgi (Run komutu / kisayol yolu)
    Approval approval = Approval::Enabled;
};

// StartupApproved ham degeri: 12 bayt. Ilk bayt tek ise kapali (03/07), cift ise acik (02/06).
Approval approval_from_raw(std::string_view raw_hex) noexcept;
std::string make_raw(Approval a, std::uint64_t filetime);  // 24 hex karakter

// Hedef: "startup:<location>:<hex(UTF-8 ad)>"; ad Unicode/bosluk icerebildigi icin hex kodlanir.
std::string format_target(Location l, std::string_view name);
struct Target {
    Location location;
    std::string name;
};
contracts::Result<Target> parse_target(std::string_view s);

class StartupApi {
public:
    virtual ~StartupApi() = default;
    virtual contracts::Result<std::vector<Entry>> list(Location l) = 0;
    virtual contracts::Result<bool> exists(Location l, const std::string& name) = 0;
    // StartupApproved ham degeri (hex); deger yoksa nullopt (= acik kabul edilir).
    virtual contracts::Result<std::optional<std::string>> get_raw(Location l, const std::string& name) = 0;
    virtual contracts::Status set_raw(Location l, const std::string& name, const std::string& raw_hex) = 0;
    virtual contracts::Status remove_raw(Location l, const std::string& name) = 0;
    virtual std::uint64_t now_filetime() = 0;
};

// Deger: "enabled" / "disabled"
class StartupAdapter final : public recovery::SettingAdapter {
public:
    explicit StartupAdapter(StartupApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    // Anlamsal durum: StartupApproved yoksa nullopt (snapshot existed=false ile tutarli).
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    StartupApi& api_;
};

class FakeStartupApi final : public StartupApi {
public:
    contracts::Result<std::vector<Entry>> list(Location l) override;
    contracts::Result<bool> exists(Location l, const std::string& name) override;
    contracts::Result<std::optional<std::string>> get_raw(Location l, const std::string& name) override;
    contracts::Status set_raw(Location l, const std::string& name, const std::string& raw) override;
    contracts::Status remove_raw(Location l, const std::string& name) override;
    std::uint64_t now_filetime() override { return 0x01DB000000000000ull; }
    std::map<std::pair<int, std::string>, std::string> entries;   // (konum, ad) -> komut
    std::map<std::pair<int, std::string>, std::string> approved;  // (konum, ad) -> ham hex
    bool elevated = false;
};

}  // namespace ef::core::adapters::startup
