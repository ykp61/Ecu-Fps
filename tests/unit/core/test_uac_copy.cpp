// Sahip raporu 2026-11-13: yonetici metni gercek yolu soyler. Hizmet yolu (UAC yok) -> ".svc" esi; UAC-yalniz roller degismez.
#include <string>
#include <string_view>

#include "core/i18n/catalog.h"
#include "core/service/channel_choice.h"
#include "core/service/uac_copy.h"
#include "ef_test.h"

using namespace ef::core;
using i18n::StringId;

namespace {
bool mentions_uac_prompt(std::string_view s) {
    return s.find("UAC") != std::string_view::npos && s.find("UAC sorulmaz") == std::string_view::npos &&
           s.find("no UAC prompt") == std::string_view::npos;
}
}  // namespace

EF_TEST(uac_copy_table_pairs_and_service_actions) {
    std::size_t n = 0;
    const auto* t = service::admin_copy_table(n);
    EF_CHECK(n >= 22);  // 3.1: DNS degistirme + surucu kurulumu metinleri artik UAC-yalniz (4 esleme cikti)
    for (std::size_t i = 0; i < n; ++i) {
        // Her esleme hizmetin yurutebildigi bir istege ait (VendorInstall asla).
        EF_CHECK(service::service_handles(t[i].action));
        EF_CHECK(t[i].action != ipc::RequestAction::VendorInstall);
        EF_CHECK(std::string(i18n::key_of(t[i].svc)) == std::string(i18n::key_of(t[i].uac)) + ".svc");
        EF_CHECK(service::admin_copy(t[i].uac, true) == t[i].svc);
        EF_CHECK(service::admin_copy(t[i].uac, false) == t[i].uac);
        EF_CHECK(service::admin_copy_key(i18n::key_of(t[i].uac), true) == i18n::key_of(t[i].svc));
        EF_CHECK(service::admin_copy_key(i18n::key_of(t[i].uac), false) == i18n::key_of(t[i].uac));
    }
}

EF_TEST(uac_copy_specific_mappings) {
    EF_CHECK(service::admin_copy(StringId::PerfImpactAdmin, true) == StringId::PerfImpactAdminSvc);
    EF_CHECK(service::admin_copy(StringId::ChkFixDetailAdmin, true) == StringId::ChkFixDetailAdminSvc);
    EF_CHECK(service::admin_copy(StringId::ChkUndoDetailDns, true) == StringId::ChkUndoDetailDnsSvc);
    EF_CHECK(service::admin_copy(StringId::ChipUac, true) == StringId::ChipUacSvc);
    EF_CHECK(service::admin_copy_key("plan.hags.desc", true) == "plan.hags.desc.svc");
    EF_CHECK(service::admin_copy_key("plan.power_mode.desc", true) == "plan.power_mode.desc");  // UAC'siz grup
    EF_CHECK(service::admin_copy_key("no.such.key", true) == "no.such.key");
}

EF_TEST(uac_copy_uac_only_roles_never_switch) {
    // Temizlik yardimcisi, oyunda yakalama, kurulum/kaldirma/onarim, Dosyadan kur (VendorInstall) ve red bildirimleri.
    for (auto id : {StringId::CleanupWindowsTempDesc, StringId::CleanupAdminNeeded, StringId::CleanupAdminPending,
                    StringId::CleanupAdminDenied, StringId::UpdatesHelpGpuLocal, StringId::SetupAdminOnce,
                    StringId::SetupConfirmBody, StringId::UpdateRepairDetail, StringId::UpdatesInstallDenied,
                    StringId::ChkNoticeFixDenied, StringId::ChkNoticeUndoDenied, StringId::RecMachineDenied,
                    StringId::GamesNvNoticeDenied, StringId::ChkNoticeRpDenied, StringId::ChkNoticeMaintDenied,
                    StringId::PerfAdminSkipped, StringId::StartupAdmin, StringId::CleanupAdminChip,
                    // Sahip karari 3.1: DNS degistirme ve Windows Update surucu kurulumu her zaman UAC
                    StringId::ChkFixDetailDns, StringId::UpdatesHelpInstall, StringId::UpdatesHelpGpuWu,
                    StringId::UpdatesInstallAdminNote}) {
        EF_CHECK(service::admin_copy(id, true) == id);
        EF_CHECK(service::admin_copy(id, false) == id);
    }
}

EF_TEST(uac_copy_texts_both_locales) {
    for (auto loc : {i18n::Locale::TrTR, i18n::Locale::EnUS}) {
        auto c = i18n::Catalog::embedded(loc);
        EF_CHECK(c.ok());
        if (!c.ok()) continue;
        const auto& cat = c.value();
        std::size_t n = 0;
        const auto* t = service::admin_copy_table(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::string_view svc = cat.get(t[i].svc);
            EF_CHECK(!svc.empty());
            EF_CHECK(!mentions_uac_prompt(svc));  // hizmet esi UAC istemi vaat etmez
            EF_CHECK(svc != cat.get(t[i].uac));
        }
        // Ornek: UAC esi UAC'yi soyler, hizmet esi "sorulmaz" der.
        EF_CHECK(mentions_uac_prompt(cat.get(StringId::PerfImpactAdmin)));
        const std::string_view s = cat.get(StringId::PerfImpactAdminSvc);
        EF_CHECK(s.find(loc == i18n::Locale::TrTR ? "UAC sorulmaz" : "no UAC prompt") != std::string_view::npos);
        // run_admin_job VendorInstall'i (her zaman UAC) da kapsar: "suruyor" esleri kanal soylemez.
        for (auto id : {StringId::ChkAdminRunningSvc, StringId::PerfAdminBusySvc}) {
            const std::string_view busy = cat.get(id);
            EF_CHECK(busy.find("UAC") == std::string_view::npos);
            EF_CHECK(busy.find(loc == i18n::Locale::TrTR ? "hizmet" : "helper service") == std::string_view::npos);
        }
    }
}
