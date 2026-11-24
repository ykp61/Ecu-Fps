// BENCH-05: depolama alt testi saf kurallari (dosya sistemi yok).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <random>
#include <set>
#include <vector>

#include "benchmark/storage/storage_rules.h"
#include "ef_test.h"

using namespace ef::benchmark::storage;

EF_TEST(storage_root_accepts_drive_letter_root_only) {
    EF_CHECK(validate_volume_root(L"C:\\") == TargetVerdict::Ok);
    EF_CHECK(validate_volume_root(L"d:") == TargetVerdict::Ok);
    EF_CHECK(validate_volume_root(L"E:/") == TargetVerdict::Ok);
    EF_CHECK(normalize_volume_root(L"d:") == L"D:\\");
    EF_CHECK(normalize_volume_root(L"\\\\.\\C:").empty());
}

EF_TEST(storage_root_refuses_raw_disk_and_volume_handles) {
    EF_CHECK(validate_volume_root(L"\\\\.\\PhysicalDrive0") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"\\\\.\\C:") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"//./C:") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"\\\\?\\C:\\") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition1") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"\\??\\PhysicalDrive1") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"\\Device\\HarddiskVolume3") == TargetVerdict::RawDevice);
    EF_CHECK(validate_volume_root(L"physicaldrive0") == TargetVerdict::RawDevice);
}

EF_TEST(storage_root_refuses_user_paths_network_relative) {
    EF_CHECK(validate_volume_root(L"") == TargetVerdict::Empty);
    EF_CHECK(validate_volume_root(L"C:\\Users\\u\\Documents\\save.dat") == TargetVerdict::NotVolumeRoot);
    EF_CHECK(validate_volume_root(L"C:\\Windows") == TargetVerdict::NotVolumeRoot);
    EF_CHECK(validate_volume_root(L"C:\\..\\") == TargetVerdict::NotVolumeRoot);
    EF_CHECK(validate_volume_root(L"\\\\server\\share") == TargetVerdict::Network);
    EF_CHECK(validate_volume_root(L"C:foo") == TargetVerdict::Relative);
    EF_CHECK(validate_volume_root(L"bench") == TargetVerdict::Relative);
    EF_CHECK(validate_volume_root(L"\\temp") == TargetVerdict::Relative);
    EF_CHECK(validate_volume_root(L"C:\\*") == TargetVerdict::InvalidChars);
    EF_CHECK(std::strcmp(verdict_reason(TargetVerdict::RawDevice), "raw_device_refused") == 0);
}

EF_TEST(storage_free_space_floor) {
    // Bosluk 7: kucuk volume 5 GiB taban; buyuk volume %5 (once max(%10, 10 GiB)).
    EF_CHECK(free_space_floor(50 * kGiB) == 5 * kGiB);
    EF_CHECK(free_space_floor(100 * kGiB) == 5 * kGiB);
    EF_CHECK(free_space_floor(2000 * kGiB) == 100 * kGiB);
    EF_CHECK(required_free_bytes(1 * kGiB, 500 * kGiB) == 26 * kGiB);
    EF_CHECK(free_space_sufficient(26 * kGiB, 500 * kGiB, 1 * kGiB));
    EF_CHECK(!free_space_sufficient(26 * kGiB - 1, 500 * kGiB, 1 * kGiB));
    // Tam disk: dosya kucuk olsa bile taban saglanmaz.
    EF_CHECK(!free_space_sufficient(5 * kGiB, 64 * kGiB, 256 * kMiB));
    // Tasma: doygun, asla yeterli sayilmaz.
    EF_CHECK(!free_space_sufficient(UINT64_MAX, 100 * kGiB, UINT64_MAX - kGiB));
}

EF_TEST(storage_folder_name_and_marker) {
    const std::wstring n = bench_folder_name(0x0123456789abcdefull);
    EF_CHECK(n == L"EcuFps-bench-0123456789abcdef");
    EF_CHECK(is_bench_folder_name(n));
    EF_CHECK(is_bench_folder_name(bench_folder_name(0)));
    EF_CHECK(!is_bench_folder_name(L"EcuFps-bench-"));
    EF_CHECK(!is_bench_folder_name(L"EcuFps-bench-0123456789abcdeg"));
    EF_CHECK(!is_bench_folder_name(L"EcuFps-bench-0123456789ABCDEF"));
    EF_CHECK(!is_bench_folder_name(L"EcuFps-bench-0123456789abcdef0"));
    EF_CHECK(!is_bench_folder_name(L"Games"));
    EF_CHECK(marker_content_valid(make_marker_content(1234)));
    EF_CHECK(!marker_content_valid("EcuFps-bench-v1"));
    EF_CHECK(!marker_content_valid("user data\n"));
}

EF_TEST(storage_path_is_under) {
    const std::wstring dir = L"C:\\EcuFps-bench-0123456789abcdef";
    EF_CHECK(path_is_under(L"C:\\EcuFps-bench-0123456789abcdef\\test.bin", dir));
    EF_CHECK(path_is_under(L"\\\\?\\c:\\ecufps-bench-0123456789ABCDEF\\test.bin", dir));
    EF_CHECK(!path_is_under(dir, dir));
    EF_CHECK(!path_is_under(L"C:\\EcuFps-bench-0123456789abcdefX\\test.bin", dir));
    EF_CHECK(!path_is_under(L"D:\\Users\\u\\save.dat", dir));
    EF_CHECK(!path_is_under(L"C:\\EcuFps-bench-0123456789abcdef\\..\\Windows\\x", dir));
    EF_CHECK(!path_is_under(L"", dir));
}

