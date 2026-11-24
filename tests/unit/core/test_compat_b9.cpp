// Uyumluluk B9 (bosluk 23, 24): yalniz test icin EF_DATA_DIR karari ve denge kosucusunun bagdastirici secimi.
#include <array>
#include <filesystem>
#include <optional>

#include "benchmark/balance/balance_runner.h"
#include "benchmark/scene/d3d_renderer.h"
#include "ef_test.h"
#include "platform/windows/win_env.h"

namespace fs = std::filesystem;
using ef::platform::data_dir_override;

namespace {
const fs::path kTemp = L"C:\\Users\\u\\AppData\\Local\\Temp\\";
}

EF_TEST(b9_data_dir_override_requires_test_background) {
    // Basibos degisken (test kipi yok): uretim gercek klasoru kullanir.
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\ef-first-run", false, kTemp));
    const auto ok = data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\ef-first-run", true, kTemp);
    EF_CHECK(ok.has_value());
    if (ok) EF_CHECK(*ok == fs::path(L"C:\\Users\\u\\AppData\\Local\\Temp\\ef-first-run"));
}

EF_TEST(b9_data_dir_override_only_under_temp) {
    // Gecici klasor disi (gercek veri klasoru, profil, kok) reddedilir: "Verileri sil" baska agaci hedefleyemez.
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\EcuFps", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:\\Users\\u", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:\\", true, kTemp));
    EF_CHECK(!data_dir_override(L"D:\\Temp\\x", true, kTemp));
    // Temp'in kendisi ve ortak onekli kardes klasor kabul edilmez.
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\TempX\\a", true, kTemp));
    // ".." ile disari kacis, UNC / ag yolu, cihaz yolu ve goreli yol reddedilir.
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\..\\EcuFps", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\a\\..\\b", true, kTemp));
    EF_CHECK(!data_dir_override(L"\\\\server\\share\\Temp\\a", true, kTemp));
    EF_CHECK(!data_dir_override(L"\\\\?\\C:\\Users\\u\\AppData\\Local\\Temp\\a", true, kTemp));
    EF_CHECK(!data_dir_override(L"ef-first-run", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:ef", true, kTemp));
    EF_CHECK(!data_dir_override(L"", true, kTemp));
    EF_CHECK(!data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\a", true, fs::path()));
}

EF_TEST(b9_data_dir_override_case_and_separators) {
    // NTFS buyuk/kucuk harf duyarsiz; "/" ve sondaki ayirici kabul edilir; Turkce karakterli klasor adi korunur.
    const auto a = data_dir_override(L"c:/users/U/appdata/local/temp/ef-ilk-çalıştırma/", true, kTemp);
    EF_CHECK(a.has_value());
    if (a) EF_CHECK(a->filename() == fs::path(L"ef-ilk-çalıştırma"));
    const auto b = data_dir_override(L"C:\\Users\\u\\AppData\\Local\\Temp\\run\\.\\data", true, L"C:\\Users\\u\\AppData\\Local\\Temp");
    EF_CHECK(b.has_value());
    if (b) EF_CHECK(*b == fs::path(L"C:\\Users\\u\\AppData\\Local\\Temp\\run\\data"));
}

EF_TEST(b9_decide_data_dir_fails_closed_in_test_mode) {
    using ef::platform::decide_data_dir;
    using ef::platform::DataDirSource;
    // Degisken yok: gercek klasor (test kipi olsa da olmasa da).
    EF_CHECK(decide_data_dir(L"", false, kTemp).source == DataDirSource::Default);
    EF_CHECK(decide_data_dir(L"", true, kTemp).source == DataDirSource::Default);
    // Uretimde basibos degisken: yok sayilir, gercek klasor.
    EF_CHECK(decide_data_dir(L"C:\\Users\\u\\AppData\\Local\\Temp\\x", false, kTemp).source == DataDirSource::Default);
    // Test kipi + gecerli deger: yonlendirme.
    const auto ok = decide_data_dir(L"C:\\Users\\u\\AppData\\Local\\Temp\\x", true, kTemp);
    EF_CHECK(ok.source == DataDirSource::Override);
    EF_CHECK(ok.path.has_value());
    // Test kipi + reddedilen deger: Blocked, yol yok (gercek klasore DUSULMEZ).
    const auto bad = decide_data_dir(L"C:\\Users\\u\\AppData\\Local\\EcuFps", true, kTemp);
    EF_CHECK(bad.source == DataDirSource::Blocked);
    EF_CHECK(!bad.path.has_value());
    EF_CHECK(decide_data_dir(L"relative", true, kTemp).source == DataDirSource::Blocked);
    // %TEMP% alinamadi (GetTempPathW 0 / cok uzun): temp_root bos -> Blocked.
    EF_CHECK(decide_data_dir(L"C:\\Users\\u\\AppData\\Local\\Temp\\x", true, fs::path()).source == DataDirSource::Blocked);
    // Betigin gordugu TMP ile uygulamaninki farkli: yine Blocked.
    EF_CHECK(decide_data_dir(L"D:\\Temp\\x", true, kTemp).source == DataDirSource::Blocked);
}

EF_TEST(b9_nth_hardware_adapter) {
    // Yuksek performans sirasi; yazilim bagdastiricilari (WARP / Basic Render) atlanir.
    const std::array<bool, 4> sw{false, true, false, true};
    EF_CHECK(ef::benchmark::nth_hardware_adapter(sw, 0) == 0);
    EF_CHECK(ef::benchmark::nth_hardware_adapter(sw, 1) == 2);
    EF_CHECK(ef::benchmark::nth_hardware_adapter(sw, 2) == -1);
    EF_CHECK(ef::benchmark::nth_hardware_adapter(sw, -1) == -1);
    const std::array<bool, 1> only_sw{true};
    EF_CHECK(ef::benchmark::nth_hardware_adapter(only_sw, 0) == -1);
}

EF_TEST(b9_balance_default_adapter_is_product_choice) {
    // Urun secenegi degismez: bagdastirici dizini verilmez (-1 = ilk yuksek performansli donanim bagdastiricisi).
    ef::benchmark::balance::BalanceOptions o;
    EF_CHECK(o.hardware_adapter_index == -1);
    EF_CHECK(o.prefer_hardware);
}
