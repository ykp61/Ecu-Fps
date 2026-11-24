#include "core/adapters/windows/appx_remove.h"

#include <algorithm>
#include <array>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;

namespace {
using OptStr = std::optional<std::string>;

// Aile adlari Windows'un kendi Deprovisioned listesinde (bu cihazda salt okuma) gorulen bicimdedir; ZuneVideo ayni yayinci kimligini tasir.
constexpr std::array<AppxPackage, 6> kPackages{{
    {"Clipchamp.Clipchamp_yxz26nhyzhsrt", "Clipchamp"},
    {"Microsoft.ZuneMusic_8wekyb3d8bbwe", "Media Player / Groove Music"},
    {"Microsoft.ZuneVideo_8wekyb3d8bbwe", "Movies & TV"},
    {"Microsoft.MicrosoftSolitaireCollection_8wekyb3d8bbwe", "Microsoft Solitaire Collection"},
    {"Microsoft.BingNews_8wekyb3d8bbwe", "Microsoft News"},
    {"Microsoft.BingWeather_8wekyb3d8bbwe", "MSN Weather"},
}};

Status denied(std::string why) { return Status::fail(ErrorCode::Denied, std::move(why)); }

bool family_chars_ok(std::string_view f) noexcept {
    if (f.empty() || f.size() > 96) return false;
    return std::all_of(f.begin(), f.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

// Hedefi cozer: yalniz CurrentUser kapsam + listedeki aile.
std::optional<std::string> family_of(const contracts::SettingOperation& op) {
    if (op.id != contracts::OperationId::AppxPackageRemove || op.scope != contracts::OperationScope::CurrentUser) return std::nullopt;
    return appx_family_from_target(op.target);
}
}  // namespace

std::span<const AppxPackage> appx_packages() noexcept { return std::span<const AppxPackage>(kPackages.data(), kPackages.size()); }

const AppxPackage* find_appx_package(std::string_view family) noexcept {
    for (const auto& p : kPackages)
        if (p.family == family) return &p;  // buyuk/kucuk harf duyarli: aile adi tek gosterim
    return nullptr;
}

std::string appx_target_for_family(std::string_view family) {
    if (!family_chars_ok(family) || !find_appx_package(family)) return {};
    return std::string(kAppxTargetPrefix) + std::string(family);
}

std::optional<std::string> appx_family_from_target(std::string_view target) {
    if (target.size() <= kAppxTargetPrefix.size() || target.substr(0, kAppxTargetPrefix.size()) != kAppxTargetPrefix) return std::nullopt;
    const auto f = target.substr(kAppxTargetPrefix.size());
    if (!family_chars_ok(f) || !find_appx_package(f)) return std::nullopt;
    return std::string(f);
}

contracts::Result<recovery::Snapshot> AppxRemoveAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    const auto fam = family_of(op);
    if (!fam) return R::fail(ErrorCode::Denied, "appx target not allowed");
    auto rest = api_.restorable(*fam);
    if (!rest.ok()) return R(rest.error());
    if (!rest.value()) return R::fail(ErrorCode::Denied, "package is not provisioned: re-register cannot be guaranteed");
    auto cur = api_.installed_full_name(*fam);
    if (!cur.ok()) return R(cur.error());
    if (!cur.value()) return R::fail(ErrorCode::Denied, "package is not installed for this user");
    recovery::Snapshot snap;
    snap.existed = true;
    snap.old_value = std::string(kAppxInstalledValue);
    snap.raw_type = "appx_package";
    snap.raw = *cur.value();  // yakalanan tam ad (kayit/gecmis icin; geri alma aile adiyla yapilir)
    return snap;
}

Status AppxRemoveAdapter::apply(const contracts::SettingOperation& op) {
    if (op.new_value != kAppxRemovedValue) return denied("only 'removed' may be applied");
    const auto fam = family_of(op);
    if (!fam) return denied("appx target not allowed");
    auto rest = api_.restorable(*fam);  // capture ile apply arasinda durum degismis olabilir
    if (!rest.ok()) return Status(rest.error());
    if (!rest.value()) return denied("package is not provisioned: re-register cannot be guaranteed");
    auto cur = api_.installed_full_name(*fam);
    if (!cur.ok()) return Status(cur.error());
    if (!cur.value()) return denied("package is not installed for this user");
    return api_.remove_for_user(*cur.value());
}

contracts::Result<OptStr> AppxRemoveAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<OptStr>;
    const auto fam = family_of(op);
    if (!fam) return R::fail(ErrorCode::ParseError, "appx target");
    auto cur = api_.installed_full_name(*fam);
    if (!cur.ok()) return R(cur.error());
    return OptStr{std::string(cur.value() ? kAppxInstalledValue : kAppxRemovedValue)};
}

Status AppxRemoveAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    const auto fam = family_of(op);
    if (!fam) return Status::fail(ErrorCode::ParseError, "appx target");
    if (!snap.existed) return contracts::ok_status();  // onceden kurulu degildi: geri alinacak bir sey yok
    auto cur = api_.installed_full_name(*fam);
    if (!cur.ok()) return Status(cur.error());
    if (cur.value()) return contracts::ok_status();  // zaten kurulu
    if (auto r = api_.register_by_family(*fam); !r.ok()) return r;
    auto after = api_.installed_full_name(*fam);  // kayit "basarili" dese de kurulu oldugu dogrulanir
    if (!after.ok()) return Status(after.error());
    if (!after.value()) return Status::fail(ErrorCode::IoError, "package is still not installed after re-register");
    return contracts::ok_status();
}

contracts::Result<OptStr> FakeAppxApi::installed_full_name(const std::string& family) {
    using R = contracts::Result<OptStr>;
    if (fail_read) return R::fail(ErrorCode::IoError, "fake read failure");
    for (const auto& p : installed)
        if (p.first == family) return OptStr{p.second};
    return OptStr{};
}

contracts::Result<bool> FakeAppxApi::restorable(const std::string& family) {
    if (fail_restorable) return contracts::Result<bool>::fail(ErrorCode::IoError, "fake provisioned read failure");
    return std::find(provisioned.begin(), provisioned.end(), family) != provisioned.end();
}

Status FakeAppxApi::remove_for_user(const std::string& full_name) {
    if (fail_remove) return Status::fail(ErrorCode::IoError, "fake remove failure");
    ++removes;
    installed.erase(std::remove_if(installed.begin(), installed.end(), [&](const auto& p) { return p.second == full_name; }), installed.end());
    return contracts::ok_status();
}

Status FakeAppxApi::register_by_family(const std::string& family) {
    if (fail_register) return Status::fail(ErrorCode::IoError, "fake register failure");
    ++registers;
    if (!register_is_noop) installed.emplace_back(family, family + "_1.0.0.0_x64__reg");
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::windows
