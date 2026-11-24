// Uyumluluk #21: atomik yazim gecici paylasim/erisim/kilit hatasinda (AV/EDR/yedek ajani dosyayi kisa sure tutar)
// artan beklemeyle tekrar denenir. Saf yeniden deneme cekirdegi + gecici dizinde gercek Windows dosya kilidi.
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "ef_test.h"
#include "platform/windows/journal_fs.h"

using namespace ef::platform;
using namespace ef::platform::detail;

namespace {

struct ScratchDir {
    std::filesystem::path dir;
    ~ScratchDir() {
        if (dir.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

std::filesystem::path make_dir() {
    std::error_code ec;
    const auto base = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    for (int i = 0; i < 100; ++i) {
        const auto p = base / (L"ef_fs_retry_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(i));
        if (std::filesystem::create_directory(p, ec)) return p;
        ec.clear();
    }
    return {};
}

}  // namespace

EF_TEST(fs_retry_transient_codes_are_sharing_access_and_lock) {
    EF_CHECK(transient_fs_error(ERROR_SHARING_VIOLATION));
    EF_CHECK(transient_fs_error(ERROR_ACCESS_DENIED));
    EF_CHECK(transient_fs_error(ERROR_LOCK_VIOLATION));
    EF_CHECK(!transient_fs_error(ERROR_DISK_FULL));
    EF_CHECK(!transient_fs_error(ERROR_PATH_NOT_FOUND));
    EF_CHECK(!transient_fs_error(ERROR_FILE_NOT_FOUND));
    // Okumada erisim reddi kalici: tarama dongusu bekletilmez.
    EF_CHECK(transient_read_error(ERROR_SHARING_VIOLATION) && transient_read_error(ERROR_LOCK_VIOLATION));
    EF_CHECK(!transient_read_error(ERROR_ACCESS_DENIED) && !transient_read_error(ERROR_FILE_NOT_FOUND));
}

EF_TEST(fs_retry_backs_off_and_stops_on_success) {
    std::vector<unsigned long> slept;
    int calls = 0, attempts = 0;
    const auto e = retry_transient(
        [&]() -> unsigned long { return ++calls < 3 ? ERROR_SHARING_VIOLATION : 0; },
        [&](unsigned long ms) { slept.push_back(ms); }, &attempts);
    EF_CHECK(e == 0);
    EF_CHECK(calls == 3 && attempts == 3);
    EF_CHECK((slept == std::vector<unsigned long>{15, 30}));
}

EF_TEST(fs_retry_gives_up_after_limit_with_last_code) {
    std::vector<unsigned long> slept;
    int calls = 0, attempts = 0;
    const auto e = retry_transient([&]() -> unsigned long { ++calls; return ERROR_ACCESS_DENIED; },
                                   [&](unsigned long ms) { slept.push_back(ms); }, &attempts);
    EF_CHECK(e == ERROR_ACCESS_DENIED);
    EF_CHECK(calls == kFsRetryAttempts && attempts == kFsRetryAttempts);
    EF_CHECK((slept == std::vector<unsigned long>{15, 30, 60, 120}));  // son denemeden sonra bekleme yok
}

EF_TEST(fs_retry_permanent_error_is_not_retried) {
    int calls = 0, attempts = 0;
    bool slept = false;
    const auto e = retry_transient([&]() -> unsigned long { ++calls; return ERROR_DISK_FULL; },
                                   [&](unsigned long) { slept = true; }, &attempts);
    EF_CHECK(e == ERROR_DISK_FULL && calls == 1 && attempts == 1 && !slept);
}

// Gercek dosya: hedef baska bir tutamakla paylasimsiz acikken (tarayici gibi) yazim, tutamak kisa sure sonra
// birakildiginda basarili olur ve geri okunan icerik yazilanla aynidir.
EF_TEST(journal_fs_write_atomic_survives_briefly_locked_target) {
    ScratchDir s{make_dir()};
    EF_CHECK(!s.dir.empty());
    if (s.dir.empty()) return;
    WinJournalFs fs(s.dir);
    EF_CHECK(fs.write_atomic("preferences.json", "old").ok());
    HANDLE lock = CreateFileW((s.dir / L"preferences.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    EF_CHECK(lock != INVALID_HANDLE_VALUE);
    std::thread release([lock] {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        CloseHandle(lock);
    });
    const auto st = fs.write_atomic("preferences.json", "new");
    release.join();
    EF_CHECK(st.ok());
    const auto back = fs.read("preferences.json");
    EF_CHECK(back.ok() && back.value() == "new");
    EF_CHECK(fs.temp_files().empty());
}

// Kilit hic birakilmazsa yazim sinirli denemeden sonra hata verir (sonsuz bekleme yok) ve gecici dosya kalmaz.
EF_TEST(journal_fs_write_atomic_fails_bounded_when_lock_persists) {
    ScratchDir s{make_dir()};
    EF_CHECK(!s.dir.empty());
    if (s.dir.empty()) return;
    WinJournalFs fs(s.dir);
    EF_CHECK(fs.write_atomic("preferences.json", "old").ok());
    HANDLE lock = CreateFileW((s.dir / L"preferences.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    EF_CHECK(lock != INVALID_HANDLE_VALUE);
    const auto t0 = std::chrono::steady_clock::now();
    const auto st = fs.write_atomic("preferences.json", "new");
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CloseHandle(lock);
    EF_CHECK(!st.ok());
    EF_CHECK(st.error().detail.find("commit journal") != std::string::npos);
    EF_CHECK(st.error().detail.find("attempts") != std::string::npos);
    EF_CHECK(ms < 2000);
    EF_CHECK(fs.temp_files().empty());
    const auto back = fs.read("preferences.json");
    EF_CHECK(back.ok() && back.value() == "old");  // eski icerik bozulmadi
}
