#pragma once
// F-47: oyun kurulum klasorunden oynanabilir EXE yolunu cozer. Saf mantik (Win32 yok; birim testlenir).
// Kaynaklar: manifest ipucu (Epic LaunchExecutable, goreli) ve izleyicinin daha once gordugu EXE yollari
// (ConsentStore kimlikleri). Steam manifesti EXE yolu vermez; oyun bir kez calistirilmadiysa yol BILINMEZ
// ve tahmin edilmez (yanlis EXE'ye yazmaktansa "bilinmiyor" demek dogrudur).
//
// Kurallar:
//  - Yalniz kurulum klasorunun ALTINDAKI mutlak .exe yollari kabul edilir (".." reddedilir).
//  - Ipucu varsa ve guvenliyse one gecer (Epic manifesti oyunun kendi EXE'sini soyler).
//  - Ipucu yoksa: tek aday -> o; birden fazla aday -> Ambiguous (secim kullaniciya sorulmadan yapilmaz).
//  - Cikti kanonik: "/" -> "\", ASCII kucuk harf.

#include <string>
#include <string_view>
#include <vector>

namespace ef::core::games {

enum class ExeResolveStatus { Resolved, NotObserved, Ambiguous, Invalid };
std::string_view to_string(ExeResolveStatus s) noexcept;

struct GameExeResolution {
    ExeResolveStatus status = ExeResolveStatus::NotObserved;
    std::string path;  // yalniz Resolved ise dolu (kanonik utf-8)
};

// path kurulum klasorunun altinda mi (sinir duyarlı, ".." ve bos bilesen reddedilir).
bool path_under_dir(std::string_view path, std::string_view dir);

GameExeResolution resolve_game_exe(std::string_view install_dir, std::string_view exe_hint,
                                   const std::vector<std::string>& observed_exe_paths);

}  // namespace ef::core::games
