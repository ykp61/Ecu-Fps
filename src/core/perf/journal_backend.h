#pragma once
// UI-02: PerfBackend'in journal + islem motoru uygulamasi. Her uygulama kalici kayitla baslar,
// geri okunur ve dogrulanir; Kapali tum etkin islemleri ters sirada geri yukler.
// Yonetici gerektiren adimlar burada calismaz: admin kanali (OPT-02/OPT-10 broker) varsa tek UAC ile ona
// gider; yoksa plan bunlari icerirse Denied. Sira: kullanici adimlari (journal) -> yonetici grubu (broker).
// UAC reddi: yonetici gruplari atlanir, kullanici adimlari etkin kalir, sonuc acikca kismi (admin_skipped).

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/broker/admin.h"
#include "core/perf/controller.h"
#include "core/recovery/service.h"

namespace ef::core::perf {

struct JournalBackendDeps {
    recovery::JournalScope scope;                // kullanici journal'i ve motoru
    std::function<contracts::Result<Plan>()> build_plan;
    std::function<std::string()> new_tx_id;     // 32 hex
    std::function<std::int64_t()> now_unix_ms;
    std::string actor_sid, target_sid;
    bool elevated = false;
    broker::AdminChannel* admin = nullptr;      // OPT-10: yonetici kanali (yoksa yonetici adimi Denied)
    // CORE-03: acilista run_startup_recovery "yeni sema" gorduyse yeni degisiklik yasak (broker disi yol).
    std::function<bool()> writes_blocked;
    // Islem siniri izni (guncel oyun/algilama durumu): uygulama/geri alma baslamadan ve UAC'tan hemen once sorulur.
    recovery::WriteGuard permit;
    // Saha 2026-11-15: geri alma tanisi (uretimde logs/restore.log, donen dosya). Her geri almada sonuc, yonetici adimlari
    // ve geri alinamamis adimlar (ayar, beklenen, okunan, kod; recovery::restore_log_line ile redakte) yazilir. Bos = yazilmaz.
    std::function<void(std::string_view)> restore_log;
    // R-F1: marka degisikligi gocu, eski veride geri alinmamis degisiklik oldugu halde onu yeni klasore aktaramadi.
    // true iken temiz "Kapali" gosterilmez: kurtarma durumu + durust bildirim (eski kayitlar eski klasorde korunuyor).
    std::function<bool()> legacy_pending;
};

class JournalBackend final : public PerfBackend {
public:
    explicit JournalBackend(JournalBackendDeps deps) : d_(std::move(deps)) {}
    contracts::Result<Plan> plan() override { return d_.build_plan(); }
    Outcome apply(const Plan& plan, const ProgressFn& progress, const std::atomic<bool>& cancel) override;
    Outcome restore(const ProgressFn& progress) override;
    std::size_t active_changes() override;
    bool recovery_needed() override;
    bool legacy_unmigrated() override { return d_.legacy_pending && d_.legacy_pending(); }
    bool external_conflict() override;
    Outcome keep_external() override;
    // Kullanici kapsami + makine Performans journal'i ("machine"); makine duzeltmeleri bu listeye girmez.
    std::vector<recovery::FailedRestoreItem> failed_items() override;
    // Acilis uzlastirmasi (kullanici kapsami; makine adimlari broker olmadan okunamaz ve burada dogrulanmis sayilmaz).
    std::vector<recovery::FailedRestoreItem> check_live() override;
    void retire_off(const std::vector<recovery::FailedRestoreItem>& items) override;
    std::vector<DriftItem> drift() override;
    Outcome reapply_drift(const std::vector<DriftItem>& items, const ProgressFn& progress) override;

private:
    Outcome restore_impl(const ProgressFn& progress);
    void log(std::string_view line) const {
        if (d_.restore_log) d_.restore_log(line);
    }
    JournalBackendDeps d_;
    broker::BatchResult last_admin_;  // son geri almanin yonetici partisi (tani satirlari; yalniz restore is parcacigi)
    bool admin_ran_ = false;
    // Son geri almada broker'in pending_reboot bildirdigi hedefler (failed_items UI is parcacigindan okunabilir).
    std::mutex reboot_mu_;
    std::vector<std::string> reboot_targets_;
};

}  // namespace ef::core::perf
