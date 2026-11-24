#include "core/service/uac_copy.h"

#include <iterator>

namespace ef::core::service {

namespace {
using i18n::StringId;
using A = ipc::RequestAction;

// Kanal tablosu: docs/superpowers/specs/2026-11-13-setup-ve-yardimci-hizmet-design.md "UAC metinleri".
constexpr AdminCopy kTable[] = {
    // Performans Ac/Kapat (journal_backend: Apply/Restore/KeepCurrent -> App::admin_channel_)
    {StringId::PerfImpactAdmin, StringId::PerfImpactAdminSvc, A::Apply},
    {StringId::PlanPowerCpuDesc, StringId::PlanPowerCpuDescSvc, A::Apply},
    {StringId::PlanPowerPcieDesc, StringId::PlanPowerPcieDescSvc, A::Apply},
    {StringId::PlanHagsDesc, StringId::PlanHagsDescSvc, A::Apply},
    {StringId::PlanNvidiaDesc, StringId::PlanNvidiaDescSvc, A::Apply},
    {StringId::PlanServicesDesc, StringId::PlanServicesDescSvc, A::Apply},
    {StringId::StartupServicesDesc, StringId::StartupServicesDescSvc, A::Apply},
    // admin_busy_ run_admin_job suresince (VendorInstall dahil: UAC da olabilir) -> .svc esleri kanal soylemez, notr
    {StringId::PerfAdminBusy, StringId::PerfAdminBusySvc, A::Fix},
    {StringId::ChkAdminRunning, StringId::ChkAdminRunningSvc, A::Fix},
    // Sistem sayfasi duzelt / geri al / koru
    {StringId::ChkFixDetailAdmin, StringId::ChkFixDetailAdminSvc, A::Fix},
    {StringId::ChkFixDetailAdminNoReboot, StringId::ChkFixDetailAdminNoRebootSvc, A::Fix},
    // ChkFixDetailDns esi yok: DNS degistirme her zaman UAC (service_handles_op). Geri alma hizmette kalir.
    {StringId::ChkUndoDetailAdmin, StringId::ChkUndoDetailAdminSvc, A::UndoFix},
    {StringId::ChkUndoDetailAdminNoReboot, StringId::ChkUndoDetailAdminNoRebootSvc, A::UndoFix},
    {StringId::ChkUndoDetailDns, StringId::ChkUndoDetailDnsSvc, A::UndoFix},
    {StringId::ChkKeepDetailAdmin, StringId::ChkKeepDetailAdminSvc, A::KeepCurrent},
    {StringId::RecMachineDetail, StringId::RecMachineDetailSvc, A::Recover},
    // Oyunlar: oyun basina NVIDIA profili (Fix / UndoFix / KeepCurrent)
    {StringId::GamesNvDetail, StringId::GamesNvDetailSvc, A::Fix},
    {StringId::GamesNvKeepDetail, StringId::GamesNvKeepDetailSvc, A::KeepCurrent},
    // Bakim ve geri yukleme noktasi
    {StringId::ChkRpDetail, StringId::ChkRpDetailSvc, A::RestorePoint},
    {StringId::ChkMaintCheckDetail, StringId::ChkMaintCheckDetailSvc, A::Maintenance},
    {StringId::ChkMaintRepairDetail, StringId::ChkMaintRepairDetailSvc, A::Maintenance},
    {StringId::ChkStandbyPurgeDetail, StringId::ChkStandbyPurgeDetailSvc, A::Maintenance},  // B5: esik bulgusunun Duzelt'i
    // Surucu kurulumu (DriverInstall) ve "Dosyadan kur" (VendorInstall) burada YOK: her zaman UAC (sahip karari 3.1).
    // Yonetici cipi (UAC -> Yonetici)
    {StringId::ChipUac, StringId::ChipUacSvc, A::Apply},
};
}  // namespace

const AdminCopy* admin_copy_table(std::size_t& count) noexcept {
    count = std::size(kTable);
    return kTable;
}

StringId admin_copy(StringId id, bool admin_without_uac) noexcept {
    if (!admin_without_uac) return id;
    for (const auto& e : kTable)
        if (e.uac == id) return e.svc;
    return id;
}

std::string admin_copy_key(std::string_view key, bool admin_without_uac) {
    if (!admin_without_uac) return std::string(key);
    const auto id = i18n::id_of(key);
    if (!id) return std::string(key);
    return std::string(i18n::key_of(admin_copy(*id, true)));
}

}  // namespace ef::core::service
