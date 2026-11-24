// BENCH-05: depolama alt testi gercek dosya sistemi kontrolu.
// - Ham disk/volume ve kullanici dosyasi hedefleri reddedilir, hicbir sey olusturulmaz.
// - Var olan dosya CREATE_NEW ile acilmaz, icerigi degismez; junction klasoru reddedilir.
// - Bos alan alt siniri saglanmazsa test atlanir (dosya olusturulmaz).
// - BENCH-09: %TEMP% volume'unda HIZLI (quick_limits) kosu: her profil icin MiB/s, IOPS, p50/p95/p99
//   gecikme yazdirilir; toplam yazma <= butce; temizlik sonrasi klasor kalmaz. TAM on ayari kosulmaz.
//   QD>1 profillerinde Little yasasi (ortalama gecikme ~ QD/IOPS, %20) yalniz tutarlilik denetimidir: gecikme
//   gonderimden alinmaya kadar olculup slot hemen yeniden gonderildigi icin neredeyse tanim geregi tutar ve
//   tamamlanma acligini yakalamaz. Aclik denetimi slot adaletidir: en dusuk slot sayimi, gozlenen slot gecikme
//   CV'sine gore istatistiksel tabanin (ortalama - z*CV*sqrt(ortalama)) VE kMinSlotFairness oraninin altindaysa
//   aclik (turetme storage_rules.h). Gonderim mesguliyeti (submit_busy) bilgi olarak yazdirilir.
// - Iptal: calisan test iptal edilir, temizlik yine yapilir.
// - Yetim temizligi: yalniz isaretli klasorler silinir.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "benchmark/storage/storage_test.h"

using namespace ef::benchmark::storage;

