#pragma once
// WIN-09: oyun oturumu boyunca arka plan hizmeti durdurma. Baslangic tipi DEGISMEZ (WIN-04'ten farki);
// yalniz calisan hizmet durdurulur ve oturum sonunda/geri almada yeniden baslatilir.
// Izin listesi yalniz resmi Microsoft belgesinde aciklamasi olan ve guvenlik/ag/ses/surucu/anti-cheat/
// guncelleme rolu tasimayan hizmetlerden olusur (research/background_services.md).
// Durdurma yonetici ister: gercek cagri broker rolunde yurutulur; yukseltmesiz yetenek Denied'dir.

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "contracts/models.h"
#include "contracts/result.h"
#include "contracts/support.h"
#include "core/adapters/startup/tasks_services.h"
#include "core/recovery/engine.h"

namespace ef::core::adapters::windows {

struct SessionStoppableService {
    std::string_view name;          // SCM kisa adi (buyuk/kucuk harf duyarsiz eslesir)
    std::string_view image_part;    // ikili yolunda beklenen parca (kucuk harf); ayni adli sahte hizmeti reddeder
    std::string_view breaks_key;    // durdukca ne calismaz (metin anahtari)
    std::string_view source;        // research/background_services.md satiri / resmi kaynak
    bool needs_idle_check = false;  // durdurmadan once "is yok" denetimi (ornek: yazdirma kuyrugu bos)
};

// Derlenmis kesin izin listesi; dis veri genisletemez.
std::span<const SessionStoppableService> session_stoppable_services() noexcept;
const SessionStoppableService* find_session_stoppable(std::string_view name) noexcept;

// Hizmet su an "is yapiyor" mu (ornek: Spooler kuyrugunda is, paylasilan yazici, sunucu SKU).
// Bilinmiyorsa hata doner; adaptor hatayi "durdurma" olarak YORUMLAMAZ, reddeder.
class ServiceBusyProbe {
public:
    virtual ~ServiceBusyProbe() = default;
    virtual contracts::Result<bool> busy(const std::string& service) = 0;
};

// Durdurma uygunlugu: izin listesi, Microsoft ikilisi, cekirdek surucusu degil, bagimli hizmetlerin
// hicbiri calismiyor, gerekiyorsa is yok. Uygun degilse Denied + neden.
contracts::Status check_session_stop(startup::ServiceApi& api, ServiceBusyProbe& probe, const startup::ServiceInfo& s);

// Hedef "svcstop:<name>", deger "stopped" (uygula) / "running" (onceki durum).
// Zaten durmus hizmette apply no-op'tur; geri yuklemede yalniz onceden calisan hizmet baslatilir.
std::string format_session_stop_target(std::string_view service);
contracts::Result<std::string> parse_session_stop_target(std::string_view target);

class ServiceSessionStopAdapter final : public recovery::SettingAdapter {
public:
    ServiceSessionStopAdapter(startup::ServiceApi& api, ServiceBusyProbe& probe) : api_(api), probe_(probe) {}
    contracts::Result<recovery::Snapshot> capture(const contracts::SettingOperation& op) override;
    contracts::Status apply(const contracts::SettingOperation& op) override;
    contracts::Result<std::optional<std::string>> read_back(const contracts::SettingOperation& op) override;
    contracts::Status restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) override;

private:
    startup::ServiceApi& api_;
    ServiceBusyProbe& probe_;
};

// Durdurma her zaman yonetici ister (SERVICE_STOP/SERVICE_START, yerel sistem hizmetleri).
inline constexpr contracts::Privilege kSessionStopPrivilege = contracts::Privilege::Admin;
constexpr bool session_stop_needs_admin() noexcept { return true; }
// elevated=false -> Denied (gerekce dolu). Ozellik kimligi "B07" (katalog kaydi ayri is).
contracts::Capability session_stop_capability(bool elevated);

// Test sahtesi: hizmet adi -> mesgul; listede yoksa false; fail=true ise hata.
class FakeServiceBusyProbe final : public ServiceBusyProbe {
public:
    contracts::Result<bool> busy(const std::string& service) override;
    std::map<std::string, bool> busy_by_service;
    bool fail = false;
    int calls = 0;
};

}  // namespace ef::core::adapters::windows
