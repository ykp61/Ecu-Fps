#include "core/adapters/windows/background.h"

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

// Kesin izin listesi. Her satirin gerekcesi ve resmi kaynagi research/background_services.md icindedir.
// Liste DISI birakilanlar (ayni belgede): WSearch (test edilmeden kapatilmamali), SysMain ve DiagTrack
// (Microsoft "No guidance"), WerSvc ve TabletInputService ("Do not disable"), TextInputManagementService
// (resmi aciklama yok + klavye/IME girdisi). Guvenlik, ag, ses, surucu, anti-cheat ve guncelleme
// hizmetleri hicbir kosulda eklenmez.
constexpr std::array<SessionStoppableService, 5> kAllow{{
    // "OK to disable if not a print server or a DC"; is varsa veya yazici paylasiliyorsa durdurulmaz.
    {"Spooler", "\\system32\\spoolsv.exe", "background.breaks.printing", "S99#print-spooler", true},
    // Faks; Spooler'a bagimlidir. Gonderim/alim isi sirasinda durdurulmaz.
    {"Fax", "\\system32\\fxssvc.exe", "background.breaks.fax", "S101#fax-service", true},
    // "OK to disable": indirilmis haritalara erisen uygulamalar oturum boyunca harita acamaz.
    {"MapsBroker", "\\system32\\svchost.exe", "background.breaks.offline_maps", "S99#downloaded-maps-manager", false},
    // "OK to disable": oturum boyunca yeni calistirilan programlar icin uyumluluk uyarisi gosterilmez.
    {"PcaSvc", "\\system32\\svchost.exe", "background.breaks.compat_assistant", "S99#program-compatibility-assistant-service", false},
    // "OK to disable": tarayici ve fotograf makinesi (WIA) aktarimi calismaz; web kamerasi WIA kullanmaz.
    {"stisvc", "\\system32\\svchost.exe", "background.breaks.scanner", "S99#windows-image-acquisition-wia", false},
}};

bool valid_service_name(std::string_view n) {
    if (n.empty() || n.size() > 256) return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

Status denied(std::string why) { return Status::fail(ErrorCode::Denied, std::move(why)); }

// Hizmetin kendisi izin listesi ve kimlik kosullarini sagliyor mu (calisma durumundan bagimsiz).
Status check_identity(const st::ServiceInfo& s) {
    const auto* entry = find_session_stoppable(s.name);
    if (!entry) return denied("service not in session-stop allow-list");
    if (s.kernel_driver) return denied("kernel driver");
    if (!s.microsoft) return denied("not a Windows binary");
    if (lower(s.image_path).find(entry->image_part) == std::string::npos) return denied("unexpected service binary");
    return contracts::ok_status();
}
}  // namespace

std::span<const SessionStoppableService> session_stoppable_services() noexcept { return kAllow; }

const SessionStoppableService* find_session_stoppable(std::string_view name) noexcept {
    const std::string n = lower(name);
    for (const auto& e : kAllow)
        if (lower(e.name) == n) return &e;
    return nullptr;
}

Status check_session_stop(st::ServiceApi& api, ServiceBusyProbe& probe, const st::ServiceInfo& s) {
    if (auto id = check_identity(s); !id.ok()) return id;
    // Calisan bagimli hizmet varsa SCM onu da durdurmamizi ister; zincirleme durdurma yapilmaz.
    for (const auto& dep : s.dependents) {
        auto d = api.get(dep);
        if (!d.ok()) return denied("dependent service state unknown: " + dep);
        if (d.value().running) return denied("dependent service running: " + dep);
    }
    if (find_session_stoppable(s.name)->needs_idle_check) {
        auto b = probe.busy(s.name);
        if (!b.ok()) return denied("cannot verify service is idle: " + b.error().detail);
        if (b.value()) return denied("service has pending work");
    }
    return contracts::ok_status();
}

