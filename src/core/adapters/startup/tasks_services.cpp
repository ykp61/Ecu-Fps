#include "core/adapters/startup/tasks_services.h"

#include <algorithm>
#include <cctype>

#include "core/adapters/startup/startup.h"

namespace ef::core::adapters::startup {

using contracts::ErrorCode;
using contracts::Status;

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
using OptStr = std::optional<std::string>;

std::string hex_encode(std::string_view s) {
    static constexpr char hx[] = "0123456789abcdef";
    std::string h;
    for (unsigned char c : s) {
        h += hx[c >> 4];
        h += hx[c & 15];
    }
    return h;
}
contracts::Result<std::string> hex_decode(std::string_view h) {
    if (h.empty() || h.size() % 2 || h.size() > 1024) return contracts::Result<std::string>::fail(ErrorCode::ParseError, "hex");
    std::string out;
    for (std::size_t i = 0; i < h.size(); i += 2) {
        auto n = [](char x) { return (x >= '0' && x <= '9') ? x - '0' : (x >= 'a' && x <= 'f') ? x - 'a' + 10 : -1; };
        const int a = n(h[i]), b = n(h[i + 1]);
        if (a < 0 || b < 0) return contracts::Result<std::string>::fail(ErrorCode::ParseError, "hex");
        out += static_cast<char>((a << 4) | b);
    }
    return out;
}

contracts::Result<std::string> task_path(std::string_view t) {
    if (t.rfind("task:", 0) != 0) return contracts::Result<std::string>::fail(ErrorCode::ParseError, "task target");
    auto p = hex_decode(t.substr(5));
    if (!p.ok() || p.value().empty() || p.value().front() != '\\' || p.value().find("..") != std::string::npos)
        return contracts::Result<std::string>::fail(ErrorCode::ParseError, "task path");
    return p;
}
}  // namespace

// ---------------- Gorevler ----------------
bool task_is_eligible(const TaskInfo& t, std::string& why) {
    const std::string p = lower(t.path);
    if (p.rfind("\\microsoft\\", 0) == 0 || p.find("agent activation runtime") != std::string::npos || p.find("\\s-1-5-") != std::string::npos) {
        why = "Windows system task";
        return false;
    }
    // Donanim/surucu bilesenleri (ornek: AMD X3D V-Cache surucu kurucusu), anti-cheat
    static constexpr std::string_view kProtected[] = {"amd", "nvidia", "intel", "realtek", "x3d", "vanguard", "anticheat",
                                                      "battleye", "easyanticheat", "faceit", "driver"};
    for (auto k : kProtected)
        if (p.find(k) != std::string::npos) {
            why = "hardware/driver or anti-cheat task";
            return false;
        }
    if (p.rfind("\\nv", 0) == 0) {  // NVIDIA gorevleri: NvTmRep*, NvDriverUpdateCheck*, NvProfileUpdater*
        why = "hardware/driver task";
        return false;
    }
    if (!t.logon_trigger) {
        why = "not a logon/startup task";
        return false;
    }
    return true;
}

contracts::Result<recovery::Snapshot> TaskAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto p = task_path(op.target);
    if (!p.ok()) return R(p.error());
    auto t = api_.get(p.value());
    if (!t.ok()) return R(t.error());
    std::string why;
    if (!task_is_eligible(t.value(), why)) return R::fail(ErrorCode::Denied, why);
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = t.value().enabled ? "enabled" : "disabled";
    s.raw_type = "task_enabled";
    return s;
}

Status TaskAdapter::apply(const contracts::SettingOperation& op) {
    auto p = task_path(op.target);
    if (!p.ok()) return Status(p.error());
    if (op.new_value != "enabled" && op.new_value != "disabled") return Status::fail(ErrorCode::ParseError, "value");
    return api_.set_enabled(p.value(), op.new_value == "enabled");  // gorev silinmez/yeniden yaratilmaz
}

contracts::Result<OptStr> TaskAdapter::read_back(const contracts::SettingOperation& op) {
    auto p = task_path(op.target);
    if (!p.ok()) return contracts::Result<OptStr>(p.error());
    auto t = api_.get(p.value());
    if (!t.ok()) return contracts::Result<OptStr>(t.error());
    return OptStr{t.value().enabled ? "enabled" : "disabled"};
}

