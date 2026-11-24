#pragma once
// EXT-CLN: urunun kendi yarim kalmis zamanli test dosyalari (benchmark depolama testi).
// Sahiplik kaniti: klasor adi "EcuFps-bench-<16 hex>" + yalniz bilinen dosya adlari + gecerli isaret icerigi.
// Bu adlar benchmark/storage/storage_rules.h ile ayni sozlesmedir; degistirilirse ikisi birlikte guncellenir
// (tests/unit/core/test_cleanup.cpp bunu benchmark yardimcilariyla capraz dogrular).

#include <cstddef>
#include <string_view>

namespace ef::core::cleanup {

inline constexpr std::wstring_view kAppTestFolderPrefix = L"EcuFps-bench-";
inline constexpr std::wstring_view kAppTestMarkerName = L"itx-bench.marker";
inline constexpr std::wstring_view kAppTestDataName = L"test.bin";
inline constexpr std::wstring_view kAppTestCacheName = L"cache.bin";
inline constexpr std::string_view kAppTestMarkerMagic = "EcuFps-bench-v1";
inline constexpr std::size_t kAppTestMarkerProbe = 32;  // read_prefix icin yeterli bayt (sihir + pid)

// "EcuFps-bench-" + tam 16 kucuk/buyuk harf hex. Baska ad sahiplenilmez.
bool is_app_test_folder_name(std::wstring_view name) noexcept;

}  // namespace ef::core::cleanup
