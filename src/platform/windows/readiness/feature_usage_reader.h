#pragma once
// T12: ozellik sihirbazi icin salt okuma kullanim olgulari (Bluetooth cihazi, gercek yazici, Xbox kullanimi).
// Yalniz SetupAPI/kayit defteri/dizin OKUMASI; hicbir hizmet/ayar yazilmaz, hizmet baslatilmaz. Okunamayan olgu nullopt'tur
// (bilinmiyor: ilgili grup sunulmaz). DOGRULANMADI: gercek cihazda dogruluk (bu surumde yalniz derleme + sahte olgu testleri).
#include "core/perf/feature_plan.h"

namespace ef::platform::readiness {

core::perf::FeatureUsage read_feature_usage();

}  // namespace ef::platform::readiness
