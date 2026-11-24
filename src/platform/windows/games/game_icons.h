#pragma once
// Oyun ikonu yukleyicileri (spec 2026-11-12 §4). Yalniz DOSYA okur: oyun surecine erisim yok, surec tutamaci
// acilmaz, ag yok, dosyaya yazilmaz.
//  - EXE ikon kaynagi: SHDefExtractIconW(yol, 0, 0, &hicon, nullptr, 64). S_FALSE = ikon kaynagi yok, E_FAIL = dosyaya
//    erisilemedi. Kabuk/genel ikon geri donusu YOKTUR (SHGetFileInfo / IShellItemImageFactory kullanilmaz).
//  - Resim dosyasi (Steam kitaplik onbellegi JPEG'i): sinirli okuma (<= 4 MiB), yalniz Windows'un kendi WIC JPEG kod
//    cozucusu (CLSID_WICJpegDecoder; ucuncu taraf codec devreye girmez), kaynak kenari <= 4096 px. Kesik (EOI'ye
//    ulasmayan) JPEG cozulmez.
//  - Cikti her zaman 64x64 on-carpilmis BGRA (kare olmayan kaynak oran korunarak ortalanir).
// Tek is parcacigindan kullanilir (izleme is parcacigi); yukleyici kendi COM kapsamini acar ve kapatir.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/games/game_icon.h"

namespace ef::platform::games {

struct IconLoad {
    core::games::ImageRef image;  // bos = ikon yok (cagiran harf kutusunu cizer)
    // true: dosya okundu ve sonuc kesin (ikon var / ikon kaynagi yok / resim bozuk ya da sinir disi).
    // false: dosyaya erisilemedi veya yukleyici hazir degil; sonuc onbellege "ikon yok" diye yazilmaz.
    bool definitive = false;
};

class GameIconLoader {
public:
    GameIconLoader();
    ~GameIconLoader();
    GameIconLoader(const GameIconLoader&) = delete;
    GameIconLoader& operator=(const GameIconLoader&) = delete;

    bool ready() const noexcept;  // WIC fabrikasi olusturuldu
    IconLoad load_exe_icon(const std::string& exe_path_utf8);
    IconLoad load_image_file(const std::string& path_utf8);
    IconLoad load(const core::games::IconCandidate& candidate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Onbellek anahtarinin dosya damgasi (yalniz oznitelik okunur; dosya acilmaz).
struct IconFileStamp {
    bool ok = false;
    std::uint64_t mtime = 0, size = 0;
};
IconFileStamp icon_file_stamp(const std::string& path_utf8);

// Izleme is parcacigina ait kucuk onbellek (kilit yok). Anahtar = kucuk harf yol + son yazma zamani + boyut.
// Kitaplikta artik olmayan satirlar ve dosyalar bastan sona derin calisan her turdan sonra dusurulur; oyun calisirken
// gelen (derin olmayan) tur hicbir girdiyi dusurmez (o turun listesi eksik olabilir, dosyalar yeniden okunamaz).
class GameIconCache {
public:
    void begin_pass();
    // Derin tur: adaylar sirayla denenir (damga ayni ise dosya yeniden okunmaz). Sonuc satir icin hatirlanir.
    core::games::ImageRef resolve(const std::string& row_key, const std::vector<core::games::IconCandidate>& candidates,
                                  GameIconLoader& loader);
    // Oyun calisirken (derin kapi kapali): hicbir dosyaya dokunmadan satirin son bilinen ikonu (yoksa bos).
    core::games::ImageRef remembered(const std::string& row_key);
    // files_complete: tur bastan sona derin calisti (yalniz o zaman kullanilmayan satir ve dosya girdileri dusurulur).
    void end_pass(bool files_complete);

    std::size_t rows() const noexcept { return rows_.size(); }
    std::size_t files() const noexcept { return files_.size(); }
    std::size_t loads() const noexcept { return loads_; }  // gercek dosya okuma sayisi (test/olcum)

private:
    struct FileEntry {
        std::uint64_t mtime = 0, size = 0;
        core::games::ImageRef image;  // bos = kesin "ikon yok"
        bool used = false;
    };
    struct RowEntry {
        core::games::ImageRef image;
        bool used = false;
    };
    std::map<std::string, FileEntry> files_;
    std::map<std::string, RowEntry> rows_;
    std::size_t loads_ = 0;
};

}  // namespace ef::platform::games
