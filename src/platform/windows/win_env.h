#pragma once
// Windows ortam bilgisi: kullanici/makine adi, bilinen klasorler, UTF-16 <-> UTF-8.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/logging/redact.h"

namespace ef::platform {

std::string to_utf8(std::wstring_view w);
std::wstring to_wide(std::string_view s);

// %LOCALAPPDATA%\EcuFps (yazilabilir kullanici verisi). EXE klasoru kullanilmaz.
// Uyumluluk B9 (bosluk 23): YALNIZ TEST icin EF_DATA_DIR gecersiz kilmasi (bkz. decide_data_dir). Test kipi disinda
// degisken yok sayilir (gercek klasor). Test kipinde gecersiz deger -> nullopt (kalicilik yok; gercek klasore
// dusulmez). Karar surec basina bir kez verilir.
std::optional<std::filesystem::path> app_data_dir();

// Saf karar (birim testlenir). EF_DATA_DIR yalniz su kosullarin HEPSI saglaninca kabul edilir:
//  - test_background (EF_TEST_BACKGROUND=1; ctest / test betikleri verir, urun kendiliginden vermez),
//  - deger surucu harfli mutlak yol (UNC / ag yolu, goreli yol, ".." bileseni yok),
//  - yol kullanicinin gecici klasorunun (temp_root) ALTINDA bir klasor (temp_root'un kendisi degil).
// Gerekce: veri klasoru kullanici journal'ini (geri alma kayitlari), tercihleri ve kaldirma hedefini tasir. Basibos bir
// degisken uretimde journal'i gizleyemesin veya "Verileri sil" gecici klasor disinda bir agaci hedefleyemesin diye
// yonlendirme yalniz test kipinde ve yalniz %TEMP% altina izinlidir. Gecersiz deger -> nullopt (bkz. decide_data_dir).
std::optional<std::filesystem::path> data_dir_override(std::wstring_view value, bool test_background,
                                                       const std::filesystem::path& temp_root);
// app_data_dir() karari (saf, birim testlenir):
//  - Default: EF_DATA_DIR bos veya test kipi yok -> gercek %LOCALAPPDATA%\EcuFps.
//  - Override: test kipi + data_dir_override kabul etti -> path.
//  - Blocked: test kipi + deger reddedildi (veya temp_root alinamadi) -> kalicilik kapali. Test betiginin gelistirici
//    PC'sindeki gercek profile sessizce yazmasini onler (fail closed).
enum class DataDirSource { Default, Override, Blocked };
struct DataDirDecision {
    DataDirSource source = DataDirSource::Default;
    std::optional<std::filesystem::path> path;
};
DataDirDecision decide_data_dir(std::wstring_view value, bool test_background, const std::filesystem::path& temp_root);
// app_data_dir() bu surecte EF_DATA_DIR yonlendirmesini kullaniyor mu (destek paketi ortam bayragi).
bool app_data_dir_overridden();

// Gercek kullanici/makine/profil degerleriyle redaksiyon baglami.
core::logging::RedactionContext current_redaction_context();

}  // namespace ef::platform