EF_TEST(storage_orphan_decision) {
    const std::wstring ours = bench_folder_name(42);
    std::vector<OrphanEntry> clean = {{L"itx-bench.marker"}, {L"test.bin"}, {L"cache.bin"}};
    EF_CHECK(decide_orphan(ours, false, true, clean) == OrphanDecision::Delete);
    EF_CHECK(decide_orphan(ours, false, true, {{L"itx-bench.marker"}}) == OrphanDecision::Delete);
    EF_CHECK(decide_orphan(L"Games", false, true, clean) == OrphanDecision::NotOurs);
    EF_CHECK(decide_orphan(ours, true, true, clean) == OrphanDecision::FolderReparse);
    EF_CHECK(decide_orphan(ours, false, false, clean) == OrphanDecision::NoMarker);
    EF_CHECK(decide_orphan(ours, false, true, {{L"test.bin"}}) == OrphanDecision::NoMarker);
    EF_CHECK(decide_orphan(ours, false, true, {}) == OrphanDecision::NoMarker);  // bos klasor de silinmez
    auto with = [&](OrphanEntry e) {
        auto v = clean;
        v.push_back(std::move(e));
        return decide_orphan(ours, false, true, v);
    };
    EF_CHECK(with({L"save.dat"}) == OrphanDecision::ForeignContent);
    EF_CHECK(with({L"sub", true}) == OrphanDecision::ForeignContent);
    std::vector<OrphanEntry> hl = {{L"itx-bench.marker"}, {L"test.bin", false, false, 2}};
    EF_CHECK(decide_orphan(ours, false, true, hl) == OrphanDecision::ForeignContent);
    std::vector<OrphanEntry> rp = {{L"itx-bench.marker"}, {L"test.bin", false, true, 1}};
    EF_CHECK(decide_orphan(ours, false, true, rp) == OrphanDecision::ForeignContent);
}

EF_TEST(storage_profiles_and_labels) {
    const auto ps = default_profiles();
    std::set<std::string> ids;
    int cache = 0;
    bool seq_read = false, seq_write = false, qd1 = false, qd4 = false, qd32 = false;
    for (const auto& p : ps) {
        ids.insert(p.id);
        if (p.cache == CacheMode::Buffered) {
            ++cache;
            EF_CHECK(label_of(p) == SpeedLabel::OsCache);
            EF_CHECK(std::strcmp(label_text(label_of(p)), "cache") == 0);
            EF_CHECK(std::string(p.id).rfind("cache_", 0) == 0);
        } else {
            EF_CHECK(label_of(p) == SpeedLabel::DiskUnbuffered);
            EF_CHECK(p.block_bytes % 4096 == 0);
        }
        if (p.pattern == Pattern::Sequential && p.cache == CacheMode::Unbuffered && p.block_bytes == kMiB) {
            seq_read |= p.op == Op::Read;
            seq_write |= p.op == Op::Write;
        }
        if (p.pattern == Pattern::Random && p.block_bytes == 4 * kKiB) {
            qd1 |= p.queue_depth == 1;
            qd4 |= p.queue_depth == 4;
            qd32 |= p.queue_depth == 32;
        }
    }
    EF_CHECK(ids.size() == ps.size());
    EF_CHECK(cache == 1);
    EF_CHECK(seq_read && seq_write && qd1 && qd4 && qd32);
}

// BENCH-09: seq QD8 okuma/yazma ve rastgele 4 KiB QD1/QD32 yazma eklendi; okumalar yazmalardan once.
EF_TEST(storage_bench09_profile_set) {
    const auto ps = default_profiles();
    auto has = [&](Pattern pat, Op op, std::uint64_t block, std::uint32_t qd) {
        for (const auto& p : ps)
            if (p.pattern == pat && p.op == op && p.block_bytes == block && p.queue_depth == qd &&
                p.cache == CacheMode::Unbuffered)
                return true;
        return false;
    };
    EF_CHECK(ps.size() == 10);
    EF_CHECK(has(Pattern::Sequential, Op::Read, kMiB, 1));
    EF_CHECK(has(Pattern::Sequential, Op::Read, kMiB, 8));
    EF_CHECK(has(Pattern::Sequential, Op::Write, kMiB, 1));
    EF_CHECK(has(Pattern::Sequential, Op::Write, kMiB, 8));
    EF_CHECK(has(Pattern::Random, Op::Read, 4 * kKiB, 1));
    EF_CHECK(has(Pattern::Random, Op::Read, 4 * kKiB, 4));
    EF_CHECK(has(Pattern::Random, Op::Read, 4 * kKiB, 32));
    EF_CHECK(has(Pattern::Random, Op::Write, 4 * kKiB, 1));
    EF_CHECK(has(Pattern::Random, Op::Write, 4 * kKiB, 32));
    EF_CHECK(count_write_profiles(ps) == 4);
    // Ilk yazma profilinden once tum tamponsuz okumalar gelir.
    std::size_t first_write = ps.size(), last_unbuf_read = 0;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (ps[i].cache != CacheMode::Unbuffered) continue;
        if (ps[i].op == Op::Write && first_write == ps.size()) first_write = i;
        if (ps[i].op == Op::Read) last_unbuf_read = i;
    }
    EF_CHECK(last_unbuf_read < first_write);
    for (const auto& p : ps) EF_CHECK(p.queue_depth >= 1 && p.queue_depth <= 32);
}

