#include "core/games/exe_resolver.h"
#include "ef_test.h"

using namespace ef::core::games;

EF_TEST(exe_resolver_hint_wins_and_canonicalizes) {
    const auto r = resolve_game_exe("C:\\Games\\Epic Game", "binaries\\Win64\\Game.exe",
                                    {"C:\\Games\\Epic Game\\launcher.exe", "C:\\Games\\Epic Game\\binaries\\Win64\\Game.exe"});
    EF_CHECK(r.status == ExeResolveStatus::Resolved);
    EF_CHECK(r.path == "c:\\games\\epic game\\binaries\\win64\\game.exe");  // "/" -> "\", kucuk harf
    // Mutlak ipucu kurulum klasoru altindaysa kabul
    const auto abs = resolve_game_exe("C:/Games/Epic Game", "C:/Games/Epic Game/Game.EXE", {});
    EF_CHECK(abs.status == ExeResolveStatus::Resolved && abs.path == "c:\\games\\epic game\\game.exe");
}

EF_TEST(exe_resolver_unsafe_hint_falls_back_to_observed) {
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "..\\evil.exe", {"C:\\Games\\G\\game.exe"}).path == "c:\\games\\g\\game.exe");
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "C:\\Other\\game.exe", {"C:\\Games\\G\\game.exe"}).path == "c:\\games\\g\\game.exe");
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "notepad.exe", {}).path == "c:\\games\\g\\notepad.exe");
}

EF_TEST(exe_resolver_observed_cardinality) {
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "", {}).status == ExeResolveStatus::NotObserved);
    const auto one = resolve_game_exe("C:\\Games\\G", "", {"C:\\Games\\G\\Game.EXE", "C:\\Games\\G\\game.exe"});  // ayni yol: tekil
    EF_CHECK(one.status == ExeResolveStatus::Resolved && one.path == "c:\\games\\g\\game.exe");
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "", {"C:\\Games\\G\\a.exe", "C:\\Games\\G\\b.exe"}).status == ExeResolveStatus::Ambiguous);
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "", {"C:\\Games\\G\\a.exe", "C:\\Games\\Other\\b.exe"}).status == ExeResolveStatus::Resolved);
    EF_CHECK(resolve_game_exe("C:\\Games\\G", "", {"C:\\Games\\G\\readme.txt"}).status == ExeResolveStatus::NotObserved);
}

EF_TEST(exe_resolver_rejects_invalid_install_dir) {
    EF_CHECK(resolve_game_exe("", "", {}).status == ExeResolveStatus::Invalid);
    EF_CHECK(resolve_game_exe("Games\\G", "", {}).status == ExeResolveStatus::Invalid);
    EF_CHECK(resolve_game_exe("C:\\Games\\..\\G", "", {}).status == ExeResolveStatus::Invalid);
    EF_CHECK(!path_under_dir("C:\\Games\\G2\\a.exe", "C:\\Games\\G"));  // sinir duyarli
    EF_CHECK(path_under_dir("C:\\Games\\G\\a.exe", "C:\\Games\\G\\"));
    EF_CHECK(!path_under_dir("C:\\Games\\G\\..\\evil.exe", "C:\\Games\\G"));
}
