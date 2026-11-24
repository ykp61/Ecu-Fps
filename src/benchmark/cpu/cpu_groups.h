#pragma once
// Islemci gruplari (> 64 mantiksal CPU). Bu derleyici surumunde std::thread::hardware_concurrency() yalniz surecin
// kendi grubunu sayar (GetNativeSystemInfo dwNumberOfProcessors) ve Windows 10'da yeni is parcaciklari surecin
// birincil grubunda kalir. Yuk isciler bu yuzden tum etkin gruplara, grup buyuklugu oraninda dagitilir.
// Yalniz bu surecin kendi is parcaciklarinin grup benzesimi degisir (sistem ayari degildir); tek gruplu
// makinede hicbir sey yapilmaz.

#include <cstdint>
#include <span>

namespace ef::benchmark {

// Tum etkin gruplardaki mantiksal CPU sayisi (GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)); okunamazsa
// hardware_concurrency(); en az 1.
std::uint32_t logical_processor_count() noexcept;

// Saf hesap: workers isciden worker'inci (0 tabanli) iscinin grubu. Isciler gruplara buyuklukleri oraninda,
// sirayla dagitilir (konum = worker * toplam / workers). Bos liste veya toplam 0 -> 0.
std::uint16_t group_for_worker(std::uint32_t worker, std::uint32_t workers, std::span<const std::uint32_t> group_sizes) noexcept;

// Cagiran is parcacigini (isci worker / workers) group_for_worker grubunun tum etkin CPU'larina baglar. Tek grup
// veya okunamayan topoloji: dokunmaz. Basarisizlik sessizdir (is parcacigi kendi grubunda calismaya devam eder).
void spread_current_thread(std::uint32_t worker, std::uint32_t workers) noexcept;

}  // namespace ef::benchmark
