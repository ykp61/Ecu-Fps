// WIN-08 Windows testi (gercek Win32 dosya API'si).
//   tree     -> %TEMP% altinda KENDI gecici agacini kurar (dosyalar, ic ice dizinler, paylasimsiz acik
//               kilitli dosya, agac DISINDAKI nobetci dosyaya isaret eden junction) ve gercek motoru yalniz
//               bu agaca enjekte edilen kokla calistirir. Kullanicinin gercek klasorlerine dokunulmaz. (ctest)
//   preview  -> gercek kategoriler icin YALNIZ onizleme (hicbir sey silinmez); elle calistirilir.
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/cleanup/cleanup.h"
#include "platform/windows/cleanup/win_cleanup.h"
#include "platform/windows/win_env.h"

using namespace ef;
namespace cc = core::cleanup;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++failures;
}

bool write_file(const fs::path& p, std::size_t size) {
    std::ofstream f(p, std::ios::binary);
    std::string buf(size, 'x');
    f.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return static_cast<bool>(f);
}

// EXT-CLN-02: %TEMP% kategorisi yalniz 7 gunden eski dosyalari onizler; taze test dosyalarini eskit.
bool age_file(const fs::path& p, std::uint64_t days) {
    HANDLE h = CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= days * 864000000000ULL;  // 100ns birim
    const FILETIME old{u.LowPart, u.HighPart};
    const bool ok = SetFileTime(h, nullptr, nullptr, &old) != 0;
    CloseHandle(h);
    return ok;
}

bool make_junction(const fs::path& link, const fs::path& target) {
    // Junction olusturmak yonetici hakki gerektirmez; cmd mklink /J kullanilir.
    std::wstring cmd = L"cmd.exe /d /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\" >nul";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    const DWORD a = GetFileAttributesW(link.c_str());
    return code == 0 && a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

std::string human(std::uint64_t b) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%llu B (%.1f MiB)", static_cast<unsigned long long>(b), static_cast<double>(b) / 1048576.0);
    return buf;
}

int run_tree() {
    wchar_t tmp[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH + 1, tmp);
    const fs::path base = fs::path(tmp) / (L"ef_cleanup_probe_" + std::to_wstring(GetCurrentProcessId()));
    const fs::path root = base / L"Temp";  // UserTemp kok ad kurali: "Temp"
    const fs::path outside = base / L"outside";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(root / L"sub" / L"deep", ec);
    fs::create_directories(root / L"emptydir", ec);
    fs::create_directories(outside, ec);

    bool ok = write_file(root / L"a.bin", 1000) && write_file(root / L"sub" / L"b.bin", 2500) &&
              write_file(root / L"sub" / L"deep" / L"c.bin", 123) && write_file(root / L"locked.bin", 4096) &&
              write_file(outside / L"sentinel.txt", 77) && write_file(root / L"rewritten.bin", 500) &&
              write_file(root / L"swapped.bin", 600);
    check(ok, "test agaci olusturuldu");
    bool aged = true;
    for (const auto& p : {root / L"a.bin", root / L"sub" / L"b.bin", root / L"sub" / L"deep" / L"c.bin", root / L"locked.bin",
                          root / L"rewritten.bin", root / L"swapped.bin"})
        aged = age_file(p, 30) && aged;
    check(aged, "test dosyalari 30 gun eskitildi (7 gun yas kurali)");
    const bool junction = make_junction(root / L"link", outside);
    check(junction, "agac disina isaret eden junction olusturuldu");

    HANDLE locked = CreateFileW((root / L"locked.bin").c_str(), GENERIC_READ, 0 /*paylasim yok*/, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "kilitli dosya paylasimsiz acik tutuluyor");

    auto env = platform::cleanup::discover_env();
    env.root_override[cc::CleanupCategory::UserTemp] = {root.wstring()};
    platform::cleanup::WinCleanupFs wfs;

    auto plan = cc::preview({cc::CleanupCategory::UserTemp}, env, wfs);
    const auto* p = plan.find(cc::CleanupCategory::UserTemp);
    check(p && p->preview.status == cc::PreviewStatus::Ready, "onizleme hazir");
    if (!p) return 1;
    std::printf("  preview: files=%zu bytes=%llu skipped_reparse=%zu unreadable=%zu\n", p->preview.files,
                static_cast<unsigned long long>(p->preview.bytes), p->preview.skipped_reparse, p->preview.skipped_unreadable);
    check(p->preview.files == 6, "onizleme 6 dosya (junction hedefi sayilmadi)");
    check(p->preview.bytes == 1000 + 2500 + 123 + 4096 + 500 + 600, "onizleme bayt = gercek boyut toplami");
    // Handoff v1 EXT-CLN-02: taramadan sonra degisen dosyalar (ayni ad + ayni boyut) silinmemeli.
    Sleep(20);
    check(write_file(root / L"rewritten.bin", 500), "tarama sonrasi icerik yeniden yazildi (ayni boyut)");
    fs::remove(root / L"swapped.bin", ec);
    check(write_file(root / L"swapped.bin", 600), "tarama sonrasi ayni adla yeni dosya (yeni kimlik)");
    check(p->preview.skipped_reparse == (junction ? 1u : 0u), "junction atlandi (inilmedi)");

    std::size_t last_done = 0, last_total = 0, calls = 0;
    cc::RunOptions opt;
    opt.on_progress = [&](const cc::CleanupProgress& pr) {
        last_done = pr.done;
        last_total = pr.total;
        ++calls;
    };
    auto r = cc::run(plan, {cc::CleanupCategory::UserTemp}, wfs, opt);
    check(r.ok(), "calistirma basarili");
    if (r.ok()) {
        const auto& rep = r.value();
        const auto& c = rep.categories.at(0);
        std::printf("  deleted=%zu/%llu locked=%zu/%llu denied=%zu reparse=%zu other=%zu dirs_removed=%zu\n", c.deleted.files,
                    static_cast<unsigned long long>(c.deleted.bytes), c.locked.files,
                    static_cast<unsigned long long>(c.locked.bytes), c.denied.files, c.reparse.files, c.other.files,
                    c.dirs_removed);
        for (const auto& v : rep.volumes)
            std::printf("  volume %s free_before=%s free_after=%s\n", platform::to_utf8(v.volume).c_str(),
                        v.free_before ? human(*v.free_before).c_str() : "olculemedi",
                        v.free_after ? human(*v.free_after).c_str() : "olculemedi");
        check(c.deleted.bytes == 1000 + 2500 + 123, "silinen bayt olculdu");
        check(c.locked.files == 1 && c.locked.bytes == 4096, "kilitli dosya atlandi ve sayildi");
        check(c.changed.files == 2 && c.changed.bytes == 1100, "degisen 2 dosya atlandi ve sayildi");
        check(p->preview.bytes == c.deleted.bytes + c.locked.bytes + c.changed.bytes, "onizleme bayt == silinen + kilitli + degisen");
        check(c.dirs_removed == 3, "bosalan alt dizinler kaldirildi (sub, deep, emptydir)");
        check(last_done == 6 && last_total == 6 && calls >= 7, "ilerleme gercek dosya sayisi (6/6)");
        check(!rep.volumes.empty() && rep.volumes[0].free_before && rep.volumes[0].free_after, "birim bos alani olculdu");
    }
    check(fs::exists(root / L"locked.bin"), "kilitli dosya hala var");
    check(fs::exists(root / L"rewritten.bin") && fs::exists(root / L"swapped.bin"), "degisen dosyalar hala var");
    check(fs::exists(outside / L"sentinel.txt"), "junction hedefindeki nobetci dosya hala var");
    check(fs::exists(root), "agac koku korundu");
    check(!fs::exists(root / L"a.bin") && !fs::exists(root / L"sub"), "aday dosyalar silindi");
    if (junction) check(fs::exists(root / L"link"), "junction'in kendisi silinmedi");

    // Yonetici kategorisi yukseltilmemis surecte calismaz.
    if (!env.elevated) {
        auto plan2 = cc::preview({cc::CleanupCategory::WindowsTemp}, env, wfs);
        check(plan2.find(cc::CleanupCategory::WindowsTemp)->preview.status == cc::PreviewStatus::NeedsAdmin,
              "Windows Temp: yonetici gerekli (onizleme olcmedi)");
        auto r2 = cc::run(plan2, {cc::CleanupCategory::WindowsTemp}, wfs);
        check(!r2.ok() && r2.code() == contracts::ErrorCode::Denied, "Windows Temp: yukseltilmemis surecte Denied");
    } else {
        std::printf("  (surec yukseltilmis; Denied kontrolu atlandi)\n");
    }

    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    if (junction) RemoveDirectoryW((root / L"link").c_str());  // yalniz baglantiyi kaldirir, hedefi degil
    fs::remove_all(base, ec);
    std::printf("%s\n", failures ? "CLEANUP PROBE FAIL" : "CLEANUP PROBE OK");
    return failures ? 1 : 0;
}

