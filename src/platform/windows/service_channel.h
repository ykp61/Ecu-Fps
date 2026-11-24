#pragma once
// ADR-033 / spec 5: UI'nin yonetici kanali. Once yardimci hizmeti dener (UAC yok), olmazsa bugunku UAC broker'i.
//  Hizmet yolu yalniz: UI kurulum yerinde + SCM'de EcuFpsHelper SERVICE_RUNNING + ImagePath == "<layout.exe>" --service
//  + pipe sunucu PID'i == SCM PID'i + HelloAck (core/service/channel_choice.h choose_path).
//  Geri donus YALNIZ hic Request gonderilmeden once; sonra yanit yoksa StepResult::unknown (UAC ile tekrar yok).
//  Uygulama ici Evet/Hayir run()'dan ONCE, bugunku yerinde kalir. Salt okuma journal sorgulari broker_'a aynen gider.

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/broker/admin.h"
#include "core/service/channel_choice.h"
#include "platform/windows/broker_channel.h"
#include "platform/windows/pipe.h"

namespace ef::platform {

// Yoklama + baglanti + Hello (seq 1). choose_path Service ise HelloAck alinmis istemci; aksi nullptr (probe doldurulur).
// Hicbir surec baslatmaz, SCM'yi yalniz okur. Sonraki cercevenin seq'i 2'dir.
std::unique_ptr<PipeClient> connect_helper_service(core::service::ServiceProbe& probe, std::uint32_t self_pid);

// Sahip raporu 2026-11-13 (UAC metinleri): ucuz, BAGLANMAYAN yoklama. Kurulum yeri + yonetici grubu uyesi + SCM'de
// hizmet var, SERVICE_RUNNING, PID != 0 ve ImagePath dogru. SCM'yi yalniz okur; pipe / Hello / surec yok. Yalniz metin
// secimi icindir: gercek yol her istekte connect_helper_service ile yeniden secilir.
bool helper_service_ready();

class ServiceFirstChannel final : public core::broker::AdminChannel {
public:
    core::broker::BatchResult run(core::ipc::RequestAction action, const std::vector<contracts::SettingOperation>& ops) override;
    std::vector<contracts::SettingOperation> active_ops() override { return broker_.active_ops(); }
    bool recovery_needed() override { return broker_.recovery_needed(); }
    std::vector<contracts::SettingOperation> perf_recovery_ops() override { return broker_.perf_recovery_ops(); }
    std::vector<contracts::SettingOperation> perf_conflict_ops() override { return broker_.perf_conflict_ops(); }
    std::vector<contracts::SettingOperation> fix_active_ops() override { return broker_.fix_active_ops(); }
    std::vector<contracts::SettingOperation> fix_recovery_ops() override { return broker_.fix_recovery_ops(); }
    std::vector<contracts::SettingOperation> unreconciled_ops() override { return broker_.unreconciled_ops(); }
    std::vector<contracts::SettingOperation> fix_conflict_ops() override { return broker_.fix_conflict_ops(); }
    // Makine kapsamindaki geri donus hatalari (RecoveryNeeded paneli, diag.json, restore.log). Ust siniftaki varsayilan {}
    // dondurur; burada ileti zorunludur, yoksa yonetici ayarlarinin hatalari hicbir yerde gorunmez.
    std::vector<core::recovery::FailedRestoreItem> machine_failed_items() override { return broker_.machine_failed_items(); }
    // Son hizmet denemesinde SCM hazir gorundugu halde (PID/Hello uyusmadi) UAC broker'ina dusuldu mu. Sonraki basarili
    // hizmet isteginde temizlenir. Yalniz metin secimi (UAC uyarisi geri gelir); davranisi etkilemez.
    // P1 deneyi: yalniz hizmet yolu; hizmete ulasilamazsa UAC broker'ina DUSULMEZ (istek gonderilmeden Failed doner).
    void set_service_only(bool on) noexcept { service_only_.store(on, std::memory_order_relaxed); }
    bool service_fell_back() const noexcept { return fell_back_.load(std::memory_order_relaxed); }
    void fix_target_lists(std::vector<std::string>& active, std::vector<std::string>& failed, std::vector<std::string>& interrupted,
                          std::vector<std::string>& conflict) override {
        broker_.fix_target_lists(active, failed, interrupted, conflict);
    }

private:
    BrokerChannel broker_;
    std::atomic<bool> fell_back_{false};
    std::atomic<bool> service_only_{false};
};

}  // namespace ef::platform
