// EF-X12.02: rehber/algilama oyun dosyalarini DEGISTIRMEZ. Gecici oyun klasoru fikstürü (gercek Windows dosya sistemi);
// rehber secimi, kitaplik taramasi (WinLibraryFs), cfg ayristirma ve risk taramasindan once/sonra SHA-256 ayni olmali.
// Bu bir fikstur testidir: gercek oyun dosyasi uzerinde kabul kaniti degildir (EF-X12.03 cihaz kabulu).
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "core/games/config_risk.h"
#include "core/games/cs2.h"
#include "core/games/game_guide.h"
#include "core/games/library_scan.h"
#include "core/recovery/sha256.h"
#include "platform/windows/games/library_scan.h"
#include "platform/windows/win_env.h"
#include "ef_test.h"

using namespace ef::core::games;
namespace sf = std::filesystem;

namespace {

struct GameFixture {
    sf::path root;
    GameFixture() {
        wchar_t t[MAX_PATH];
        GetTempPathW(MAX_PATH, t);
        root = sf::path(t) / (L"ef_noguidewrite_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()));
        sf::create_directories(game_dir() / L"game" / L"bin" / L"win64");
        sf::create_directories(game_dir() / L"game" / L"csgo" / L"cfg");
        sf::create_directories(root / L"steam" / L"steamapps");
        write(root / L"steam" / L"steamapps" / L"appmanifest_730.acf",
              "\"AppState\"\n{\n\t\"appid\"\t\t\"730\"\n\t\"name\"\t\t\"Counter-Strike 2\"\n\t\"installdir\"\t\t\"cs2\"\n"
              "\t\"SizeOnDisk\"\t\t\"1024\"\n\t\"buildid\"\t\t\"100\"\n}\n");
        std::string bin;
        for (int i = 0; i < 4096; ++i) bin.push_back(static_cast<char>((i * 31 + 7) & 0xFF));  // yapay "exe/dll" baytlari
        write(game_dir() / L"game" / L"bin" / L"win64" / L"cs2.exe", bin);
        write(game_dir() / L"game" / L"bin" / L"win64" / L"engine2.dll", bin + "x");
        write(game_dir() / L"game" / L"csgo" / L"cfg" / L"video.txt",
              "\"video.cfg\"\n{\n\t\"setting.fullscreen\"\t\t\"1\"\n\t\"setting.vsync\"\t\t\"1\"\n\t\"setting.msaa_samples\"\t\t\"8\"\n}\n");
        write(game_dir() / L"game" / L"csgo" / L"cfg" / L"autoexec.cfg", "fps_max 0\n");
    }
    ~GameFixture() {
        std::error_code ec;
        sf::remove_all(root, ec);
    }
    sf::path game_dir() const { return root / L"steam" / L"steamapps" / L"common" / L"cs2"; }
    static void write(const sf::path& p, const std::string& text) {
        std::ofstream f(p, std::ios::binary);
        f << text;
    }
    static std::string read(const sf::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    // Agacin tum dosyalari: goreli yol -> SHA-256 (steamapps manifestleri dahil: algilama yalniz okur).
    std::map<std::string, std::string> snapshot() const {
        std::map<std::string, std::string> m;
        for (const auto& e : sf::recursive_directory_iterator(root)) {
            if (!e.is_regular_file()) continue;
            m[ef::platform::to_utf8(sf::relative(e.path(), root).wstring())] = ef::core::recovery::sha256_hex(read(e.path()));
        }
        return m;
    }
};

}  // namespace

EF_TEST(x12_guide_and_detection_leave_game_files_hash_identical) {
    GameFixture fx;
    const auto before = fx.snapshot();
    EF_CHECK(before.size() == 5);  // fikstur gercekten dolu (bos agac karsilastirmasi anlamsiz olur)

    // 1) Algilama: gercek dosya sistemi uzerinden kitaplik taramasi.
    ef::platform::games::WinLibraryFs fs;
    fs.set_overrides(ef::platform::to_utf8((fx.root / L"steam").wstring()), {});
    const auto lib = scan_library(fs);
    EF_CHECK(lib.state == LibraryScanState::Ok && lib.games.size() == 1);

    // 2) cfg okuma + risk taramasi + rehber secimi (rehber girdisi okunan cfg'den).
    const auto cfg_text = GameFixture::read(fx.game_dir() / L"game" / L"csgo" / L"cfg" / L"video.txt");
    const auto kv = cs2::parse_video_cfg(cfg_text);
    EF_CHECK(kv.has_value());
    const auto risks = scan_cs2_video(cs2::typed(*kv));
    EF_CHECK(!risks.empty());
    GuideInput in;
    in.store = "steam";
    in.app_id = std::string(cs2::kAppId);
    in.build = "100";
    in.verified_builds = {"100"};
    in.renderer = GuideRenderer::D3D11;
    for (const auto& r : risks) in.risk_keys.push_back(r.code);
    const auto items = select_game_guide(in);
    EF_CHECK(!items.empty() && items.size() <= kMaxGuideItems);
    in.verified_builds.clear();  // surum uyusmazligi yolu da calissin
    (void)select_game_guide(in);
    (void)scan_launch_options("-high -threads 8 -novid");

    const auto after = fx.snapshot();
    EF_CHECK(after == before);  // yol kumesi ve her dosyanin SHA-256'si ayni
}

EF_TEST(x12_hash_snapshot_detects_a_change) {
    // Kontrol testi: anlik goruntu gercekten degisikligi yakalar (ayni dosya adi, tek bayt farki).
    GameFixture fx;
    const auto before = fx.snapshot();
    GameFixture::write(fx.game_dir() / L"game" / L"csgo" / L"cfg" / L"autoexec.cfg", "fps_max 1\n");
    EF_CHECK(fx.snapshot() != before);
}