EF_TEST(storage_quick_and_full_presets) {
    const Limits q = quick_limits();
    const Limits d;
    EF_CHECK(q.file_bytes == d.file_bytes && q.cache_file_bytes == d.cache_file_bytes &&
              q.write_budget_bytes == d.write_budget_bytes && q.max_free_permille == d.max_free_permille &&
              q.profile_ms == d.profile_ms && q.total_ms == d.total_ms);
    EF_CHECK(q.file_bytes == 512 * kMiB && q.write_budget_bytes == 1 * kGiB && q.profile_ms == 3000);
    EF_CHECK(validate_limits(q) == LimitsVerdict::Ok);
    const Limits f = full_limits();
    EF_CHECK(f.file_bytes == 2 * kGiB && f.write_budget_bytes == 4 * kGiB && f.profile_ms == 8000);
    EF_CHECK(f.max_free_permille == 100);
    EF_CHECK(validate_limits(f) == LimitsVerdict::Ok);
    EF_CHECK(max_host_write_bytes(q) == 1 * kGiB && max_host_write_bytes(f) == 4 * kGiB);
    // Tum profiller zaman ust sinirina sigar.
    const std::size_t n = default_profiles().size();
    EF_CHECK(planned_measure_ms(q, n) == 30000 && planned_measure_ms(q, n) <= q.total_ms);
    EF_CHECK(planned_measure_ms(f, n) == 80000 && planned_measure_ms(f, n) <= f.total_ms);
    EF_CHECK(planned_measure_ms(q, 100) == q.total_ms);
    // %10 kurali (bosluk 7): TAM 2 GiB dosya icin ~20 GiB bos alan gerekir.
    EF_CHECK(!within_free_share(f.file_bytes, 20 * kGiB, f.max_free_permille));
    EF_CHECK(within_free_share(f.file_bytes, 21 * kGiB, f.max_free_permille));
    EF_CHECK(within_free_share(q.file_bytes, 6 * kGiB, q.max_free_permille));
    EF_CHECK(!within_free_share(q.file_bytes, 5 * kGiB, q.max_free_permille));
}

EF_TEST(storage_write_share_stays_within_budget) {
    const auto m1 = static_cast<std::uint32_t>(kMiB);
    const auto k4 = static_cast<std::uint32_t>(4 * kKiB);
    // HIZLI: 1 GiB - 512 MiB hazirlik - 32 MiB cache ayrimi = 480 MiB, 4 yazici -> 120 MiB.
    const Limits q = quick_limits();
    EF_CHECK(write_share(q.write_budget_bytes, q.file_bytes, q.cache_file_bytes, 4, k4) == 120 * kMiB);
    // Esit pay + kullanilmayan pay devri: toplam asla butceyi asmaz.
    std::uint64_t used = q.file_bytes;
    const std::uint64_t reserve = q.cache_file_bytes;
    const std::uint32_t blocks[4] = {k4, k4, m1, m1};
    const double use_frac[4] = {0.10, 0.50, 1.0, 1.0};  // rastgele yazmalar sure dolunca payin altinda kalir
    std::uint64_t got[4] = {};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const std::uint64_t share = write_share(q.write_budget_bytes, used, reserve, 4 - i, blocks[i]);
        EF_CHECK(share % blocks[i] == 0);
        std::uint64_t w = static_cast<std::uint64_t>(static_cast<double>(share) * use_frac[i]);
        w -= w % blocks[i];
        got[i] = share;
        used += w;
    }
    EF_CHECK(got[3] > got[0]);  // onceki profillerin artan payi sonrakilere kaldi
    EF_CHECK(used + reserve <= q.write_budget_bytes);
    // Kenar durumlar.
    EF_CHECK(write_share(q.write_budget_bytes, used, reserve, 0, k4) == 0);
    EF_CHECK(write_share(q.write_budget_bytes, q.write_budget_bytes, 0, 2, k4) == 0);
    EF_CHECK(write_share(q.write_budget_bytes, 0, q.write_budget_bytes, 2, k4) == 0);
    EF_CHECK(write_share(10 * kMiB, 0, 0, 3, m1) == 3 * kMiB);  // 3.33 MiB -> blok hizali 3 MiB
    EF_CHECK(write_share(UINT64_MAX, UINT64_MAX - 1, 5, 1, 1) == 0);  // tasma doygun
    EF_CHECK(write_share(10 * kMiB, 0, 0, 1, 0) == 0);
    // TAM: 4 GiB - 2 GiB - 32 MiB = 2016 MiB / 4 = 504 MiB.
    const Limits f = full_limits();
    EF_CHECK(write_share(f.write_budget_bytes, f.file_bytes, f.cache_file_bytes, 4, m1) == 504 * kMiB);
}

