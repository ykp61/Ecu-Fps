#pragma once
// T12 (ADR-035): ozellik sihirbazi. Kullanilmayan Windows ozelliklerinin hizmetlerini KALICI kapatir (baslangic tipi Devre Disi)
// ve calisiyorsa durdurur. Oturumluk durdurmadan (ServiceSessionStopAdapter) farki: baslangic tipi degisir; Kapali onceki
// baslangic tipini (gecikmeli otomatik bayragi dahil) geri yazar ve onceden calisiyorsa hizmeti yeniden baslatir.
// Hedef "svcstart:<ad>", deger yalniz "disabled". Ad derlenmis tabloda olmali; dis veri tabloyu genisletemez.
// Guvenlik/ag/ses/surucu/anti-cheat/guncelleme hizmetleri tabloda YOKTUR. Yazma yonetici ister (broker).

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "contracts/models.h"
#include "contracts/result.h"
#include "core/adapters/startup/tasks_services.h"
#include "core/adapters/windows/background.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

struct FeatureService {
    std::string_view name;          // SCM kisa adi (buyuk/kucuk harf duyarsiz)
    std::string_view group;         // plan grubu ("plan.feature.bluetooth")
    std::string_view image_part;    // ikili yolunda beklenen parca (kucuk harf); ayni adli sahte hizmeti reddeder
    bool needs_idle_check = false;  // kapatmadan once "is yok" denetimi (yazdirma kuyrugu)
};

// Derlenmis kesin liste; sira = dis sira: bagimli hizmetler ana hizmetten ONCE gelir.
std::span<const FeatureService> feature_services() noexcept;
const FeatureService* find_feature_service(std::string_view name) noexcept;

std::string format_service_start_target(std::string_view service);
contracts::Result<std::string> parse_service_start_target(std::string_view target);

// Uygunluk: tablo, Microsoft ikilisi (beklenen ikili), cekirdek surucusu degil, bagimli hizmet denetimi, gerekiyorsa is yok.
// strict_dependents=false (yakalama/plan): calisan bagimli hizmet tablodaysa ayni islemde once kapatilacagindan engel degildir;
// true (uygulama): hicbir calisan bagimli kalamaz.
contracts::Status check_feature_disable(startup::ServiceApi& api, ServiceBusyProbe& probe, const startup::ServiceInfo& s,
                                        bool strict_dependents);

class ServiceStartTypeAdapter final : public recovery::SettingAdapter {
public:
    ServiceStartTypeAdapter(startup::ServiceApi& api, ServiceBusyProbe& probe) : api_(api), probe_(probe) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    startup::ServiceApi& api_;
    ServiceBusyProbe& probe_;
};

// Broker izni: Machine kapsam, tablodaki hizmet, deger "disabled".
bool service_start_admin_allowed(const contracts::SettingOperation& op) noexcept;

}  // namespace ef::core::adapters::windows
