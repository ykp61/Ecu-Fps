#pragma once
// EF-X11: kullanicinin elle ekledigi oyun EXE'leri. Saf dogrulama (Win32 yok). Yol yalniz bu uygulamanin tercih
// dosyasina yazilir; oyun dosyasina/launcher'a dokunulmaz.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::games {

inline constexpr std::size_t kMaxManualGames = 32;
inline constexpr std::size_t kMaxManualPathBytes = 512;  // Preferences yol siniri

enum class ManualExeCheck { Ok, Empty, TooLong, NotAbsolute, Network, ParentComponent, NotExe, DriveRoot, SystemDir, ProtectedRoot, Duplicate, Limit };

struct ManualExeContext {
    std::string windows_dir;               // %WINDIR%: altindaki her EXE reddedilir
    std::vector<std::string> exact_roots;  // Program Files(x86), ProgramData, profil, Masaustu, Indirilenler: EXE dogrudan burada olamaz
};

std::string canonical_manual_exe(std::string_view path);  // kirpilmis, "\" ayracli, ASCII kucuk harf
std::string manual_exe_dir(std::string_view path);        // ust klasor (ozgun yazim); gecersizse bos
std::string manual_exe_name(std::string_view path);       // dosya adi, ".exe" olmadan
ManualExeCheck check_manual_exe(std::string_view path, const ManualExeContext& ctx, const std::vector<std::string>& existing);
// Diskten okunan liste (elle/baska programla duzenlenmis olabilir) ekleme ile AYNI kurallardan gecer: Windows klasoru,
// korumali kok, tekrar ve sinir. Gecmeyen giris izlenmez (sistem sureci "oyun" sayilmaz). Sira korunur.
std::vector<std::string> filter_manual_exes(const std::vector<std::string>& stored, const ManualExeContext& ctx);

}  // namespace ef::core::games