namespace {
int g_fail = 0;
#define CHECK(expr)                                                         \
    do {                                                                    \
        if (!(expr)) {                                                      \
            ++g_fail;                                                       \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
        }                                                                   \
    } while (0)

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

int count_bench_folders(const std::wstring& parent) {
    WIN32_FIND_DATAW fd{};
    std::wstring pat = parent;
    if (pat.back() != L'\\') pat += L'\\';
    pat += L"EcuFps-bench-*";
    HANDLE f = FindFirstFileW(pat.c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do ++n;
    while (FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}

bool write_file(const std::wstring& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &n, nullptr) && n == data.size();
    CloseHandle(h);
    return ok;
}

std::string read_file(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    char buf[512];
    DWORD n = 0;
    std::string out;
    if (ReadFile(h, buf, sizeof buf, &n, nullptr)) out.assign(buf, n);
    CloseHandle(h);
    return out;
}

void remove_tree_simple(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(p.c_str());  // junction: hedefe inilmez
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree_simple(p);
            else DeleteFileW(p.c_str());
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(dir.c_str());
}

// Little yasasi: kuyruk dolu tutulursa ortalama gecikme ~ QD / IOPS. Sapma orani (0 = tam uyum); IOPS yoksa -1.
// Not: olcum tanimi geregi buyuk olcude ozdesliktir; yalniz alim -> yeniden gonderim arasindaki boslugu olcer.
double little_dev(const ProfileResult& p) {
    const double iops = p.iops();
    if (iops <= 0 || p.lat_samples == 0) return -1.0;
    const double want_us = p.profile.queue_depth / iops * 1e6;
    return (p.lat_mean_us > want_us ? p.lat_mean_us - want_us : want_us - p.lat_mean_us) / want_us;
}

void print_result(const RunResult& r) {
    std::printf("  status=%s reason=%s root=%ls\n", status_text(r.status), r.reason.c_str(), r.volume_root.c_str());
    std::printf("  volume total=%.1f GiB free=%.1f GiB required_free=%.1f GiB sector=%u written=%.0f/%.0f MiB prep=%.2fs\n",
                r.volume_total_bytes / double(kGiB), r.volume_free_bytes / double(kGiB),
                r.required_free_bytes / double(kGiB), r.sector_bytes, r.bytes_written / double(kMiB),
                r.write_budget_bytes / double(kMiB), r.preparation_seconds);
    for (const auto& p : r.profiles)
        std::printf("  %-18s %-5s QD%-2u T%u %-9s %8.1f MiB/s %8.0f IOPS %5.2fs %6.0f MiB  lat us p50=%.1f p95=%.1f "
                    "p99=%.1f mean=%.1f max=%.0f n=%llu little(QD/IOPS)=%.1f us dev=%.3f slot_ops min/ort/max=%llu/%.0f/%llu "
                    "adalet=%.2f cv=%.2f taban=%.0f%s gonderim_mesgul=%.2f %s\n",
                    p.profile.id, label_text(p.label), p.profile.queue_depth, p.io_threads, status_text(p.status), p.mb_per_s(),
                    p.iops(), p.seconds, p.bytes / double(kMiB), p.lat_p50_us, p.lat_p95_us, p.lat_p99_us,
                    p.lat_mean_us, p.lat_max_us, static_cast<unsigned long long>(p.lat_samples),
                    p.iops() > 0 ? p.profile.queue_depth / p.iops() * 1e6 : 0.0, little_dev(p),
                    static_cast<unsigned long long>(p.slot.min_ops), p.slot.mean_ops,
                    static_cast<unsigned long long>(p.slot.max_ops), p.slot.ratio, p.slot.cycle_cv.value_or(-1),
                    p.slot.floor_ops,
                    p.slot.slots == 0 ? "(yok)" : (p.slot.known ? (p.slot.starved ? "[ACLIK]" : "") : "(yetersiz ornek)"),
                    p.submit_busy, p.reason.c_str());
    std::printf("  cleanup_ok=%d folder=%ls\n", r.cleanup_ok ? 1 : 0, r.folder.c_str());
}
}  // namespace

int main() {
    wchar_t tmp[MAX_PATH + 1]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring temp = tmp;
    wchar_t lp[32768]{};
    if (GetLongPathNameW(temp.c_str(), lp, 32768)) temp = lp;
    if (temp.back() == L'\\') temp.pop_back();
    const std::wstring root = temp.substr(0, 3);  // "C:\"
    std::printf("storage: TEMP=%ls root=%ls\n", temp.c_str(), root.c_str());
    const int folders_before = count_bench_folders(root);
    std::atomic<bool> no_cancel{false};

    // 1) Ham disk / volume / kullanici yolu reddi: hicbir sey olusturulmaz.
    std::printf("[1] ham disk ve kullanici yolu reddi\n");
    for (const wchar_t* bad : {L"\\\\.\\PhysicalDrive0", L"\\\\.\\C:", L"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition0",
                               L"C:\\Windows\\notepad.exe", L"\\\\localhost\\C$"}) {
        RunConfig cfg;
        cfg.volume_root = bad;
        const RunResult r = run_storage_test(cfg, no_cancel);
        std::printf("  %ls -> %s %s\n", bad, status_text(r.status), r.reason.c_str());
        CHECK(r.status == Status::Refused);
        CHECK(r.folder.empty());
        CHECK(r.profiles.empty());
    }
    CHECK(count_bench_folders(root) == folders_before);

    // Calisma klasoru (TEMP altinda, yalniz bu test icin).
    wchar_t uniq[64];
    swprintf_s(uniq, L"ef-storage-test-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    const std::wstring work = temp + L"\\" + uniq;
    CHECK(CreateDirectoryW(work.c_str(), nullptr));

    // 2) Var olan kullanici dosyasi: CREATE_NEW ile reddedilir, icerik degismez.
    std::printf("[2] var olan dosya hedef olamaz\n");
    {
        const std::wstring user = work + L"\\test.bin";
        CHECK(write_file(user, "kullanici verisi"));
        OwnedFile f;
        const auto oc = create_owned_file(work, L"test.bin", FileKind::Unbuffered, f);
        std::printf("  existing test.bin -> %s\n", create_outcome_text(oc));
        CHECK(oc == CreateOutcome::Exists);
        CHECK(!f.valid());
        CHECK(read_file(user) == "kullanici verisi");
        // Ayni klasorde yeni ad olusturulabilir ve kapaninca (DELETE_ON_CLOSE) kaybolur.
        const auto oc2 = create_owned_file(work, L"fresh.bin", FileKind::Unbuffered, f);
        CHECK(oc2 == CreateOutcome::Created);
        f.reset();
        CHECK(!exists(work + L"\\fresh.bin"));
    }

    // 3) Junction klasoru: reparse point uzerinden hedef kacisi engellenir.
    std::printf("[3] junction klasoru reddi\n");
    {
        const std::wstring target = work + L"\\target";
        const std::wstring junction = work + L"\\junction";
        CHECK(CreateDirectoryW(target.c_str(), nullptr));
        const std::wstring cmd = L"cmd /d /c mklink /J \"" + junction + L"\" \"" + target + L"\" >nul";
        if (_wsystem(cmd.c_str()) == 0 && exists(junction)) {
            OwnedFile f;
            const auto oc = create_owned_file(junction, L"x.bin", FileKind::Unbuffered, f);
            std::printf("  junction -> %s\n", create_outcome_text(oc));
            CHECK(oc == CreateOutcome::FolderReparse);
            CHECK(!exists(target + L"\\x.bin"));
            // Ust bilesen junction ise de reddedilir.
            const std::wstring inner = target + L"\\inner";
            CHECK(CreateDirectoryW(inner.c_str(), nullptr));
            const auto oc2 = create_owned_file(junction + L"\\inner", L"y.bin", FileKind::Unbuffered, f);
            std::printf("  junction\\inner -> %s\n", create_outcome_text(oc2));
            CHECK(oc2 == CreateOutcome::FolderReparse);
            CHECK(!exists(inner + L"\\y.bin"));
        } else {
            std::printf("  mklink /J kullanilamadi - junction alt testi atlandi\n");
        }
    }

    // 4) Yetim temizligi: yalniz isaretli ve yalniz bizim dosyalarimizi iceren klasor silinir.
    std::printf("[4] yetim temizligi\n");
    {
        const std::wstring orphan = work + L"\\" + bench_folder_name(0x1111);
        const std::wstring nomark = work + L"\\" + bench_folder_name(0x2222);
        const std::wstring foreign = work + L"\\" + bench_folder_name(0x3333);
        const std::wstring other = work + L"\\EcuFps-bench-userfolder";
        for (const auto& d : {orphan, nomark, foreign, other}) CHECK(CreateDirectoryW(d.c_str(), nullptr));
        CHECK(write_file(orphan + L"\\itx-bench.marker", make_marker_content(1)));
        CHECK(write_file(orphan + L"\\test.bin", std::string(4096, 'x')));
        CHECK(write_file(nomark + L"\\test.bin", std::string(4096, 'x')));
        CHECK(write_file(foreign + L"\\itx-bench.marker", make_marker_content(1)));
        CHECK(write_file(foreign + L"\\save.dat", "oyun kaydi"));
        CHECK(write_file(other + L"\\itx-bench.marker", make_marker_content(1)));
        const OrphanReport rep = cleanup_orphans(work);
        std::printf("  scanned=%u removed=%u kept=%u\n", rep.scanned, rep.removed, rep.kept);
        CHECK(rep.scanned == 3);
        CHECK(rep.removed == 1);
        CHECK(rep.kept == 2);
        CHECK(!exists(orphan));
        CHECK(exists(nomark + L"\\test.bin"));
        CHECK(read_file(foreign + L"\\save.dat") == "oyun kaydi");
        CHECK(exists(other + L"\\itx-bench.marker"));
    }
    remove_tree_simple(work);
    CHECK(!exists(work));

    // 5) Bos alan alt siniri: dev dosya istenirse atlanir, klasor olusturulmaz.
    std::printf("[5] bos alan alt siniri\n");
    {
        RunConfig cfg;
        cfg.volume_root = root;
        cfg.limits.file_bytes = 64ull * 1024 * kGiB;  // 64 TiB
        cfg.limits.write_budget_bytes = UINT64_MAX;
        const RunResult r = run_storage_test(cfg, no_cancel);
        print_result(r);
        CHECK(r.status == Status::Skipped);
        CHECK(r.reason == "free_space_floor");
        CHECK(r.folder.empty());
        CHECK(r.required_free_bytes > r.volume_free_bytes);
    }

    // 6) BENCH-09 gercek HIZLI kosu: quick_limits() (512 MiB dosya, en fazla 1 GiB yazma, profil basi 3 s).
    //    EF_STORAGE_SHORT=1 ise profil basi 1 s (yalniz gelistirme turu; kanit kosusu varsayilan).
    {
        RunConfig cfg;
        cfg.volume_root = root;
        cfg.limits = quick_limits();
        char* env = nullptr;
        std::size_t envn = 0;
        if (_dupenv_s(&env, &envn, "EF_STORAGE_SHORT") == 0 && env && env[0] == '1') cfg.limits.profile_ms = 1000;
        std::free(env);
        std::printf("[6] HIZLI depolama testi (%ls): dosya=%llu MiB yazma_ust_siniri=%llu MiB plan=%llu ms + hazirlik\n",
                    root.c_str(), static_cast<unsigned long long>(cfg.limits.file_bytes / kMiB),
                    static_cast<unsigned long long>(max_host_write_bytes(cfg.limits) / kMiB),
                    static_cast<unsigned long long>(planned_measure_ms(cfg.limits, cfg.profiles.size())));
        const DWORD t0 = GetTickCount();
        const RunResult r = run_storage_test(cfg, no_cancel);
        const DWORD wall = GetTickCount() - t0;
        print_result(r);
        std::printf("  wall=%.1fs\n", wall / 1000.0);
        CHECK(r.status == Status::Ok);
        CHECK(r.cleanup_ok);
        CHECK(!r.folder.empty() && !exists(r.folder));
        CHECK(r.write_budget_bytes == cfg.limits.write_budget_bytes);
        CHECK(r.bytes_written <= cfg.limits.write_budget_bytes);
        CHECK(r.profiles.size() == default_profiles().size());
        std::uint64_t profile_writes = 0;
        for (const auto& p : r.profiles) {
            CHECK(p.status == Status::Ok);
            CHECK(p.ops > 0 && p.mb_per_s() > 0 && p.iops() > 0);
            CHECK(p.lat_samples == p.ops);
            CHECK(p.lat_p50_us > 0 && p.lat_p50_us <= p.lat_p95_us && p.lat_p95_us <= p.lat_p99_us &&
                  p.lat_p99_us <= p.lat_max_us);
            CHECK((p.profile.cache == CacheMode::Buffered) == (p.label == SpeedLabel::OsCache));
            CHECK(p.lat_mean_us > 0 && p.lat_mean_us <= p.lat_max_us);
            // QD>1: Little tutarliligi (|mean - QD/IOPS| / (QD/IOPS) < 0.2; tanim geregi zayif denetim) +
            // aclik denetimi: yeterli ornekte en dusuk slot istatistiksel tabanin ve oran esiginin altinda degil.
            if (p.profile.cache == CacheMode::Unbuffered) CHECK(p.slot.slots == p.profile.queue_depth);
            if (p.profile.queue_depth > 1 && p.profile.cache == CacheMode::Unbuffered) {
                const double dev = little_dev(p);
                CHECK(dev >= 0 && dev < 0.2);
                if (p.slot.known) CHECK(!p.slot.starved);
                else std::printf("  [NOT] %s: slot adaleti icin ornek yetersiz (ort %.0f < %.0f G/C/slot)\n", p.profile.id,
                                 p.slot.mean_ops, kMinOpsPerSlot);
            }
            if (p.profile.op == Op::Write) {
                CHECK(p.byte_cap > 0 && p.bytes <= p.byte_cap);
                profile_writes += p.bytes;
            }
        }
        // Hazirlik (dosya + cache dosyasi) + yazma profilleri = toplam yazma.
        CHECK(r.verify.status == VerifyStatus::Ok && r.verify.mismatched_blocks == 0);
        CHECK(r.bytes_written == preparation_write_bytes(cfg.limits) + profile_writes + r.verify.bytes_written);
        CHECK(wall < cfg.limits.total_ms + 15000);
    }

    // 6b) TAM on ayari: kosulmaz (4 GiB yazma onceden gosterilmeli); yalniz bos alan karari raporlanir (bosluk 7:
    //     alt sinir dosya + max(5 GiB, %5), dosya payi %10). Yetersizse gercek cagri dosya olusturmadan atlanmali;
    //     neden once alt sinir ("free_space_floor"), o gecerse dosya payi ("free_share_limit"), gereken > mevcut.
    {
        const Limits f = full_limits();
        ULARGE_INTEGER avail{}, total{}, freeb{};
        GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeb);
        const bool floor_ok = free_space_sufficient(avail.QuadPart, total.QuadPart, preparation_write_bytes(f));
        const bool fits = floor_ok && within_free_share(f.file_bytes, avail.QuadPart, f.max_free_permille);
        std::printf("[6b] TAM: dosya=%llu MiB yazma_ust_siniri=%llu MiB plan=%llu ms free=%.1f GiB -> %s\n",
                    static_cast<unsigned long long>(f.file_bytes / kMiB),
                    static_cast<unsigned long long>(max_host_write_bytes(f) / kMiB),
                    static_cast<unsigned long long>(planned_measure_ms(f, default_profiles().size())),
                    avail.QuadPart / double(kGiB), fits ? "kosulabilir (bu probda kosulmadi)" : "atlanir");
        if (!fits) {
            RunConfig cfg;
            cfg.volume_root = root;
            cfg.limits = f;
            const RunResult r = run_storage_test(cfg, no_cancel);
            print_result(r);
            CHECK(r.status == Status::Skipped && r.reason == (floor_ok ? "free_share_limit" : "free_space_floor") && r.folder.empty());
            CHECK(r.required_free_bytes == required_free_for(f, total.QuadPart) && r.required_free_bytes > r.volume_free_bytes);
        }
        // Yapay buyuk dosya: 1 TiB dosya %10 kuralina takilir (bos alan alt siniri da) ve hicbir sey yazilmaz.
        RunConfig big;
        big.volume_root = root;
        big.limits = f;
        big.limits.file_bytes = 1024 * kGiB;
        big.limits.write_budget_bytes = 2048 * kGiB;
        const RunResult rb = run_storage_test(big, no_cancel);
        std::printf("  1 TiB dosya -> %s %s written=%llu\n", status_text(rb.status), rb.reason.c_str(),
                    static_cast<unsigned long long>(rb.bytes_written));
        CHECK(rb.status == Status::Skipped && rb.folder.empty() && rb.bytes_written == 0);
    }

    // 8) EF-X22: kucuk gercek yaz-oku-karsilastir (64 MiB dosya, 16 MiB dogrulama, profil yok) ve kapali hali.
    std::printf("[8] veri dogrulama fazi\n");
    {
        RunConfig cfg;
        cfg.volume_root = root;
        cfg.limits.file_bytes = 64 * kMiB;
        cfg.limits.verify_bytes = 16 * kMiB;
        cfg.profiles.clear();
        const RunResult r = run_storage_test(cfg, no_cancel);
        print_result(r);
        std::printf("  verify status=%d verified=%llu/%llu mismatch=%llu written=%llu\n", static_cast<int>(r.verify.status),
                    static_cast<unsigned long long>(r.verify.blocks_verified),
                    static_cast<unsigned long long>(r.verify.blocks_planned),
                    static_cast<unsigned long long>(r.verify.mismatched_blocks),
                    static_cast<unsigned long long>(r.verify.bytes_written));
        CHECK(r.status == Status::Ok && r.verify.status == VerifyStatus::Ok);
        CHECK(r.verify.blocks_planned == 16 && r.verify.blocks_verified == 16 && r.verify.mismatched_blocks == 0);
        CHECK(r.bytes_written == cfg.limits.file_bytes + r.verify.bytes_written);  // cache profili yok
        CHECK(r.cleanup_ok && !r.folder.empty() && !exists(r.folder));
        RunConfig off = cfg;
        off.verify = false;
        const RunResult r2 = run_storage_test(off, no_cancel);
        CHECK(r2.status == Status::Ok && r2.verify.status == VerifyStatus::NotRun && r2.bytes_written == off.limits.file_bytes);
        CHECK(r2.cleanup_ok && !exists(r2.folder));
    }

    // 7) Iptal: test calisirken iptal edilir; sonuc Cancelled ve klasor kalmaz.
    std::printf("[7] iptal\n");
    {
        RunConfig cfg;
        cfg.volume_root = root;
        cfg.limits.file_bytes = 256 * kMiB;
        cfg.limits.write_budget_bytes = 1 * kGiB;
        cfg.limits.profile_ms = 5000;
        std::atomic<bool> cancel{false};
        RunResult r;
        std::thread t([&] { r = run_storage_test(cfg, cancel); });
        Sleep(1200);
        cancel = true;
        t.join();
        print_result(r);
        CHECK(r.status == Status::Cancelled);
        CHECK(r.cleanup_ok);
        CHECK(!r.folder.empty() && !exists(r.folder));
    }

    CHECK(count_bench_folders(root) == folders_before);
    std::printf("storage: %s (%d hata)\n", g_fail == 0 ? "OK" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
