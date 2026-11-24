#pragma once
// ADR-033 ek: SCM hata eylemlerini okuma/yazma/onarma (hizmet tanitici verilir; gercek SCM testlenmez, politika saftir:
// core/setup/service_policy.h). Hizmet LocalSystem olarak kendi tanitici ile cagirabilir.
#include <windows.h>

#include "core/setup/service_policy.h"

namespace ef::platform {

// QueryServiceConfig2: sifirlama suresi, eylemler, "cikis kodu da hata" bayragi. Okunamazsa false.
bool read_failure_config(SC_HANDLE svc, core::setup::FailureConfig& out, DWORD* err = nullptr);
// Gereken politikayi yazar (SERVICE_CHANGE_CONFIG gerekir).
bool write_required_failure_config(SC_HANDLE svc, DWORD* err = nullptr);
// Oku; uymuyorsa yaz ve geri oku. repaired: yazildi. Sonuc true = politika dogrulandi.
bool ensure_failure_config(SC_HANDLE svc, bool* repaired, DWORD* err = nullptr);

}  // namespace ef::platform
