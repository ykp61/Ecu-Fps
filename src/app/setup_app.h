#pragma once
// ADR-033 / spec 3 ve 8: Setup rolu — kurulum penceresi (ayni EXE; kurulum yeri disindan bos acilis veya --setup).
// Pencere yukseltilmemis calisir: imzali manifest denetimi (Ui ile ayni kod), daha yeni surum varsa uygulama icinde
// indirme + boyut/SHA-256 dogrulamasi + geri okuma ve o dosyaya devir; Kur -> uygulama ici Evet/Hayir -> tek UAC
// (yukseltilmis SetupWorker islemsel kurulum yapar). Basaridan sonra HKCU Run degeri bu (yukseltilmemis) surecte yazilir.

#include <windows.h>

#include "core/i18n/catalog.h"
#include "core/roles.h"

namespace ef::app {

int run_setup(HINSTANCE instance, const core::LaunchArgs& args, const core::i18n::Catalog& catalog);

// Test/otomasyon anahtari: EF_SETUP_ROLE=0 iken kurulum yeri disindan bos acilis bugunku gibi Ui'dir ve Ui "Kurulum
// gerekli" seridini gostermez (ctest pencere testleri derleme klasorunden calisir). Kullanici icin guvenlik farki yok:
// iki yol da yukseltilmemis pencere acar.
bool setup_role_disabled_by_env();
// Ui kurulum yeri disinda calisiyor ve kurulum sunulmali (spec 8). Salt okuma.
bool install_offer_applies();
// Ayni EXE'yi --setup ile yukseltilmemis baslatir (sabit arguman). Baslatildiysa true.
bool launch_self_setup();

}  // namespace ef::app
