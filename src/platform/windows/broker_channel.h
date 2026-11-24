#pragma once
// OPT-02 / OPT-10 platform: yonetici yurutme yolunun gercek tasiyicisi.
//  UI tarafi  : BrokerChannel — tek UAC (ShellExecuteEx runas) ile ayni EXE'yi broker rolunde baslatir,
//               broker PID'i dogrulanmis pipe'a baglanir (nonce + PID el sikismasi), istekleri gonderir.
//  Broker rolu: run_broker — makine kilidi, makine journal'i (ProgramData, korumali ACL), yalniz HAGS + NVIDIA
//               DRS adaptorleri, tek istemcili pipe sunucusu, SEC-02 oturumu, SEC-03 dagitici. Serbest komut yok.
// Gercek UAC turu kullanici varken yapilir (PENDING); birim testler core/broker uzerinden sahte tasiyiciyla calisir.

#include <memory>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "platform/windows/elevation.h"

namespace ef::platform {

class BrokerChannel final : public core::broker::AdminChannel {
public:
    core::broker::BatchResult run(core::ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops) override;
    std::vector<contracts::SettingOperation> active_ops() override;
    bool recovery_needed() override;
    std::vector<contracts::SettingOperation> perf_recovery_ops() override;
    std::vector<contracts::SettingOperation> perf_conflict_ops() override;
    std::vector<core::recovery::FailedRestoreItem> machine_failed_items() override;
    std::vector<contracts::SettingOperation> fix_active_ops() override;
    std::vector<contracts::SettingOperation> fix_recovery_ops() override;
    std::vector<contracts::SettingOperation> unreconciled_ops() override;
    std::vector<contracts::SettingOperation> fix_conflict_ops() override;
    void fix_target_lists(std::vector<std::string>& active, std::vector<std::string>& failed, std::vector<std::string>& interrupted,
                          std::vector<std::string>& conflict) override;
};

// Makine islemleri kilidi (broker ile ayni ad). Yalniz yukseltilmis surec alabilir; alinamazsa nullptr.
// --recovery rolu makine kapsamlarina yalniz bu kilitle dokunur (broker ile es zamanli uzlastirma yok).
class MachineOpsLock {
public:
    static std::unique_ptr<MachineOpsLock> try_acquire(unsigned timeout_ms);
    ~MachineOpsLock();
    MachineOpsLock(const MachineOpsLock&) = delete;
    MachineOpsLock& operator=(const MachineOpsLock&) = delete;

private:
    explicit MachineOpsLock(void* h) : h_(h) {}
    void* h_;
};

// Yonetici yurutucusunun kimlik girdileri (UAC broker ve yardimci hizmet ortak fabrikasi).
//  actor_sid       : islemi yurutenin belirtec kullanicisi (broker: kendi kullanicisi; hizmet: S-1-5-18).
//  target_user_sid : hedef (oyuncu) kullanici; oyun kapisi ve kitaplik bu kullanici icin okunur.
//  client_process  : SYNCHRONIZE tanitici (sahibi cagiran); istemci kapandiysa surucu/paket kurulumu baslamaz.
//  machine_lock_held: MachineOpsLock cagiranda tutuluyor (Dispatcher makine kapsami icin ister).
struct AdminIdentity {
    std::string actor_sid, target_user_sid;
    void* client_process = nullptr;
    bool machine_lock_held = false;
};

// Journal'lar, motorlar, izin listesindeki adaptorler, oyun izni ve is kayitlari; AdminExecutor bunlarin uzerinde.
// Yalniz AdminExecutor::handle yurutur (serbest komut yok).
class AdminRuntime {
public:
    virtual ~AdminRuntime() = default;
    virtual core::broker::AdminExecutor& executor() = 0;
    virtual const core::broker::ExecutorDeps& deps() const = 0;
    // Istemci dogrulandiktan sonra, ilk istekten once: yarim kalmis makine kayitlarini uzlastirir (M1/REC-03) ve
    // oyun kapisi aciksa onceki oturumdan yarim kalan geri yukleme noktasini kapatir (EF-X16).
    virtual core::broker::SessionReconcile begin_session() = 0;
};
std::unique_ptr<AdminRuntime> make_admin_runtime(const AdminIdentity& id);

// SRSetRestorePointW icin belgelenen COM guvenligi (MTA). Surec/is parcacigi basina bir kez; omru boyunca tutulur.
class AdminCom {
public:
    AdminCom();
    ~AdminCom();
    AdminCom(const AdminCom&) = delete;
    AdminCom& operator=(const AdminCom&) = delete;

private:
    bool owned_ = false;
};

// --role=broker: yukseltme/ust surec/imza kontrolleri main.cpp'de yapildiktan sonra cagrilir. Cikis kodu doner.
// parent: main.cpp'de dogrulanan ust surec kimligi; pipe kabulunde PID yeniden kullanimi dahil birebir eslesir.
int run_broker(const core::LaunchArgs& args, const ParentIdentity& parent);

}  // namespace ef::platform
