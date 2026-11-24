#pragma once
// P2 WP1: arka plan program taramasi (SALT OKUMA). Calisan sureclerden yalniz derlenmis izinli listedeki dosya adlarina
// sahip olanlar icin olculmus BgProcessInfo uretir: yol, DOGRULANMIS imza yayincisi (Authenticode), iki ornekten CPU%
// ve I/O, calisma kumesi, on plan, oturum/kullanici, yukseltme. Erisim yalniz PROCESS_QUERY_LIMITED_INFORMATION;
// surece yazma, bellek okuma, kapatma, enjeksiyon YOK. Oyun bellegine dokunulmaz.
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/perf/background_candidates.h"

namespace ef::platform::background {

class ProcessScanner {
public:
    // `game_active`: koruma kapisi oyun/belirsizlik bildiriyor -> lansor ve sohbet programlari muhafazakar elenir
    // (oyun lansorlari ve overlay'ler algilanamaz; tahmin yok, dislanir). Cagri ~sample_ms sureyle bloklar
    // (arka plan is parcaciginda cagrilmalidir).
    std::vector<core::perf::BgProcessInfo> scan(bool game_active, int sample_ms = 700);

private:
    struct SigInfo {
        std::string publisher;
        bool valid = false;
    };
    SigInfo signature_of(const std::wstring& path);
    std::mutex mu_;
    std::map<std::wstring, SigInfo> sig_cache_;          // kucuk harf yol -> sonuc
    std::map<std::uint32_t, std::uint64_t> last_fg_ms_;  // pid -> son on plan zamani (monoton ms)
};

}  // namespace ef::platform::background