Status TaskAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto p = task_path(op.target);
    if (!p.ok()) return Status(p.error());
    return api_.set_enabled(p.value(), snap.old_value == "enabled");
}

// ---------------- Servisler ----------------
std::string_view to_string(StartType s) noexcept {
    switch (s) {
        case StartType::Boot: return "boot";
        case StartType::System: return "system";
        case StartType::Automatic: return "automatic";
        case StartType::AutomaticDelayed: return "automatic_delayed";
        case StartType::Manual: return "manual";
        case StartType::Disabled: return "disabled";
    }
    return "?";
}

std::optional<StartType> parse_start_type(std::string_view s) noexcept {
    for (auto t : {StartType::Boot, StartType::System, StartType::Automatic, StartType::AutomaticDelayed, StartType::Manual, StartType::Disabled})
        if (to_string(t) == s) return t;
    return std::nullopt;
}

bool service_is_eligible(const ServiceInfo& s, std::string& why) {
    // Kesin yasak: ses, ag, guvenlik, termal/guc, girdi, GPU suruculeri, anti-cheat, guncelleme/kurtarma.
    static constexpr std::string_view kDenyNames[] = {
        "audiosrv", "audioendpointbuilder", "dhcp", "dnscache", "nsi", "wlansvc", "netprofm", "nlasvc", "lanmanworkstation",
        "bfe", "mpssvc", "windefend", "sense", "wscsvc", "securityhealthservice", "eventlog", "rpcss", "power", "plugplay",
        "wuauserv", "trustedinstaller", "cryptsvc", "vgc", "vgk", "easyanticheat", "easyanticheat_eos", "beservice",
        "faceit", "faceitservice", "esportal", "nvdisplay.containerlocalsystem", "amd external events utility",
        "amdfendr", "intelaudioservice", "rtkaudiouniversalservice", "realtekaudio", "lghub_updater", "ipfsvc", "dptftcs",
        // Oyun platformlari: kapatilirsa oyunlar acilmaz/guncellenmez
        "gamingservices", "gamingservicesnet", "xboxservice", "xblauthmanager", "xblgamesave", "xboxnetapisvc", "xboxgipsvc",
        "gameinputredistservice", "gameinputsvc", "epiconlineservices", "steam client service", "eaanticheatservice"};
    static constexpr std::string_view kDenyPathParts[] = {
        "\\nvidia corporation\\", "\\amd\\", "\\intel\\", "anticheat", "anti-cheat", "vanguard", "battleye", "faceit", "audio", "thermal",
        // Anakart/fan/RGB/hiz asirtma araclari (termal kategori)
        "easytune", "\\gigabyte\\", "armoury", "\\asus\\", "\\msi\\", "dragon center", "icue", "\\corsair\\", "nzxt", "fancontrol",
        "\\windowsapps\\microsoft.gaming", "\\windowsapps\\microsoft.xbox"};
    const std::string n = lower(s.name), p = lower(s.image_path), d = lower(s.display_name);
    for (auto x : kDenyNames)
        if (n == x) {
            why = "protected service (audio/network/security/power/anti-cheat/GPU)";
            return false;
        }
    for (auto x : kDenyPathParts)
        if (p.find(x) != std::string::npos || d.find(x) != std::string::npos) {
            why = "protected service by vendor/category";
            return false;
        }
    if (s.kernel_driver) { why = "kernel driver"; return false; }
    if (s.microsoft) { why = "Windows service"; return false; }
    if (!s.own_process) { why = "shared-process (svchost) service"; return false; }
    if (!s.dependents.empty()) { why = "other services depend on it"; return false; }
    if (s.start == StartType::Boot || s.start == StartType::System) { why = "boot/system start"; return false; }
    return true;
}

namespace {
contracts::Result<std::string> svc_name(std::string_view t) {
    using R = contracts::Result<std::string>;
    if (t.rfind("svc:", 0) != 0 || t.size() < 11 || t.substr(t.size() - 6) != ":start") return R::fail(ErrorCode::ParseError, "service target");
    const auto name = t.substr(4, t.size() - 10);
    if (name.empty() || name.size() > 256) return R::fail(ErrorCode::ParseError, "service name");
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return R::fail(ErrorCode::ParseError, "service name");
    return std::string(name);
}
}  // namespace

