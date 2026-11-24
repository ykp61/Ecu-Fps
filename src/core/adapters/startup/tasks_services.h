#pragma once
// WIN-04: B04 oturum acma zamanlanmis gorevleri (yalniz Enabled), B05 ucuncu taraf servis baslangici,
// B06 paketli uygulama siniri. Kritik ses/ag/termal/guvenlik/anti-cheat/surucu servisleri ve
// \Microsoft\ gorevleri liste disidir. Servis baslangic tipi ile calisma durumu AYRI geri yuklenir.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "contracts/support.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::startup {

// ---------------- Gorevler ----------------
struct TaskInfo {
    std::string path;            // "\Vendor\Updater"
    bool enabled = true;
    bool logon_trigger = false;  // en az bir oturum acma/baslangic tetikleyicisi
};
// Yalniz oturum acma tetikleyicili, \Microsoft\ disindaki gorevler degistirilebilir.
bool task_is_eligible(const TaskInfo& t, std::string& why);

class TaskApi {
public:
    virtual ~TaskApi() = default;
    virtual contracts::Result<std::vector<TaskInfo>> list() = 0;
    virtual contracts::Result<TaskInfo> get(const std::string& path) = 0;
    virtual contracts::Status set_enabled(const std::string& path, bool enabled) = 0;
};

// Hedef "task:<hex(path)>", deger "enabled"/"disabled"
class TaskAdapter final : public recovery::SettingAdapter {
public:
    explicit TaskAdapter(TaskApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    TaskApi& api_;
};

// ---------------- Servisler ----------------
enum class StartType { Boot, System, Automatic, AutomaticDelayed, Manual, Disabled };
std::string_view to_string(StartType s) noexcept;
std::optional<StartType> parse_start_type(std::string_view s) noexcept;

struct ServiceInfo {
    std::string name;           // kisa ad
    std::string display_name;
    std::string image_path;     // ikili yolu (kucuk harf karsilastirma)
    bool own_process = false;   // SERVICE_WIN32_OWN_PROCESS (svchost paylasimli degil)
    bool kernel_driver = false;
    bool microsoft = false;     // yayinci/imza Microsoft ya da System32 altinda
    StartType start = StartType::Manual;
    bool running = false;
    std::vector<std::string> dependents;  // bu servise bagli olanlar
};

// Kesin yasak listesi + kurallar. Uygun degilse nedeni doner.
bool service_is_eligible(const ServiceInfo& s, std::string& why);

class ServiceApi {
public:
    virtual ~ServiceApi() = default;
    virtual contracts::Result<std::vector<ServiceInfo>> list() = 0;
    virtual contracts::Result<ServiceInfo> get(const std::string& name) = 0;
    virtual contracts::Status set_start_type(const std::string& name, StartType t) = 0;
    virtual contracts::Status start(const std::string& name) = 0;
    virtual contracts::Status stop(const std::string& name) = 0;
};

// Hedef "svc:<name>:start" (deger StartType) — calisan servisi DURDURMAZ (baslangic etkisi sonraki acilis).
// Geri yukleme: eski baslangic tipi; servis onceden calisiyor ve simdi durmussa yeniden baslatilir.
class ServiceStartAdapter final : public recovery::SettingAdapter {
public:
    explicit ServiceStartAdapter(ServiceApi& api) : api_(api) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    ServiceApi& api_;
};

// ---------------- Paketli uygulamalar (B06) ----------------
// Ucuncu taraf paketlerin StartupTask durumu baska paketten API ile degistirilemez.
contracts::Capability packaged_startup_capability();

// ---------------- Testler icin sahteler ----------------
class FakeTaskApi final : public TaskApi {
public:
    contracts::Result<std::vector<TaskInfo>> list() override;
    contracts::Result<TaskInfo> get(const std::string& path) override;
    contracts::Status set_enabled(const std::string& path, bool enabled) override;
    std::map<std::string, TaskInfo> tasks;
    bool deny = false;
};

class FakeServiceApi final : public ServiceApi {
public:
    contracts::Result<std::vector<ServiceInfo>> list() override;
    contracts::Result<ServiceInfo> get(const std::string& name) override;
    contracts::Status set_start_type(const std::string& name, StartType t) override;
    contracts::Status start(const std::string& name) override;
    contracts::Status stop(const std::string& name) override;
    std::map<std::string, ServiceInfo> services;
    bool deny = false;
    int starts = 0, stops = 0;
};

}  // namespace ef::core::adapters::startup
