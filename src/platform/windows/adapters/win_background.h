#pragma once
// WIN-09 platform: oturumluk hizmet durdurma oncesi "is yok" denetimi (salt okuma).
// Spooler/Fax: Windows Server SKU'da (yazdirma sunucusu/DC olabilir), paylasilan yerel yazici varken
// veya herhangi bir yerel kuyrukta is bekliyorken mesgul sayilir. Diger hizmetler icin denetim yok.
// Hizmet kontrolu WinServiceApi (win_tasks_services) ile yapilir; burada SCM yazimi yoktur.

#include "core/adapters/windows/background.h"

namespace ef::platform::adapters {

class WinServiceBusyProbe final : public core::adapters::windows::ServiceBusyProbe {
public:
    contracts::Result<bool> busy(const std::string& service) override;
};

}  // namespace ef::platform::adapters
