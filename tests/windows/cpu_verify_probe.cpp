// EF-X22: gercek islemcide hesap dogrulamasi (degisken yuk + CPUID etiketli SIMD varyantlari). Pencere acmaz,
// ayar yazmaz, oncelik/affinity degistirmez.
//  [1] CPUID/XCR0 okumasi ve varyant destegi (desteklenmeyen = desteklenmiyor, basarisizlik degil).
//  [2] Hizli yapilandirma, tum mantiksal islemciler: desteklenen her varyant referansla bit-ayni -> Ok;
//      dort yuk fazinin her birinde blok karsilastirildi.
//  [3] Hatali hesap enjeksiyonu (blok 5): Mismatch, ilk uyusmaz blok 5.
//  [4] Baslamadan iptal: Cancelled, 0 blok.
//  [5] Yalniz skaler CPUID benzetimi: SIMD varyantlari calistirilmaz, sonuc Ok.
// Sonuc bu islemci icin hesap tutarliligi kanitidir; donanim saglik karari degildir.
#include <atomic>
#include <cstdio>
#include <string>

#include "benchmark/cpu/cpu_verify.h"

using namespace ef::benchmark::cpu_verify;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}
void print(const char* tag, const Result& r) {
    std::printf("%s status=%s threads=%u blocks=%llu mismatch_blocks=%llu total_ms=%.1f phases=[%llu,%llu,%llu,%llu]\n", tag,
                std::string(to_string(r.status)).c_str(), r.threads, static_cast<unsigned long long>(r.blocks),
                static_cast<unsigned long long>(r.mismatched_blocks), r.total_ms, static_cast<unsigned long long>(r.phase_blocks[0]),
                static_cast<unsigned long long>(r.phase_blocks[1]), static_cast<unsigned long long>(r.phase_blocks[2]),
                static_cast<unsigned long long>(r.phase_blocks[3]));
    for (const auto& i : r.isas)
        std::printf("    isa=%s supported=%d blocks=%llu mismatch_blocks=%llu mismatch_words=%llu\n", std::string(isa_name(i.isa)).c_str(),
                    i.supported ? 1 : 0, static_cast<unsigned long long>(i.blocks), static_cast<unsigned long long>(i.mismatched_blocks),
                    static_cast<unsigned long long>(i.mismatched_words));
}
}  // namespace

int main() {
    const auto bits = read_cpuid();
    std::printf("[1] cpuid_ok=%d sse2=%d osxsave=%d avx=%d avx2=%d avx512f=%d xcr0=0x%llx\n", bits.cpuid_ok, bits.sse2, bits.osxsave, bits.avx,
                bits.avx2, bits.avx512f, static_cast<unsigned long long>(bits.xcr0));
    for (std::uint32_t k = 0; k < kIsaCount; ++k)
        std::printf("    %s: %s\n", std::string(isa_name(static_cast<Isa>(k))).c_str(),
                    isa_supported(static_cast<Isa>(k), bits) ? "supported" : "unsupported");
    check(isa_supported(Isa::Scalar, bits), "[1] scalar supported");

    std::atomic<bool> no{false};
    const auto q = quick_config();
    const auto r2 = run(q, no);
    print("[2]", r2);
    check(r2.status == Status::Ok, "[2] quick config Ok on this CPU");
    check(r2.mismatched_blocks == 0, "[2] no mismatched blocks");
    bool all_phases = true;
    for (auto n : r2.phase_blocks) all_phases = all_phases && n > 0;
    check(all_phases, "[2] every load phase (100/25/75/50%) compared blocks");
    bool labels_ok = true;
    for (const auto& i : r2.isas) {
        labels_ok = labels_ok && i.supported == isa_supported(i.isa, bits);
        if (i.supported) labels_ok = labels_ok && i.blocks >= q.min_blocks_per_isa;
        else labels_ok = labels_ok && i.blocks == 0;
    }
    check(labels_ok, "[2] supported variants verified, unsupported variants not executed");
    check(r2.total_ms < q.time_budget_ms + 1500.0, "[2] time budget respected");

    auto f = q;
    f.time_budget_ms = 600;
    f.inject_fault_block = 5;
    const auto r3 = run(f, no);
    print("[3]", r3);
    check(r3.status == Status::Mismatch && r3.mismatched_blocks == 1, "[3] injected fault -> Mismatch");
    check(r3.first_mismatch_block && *r3.first_mismatch_block == 5, "[3] first mismatch block 5");

    std::atomic<bool> yes{true};
    const auto r4 = run(q, yes);
    print("[4]", r4);
    check(r4.status == Status::Cancelled && r4.blocks == 0, "[4] pre-cancel -> Cancelled, 0 blocks");

    auto s = q;
    s.time_budget_ms = 400;
    s.cpuid_override = CpuidBits{};
    const auto r5 = run(s, no);
    print("[5]", r5);
    check(r5.status == Status::Ok && r5.isas[0].blocks > 0 && !r5.isas[1].supported && !r5.isas[2].supported && !r5.isas[3].supported,
          "[5] scalar-only CPUID: SIMD unsupported, not failed");

    std::printf("%s\n", g_fail ? "FAILED" : "ALL PASS");
    return g_fail ? 1 : 0;
}
