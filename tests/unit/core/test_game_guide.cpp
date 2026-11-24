// EF-X12: oyun rehberi secimi; EF-X25: gecikme bolumu. Sentetik girdiler.
#include <algorithm>
#include <cctype>
#include <string>

#include "core/games/cs2.h"
#include "core/games/game_guide.h"
#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::games;
namespace cr = ef::core::readiness;

namespace {
GuideInput cs2_input() {
    GuideInput in;
    in.store = "steam";
    in.app_id = std::string(cs2::kAppId);
    in.build = "100";
    return in;
}
}  // namespace

EF_TEST(x12_unknown_game_has_no_entry) {
    GuideInput in;
    in.store = "steam";
    in.app_id = "1";
    in.risk_keys = {"risk.video.vsync"};
    EF_CHECK(select_game_guide(in).empty());
}

EF_TEST(x12_t01_build_mismatch_withholds_exact_menu_path) {
    auto in = cs2_input();
    in.risk_keys = {"risk.video.reflex_off"};
    in.verified_builds = {"99"};
    EF_CHECK(select_game_guide(in).at(0).menu_key.empty());
    in.verified_builds.clear();
    EF_CHECK(select_game_guide(in).at(0).menu_key.empty());  // dogrulanmis surum yok
    in.verified_builds = {"100"};
    EF_CHECK(!select_game_guide(in).at(0).menu_key.empty());
    in.build.clear();  // bilinmeyen surum eslesme sayilmaz
    EF_CHECK(select_game_guide(in).at(0).menu_key.empty());
}

EF_TEST(x12_selects_at_most_three_findings_first) {
    auto in = cs2_input();
    in.risk_keys = {"risk.video.vsync", "risk.video.reflex_off"};
    in.renderer = GuideRenderer::D3D11;
    const auto v = select_game_guide(in);
    EF_CHECK(v.size() <= kMaxGuideItems && v.size() == 3 && v[0].title_key == "guide.cs2.reflex" && v[1].title_key == "guide.cs2.vsync");
    EF_CHECK(select_game_guide(cs2_input()).empty());  // bulgu yoksa oneri uydurulmaz
}

EF_TEST(x12_t03_driver_queue_only_for_dx9_dx11) {
    const auto with = [](GuideRenderer r) {
        auto in = cs2_input();
        in.renderer = r;
        const auto v = select_game_guide(in);
        return std::any_of(v.begin(), v.end(), [](const GuideItem& g) { return g.title_key == "guide.driver_queue"; });
    };
    EF_CHECK(with(GuideRenderer::D3D9) && with(GuideRenderer::D3D11));
    EF_CHECK(!with(GuideRenderer::D3D12) && !with(GuideRenderer::Vulkan) && !with(GuideRenderer::Unknown));
}

EF_TEST(x25_latency_checks_move_to_one_section_others_stay) {
    std::vector<cr::Check> v(3);
    v[0].id = "display.0.vrr";
    v[0].section = cr::Section::Display;
    v[1].id = "windows.hags";
    v[1].section = cr::Section::Windows;
    v[2].id = "windows.vbs";
    v[2].section = cr::Section::Windows;
    cr::group_latency_checks(v);
    EF_CHECK(v[0].section == cr::Section::Latency && v[1].section == cr::Section::Latency && v[2].section == cr::Section::Windows);
    {
        std::vector<cr::Check> g(1);
        g[0].id = "graphics.gsync";
        g[0].section = cr::Section::Graphics;
        cr::group_latency_checks(g);
        EF_CHECK(g[0].section == cr::Section::Latency);
        std::vector<cr::Check> other(1);
        other[0].id = "display.0.refresh";
        other[0].section = cr::Section::Display;
        cr::group_latency_checks(other);
        EF_CHECK(other.size() == 1 && other[0].section == cr::Section::Display);
    }
    EF_CHECK(v.size() == 4 && v[3].id == "latency.guide" && v[3].level == cr::Level::Info && v[3].section == cr::Section::Latency);
}

