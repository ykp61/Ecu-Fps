#include "core/inventory/disk_space.h"
#include "ef_test.h"

using namespace ef::core::inventory;
using ef::contracts::Field;
using ef::contracts::Provenance;

namespace {
VolumeInfo vol(const char* root, std::uint32_t disk, std::uint64_t total, std::uint64_t free_gib) {
    VolumeInfo v;
    v.root = root;
    v.disk_number = Field<std::uint32_t>::known(disk, Provenance::Api);
    v.total_gib = Field<std::uint64_t>::known(total, Provenance::Api);
    v.free_gib = Field<std::uint64_t>::known(free_gib, Provenance::Api);
    v.removable = Field<bool>::known(false, Provenance::Api);
    return v;
}
StorageDevice disk(std::uint32_t n, MediaKind m) {
    StorageDevice d;
    d.disk_number = n;
    d.media = m;
    d.bus = m == MediaKind::Hdd ? BusKind::Sata : BusKind::Nvme;
    d.model = Field<std::string>::known("Disk", Provenance::Api);
    return d;
}
}  // namespace

EF_TEST(disk_steam_and_epic_parsers) {
    const char* vdf = R"vdf("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"apps" { "228980" "550908377" }
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
	}
})vdf";
    const auto libs = parse_steam_libraryfolders(vdf);
    EF_CHECK(libs.size() == 2 && libs[0] == "C:\\Program Files (x86)\\Steam" && libs[1] == "D:\\SteamLibrary");
    const char* acf = R"acf("AppState"
{
	"appid"		"730"
	"name"		"Counter-Strike 2"
	"installdir"		"Counter-Strike Global Offensive"
	"SizeOnDisk"		"37580963840"
	"UserConfig" { "name" "ignored" }
})acf";
    const auto g = parse_steam_appmanifest(acf, "D:\\SteamLibrary");
    EF_CHECK(g && g->name == "Counter-Strike 2" && g->size_bytes == 37580963840ULL &&
              g->install_dir == "D:\\SteamLibrary\\steamapps\\common\\Counter-Strike Global Offensive");
    EF_CHECK(!parse_steam_appmanifest(R"("AppState" { "name" "x" })", "C:\\"));  // boyut/dizin yok
    EF_CHECK(!parse_steam_appmanifest(R"("AppState" { "appid" "228980" "name" "Steamworks Common Redistributables" "installdir" "Steamworks Shared" "SizeOnDisk" "550908377" })",
                                       "C:\\"));  // ortak paket oyun sayilmaz
    const auto e = parse_epic_manifest(R"({"DisplayName":"Windrose","InstallLocation":"F:\\WIndrose","InstallSize":22415019116})");
    EF_CHECK(e && e->launcher == "epic" && e->size_bytes == 22415019116ULL);
    EF_CHECK(!parse_epic_manifest("{}") && !parse_epic_manifest("bozuk"));
    EF_CHECK(parse_steam_libraryfolders("bozuk {{{").empty());
}

EF_TEST(disk_space_thresholds_and_game_advice) {
    const std::vector<VolumeInfo> vols{vol("C:\\", 0, 1000, 500), vol("D:\\", 1, 1000, 120), vol("E:\\", 2, 1000, 60), vol("F:\\", 3, 2000, 30)};
    const std::vector<StorageDevice> disks{disk(0, MediaKind::Ssd), disk(1, MediaKind::Ssd), disk(2, MediaKind::Hdd), disk(3, MediaKind::Ssd)};
    const std::vector<GameInstall> games{{"Big", "d:\\SteamLibrary\\steamapps\\common\\Big", 400ULL << 30, "steam"},
                                         {"Small", "D:\\Games\\Small", 10ULL << 30, "epic"},
                                         {"Old", "E:\\Games\\Old", 50ULL << 30, "steam"},
                                         {"Mid", "F:\\G", 40ULL << 30, "epic"}};
    const auto a = advise_volumes(vols, disks, games);
    EF_CHECK(a.size() == 4);
    EF_CHECK(a[0].status == SpaceStatus::Ok && a[0].games == 0 && !a[0].recommended_free_gib);  // oyun yok: tavsiye uydurulmaz
    // D: %12 bos -> uyari; 2 oyun, en buyugu 400 GB -> tavsiye 100 GB (yeterli)
    EF_CHECK(a[1].status == SpaceStatus::Warning && a[1].games == 2 && a[1].largest_game == "Big" && *a[1].recommended_free_gib == 100);
    // E: %6 bos -> kritik; HDD'de oyun
    EF_CHECK(a[2].status == SpaceStatus::Critical && a[2].games_on_hdd);
    // F: %1.5 bos -> kritik; tavsiye en az 20 GB
    EF_CHECK(a[3].status == SpaceStatus::Critical && *a[3].recommended_free_gib == 20);
    // Yuzde yeterli ama oyun icin alan yetersiz -> uyari
    const auto b = advise_volumes({vol("G:\\", 4, 1000, 160)}, {disk(4, MediaKind::Ssd)}, {{"Huge", "G:\\x", 800ULL << 30, "steam"}});
    EF_CHECK(b[0].status == SpaceStatus::Warning && *b[0].recommended_free_gib == 200);
    // Bilinmeyen boyut: Unknown (0 sayilmaz)
    VolumeInfo u;
    u.root = "H:\\";
    u.total_gib = Field<std::uint64_t>::unknown("x");
    u.free_gib = Field<std::uint64_t>::unknown("x");
    EF_CHECK(advise_volumes({u}, {}, {})[0].status == SpaceStatus::Unknown);
}
