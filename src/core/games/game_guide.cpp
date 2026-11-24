#include "core/games/game_guide.h"

#include <algorithm>

#include "core/games/cs2.h"

namespace ef::core::games {

namespace {

constexpr char kCs2VideoMenu[] = "guide.cs2.menu_advanced_video";

bool has_risk(const GuideInput& in, std::string_view key) {
    return std::find(in.risk_keys.begin(), in.risk_keys.end(), key) != in.risk_keys.end();
}

}  // namespace

namespace {
// T20: Riot kimlikleri = product_settings urun adi (library_scan). Surum katalogda dogrulanmadigindan kesin menu yolu yok.
bool is_valorant(std::string_view store, std::string_view app_id) noexcept { return store == "riot" && app_id == "valorant.live"; }
bool is_lol(std::string_view store, std::string_view app_id) noexcept { return store == "riot" && app_id == "league_of_legends.live"; }

// B8: kimlikler yalniz kutuphane taramasinin dogrulayabildigi olanlardir. Fortnite = Epic manifest AppName; Warzone = Steam uygulama kimligi
// (Steam magaza sayfasi ve yayincinin Warzone yazisi ayni kimligi gosterir). Battle.net kaydi kararli kimlik vermedigi icin satir yok.
bool is_fortnite(std::string_view store, std::string_view app_id) noexcept { return store == "epic" && app_id == "Fortnite"; }
bool is_warzone(std::string_view store, std::string_view app_id) noexcept { return store == "steam" && app_id == "1962663"; }
}  // namespace

bool has_game_guide(std::string_view store, std::string_view app_id) noexcept {
    return (store == "steam" && app_id == cs2::kAppId) || is_valorant(store, app_id) || is_lol(store, app_id) || is_fortnite(store, app_id) ||
           is_warzone(store, app_id);
}

std::string_view guide_source(std::string_view store, std::string_view app_id) noexcept {
    // Adres degil kaynak etiketi (ag ilkesi: kaynakta https adresi yok); tam adresler artifacts/evidence/R-B8.md icindedir.
    if (is_fortnite(store, app_id)) return "epic.help.fortnite_fps_vsync+epic.news.competitive_settings_2026";
    if (is_warzone(store, app_id)) return "activision.blog.2021_04_warzone_graphics";
    return {};
}

std::array<std::string_view, kMaxPerfTips> perf_tip_keys() noexcept {
    return {"perf.tips.1", "perf.tips.2", "perf.tips.3", "perf.tips.4"};
}

std::string_view driver_queue_note_key(GuideRenderer r) noexcept {
    return r == GuideRenderer::D3D12 || r == GuideRenderer::Vulkan ? std::string_view("guide.driver_queue_game_managed") : std::string_view();
}

std::vector<GuideItem> select_game_guide(const GuideInput& in) {
    std::vector<GuideItem> out;
    if (!has_game_guide(in.store, in.app_id)) return out;
    // T20: Riot oyunlari. Yapilandirma dosyasi okunmaz (anti-hile alani); yalniz menude kontrol edilecek ayarlar. Vanguard
    // on kosullari (Secure Boot/TPM/HVCI) hicbir zaman kapatilmaz. Menu yolu yok: surum dogrulama kataloguna girmedi.
    if (is_valorant(in.store, in.app_id)) {
        out.push_back({"guide.valorant.reflex", "guide.valorant.reflex_effect", ""});
        out.push_back({"guide.valorant.vsync", "guide.valorant.vsync_effect", ""});
        return out;
    }
    if (is_lol(in.store, in.app_id)) {
        out.push_back({"guide.lol.vsync", "guide.lol.vsync_effect", ""});
        out.push_back({"guide.lol.fps_cap", "guide.lol.fps_cap_effect", ""});
        return out;
    }
    // B8: Epic/Activision resmi belgelerinden dogrulanan ayarlar. Oyun dosyasi, cfg ve baslatma secenegi degismez; surum katalogda
    // dogrulanmadigindan menu yolu yok; render API'sine (DX12 dahil) surucu kuyrugu vaadi yok; sayi/yuzde yok.
    if (is_fortnite(in.store, in.app_id)) {
        out.push_back({"guide.fortnite.reflex", "guide.fortnite.reflex_effect", ""});
        out.push_back({"guide.fortnite.vsync", "guide.fortnite.vsync_effect", ""});
        out.push_back({"guide.fortnite.fps_cap", "guide.fortnite.fps_cap_effect", ""});
        return out;
    }
    if (is_warzone(in.store, in.app_id)) {
        out.push_back({"guide.warzone.dynamic_resolution", "guide.warzone.dynamic_resolution_effect", ""});
        return out;
    }
    const bool exact =!in.build.empty() && std::find(in.verified_builds.begin(), in.verified_builds.end(), in.build) != in.verified_builds.end();
    const auto add = [&](const char* title, const char* effect) {
        if (out.size() < kMaxGuideItems) out.push_back({title, effect, exact ? kCs2VideoMenu : ""});
    };
    // Yalniz dogrulanmis yapilandirma bulgusu ve gozlenen render API'si.
    if (has_risk(in, "risk.video.reflex_off")) add("guide.cs2.reflex", "guide.cs2.reflex_effect");
    if (has_risk(in, "risk.video.vsync")) add("guide.cs2.vsync", "guide.cs2.vsync_effect");
    if (in.renderer == GuideRenderer::D3D9 || in.renderer == GuideRenderer::D3D11) add("guide.driver_queue", "guide.driver_queue_effect");
    return out;
}

}  // namespace ef::core::games
