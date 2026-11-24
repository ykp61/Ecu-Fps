#include "core/adapters/windows/service_start_type.h"

#include <array>
#include <cctype>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;
namespace st = core::adapters::startup;

namespace {
std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
using OptStr = std::optional<std::string>;

// Her satir Windows'un kendi hizmetidir; yalniz kullanilmayan ozelligin hizmetleri. Ses, ag, guvenlik, guncelleme, surucu,
// anti-cheat ve oyun platformu cekirdegi (GamingServices dahil) burada YOKTUR. Bagimli hizmetler ana hizmetten once gelir.
// DOGRULANMADI: gercek SCM davranisi (bagimlilik, otomatik yeniden baslatma) cihazda sinanmadi.
constexpr std::array<FeatureService, 9> kTable{{
    {"BTAGService", "plan.feature.bluetooth", "\\system32\\svchost.exe", false},
    {"bthserv", "plan.feature.bluetooth", "\\system32\\svchost.exe", false},
    {"PrintNotify", "plan.feature.print", "\\system32\\svchost.exe", false},
    {"Spooler", "plan.feature.print", "\\system32\\spoolsv.exe", true},
    {"XblGameSave", "plan.feature.xbox", "\\system32\\svchost.exe", false},
    {"XboxNetApiSvc", "plan.feature.xbox", "\\system32\\svchost.exe", false},
    {"XboxGipSvc", "plan.feature.xbox", "\\system32\\svchost.exe", false},
    {"XblAuthManager", "plan.feature.xbox", "\\system32\\svchost.exe", false},
    {"WSearch", "plan.feature.search", "\\system32\\searchindexer.exe", false},
}};

bool valid_service_name(std::string_view n) {
    if (n.empty() || n.size() > 256) return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

Status denied(std::string why) { return Status::fail(ErrorCode::Denied, std::move(why)); }

Status check_identity(const st::ServiceInfo& s) {
    const auto* entry = find_feature_service(s.name);
    if (!entry) return denied("service not in feature table");
    if (s.kernel_driver) return denied("kernel driver");
    if (!s.microsoft) return denied("not a Windows binary");
    if (lower(s.image_path).find(entry->image_part) == std::string::npos) return denied("unexpected service binary");
    return contracts::ok_status();
}
}  // namespace

std::span<const FeatureService> feature_services() noexcept { return kTable; }

const FeatureService* find_feature_service(std::string_view name) noexcept {
    const std::string n = lower(name);
    for (const auto& e : kTable)
        if (lower(e.name) == n) return &e;
    return nullptr;
}

std::string format_service_start_target(std::string_view service) { return "svcstart:" + std::string(service); }

contracts::Result<std::string> parse_service_start_target(std::string_view t) {
    using R = contracts::Result<std::string>;
    if (t.rfind("svcstart:", 0) != 0) return R::fail(ErrorCode::ParseError, "service start target");
    const auto name = t.substr(9);
    if (!valid_service_name(name)) return R::fail(ErrorCode::ParseError, "service name");
    return std::string(name);
}

Status check_feature_disable(st::ServiceApi& api, ServiceBusyProbe& probe, const st::ServiceInfo& s, bool strict_dependents) {
    if (auto id = check_identity(s); !id.ok()) return id;
    if (s.running) {
        for (const auto& dep : s.dependents) {
            auto d = api.get(dep);
            if (!d.ok()) {
                if (d.code() == ErrorCode::MissingValue) continue;  // kayitsiz bagimli calisamaz
                return denied("dependent service state unknown: " + dep);
            }
            if (!d.value().running) continue;
            if (!strict_dependents && find_feature_service(dep)) continue;  // ayni plandaki bagimli once kapatilir
            return denied("dependent service running: " + dep);
        }
        if (find_feature_service(s.name)->needs_idle_check) {
            auto b = probe.busy(s.name);
            if (!b.ok()) return denied("cannot verify service is idle: " + b.error().detail);
            if (b.value()) return denied("service has pending work");
        }
    }
    return contracts::ok_status();
}

contracts::Result<recovery::Snapshot> ServiceStartTypeAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (op.scope != contracts::OperationScope::Machine) return R::fail(ErrorCode::Denied, "service start type is machine scope");
    auto n = parse_service_start_target(op.target);
    if (!n.ok()) return R(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return R(s.error());
    if (auto c = check_feature_disable(api_, probe_, s.value(), false); !c.ok()) return R(c.error());
    // Boot/System baslangic tipi geri yazilamaz: yakalamada reddedilir (kismi geri alma olmasin).
    if (s.value().start == st::StartType::Boot || s.value().start == st::StartType::System) return R::fail(ErrorCode::Denied, "boot/system start");
    recovery::Snapshot snap;
    snap.existed = true;
    snap.old_value = std::string(st::to_string(s.value().start));  // gecikmeli otomatik ayri deger: bayrak korunur
    snap.raw = s.value().running ? "running" : "stopped";
    snap.raw_type = "service_start_type";
    return snap;
}

Status ServiceStartTypeAdapter::apply(const contracts::SettingOperation& op) {
    if (op.new_value != "disabled") return Status::fail(ErrorCode::Denied, "only 'disabled' may be applied");
    auto n = parse_service_start_target(op.target);
    if (!n.ok()) return Status(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return Status(s.error());
    // capture ile apply arasinda durum degismis olabilir: yeniden, bu kez siki denetle.
    if (auto c = check_feature_disable(api_, probe_, s.value(), true); !c.ok()) return c;
    const auto prev = s.value().start;
    const bool was_running = s.value().running;
    if (auto r = api_.set_start_type(n.value(), st::StartType::Disabled); !r.ok()) return r;
    if (was_running) {
        if (auto r = api_.stop(n.value()); !r.ok()) {
            (void)api_.set_start_type(n.value(), prev);  // yarim kalmasin: baslangic tipi geri; motor da geri alir
            return r;
        }
    }
    return contracts::ok_status();
}

contracts::Result<OptStr> ServiceStartTypeAdapter::read_back(const contracts::SettingOperation& op) {
    auto n = parse_service_start_target(op.target);
    if (!n.ok()) return contracts::Result<OptStr>(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return contracts::Result<OptStr>(s.error());
    return OptStr{std::string(st::to_string(s.value().start))};
}

Status ServiceStartTypeAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto n = parse_service_start_target(op.target);
    if (!n.ok()) return Status(n.error());
    if (!find_feature_service(n.value())) return denied("service not in feature table");
    const auto t = st::parse_start_type(snap.old_value);
    if (!t || *t == st::StartType::Boot || *t == st::StartType::System) return Status::fail(ErrorCode::ParseError, "snapshot start type");
    if (auto r = api_.set_start_type(n.value(), *t); !r.ok()) return r;
    // Yalniz onceden calisan ve su an durmus hizmet baslatilir; onceden durmus/Devre Disi olan baslatilmaz.
    auto cur = api_.get(n.value());
    if (!cur.ok()) return Status(cur.error());
    if (snap.raw == "running" && *t != st::StartType::Disabled && !cur.value().running) return api_.start(n.value());
    return contracts::ok_status();
}

bool service_start_admin_allowed(const contracts::SettingOperation& op) noexcept {
    if (op.scope != contracts::OperationScope::Machine || op.new_value != "disabled") return false;
    auto n = parse_service_start_target(op.target);
    return n.ok() && find_feature_service(n.value()) != nullptr;
}

}  // namespace ef::core::adapters::windows
