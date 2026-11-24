#pragma once
// T06: fare rapor zaman damgalari (salt okuma; ayar yazilmaz, sistem durumu degismez; yonetici gerekmez).
//  - Yalniz cagiran is parcacigi uzerinde mesaj-yalniz pencere (HWND_MESSAGE) + Raw Input (usage page 1, usage 2,
//    RIDEV_INPUTSINK) kaydi acilir; sure sonunda ya da iptalde kayit kaldirilir, pencere yok edilir.
//  - Her WM_INPUT'ta once GetRawInputData, sonra GetRawInputBuffer ile birikmis kayitlar da okunur; yalniz bagil
//    HAREKET olan kayitlar sayilir (dugme/tekerlek degil). Kayit basina zaman damgasi yoktur: ayni toplu okumadaki
//    kayitlar ayni QPC degerini paylasir (estimate_polling_rate bunu "alt sinir" olarak isaretler; tahmin uretilmez).
//  - Fare icerigi (x/y/dugme) saklanmaz; yalniz zaman damgasi bellekte tutulur.
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace ef::platform::metrics {

struct RawMouseCapture {
    std::vector<std::int64_t> qpc_ticks;
    std::int64_t qpc_freq = 0;
};

// duration_ms boyunca (veya cancel true olana dek) kaydeder. Pencere/kayit olusamazsa nullopt.
std::optional<RawMouseCapture> capture_raw_mouse(int duration_ms, const std::atomic<bool>& cancel);

}  // namespace ef::platform::metrics
