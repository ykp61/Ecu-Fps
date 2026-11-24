#pragma once
// T12 (ADR-035): ozellik sihirbazi, tek dugme kuralina katlanmis. Sorular Gelismis altindaki Evet/Hayir gruplaridir:
// "Kullanmiyorsaniz Evet deyin". Bluetooth / Yazici / Xbox / Arama / Magaza; hepsi varsayilan Hayir (Gelismis).
//  - Bluetooth, yazici ve Xbox gruplari YALNIZ kullanim yoksa sunulur; kullanim bilinmiyorsa (okunamadi) sunulmaz.
//  - Hizmet bu Windows'ta yoksa sessizce atlanir; zaten Devre Disi olan atlanir; bagimli/mesgul denetimi gecmeyen atlanir.
//  - plan.services (oturumluk durdurma) plan ogelerinden biri bu grubun hizmetini zaten durduruyorsa grup sunulmaz:
//    ayni hizmet ayni planda hem oturumluk durdurulup hem Devre Disi birakilmaz.
//  - Magaza: katalog ayari (AutoDownload=2, otomatik uygulama guncellemesi kapali); Magaza kullanilabilir kalir.
//    DOGRULANMADI: politika Home/Pro surumlerde yok sayilabilir; metin etki vaat etmez.
// Yazma journal'li motor uzerinden (ServiceStartTypeAdapter / TweakAdapter): once kayit, uygula, geri oku, Kapali geri alir.
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/adapters/startup/tasks_services.h"
#include "core/adapters/windows/background.h"
#include "core/adapters/windows/tweak_adapter.h"
#include "core/perf/controller.h"

namespace ef::core::perf {

inline constexpr std::string_view kFeatureBluetoothGroup = "plan.feature.bluetooth";
inline constexpr std::string_view kFeaturePrintGroup = "plan.feature.print";
inline constexpr std::string_view kFeatureXboxGroup = "plan.feature.xbox";
inline constexpr std::string_view kFeatureSearchGroup = "plan.feature.search";
inline constexpr std::string_view kFeatureStoreGroup = "plan.feature.store";

// "Kullanimda mi": true = kullaniliyor, false = kullanilmiyor (okundu), nullopt = bilinmiyor (okunamadi).
struct FeatureUsage {
    std::optional<bool> bluetooth_devices;  // eslesmis/bagli Bluetooth cihaz
    std::optional<bool> printers;           // sanal olmayan herhangi bir yazici
    std::optional<bool> xbox_games;         // Xbox/Game Pass oyunu, Gaming Services, Xbox denetleyici/kablosuz adaptor, Game Bar kaydi
};

// Birlesim: herhangi biri true -> true; hicbiri true degil ama biri bilinmiyor -> bilinmiyor; hepsi false -> false.
std::optional<bool> combine_usage(std::initializer_list<std::optional<bool>> parts) noexcept;

// Sanal/dosya yazicisi mi (PDF, XPS, OneNote, Faks, dosya portu): bunlar "kullanimda" sayilmaz.
bool is_virtual_printer(std::string_view name, std::string_view port) noexcept;

// Grup sunulabilir mi (kullanim kurali). Arama ve Magaza icin kullanim denetimi yoktur.
bool feature_group_offered(std::string_view group, const FeatureUsage& usage) noexcept;

// Tum grup ogeleri. already_planned: ayni planda onceden kurulmus ogeler (oturumluk durdurma cakisma denetimi).
std::vector<PlanItem> feature_plan_items(const FeatureUsage& usage, adapters::startup::ServiceApi& services,
                                         adapters::windows::ServiceBusyProbe& probe, adapters::windows::TweakAdapter& tweak,
                                         std::span<const PlanItem> already_planned);

}  // namespace ef::core::perf