EF_TEST(storage_latency_histogram_buckets) {
    using H = LatencyHistogram;
    // 0..63 ns tam; kovalar monoton ve bitisik.
    EF_CHECK(H::bucket_of(0) == 0 && H::bucket_of(63) == 63 && H::bucket_of(64) == 64);
    for (std::uint32_t i = 1; i < H::kBuckets; ++i) {
        EF_CHECK(H::bucket_low_ns(i) == H::bucket_low_ns(i - 1) + H::bucket_width_ns(i - 1));
        EF_CHECK(H::bucket_of(H::bucket_low_ns(i)) == i);
        EF_CHECK(H::bucket_of(H::bucket_low_ns(i) + H::bucket_width_ns(i) - 1) == i);
    }
    // Goreli kova genisligi <= 1/64.
    for (std::uint32_t i = H::kSub; i < H::kBuckets; ++i)
        EF_CHECK(H::bucket_width_ns(i) * 64 <= H::bucket_low_ns(i));
    EF_CHECK(H::bucket_of(UINT64_MAX) == H::kBuckets - 1);
}

EF_TEST(storage_latency_percentiles) {
    LatencyHistogram e;
    EF_CHECK(e.count() == 0 && e.percentile_us(99) == 0.0 && e.mean_us() == 0.0);
    // 1..100 us esit dagilim: p50 ~ 50 us, p95 ~ 95 us, p99 ~ 99 us (kova hatasi <= %1.6).
    LatencyHistogram h;
    for (int us = 1; us <= 100; ++us) h.add_ns(static_cast<std::uint64_t>(us) * 1000);
    EF_CHECK(h.count() == 100);
    auto close_to = [](double v, double want) { return v >= want * 0.98 && v <= want * 1.02; };
    EF_CHECK(close_to(h.percentile_us(50), 50));
    EF_CHECK(close_to(h.percentile_us(95), 95));
    EF_CHECK(close_to(h.percentile_us(99), 99));
    EF_CHECK(close_to(h.percentile_us(100), 100));
    EF_CHECK(close_to(h.percentile_us(0), 1));
    EF_CHECK(h.mean_us() == 50.5);
    EF_CHECK(h.max_us() == 100.0);
    EF_CHECK(h.percentile_us(100) <= h.max_us());  // kova ortasi maksimumu asmaz
    // Kuyruk: 990 hizli + 10 yavas -> p99 hizli, p99.1+ yavas (yakin-sira).
    LatencyHistogram t;
    for (int i = 0; i < 990; ++i) t.add_ns(20'000);
    for (int i = 0; i < 10; ++i) t.add_ns(5'000'000);
    EF_CHECK(close_to(t.percentile_us(99), 20));
    EF_CHECK(close_to(t.percentile_us(99.5), 5000));
    // Tamsayi sira: kayan nokta ile 7/100*100 = 7.000000000000001 -> ceil 8 olurdu; dogru sira 7.
    EF_CHECK(close_to(h.percentile_us(7), 7));
    EF_CHECK(close_to(h.percentile_us(29), 29));
    EF_CHECK(close_to(h.percentile_us(57), 57));
    // 99 hizli + 1 yavas, n=100: p99 sirasi tam 99 -> hizli; p99.01 -> yavas.
    LatencyHistogram u;
    for (int i = 0; i < 99; ++i) u.add_ns(1'000);
    u.add_ns(9'000'000);
    EF_CHECK(close_to(u.percentile_us(99), 1));
    EF_CHECK(close_to(u.percentile_us(99.01), 9000));
    // Buyuk sayac (q > 0 dali): n = 20000, p99.99 -> sira 19998.
    LatencyHistogram big;
    for (int i = 0; i < 19998; ++i) big.add_ns(2'000);
    big.add_ns(500'000);
    big.add_ns(500'000);
    EF_CHECK(close_to(big.percentile_us(99.99), 2));
    EF_CHECK(close_to(big.percentile_us(100), 500));
    // Kirpma: 2^37 ns (~137.4 s) ustu son kovaya duser; 2^37 - 1 son tam buyuklugun son kovasi.
    EF_CHECK(LatencyHistogram::bucket_of((1ull << 37) - 1) == LatencyHistogram::kBuckets - 1);
    EF_CHECK(LatencyHistogram::bucket_of(1ull << 36) == LatencyHistogram::kBuckets - LatencyHistogram::kSub);
    // Birlestirme = tek histogram.
    LatencyHistogram a, b;
    for (int us = 1; us <= 50; ++us) a.add_ns(static_cast<std::uint64_t>(us) * 1000);
    for (int us = 51; us <= 100; ++us) b.add_ns(static_cast<std::uint64_t>(us) * 1000);
    a.merge(b);
    EF_CHECK(a.count() == 100 && a.percentile_us(95) == h.percentile_us(95) && a.max_us() == 100.0);
    // ProfileResult alanlari doldurulur.
    ProfileResult pr;
    pr.set_latency(h);
    EF_CHECK(pr.lat_samples == 100 && pr.lat_p50_us <= pr.lat_p95_us && pr.lat_p95_us <= pr.lat_p99_us &&
              pr.lat_p99_us <= pr.lat_max_us);
}

EF_TEST(storage_defaults_follow_handoff_and_free_share) {
    Limits l;
    EF_CHECK(l.file_bytes == 512 * kMiB && l.write_budget_bytes == 1 * kGiB && l.max_free_permille == 100);
    EF_CHECK(within_free_share(512 * kMiB, 52 * kGiB, 10));   // %1 = 532 MiB
    EF_CHECK(!within_free_share(512 * kMiB, 50 * kGiB, 10));  // %1 = 512 MiB'den az (yuvarlama korumaci)
    EF_CHECK(within_free_share(512 * kMiB, 1, 0));            // kural kapali
}

EF_TEST(storage_share_required_free_is_tight_bound) {
    // Bosluk 7: within_free_share'i saglayan en kucuk bos alan; bir byte eksigi saglamaz.
    for (const std::uint64_t file : {512 * kMiB, 2 * kGiB, std::uint64_t{1}, std::uint64_t{999}, 3 * kGiB + 7}) {
        for (const std::uint32_t pm : {10u, 100u, 7u, 1000u}) {
            const std::uint64_t need = share_required_free(file, pm);
            EF_CHECK(within_free_share(file, need, pm));
            EF_CHECK(!within_free_share(file, need - 1, pm));
        }
    }
    EF_CHECK(share_required_free(512 * kMiB, 0) == 0);                    // kural kapali
    EF_CHECK(share_required_free(UINT64_MAX, 1) == UINT64_MAX);           // tasma doygun
}

EF_TEST(storage_required_free_for_is_max_of_both_rules) {
    // HIZLI (512 MiB + 32 MiB hazirlik): 256 GB disk -> taban max(12.8 GiB, 5 GiB) baskin.
    const Limits q = quick_limits();
    EF_CHECK(required_free_for(q, 256 * kGiB) == preparation_write_bytes(q) + 256 * kGiB / 20);
    // 64 GiB disk: taban 5 GiB; %10 payi 5 GiB + biraz (512 MiB / 100 * 1000) -> taban (5.53 GiB) baskin.
    EF_CHECK(required_free_for(q, 64 * kGiB) == preparation_write_bytes(q) + 5 * kGiB);
    // TAM (2 GiB dosya): kucuk diskte %10 payi (~20 GiB) baskin.
    const Limits f = full_limits();
    EF_CHECK(required_free_for(f, 100 * kGiB) == share_required_free(f.file_bytes, f.max_free_permille));
    EF_CHECK(required_free_for(f, 100 * kGiB) > 20 * kGiB && required_free_for(f, 100 * kGiB) < 21 * kGiB);
    // Gerekli alan varken iki kural da gecer; bir byte eksigiyle en az biri atlatir.
    for (const std::uint64_t total : {64 * kGiB, 256 * kGiB, 512 * kGiB, 2000 * kGiB}) {
        for (const Limits& l : {q, f}) {
            const std::uint64_t need = required_free_for(l, total);
            EF_CHECK(free_space_sufficient(need, total, preparation_write_bytes(l)) && within_free_share(l.file_bytes, need, l.max_free_permille));
            EF_CHECK(!(free_space_sufficient(need - 1, total, preparation_write_bytes(l)) &&
                        within_free_share(l.file_bytes, need - 1, l.max_free_permille)));
        }
    }
    // Rapordaki tipik durum: 512 GB SSD %90 dolu (~51 GiB bos) artik HIZLI ve TAM testi atlamaz.
    EF_CHECK(required_free_for(q, 512 * kGiB) <= 51 * kGiB && required_free_for(f, 512 * kGiB) <= 51 * kGiB);
}

EF_TEST(storage_bounded_io_wait_never_waits_forever) {
    // Bosluk 19: ilk beklemede biterse iptal yok.
    {
        int cancels = 0;
        std::vector<std::uint32_t> waits;
        const auto o = bounded_io_wait([&](std::uint32_t ms) { waits.push_back(ms); return true; }, [&] { ++cancels; }, 10000, 10000);
        EF_CHECK(o == IoWaitOutcome::Completed && cancels == 0 && waits.size() == 1 && waits[0] == 10000);
    }
    // Iptalden sonra biter: CancelledDone; iptal bir kez.
    {
        int cancels = 0;
        std::vector<std::uint32_t> waits;
        const auto o = bounded_io_wait([&](std::uint32_t ms) { waits.push_back(ms); return cancels > 0; }, [&] { ++cancels; }, 10000, 2500);
        EF_CHECK(o == IoWaitOutcome::CancelledDone && cancels == 1 && waits.size() == 2 && waits[1] == 2500);
    }
    // Hic bitmez: Stuck; ikinci bekleme de sinirli (INFINITE degil), ucuncu bekleme yok.
    {
        int cancels = 0;
        std::vector<std::uint32_t> waits;
        const auto o = bounded_io_wait([&](std::uint32_t ms) { waits.push_back(ms); return false; }, [&] { ++cancels; }, 10000, 10000);
        EF_CHECK(o == IoWaitOutcome::Stuck && cancels == 1 && waits.size() == 2);
        for (const auto ms : waits) EF_CHECK(ms != 0xFFFFFFFFu);
    }
}

EF_TEST(storage_limits_and_budget) {
    Limits l;
    EF_CHECK(validate_limits(l) == LimitsVerdict::Ok);
    Limits a = l;
    a.file_bytes = 8 * kMiB;
    EF_CHECK(validate_limits(a) == LimitsVerdict::FileTooSmall);
    a = l;
    a.file_bytes = 256 * kMiB + 4096;
    EF_CHECK(validate_limits(a) == LimitsVerdict::FileNotAligned);
    a = l;
    a.write_budget_bytes = l.file_bytes;  // cache dosyasi icin yer yok
    EF_CHECK(validate_limits(a) == LimitsVerdict::BudgetBelowPreparation);
    a = l;
    a.total_ms = l.profile_ms - 1;
    EF_CHECK(validate_limits(a) == LimitsVerdict::TimeInvalid);
    a = l;
    a.cache_file_bytes = 0;
    EF_CHECK(validate_limits(a) == LimitsVerdict::CacheFileInvalid);
    EF_CHECK(preparation_write_bytes(l) == l.file_bytes + l.cache_file_bytes);
    EF_CHECK(clamp_write(10 * kMiB, 4 * kMiB, 100 * kMiB, static_cast<std::uint32_t>(kMiB)) == 6 * kMiB);
    EF_CHECK(clamp_write(10 * kMiB, 4 * kMiB + 1, 100 * kMiB, static_cast<std::uint32_t>(kMiB)) == 5 * kMiB);
    EF_CHECK(clamp_write(10 * kMiB, 10 * kMiB, 1, 1) == 0);
    EF_CHECK(clamp_write(10 * kMiB, 0, 3 * kMiB, static_cast<std::uint32_t>(kMiB)) == 3 * kMiB);
}

EF_TEST(storage_result_rates) {
    ProfileResult r;
    r.bytes = 100 * kMiB;
    r.ops = 100;
    r.seconds = 2.0;
    EF_CHECK(r.mb_per_s() == 50.0);
    EF_CHECK(r.iops() == 50.0);
    ProfileResult z;
    EF_CHECK(z.mb_per_s() == 0.0 && z.iops() == 0.0);
    EF_CHECK(std::strcmp(status_text(Status::Partial), "partial") == 0);
}

EF_TEST(storage_random_data_not_zero_and_stamped) {
    std::vector<unsigned char> a(64 * 1024), b(64 * 1024);
    std::uint64_t s = 7;
    fill_random(a.data(), a.size(), s);
    fill_random(b.data(), b.size(), s);
    std::size_t zeros = 0;
    for (auto c : a) zeros += c == 0;
    EF_CHECK(zeros < a.size() / 64);  // ~1/256 beklenir
    EF_CHECK(a != b);
    // Byte dagilimi kaba kontrol: 256 degerin hepsi gorulur (sikistirilamaz desen).
    std::set<unsigned char> seen(a.begin(), a.end());
    EF_CHECK(seen.size() == 256);
    auto c1 = a, c2 = a;
    stamp_sectors(c1.data(), c1.size(), 1);
    stamp_sectors(c2.data(), c2.size(), 2);
    EF_CHECK(c1 != c2);
    // Ayni tampon icinde sektorler birbirinden farkli damgalanir.
    EF_CHECK(std::memcmp(c1.data(), c1.data() + 4096, 8) != 0);
}

EF_TEST(storage_short_capped_write_is_burst_not_disk) {
    using namespace ef::benchmark::storage;
    EF_CHECK(label_after_run(SpeedLabel::DiskUnbuffered, true, 0.16) == SpeedLabel::Burst);
    EF_CHECK(std::string(label_text(SpeedLabel::Burst)) == "burst");
    EF_CHECK(label_after_run(SpeedLabel::DiskUnbuffered, true, 2.0) == SpeedLabel::DiskUnbuffered);
    EF_CHECK(label_after_run(SpeedLabel::DiskUnbuffered, false, 0.1) == SpeedLabel::DiskUnbuffered);
    EF_CHECK(label_after_run(SpeedLabel::OsCache, true, 0.1) == SpeedLabel::OsCache);
}


namespace {
// Sentetik kapali dongu slotlari: her slot T suresince dongu suresi cekip sayar (yenileme sureci).
// heavy = hiperustel (p 0.95: ortalama 1, p 0.05: ortalama 20) -> CV ~3.16 (GC duraklamali SSD benzetimi);
// degilse ustel (CV 1). Kendi ustel cekimi: platformdan bagimsiz deterministik.
struct SimSlots {
    std::vector<std::uint64_t> ops;
    std::vector<SlotLatency> lat;
};
double uniform01(std::mt19937_64& g) { return (static_cast<double>(g() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
double draw_cycle(std::mt19937_64& g, bool heavy) {
    const double mean = heavy && uniform01(g) < 0.05 ? 20.0 : 1.0;
    return -std::log(uniform01(g)) * mean;
}
void sim_slot(SimSlots& s, std::mt19937_64& g, double horizon, bool heavy) {
    std::uint64_t n = 0;
    SlotLatency l;
    double t = 0.0;
    for (;;) {
        const double x = draw_cycle(g, heavy);
        if (t + x > horizon) break;
        t += x;
        ++n;
        l.add(x);
    }
    s.ops.push_back(n);
    s.lat.push_back(l);
}
}  // namespace

EF_TEST(storage_slot_fairness_basics_and_derivation) {
    // Bos/tek slot/CV yok/az ornek: bilinmez, iddia yok.
    EF_CHECK(!slot_fairness({}, 1.0).known && slot_fairness({}, 1.0).slots == 0);
    EF_CHECK(!slot_fairness({5000}, 1.0).known);
    EF_CHECK(!slot_fairness({4000, 3}, std::nullopt).known && !slot_fairness({4000, 3}, std::nullopt).starved);
    const auto few = slot_fairness(std::vector<std::uint64_t>(8, 17), 1.0);
    EF_CHECK(!few.known && !few.starved && few.ratio == 1.0);
    // z = sqrt(2 ln(k / (2 alfa))): k = 32, alfa = 1e-4 -> ~4.895.
    EF_CHECK(std::fabs(slot_starvation_z(32) - std::sqrt(2.0 * std::log(32.0 / 2e-4))) < 1e-12);
    EF_CHECK(slot_starvation_z(32) > 4.8 && slot_starvation_z(32) < 5.0);
    EF_CHECK(slot_starvation_z(0) == slot_starvation_z(1));
    // Taban = ortalama - z*c*sqrt(ortalama); iki kosul birlikte (taban VE oran < 0.5).
    const auto f = slot_fairness({1000, 1000, 1000, 100}, 1.0);  // ort 775, taban ~775 - 4.1*27.8 ~ 660
    EF_CHECK(f.known && f.starved && f.floor_ops > 600 && f.floor_ops < 700 && f.cycle_cv && *f.cycle_cv == 1.0);
    EF_CHECK(!slot_fairness({1000, 1000, 1000, 500}, 1.0).starved);   // oran 0.57: pratik esigin ustunde
    EF_CHECK(!slot_fairness({130, 130, 130, 40}, 3.0).starved);       // oran 0.37 ama taban < 0: gurultu icinde
    // fairness_cycle_cv: < kMinCvSamples ornekli slotlar sayilmaz; sabit dongu CV 0.
    std::vector<SlotLatency> v(3);
    for (int i = 0; i < 20; ++i) v[0].add(5.0);
    for (int i = 0; i < 20; ++i) v[1].add(i % 2 ? 1.0 : 3.0);  // ort 2, sd 1 -> 0.5
    for (int i = 0; i < 3; ++i) v[2].add(i == 0 ? 1.0 : 1000.0);
    const auto cv = fairness_cycle_cv(v);
    EF_CHECK(cv && std::fabs(*cv - 0.5) < 1e-9);
    EF_CHECK(!fairness_cycle_cv({v[2]}));
    EF_CHECK(fairness_cycle_cv({v[0]}) && *fairness_cycle_cv({v[0]}) == 0.0);
    EF_CHECK(!fairness_cycle_cv({}));
    // Alt medyanin altindaki slot c'ye girmez: ornek sayilari 40,40,40,12,3 -> alt medyan 40; 12 ornekli yuksek
    // CV'li slot (ac kalmis) dislanir, c = 40 ornekli slotlarin en buyugu.
    std::vector<SlotLatency> w(5);
    for (int i = 0; i < 40; ++i) {
        w[0].add(5.0);
        w[1].add(i % 2 ? 1.0 : 3.0);
        w[2].add(2.0);
    }
    for (int i = 0; i < 12; ++i) w[3].add(i < 11 ? 1.0 : 1000.0);
    for (int i = 0; i < 3; ++i) w[4].add(1.0);
    const auto wc = fairness_cycle_cv(w);
    EF_CHECK(wc && std::fabs(*wc - 0.5) < 1e-9);
}

EF_TEST(storage_slot_fairness_high_cv_healthy_not_flagged) {
    // Saglikli ama yuksek degiskenlikli cihaz: 32 slot, slot basina ~100 G/C, dongu CV ~3.16. Eski sabit 0.5 esigi
    // bu dagilimda en dusuk slot ~%35-40'ta kaldigi icin yanlis "aclik" derdi; yeni taban hicbir tohumda isaretlemez.
    // Ufuklar: E[x] = 1.95 -> slot basina ort ~105 / ~410 / ~1640 G/C.
    for (const double horizon : {205.0, 800.0, 3200.0}) {
        int old_rule_would_flag = 0, new_rule_flags = 0, known = 0;
        for (std::uint64_t seed = 1; seed <= 40; ++seed) {
            std::mt19937_64 g(seed * 0x9E3779B97F4A7C15ull + static_cast<std::uint64_t>(horizon));
            SimSlots s;
            for (int i = 0; i < 32; ++i) sim_slot(s, g, horizon, true);
            const auto cv = fairness_cycle_cv(s.lat);
            EF_CHECK(cv && *cv > 2.0);
            const auto f = slot_fairness(s.ops, cv);
            EF_CHECK(!f.starved);
            known += f.known ? 1 : 0;
            new_rule_flags += f.starved ? 1 : 0;
            if (f.known && f.ratio < kMinSlotFairness) ++old_rule_would_flag;
        }
        EF_CHECK(known == 40);
        // En kisa ufukta senaryo gercekten eski kuralin yanlis alarm bolgesinde.
        if (horizon < 300.0) EF_CHECK(old_rule_would_flag > 0);
        std::printf("  yuksek CV saglikli (ufuk %.0f): 40 tohumun %d'inde eski 0.5 kurali aclik derdi; yeni kural %d\n", horizon,
                    old_rule_would_flag, new_rule_flags);
    }
}

EF_TEST(storage_slot_fairness_true_starvation_flagged) {
    // Eski WaitForMultipleObjects(FALSE) davranisi: 8 dusuk slot kayrilir (~4000 G/C, ustel dongu CV ~1),
    // 24 slot ~0 (3 G/C, her biri ~T/3 bekler). Ac slotlarin az ornegi CV'ye girmez; taban ~1000 - 4.9*1*31.6.
    {
        std::mt19937_64 g(7);
        SimSlots s;
        for (int i = 0; i < 8; ++i) sim_slot(s, g, 4000.0, false);
        for (int i = 0; i < 24; ++i) {
            SlotLatency l;
            for (int j = 0; j < 3; ++j) l.add(4000.0 / 3.0);
            s.ops.push_back(3);
            s.lat.push_back(l);
        }
        const auto f = slot_fairness(s.ops, fairness_cycle_cv(s.lat));
        EF_CHECK(f.known && f.starved && f.min_ops == 3 && f.ratio < 0.01);
    }
    // Tek slot kismen ac (ortalamanin ~%25'i), digerleri saglikli ustel: isaretlenir.
    {
        std::mt19937_64 g(11);
        SimSlots s;
        for (int i = 0; i < 31; ++i) sim_slot(s, g, 1000.0, false);
        SlotLatency l;
        for (int j = 0; j < 250; ++j) l.add(4.0);
        s.ops.push_back(250);
        s.lat.push_back(l);
        const auto f = slot_fairness(s.ops, fairness_cycle_cv(s.lat));
        EF_CHECK(f.known && f.starved && f.min_ops == 250);
    }
}

namespace {
// Duzensiz gecikmeli ac slot: n - 2 kisa dongu (1) ve 2 cok uzun dongu (big) -> yuksek CV.
SlotLatency spread_latency(std::uint64_t n, double big) {
    SlotLatency l;
    for (std::uint64_t j = 0; j + 2 < n; ++j) l.add(1.0);
    l.add(big);
    l.add(big);
    return l;
}
double cv_of(const SlotLatency& l) {
    const double n = static_cast<double>(l.n), mean = l.sum / n;
    return std::sqrt(std::max(0.0, l.sum_sq / n - mean * mean)) / mean;
}
// Eski tahmin (>= kMinCvSamples ornekli TUM slotlarin en buyugu): ac slotun kendi CV'si de girerdi.
std::optional<double> old_max_cv(const std::vector<SlotLatency>& v) {
    std::optional<double> best;
    for (const auto& l : v)
        if (l.n >= kMinCvSamples && (!best || cv_of(l) > *best)) best = cv_of(l);
    return best;
}
}  // namespace

EF_TEST(storage_slot_fairness_starved_slot_with_spread_latency_flagged) {
    // Ac slotun gecikmeleri duzensiz/uzun (CV >= 3) oldugunda tabani kendisi dusurup kendi acligini gizlememeli.
    struct Case {
        double horizon;
        std::uint64_t starved_n;
        double big;
    };
    // (1) 32 slot ort ~200 (ustel, CV ~1), ac slot 50 G/C; (2) ort ~1000, ac slot 250 G/C.
    for (const Case c : {Case{200.0, 50, 200.0}, Case{1000.0, 250, 1000.0}}) {
        std::mt19937_64 g(static_cast<std::uint64_t>(c.horizon) * 31 + 5);
        SimSlots s;
        for (int i = 0; i < 31; ++i) sim_slot(s, g, c.horizon, false);
        const SlotLatency starved = spread_latency(c.starved_n, c.big);
        EF_CHECK(starved.n == c.starved_n && cv_of(starved) >= 3.0);
        s.ops.push_back(c.starved_n);
        s.lat.push_back(starved);
        // Eski tahminle ac slot kendi acligini gizliyordu (senaryo gercekten bu hatanin bolgesinde).
        const auto old_f = slot_fairness(s.ops, old_max_cv(s.lat));
        EF_CHECK(old_f.known && !old_f.starved);
        const auto cv = fairness_cycle_cv(s.lat);
        EF_CHECK(cv && *cv < 2.0);  // c saglikli slotlardan (ustel, ~1)
        const auto f = slot_fairness(s.ops, cv);
        EF_CHECK(f.known && f.starved && f.min_ops == c.starved_n && f.ratio < kMinSlotFairness);
        std::printf("  duzensiz gecikmeli ac slot (ort %.0f, min %llu, ac slot CV %.1f): eski c %.2f isaretlemezdi, yeni c %.2f isaretler\n",
                    f.mean_ops, static_cast<unsigned long long>(f.min_ops), cv_of(starved), old_f.cycle_cv ? *old_f.cycle_cv : -1.0, *cv);
    }
}

EF_TEST(storage_slot_fairness_healthy_high_cv_median_cut_not_flagged) {
    // Karsi durum: tum slotlar saglikli ama yuksek degiskenlikli (CV ~3.16). Medyan kesimi c'yi saglikli slotlardan
    // almaya devam eder; en dusuk slotun kendisi de yuksek CV'li olsa isaretlenmez.
    for (const double horizon : {410.0, 1600.0}) {  // slot basina ort ~210 / ~820 G/C (>= kMinOpsPerSlot)
        int flags = 0;
        for (std::uint64_t seed = 101; seed <= 140; ++seed) {
            std::mt19937_64 g(seed * 0xD1B54A32D192ED03ull + static_cast<std::uint64_t>(horizon));
            SimSlots s;
            for (int i = 0; i < 32; ++i) sim_slot(s, g, horizon, true);
            const auto cv = fairness_cycle_cv(s.lat);
            EF_CHECK(cv && *cv > 2.0);
            const auto f = slot_fairness(s.ops, cv);
            EF_CHECK(f.known);
            flags += f.starved ? 1 : 0;
        }
        EF_CHECK(flags == 0);
    }
}
