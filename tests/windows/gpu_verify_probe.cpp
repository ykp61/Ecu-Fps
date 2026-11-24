// EF-X22: gercek GPU'da compute/geri okuma dogrulamasi (ekran disi; pencere acilmaz, ayar yazilmaz).
//  [1] Hizli yapilandirma: donanim adaptorunde tum tekrarlar CPU referansiyla bit-ayni -> Ok.
//  [2] Hatali hesap enjeksiyonu (tekrar 1'in geri okumasinda tek bit): Mismatch, skor etkisi FailNoScore.
//  [3] Baslamadan iptal: Cancelled, 0 tekrar, "gecti" yok.
//  [4] Tekrar sayisi butcesi: max_repeats tavani asilmaz.
// Donanim ekran karti yoksa (yalniz WARP / cihaz kurulamaz) sonuc Unsupported: cikis 77 (atlandi), gecti DEGIL.
// Sonuc bu GPU + surucu icin hesap tutarliligi kanitidir; donanim saglik karari degildir.
#include <atomic>
#include <cstdio>
#include <string>

#include "benchmark/scene/gpu_verify.h"

using namespace ef::benchmark::gpu_verify;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}
void print(const char* tag, const Result& r) {
    std::printf("%s status=%s err=%s adapter=%ls vendor=0x%04x device=0x%04x words=%u iter=%u repeats=%u mismatch=%llu prep_ms=%.1f total_ms=%.1f bytes=%llu\n",
                tag, std::string(to_string(r.status)).c_str(), r.error.c_str(), r.adapter.c_str(), r.vendor_id, r.device_id, r.words,
                r.iterations, r.repeats_done, static_cast<unsigned long long>(r.mismatched_words), r.prepare_ms, r.total_ms,
                static_cast<unsigned long long>(r.bytes));
}
}  // namespace

int main() {
    std::atomic<bool> no_cancel{false};
    const auto q = quick_config();
    const auto r1 = run(q, no_cancel);
    print("[1]", r1);
    if (r1.status == Status::Unsupported) {
        std::printf("SKIP: donanim GPU / D3D11 compute yok (unsupported; gecti sayilmaz)\n");
        return 77;
    }
    check(r1.status == Status::Ok, "[1] quick config Ok on hardware GPU");
    check(r1.mismatched_words == 0, "[1] no mismatched words");
    check(r1.repeats_done >= q.min_repeats && r1.repeats_done <= q.max_repeats, "[1] repeats within [min,max]");
    check(r1.total_ms <= q.time_budget_ms + q.wait_timeout_ms, "[1] total time bounded by budget + one wait");

    auto bad = q;
    bad.inject_fault_repeat = 1;
    bad.max_repeats = bad.min_repeats;
    const auto r2 = run(bad, no_cancel);
    print("[2]", r2);
    check(r2.status == Status::Mismatch, "[2] injected fault -> Mismatch");
    check(r2.mismatched_words == 1 && r2.first_mismatch_repeat == 1u && r2.first_mismatch_index == 0u, "[2] exactly word 0 of repeat 1");
    check(verdict(r2.status).effect == Effect::FailNoScore && verdict(r2.status).reason == "gpu_compute_mismatch", "[2] no score, reason");

    std::atomic<bool> cancelled{true};
    const auto r3 = run(q, cancelled);
    print("[3]", r3);
    check(r3.status == Status::Cancelled && r3.repeats_done == 0, "[3] pre-cancel -> Cancelled, 0 repeats");
    check(verdict(r3.status).effect != Effect::Pass, "[3] cancel is not a pass");

    auto capped = q;
    capped.min_repeats = 2;
    capped.max_repeats = 2;
    capped.time_budget_ms = 60000;
    const auto r4 = run(capped, no_cancel);
    print("[4]", r4);
    check(r4.status == Status::Ok && r4.repeats_done == 2, "[4] max_repeats cap honored");

    const auto f = full_config();
    const auto r5 = run(f, no_cancel);
    print("[5]", r5);
    check(r5.status == Status::Ok && r5.mismatched_words == 0, "[5] full config Ok");
    check(r5.repeats_done >= f.min_repeats && r5.total_ms <= f.time_budget_ms + f.wait_timeout_ms, "[5] full config bounded");

    std::printf("%s (%d failed)\n", g_fail ? "FAIL" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