const char* status_name(cc::PreviewStatus s) {
    switch (s) {
        case cc::PreviewStatus::Ready: return "ready";
        case cc::PreviewStatus::NotPresent: return "not_present";
        case cc::PreviewStatus::NeedsAdmin: return "needs_admin";
        case cc::PreviewStatus::OtherVolume: return "other_volume";
        case cc::PreviewStatus::Error: return "error";
    }
    return "?";
}

// volume bos: birim filtresi yok; "D:\" verilirse EXT-CLN-01 birim secimi (kimlik + yalniz o birimdeki kokler).
int run_preview(const std::wstring& volume) {
    auto env = platform::cleanup::discover_env();
    platform::cleanup::WinCleanupFs wfs;
    std::vector<cc::CleanupCategory> all(cc::kAllCategories.begin(), cc::kAllCategories.end());
    auto plan = cc::preview(all, env, wfs, volume);
    std::printf("elevated=%d (YALNIZ ONIZLEME, hicbir sey silinmedi)\n", env.elevated ? 1 : 0);
    if (!volume.empty())
        std::printf("volume=%ls known=%d serial=%08X fs=%ls cluster=%u\n", volume.c_str(), plan.volume.known ? 1 : 0, plan.volume.serial,
                    plan.volume.fs.c_str(), plan.volume.cluster);
    for (const auto& c : plan.categories) {
        const auto& pv = c.preview;
        std::printf("%-22s %-12s files=%zu bytes=%s allocated=%s reparse_skipped=%zu unreadable=%zu\n",
                    std::string(cc::to_string(pv.category)).c_str(), status_name(pv.status), pv.files, human(pv.bytes).c_str(),
                    human(pv.allocated).c_str(), pv.skipped_reparse, pv.skipped_unreadable);
        // Dosya listesi (en buyuk 3): yalniz ad uzunlugu ve boyut yazilir, yol icerigi loga kopyalanmaz.
        for (const auto& f : cc::largest_files(c, 3)) std::printf("    top: rel_len=%zu bytes=%s\n", f.relative.size(), human(f.bytes).c_str());
    }
    return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring mode = argc > 1 ? argv[1] : L"tree";
    if (mode == L"tree") return run_tree();
    if (mode == L"preview") return run_preview(argc > 2 ? argv[2] : L"");
    std::fprintf(stderr, "kullanim: ef_cleanup_probe [tree|preview [X:\\]]\n");
    return 2;
}
