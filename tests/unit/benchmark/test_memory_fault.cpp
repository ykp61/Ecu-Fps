// EF-X22.01: RAM alt testinde hatali hesap/veri enjekte edilince sonuc Failed olur ve skor uretilmez.
// Gercek bellek arizasi taramasi degildir; dogrulama dallarinin "hata -> Failed, skor yok" eslemesini sinar.
#include <atomic>
#include <string>

#include "benchmark/memory/memory_bench.h"
#include "benchmark/memory/memory_budget.h"
#include "ef_test.h"

using namespace ef::benchmark::memory;

namespace {
// Kisa ve kucuk tahsisli yapilandirma: sahte bos RAM girdisi, 64 MiB tavan, kisa olcum pencereleri, MT yok.
MemoryConfig small_cfg() {
    MemoryConfig c;
    c.snapshot_override = MemorySnapshot{32 * kGiB, 16 * kGiB};
    c.policy.cap_bytes = 64 * kMiB;
    c.min_seconds_per_measure = 0.005;
    c.max_seconds_per_measure = 0.01;
    c.multi_thread = false;
    return c;
}

// Basarisiz sonucta hicbir skor/olcum bulunmamali.
bool no_score(const MemoryResult& r) {
    return r.buffers.empty() && !r.latency_ns.has_value() && r.large_buffer_bytes == 0 && !r.mt.read_gbps.has_value() &&
           !r.mt.write_gbps.has_value() && !r.mt.copy_gbps.has_value();
}
}  // namespace

EF_TEST(x22_memory_control_run_completes_with_scores) {
    // Kontrol: enjeksiyon yokken ayni yapilandirma tamamlanir ve skor uretir (sonraki testler kancadan dolayi basarisiz olur).
    std::atomic<bool> cancel{false};
    const MemoryResult r = run_memory_bench(small_cfg(), cancel);
    EF_CHECK(r.status == MemStatus::Completed);
    EF_CHECK(!r.buffers.empty());
    EF_CHECK(r.buffers.front().seq_read_gbps.has_value() && r.buffers.front().seq_write_gbps.has_value());
}

EF_TEST(x22_memory_injected_write_corruption_is_failed_not_score) {
    std::atomic<bool> cancel{false};
    auto cfg = small_cfg();
    cfg.inject_corruption = MemoryConfig::Corruption::WritePass;
    cfg.inject_corruption_at_buffer = 0;
    const MemoryResult r = run_memory_bench(cfg, cancel);
    EF_CHECK(r.status == MemStatus::Failed);
    EF_CHECK(r.reason == "memory_verify_mismatch");
    EF_CHECK(no_score(r));
    EF_CHECK(!r.is_fault_test);  // hata bildirimi, ariza testi iddiasi degil
}

EF_TEST(x22_memory_injected_address_pattern_corruption_is_failed_not_score) {
    std::atomic<bool> cancel{false};
    auto cfg = small_cfg();
    cfg.inject_corruption = MemoryConfig::Corruption::AddressPattern;
    cfg.inject_corruption_at_buffer = 0;
    const MemoryResult r = run_memory_bench(cfg, cancel);
    EF_CHECK(r.status == MemStatus::Failed);
    EF_CHECK(r.reason == "memory_address_pattern_mismatch");
    EF_CHECK(no_score(r));
}

EF_TEST(x22_memory_corruption_on_later_buffer_is_still_failed) {
    // Ilk tampon temiz, ikincisi bozuk: genel durum yine Failed (bozuk tamponun kaydi yok). Onceki temiz tamponun ham
    // kaydi sonucta kalir; runner yalniz Completed durumunda metrik uretir (Failed -> yalniz neden), skor sunulmaz.
    std::atomic<bool> cancel{false};
    auto cfg = small_cfg();
    cfg.inject_corruption = MemoryConfig::Corruption::WritePass;
    cfg.inject_corruption_at_buffer = 1;
    const MemoryResult r = run_memory_bench(cfg, cancel);
    EF_CHECK(r.status == MemStatus::Failed && r.reason == "memory_verify_mismatch");
    EF_CHECK(r.buffers.size() == 1);  // yalniz bozuk olmayan onceki tampon kaydi; bozuk tampon kaydi yok
}
