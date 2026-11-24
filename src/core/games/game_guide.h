#pragma once
// EF-X12: surumlu oyun ayar rehberi (saf; birim testlenir).
//  - Kullanici ayari oyun icinde kendisi uygular; cfg, launch secenegi veya oyun dosyasi DEGISTIRILMEZ.
//  - Kurulu surum katalogda dogrulanmamissa kesin menu yolu gosterilmez, yalniz ayar adi ve etkisi yazilir.
//  - Olculmemis yuzde FPS kazanci yok. En cok kMaxGuideItems ayar.
//  - Surucu kuyrugu yalniz DX9/DX11 icin anlatilir; DX12/Vulkan'a veya bilinmeyen renderer'a vaat edilmez.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::games {

inline constexpr std::size_t kMaxGuideItems = 3;

enum class GuideRenderer { Unknown, D3D9, D3D11, D3D12, Vulkan };

struct GuideItem {
    std::string title_key;
    std::string effect_key;
    std::string menu_key;  // bos: kesin menu yolu yok
};

struct GuideInput {
    std::string store, app_id, build;
    std::vector<std::string> verified_builds;
    GuideRenderer renderer = GuideRenderer::Unknown;
    std::vector<std::string> risk_keys;  // salt okunur yapilandirma bulgulari
};

// Katalogda oyun yoksa bos doner; genel rehber gorunur kalir.
std::vector<GuideItem> select_game_guide(const GuideInput& in);
bool has_game_guide(std::string_view store, std::string_view app_id) noexcept;
// B8: satirin dayandigi resmi (yayinci) belgenin kaynak etiketi (adres degil; tam adres kanit dosyasindadir). Arayuzde gosterilmez.
// Kaynagi olmayan oyun icin bos.
std::string_view guide_source(std::string_view store, std::string_view app_id) noexcept;
// EF-X25 T01: surucu kare kuyrugu kapsami; yalniz aciklama secer (oyun dosyasi/surucu yazilmaz).
// DX12/Vulkan gozlendiyse "oyun yonetir" notu; DX9/DX11 bos (guide.driver_queue anlatir); Unknown bos (iddia yok).
std::string_view driver_queue_note_key(GuideRenderer r) noexcept;

// T20: Performans Acik sonrasi "Oyun ici oneriler" (sabit, sayisiz; FPS vaadi yok). En cok kMaxPerfTips madde.
inline constexpr std::size_t kMaxPerfTips = 4;
std::array<std::string_view, kMaxPerfTips> perf_tip_keys() noexcept;

}  // namespace ef::core::games
