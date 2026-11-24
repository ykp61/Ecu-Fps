#pragma once
// Yerel cokme kaydi: islenmemis istisnada kucuk minidump yazar (MiniDumpNormal: heap yok,
// kisisel veri riski dusuk). En fazla max_dumps dosya tutulur. Otomatik gonderim yoktur.

#include <cstddef>
#include <filesystem>

namespace ef::platform {

struct CrashConfig {
    std::filesystem::path dir;   // ornek: %LOCALAPPDATA%\EcuFps\crash
    std::size_t max_dumps = 3;
};

// Islenmemis istisna filtresini kurar. Tekrar cagrilirsa yapilandirmayi gunceller.
bool install_crash_handler(const CrashConfig& cfg);

// Denetim 4.1: arka plan is parcacigindan kacan C++ istisnasi. MSVC'de sonlandirma isleyicisi is parcacigina ozgudur ve
// abort() islenmemis istisna filtresini atlar; dump yazilmaz, neden kaybolurdu. Bu cagri istisna metnini parametre olarak
// tasiyan yapay bir SEH istisnasi firlatir: filtre minidump yazar ve surec sessizce sonlanir. Islem journal'i bir sonraki
// acilista uzlastirilir (yarim kalan degisiklik geri alinir); belirsiz durumda devam etmekten guvenlidir.
[[noreturn]] void fatal_worker_exception(const char* what) noexcept;
// Cagiran is parcacigi icin std::terminate isleyicisini fatal_worker_exception'a baglar.
void guard_current_thread() noexcept;
inline constexpr unsigned long kWorkerExceptionCode = 0xE17A0001;  // istisna kodu (dump'ta)

// Eski dump'lari siler; en yeni max_dumps kalir. Test ve baslangicta cagrilabilir.
void prune_dumps(const std::filesystem::path& dir, std::size_t max_dumps);

}  // namespace ef::platform
