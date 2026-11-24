#pragma once
// Oyun ikonlari (spec 2026-11-12 §4): hangi dosyadan ikon okunabilecegine karar veren saf mantik (Win32 yok; birim
// testlenir). Gercek okuma platform/windows/games/game_icons.* icindedir.
//  - Kaynaklar yalniz dosyadir: Steam istemcisinin yerel kitaplik onbellegi ve oyunun kendi EXE'sinin ikon kaynagi.
//    Oyun surecine erisim, ag ve tahmin yoktur.
//  - Aday bulunamazsa liste bostur; cagiran harf kutusunu cizer. Genel/kabuk ikonu ASLA aday degildir.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/games/library_scan.h"
#include "core/inventory/disk_space.h"

namespace ef::core::games {

// Cizime hazir pikseller: on-carpilmis (premultiplied) BGRA, yukaridan asagi, satir = w * 4 bayt.
struct ImagePixels {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> bgra;
};
using ImageRef = std::shared_ptr<const ImagePixels>;

inline constexpr std::uint32_t kGameIconPx = 64;                       // yukleyici ciktisi (32 DIP kutu, %200'e kadar net)
inline constexpr std::uint32_t kImagePixelsMaxSide = 256;              // cizim katmaninin kabul ettigi en buyuk kenar
inline constexpr std::size_t kGameIconMaxFileBytes = 4u * 1024 * 1024;  // okunacak en buyuk resim dosyasi
inline constexpr std::uint32_t kGameIconMaxSourcePx = 4096;            // kaynak resmin en buyuk kenari

// Boyutlar 1..kImagePixelsMaxSide ve arabellek tam w * h * 4 bayt mi. Gecersiz resim cizilmez (harf kutusu kalir).
bool image_pixels_valid(const ImagePixels& p) noexcept;
// Ayni denetim, cagiranin verdigi en buyuk kenarla (canli test ekrani onizlemesi 960x540'a kadardir).
bool image_pixels_valid(const ImagePixels& p, std::uint32_t max_side) noexcept;

enum class IconSourceKind {
    ImageFile,    // resim dosyasi (Steam kitaplik onbellegindeki JPEG)
    ExeResource,  // EXE'nin ilk ikon kaynagi
};
struct IconCandidate {
    IconSourceKind kind = IconSourceKind::ExeResource;
    std::string path;  // utf-8 tam yol
    friend bool operator==(const IconCandidate&, const IconCandidate&) = default;
};

// Steam kitaplik ikonu dosya adi: tam olarak 40 kucuk onaltilik hane + ".jpg".
bool steam_icon_file_name(std::string_view name) noexcept;

// JPEG akisi bastan sona tam mi: SOI ile baslar, isaretci bolumleri uzunluklariyla yurunur, tarama verisi atlanir ve
// EOI isaretcisine ULASILIR (EOI'den sonraki artik baytlar onemsizdir). Yarim yazilmis / kesik dosya (baslikta ya da
// tarama verisinde biten) false doner: kod cozucu boyle bir dosyayi kismen cozebilir, yarim resim ikon diye
// gosterilmez. Yalniz yapisal denetimdir; piksel cozmez.
bool jpeg_stream_complete(const std::uint8_t* data, std::size_t size) noexcept;

// Oncelik sirasiyla ikon adaylari.
//  - steam: <steam_root>\appcache\librarycache\<appid>\ icinde TEK ikon dosyasi varsa o (0 veya >1: atlanir; klasor
//    listelenemiyorsa/yarim listelendiyse atlanir), sonra cozulmus EXE.
//  - diger launcher satirlari (epic, gog, ubisoft, xbox): cozulmus EXE (kurulum klasorunun altinda olmali). Xbox/UWP
//    paket logosu bu surumde aday DEGILDIR (saglayici dogrulanmadi).
//  - launcher satirina bagli elle eklenen EXE: cozulmus EXE yoksa o (yine kurulum klasorunun altinda olmali).
//  - manual: kullanicinin ekledigi EXE; dosya artik yoksa (manual_missing) aday yok.
//  - EXE cozulmediyse (Steam oyunu hic calistirilmadiysa) EXE adayi yoktur; tahmin edilmez.
// resolved_exe: resolve_game_exe sonucu (Resolved degilse bos).
std::vector<IconCandidate> icon_candidates(const inventory::GameInstall& game, std::string_view resolved_exe, std::string_view steam_root,
                                           LibraryFs& fs);

}  // namespace ef::core::games