contracts::Result<recovery::Snapshot> ServiceStartAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto n = svc_name(op.target);
    if (!n.ok()) return R(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return R(s.error());
    std::string why;
    if (!service_is_eligible(s.value(), why)) return R::fail(ErrorCode::Denied, why);
    recovery::Snapshot snap;
    snap.existed = true;
    snap.old_value = std::string(to_string(s.value().start));
    snap.raw = s.value().running ? "running" : "stopped";  // calisma durumu ayri tutulur
    snap.raw_type = "service_start";
    return snap;
}

Status ServiceStartAdapter::apply(const contracts::SettingOperation& op) {
    auto n = svc_name(op.target);
    if (!n.ok()) return Status(n.error());
    const auto t = parse_start_type(op.new_value);
    if (!t || (*t != StartType::Manual && *t != StartType::Disabled)) return Status::fail(ErrorCode::Denied, "only manual/disabled may be set");
    return api_.set_start_type(n.value(), *t);  // calisan servis durdurulmaz
}

contracts::Result<OptStr> ServiceStartAdapter::read_back(const contracts::SettingOperation& op) {
    auto n = svc_name(op.target);
    if (!n.ok()) return contracts::Result<OptStr>(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return contracts::Result<OptStr>(s.error());
    return OptStr{std::string(to_string(s.value().start))};
}

Status ServiceStartAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto n = svc_name(op.target);
    if (!n.ok()) return Status(n.error());
    const auto t = parse_start_type(snap.old_value);
    if (!t) return Status::fail(ErrorCode::ParseError, "snapshot start type");
    if (auto st = api_.set_start_type(n.value(), *t); !st.ok()) return st;
    // Yalniz onceden calisan ve su an durmus servis baslatilir; durmus olan baslatilmaz.
    auto cur = api_.get(n.value());
    if (snap.raw == "running" && cur.ok() && !cur.value().running) return api_.start(n.value());
    return contracts::ok_status();
}

contracts::Capability packaged_startup_capability() {
    contracts::Capability c;
    c.feature_id = "B06";
    c.device_scope = "user";
    c.state = contracts::SupportState::Unsupported;
    c.provider = "Windows.ApplicationModel.StartupTask";
    c.reason = "StartupTask can only be controlled by its own package; third-party control is not supported";
    return c;
}

// ---------------- Fakes ----------------
contracts::Result<std::vector<TaskInfo>> FakeTaskApi::list() {
    std::vector<TaskInfo> v;
    for (auto& [k, t] : tasks) v.push_back(t);
    return v;
}
contracts::Result<TaskInfo> FakeTaskApi::get(const std::string& p) {
    const auto it = tasks.find(p);
    if (it == tasks.end()) return contracts::Result<TaskInfo>::fail(ErrorCode::MissingValue, "task");
    return it->second;
}
Status FakeTaskApi::set_enabled(const std::string& p, bool e) {
    if (deny) return Status::fail(ErrorCode::Denied, "task scheduler refused");
    tasks.at(p).enabled = e;
    return contracts::ok_status();
}
contracts::Result<std::vector<ServiceInfo>> FakeServiceApi::list() {
    std::vector<ServiceInfo> v;
    for (auto& [k, s] : services) v.push_back(s);
    return v;
}
contracts::Result<ServiceInfo> FakeServiceApi::get(const std::string& n) {
    const auto it = services.find(n);
    if (it == services.end()) return contracts::Result<ServiceInfo>::fail(ErrorCode::MissingValue, "service");
    return it->second;
}
Status FakeServiceApi::set_start_type(const std::string& n, StartType t) {
    if (deny) return Status::fail(ErrorCode::Denied, "SCM access denied");
    services.at(n).start = t;
    return contracts::ok_status();
}
Status FakeServiceApi::start(const std::string& n) {
    if (deny) return Status::fail(ErrorCode::Denied, "SCM access denied");
    ++starts;
    services.at(n).running = true;
    return contracts::ok_status();
}
Status FakeServiceApi::stop(const std::string& n) {
    if (deny) return Status::fail(ErrorCode::Denied, "SCM access denied");
    ++stops;
    services.at(n).running = false;
    return contracts::ok_status();
}

}  // namespace ef::core::adapters::startup