// ---- T20: Riot rehberi ve oyun ici oneriler ----
namespace {
GuideInput riot_input(const char* app_id) {
    GuideInput in;
    in.store = "riot";
    in.app_id = app_id;
    in.build = "14.1";
    return in;
}
bool catalog_has(const std::string& key) {
    const auto tr = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::TrTR);
    const auto en = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::EnUS);
    const auto id = ef::core::i18n::id_of(key);
    return tr.ok() && en.ok() && id && !tr.value().get(*id).empty() && !en.value().get(*id).empty();
}
}  // namespace

EF_TEST(t20_riot_titles_have_guides_and_other_riot_ids_do_not) {
    EF_CHECK(has_game_guide("riot", "valorant.live") && has_game_guide("riot", "league_of_legends.live"));
    EF_CHECK(!has_game_guide("riot", "bacon.live") && !has_game_guide("steam", "valorant.live"));
    EF_CHECK(select_game_guide(riot_input("bacon.live")).empty());
}

EF_TEST(t20_riot_unverified_build_gives_no_menu_path) {
    for (const char* id : {"valorant.live", "league_of_legends.live"}) {
        auto in = riot_input(id);
        in.verified_builds = {"14.1"};  // katalogda dogrulanmis menu yolu olmadigindan eslesse bile yol uretilmez
        const auto v = select_game_guide(in);
        EF_CHECK(!v.empty() && v.size() <= kMaxGuideItems);
        for (const auto& it : v) {
            EF_CHECK(it.menu_key.empty());
            EF_CHECK(catalog_has(it.title_key) && catalog_has(it.effect_key));
        }
        in.build.clear();
        in.verified_builds.clear();
        for (const auto& it : select_game_guide(in)) EF_CHECK(it.menu_key.empty());
    }
}

EF_TEST(t20_riot_guides_never_touch_vanguard_prerequisites) {
    // Rehber metinleri Secure Boot/TPM/HVCI/Vanguard kapatmayi anmaz.
    for (const char* id : {"valorant.live", "league_of_legends.live"}) {
        for (const auto& it : select_game_guide(riot_input(id))) {
            for (const auto* key : {&it.title_key, &it.effect_key}) {
                const auto cat = ef::core::i18n::Catalog::embedded(ef::core::i18n::Locale::EnUS);
                const auto sid = ef::core::i18n::id_of(*key);
                EF_CHECK(cat.ok() && sid);
                if (!cat.ok() || !sid) continue;
                std::string t(cat.value().get(*sid));
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                for (const char* bad : {"secure boot", "tpm", "hvci", "vanguard", "disable", "anti-cheat"}) EF_CHECK(t.find(bad) == std::string::npos);
            }
        }
    }
}

EF_TEST(t20_perf_tips_are_four_static_keys_without_numbers) {
    const auto keys = perf_tip_keys();
    EF_CHECK(keys.size() == kMaxPerfTips && kMaxPerfTips == 4);
    for (const auto k : keys) {
        EF_CHECK(catalog_has(std::string(k)));
        for (auto loc : {ef::core::i18n::Locale::TrTR, ef::core::i18n::Locale::EnUS}) {
            const auto cat = ef::core::i18n::Catalog::embedded(loc);
            const auto sid = ef::core::i18n::id_of(k);
            EF_CHECK(cat.ok() && sid);
            if (!cat.ok() || !sid) continue;
            const auto txt = cat.value().get(*sid);
            EF_CHECK(txt.find('%') == std::string_view::npos);  // FPS/yuzde vaadi yok
            EF_CHECK(!txt.empty() && txt.back() == '.');
        }
    }
    EF_CHECK(catalog_has("perf.tips.title"));
}

// ---- B8: Fortnite (Epic) ve Warzone (Steam) satirlari ----
namespace {
GuideInput b8_input(const char* store, const char* app_id) {
    GuideInput in;
    in.store = store;
    in.app_id = app_id;
    in.build = "1.0";
    return in;
}
std::string b8_text(ef::core::i18n::Locale loc, const std::string& key) {
    const auto cat = ef::core::i18n::Catalog::embedded(loc);
    const auto sid = ef::core::i18n::id_of(key);
    if (!cat.ok() || !sid) return {};
    return std::string(cat.value().get(*sid));
}
constexpr const char* kB8Games[][2] = {{"epic", "Fortnite"}, {"steam", "1962663"}};
}  // namespace