std::string format_session_stop_target(std::string_view service) { return "svcstop:" + std::string(service); }

contracts::Result<std::string> parse_session_stop_target(std::string_view t) {
    using R = contracts::Result<std::string>;
    if (t.rfind("svcstop:", 0) != 0) return R::fail(ErrorCode::ParseError, "session stop target");
    const auto name = t.substr(8);
    if (!valid_service_name(name)) return R::fail(ErrorCode::ParseError, "service name");
    return std::string(name);
}

contracts::Result<recovery::Snapshot> ServiceSessionStopAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    if (op.scope != contracts::OperationScope::Machine) return R::fail(ErrorCode::Denied, "service stop is machine scope");
    auto n = parse_session_stop_target(op.target);
    if (!n.ok()) return R(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return R(s.error());
    if (auto id = check_identity(s.value()); !id.ok()) return R(id.error());
    // Zaten durmus hizmet: no-op; bagimli/is denetimi gereksiz (hicbir sey durdurulmayacak).
    if (s.value().running)
        if (auto c = check_session_stop(api_, probe_, s.value()); !c.ok()) return R(c.error());
    recovery::Snapshot snap;
    snap.existed = true;
    snap.old_value = s.value().running ? "running" : "stopped";
    snap.raw = std::string(st::to_string(s.value().start));  // bilgi: baslangic tipi degismez
    snap.raw_type = "service_run_state";
    return snap;
}

Status ServiceSessionStopAdapter::apply(const contracts::SettingOperation& op) {
    if (op.new_value != "stopped") return Status::fail(ErrorCode::Denied, "only 'stopped' may be applied");
    auto n = parse_session_stop_target(op.target);
    if (!n.ok()) return Status(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return Status(s.error());
    if (!s.value().running) return contracts::ok_status();  // zaten durmus: no-op
    // capture ile apply arasinda durum degismis olabilir (yeni yazdirma isi, bagimli baslatildi): yeniden denetle.
    if (auto c = check_session_stop(api_, probe_, s.value()); !c.ok()) return c;
    return api_.stop(n.value());
}

contracts::Result<OptStr> ServiceSessionStopAdapter::read_back(const contracts::SettingOperation& op) {
    auto n = parse_session_stop_target(op.target);
    if (!n.ok()) return contracts::Result<OptStr>(n.error());
    auto s = api_.get(n.value());
    if (!s.ok()) return contracts::Result<OptStr>(s.error());
    return OptStr{s.value().running ? "running" : "stopped"};
}

Status ServiceSessionStopAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto n = parse_session_stop_target(op.target);
    if (!n.ok()) return Status(n.error());
    if (snap.old_value != "running" && snap.old_value != "stopped") return Status::fail(ErrorCode::ParseError, "snapshot run state");
    auto cur = api_.get(n.value());
    if (!cur.ok()) return Status(cur.error());
    // Yalniz onceden calisan ve su an durmus hizmet baslatilir; onceden durmus olan durdurulmaz/baslatilmaz.
    if (snap.old_value == "running" && !cur.value().running) return api_.start(n.value());
    return contracts::ok_status();
}

contracts::Capability session_stop_capability(bool elevated) {
    contracts::Capability c;
    c.feature_id = "B07";
    c.device_scope = "system";
    c.provider = "SCM ControlService/StartService";
    c.required = kSessionStopPrivilege;
    if (elevated) {
        c.state = contracts::SupportState::Supported;
    } else {
        c.state = contracts::SupportState::Denied;
        c.reason = "stopping system services requires administrator rights (broker role)";
    }
    return c;
}

contracts::Result<bool> FakeServiceBusyProbe::busy(const std::string& service) {
    ++calls;
    if (fail) return contracts::Result<bool>::fail(ErrorCode::IoError, "probe failed");
    const auto it = busy_by_service.find(service);
    return it != busy_by_service.end() && it->second;
}

}  // namespace ef::core::adapters::windows
