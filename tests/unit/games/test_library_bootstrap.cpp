// B2: kitaplik taramasi -> izleyici baslangic karari. Hata nedeniyle bos liste "oyun yok" degildir.
#include "platform/windows/games/library_scan.h"
#include "ef_test.h"

using namespace ef::platform::games;
using ef::core::games::DetectionState;

namespace {
LibraryScanResult scan(LibraryScanState state, std::size_t games = 0) {
    LibraryScanResult r;
    r.state = state;
    r.games.resize(games);
    return r;
}
}  // namespace

EF_TEST(library_bootstrap_trusted_empty_is_no_games) {
    const auto b = watch_bootstrap(scan(LibraryScanState::Empty));
    EF_CHECK(b.trusted && b.no_games && b.detection == DetectionState::Ok);
    const auto with_games = watch_bootstrap(scan(LibraryScanState::Ok, 2));
    EF_CHECK(with_games.trusted && !with_games.no_games && with_games.detection == DetectionState::Ok);
}

EF_TEST(library_bootstrap_error_empty_is_not_no_games) {
    const auto failed = watch_bootstrap(scan(LibraryScanState::Failed));
    EF_CHECK(!failed.trusted && !failed.no_games && failed.detection == DetectionState::Failed);
    const auto partial_empty = watch_bootstrap(scan(LibraryScanState::Partial));
    EF_CHECK(!partial_empty.trusted && !partial_empty.no_games && partial_empty.detection == DetectionState::Incomplete);
}

EF_TEST(library_bootstrap_partial_with_games_stays_untrusted) {
    const auto b = watch_bootstrap(scan(LibraryScanState::Partial, 3));
    EF_CHECK(!b.trusted && !b.no_games && b.detection == DetectionState::Incomplete);
}

EF_TEST(library_bootstrap_default_result_is_fail_closed) {
    const auto b = watch_bootstrap(LibraryScanResult{});
    EF_CHECK(!b.trusted && !b.no_games && b.detection == DetectionState::Failed);
}