EF_TEST(guide_supports_fortnite_warzone) {
    EF_CHECK(has_game_guide("epic", "Fortnite"));
    EF_CHECK(has_game_guide("steam", "1962663"));
    // kimlik yalniz dogrulayan saglayicidan eslesir; baska saglayici/kimlik rehber almaz
    EF_CHECK(!has_game_guide("steam", "Fortnite") && !has_game_guide("epic", "1962663") && !has_game_guide("epic", "fortnite"));
    EF_CHECK(!has_game_guide("battlenet", "1962663") && !has_game_guide("steam", "1938090"));
    EF_CHECK(select_game_guide(b8_input("epic", "Sugar")).empty());  // bilinmeyen oyun: rehber yok
    for (const auto& g : kB8Games) EF_CHECK(!select_game_guide(b8_input(g[0], g[1])).empty());
}

EF_TEST(guide_b8_rows_have_official_source_and_strings) {
    for (const auto& g : kB8Games) {
        EF_CHECK(!guide_source(g[0], g[1]).empty());
        for (const auto& it : select_game_guide(b8_input(g[0], g[1]))) {
            EF_CHECK(catalog_has(it.title_key) && catalog_has(it.effect_key));
        }
    }
    EF_CHECK(guide_source("epic", "Sugar").empty() && guide_source("steam", "730").empty());
}

EF_TEST(guide_b8_rows_have_expected_keys) {
    const auto v = select_game_guide(b8_input("epic", "Fortnite"));
    EF_CHECK(v.size() == 3);
    if (v.size() == 3) {
        EF_CHECK(v[0].title_key == "guide.fortnite.reflex" && v[1].title_key == "guide.fortnite.vsync" &&
                  v[2].title_key == "guide.fortnite.fps_cap");
    }
    const auto w = select_game_guide(b8_input("steam", "1962663"));
    EF_CHECK(w.size() == 1 && w[0].title_key == "guide.warzone.dynamic_resolution");
}

EF_TEST(guide_unverified_build_has_no_menu_path) {
    for (const auto& g : kB8Games) {
        auto in = b8_input(g[0], g[1]);
        in.verified_builds = {"1.0"};  // katalogda dogrulanmis menu yolu yok: eslesse bile yol uretilmez
        for (const auto& it : select_game_guide(in)) EF_CHECK(it.menu_key.empty());
        in.verified_builds.clear();
        for (const auto& it : select_game_guide(in)) EF_CHECK(it.menu_key.empty());
        in.build.clear();
        for (const auto& it : select_game_guide(in)) EF_CHECK(it.menu_key.empty());
    }
}

EF_TEST(guide_item_cap) {
    for (const auto& g : kB8Games) {
        auto in = b8_input(g[0], g[1]);
        in.risk_keys = {"risk.video.vsync", "risk.video.reflex_off"};
        in.renderer = GuideRenderer::D3D11;
        EF_CHECK(select_game_guide(in).size() <= kMaxGuideItems);
    }
}

EF_TEST(guide_no_prerender_for_dx12) {
    for (const auto& g : kB8Games) {
        for (auto r : {GuideRenderer::D3D12, GuideRenderer::Vulkan, GuideRenderer::Unknown, GuideRenderer::D3D11, GuideRenderer::D3D9}) {
            auto in = b8_input(g[0], g[1]);
            in.renderer = r;
            for (const auto& it : select_game_guide(in))
                EF_CHECK(it.title_key != "guide.driver_queue" && it.effect_key != "guide.driver_queue_effect");
        }
    }
}

EF_TEST(guide_b8_texts_make_no_numeric_claims) {
    for (const auto& g : kB8Games) {
        for (const auto& it : select_game_guide(b8_input(g[0], g[1]))) {
            for (const auto* key : {&it.title_key, &it.effect_key}) {
                for (auto loc : {ef::core::i18n::Locale::TrTR, ef::core::i18n::Locale::EnUS}) {
                    const auto t = b8_text(loc, *key);
                    EF_CHECK(!t.empty());
                    EF_CHECK(t.find('%') == std::string::npos);
                    EF_CHECK(std::none_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; }));
                }
            }
        }
    }
}
