// T16: saglik testi saf mantigi: CPU hukmu, RAM desen dogrulama, GPU hukmu eslemesi, hazirlik satirlari.
#include <cstdint>
#include <optional>
#include <vector>

#include "core/diagnostics/health_test.h"
#include "ef_test.h"

using namespace ef::core::diagnostics;
namespace cr = ef::core::readiness;

namespace {
std::vector<CpuSample> series(int n, double first, double later, std::optional<double> limit = std::nullopt) {
    std::vector<CpuSample> v;
    for (int i = 0; i < n; ++i) {
        CpuSample s;
        s.pct_of_max_freq = i < 10 ? first : later;
        s.perf_limit_pct = limit;
        v.push_back(s);
    }
    return v;
}
}  // namespace

EF_TEST(health_cpu_stable_throttled_insufficient) {
    EF_CHECK(cpu_verdict(series(60, 100.0, 98.0)) == CpuVerdict::Stable);
    EF_CHECK(cpu_verdict(series(60, 100.0, 84.0)) == CpuVerdict::Throttled);  // son 20 sn ortancasi ilk 10 sn'nin %85'inin altinda
    EF_CHECK(cpu_verdict(series(60, 100.0, 85.0)) == CpuVerdict::Stable);     // tam %85 siniri throttled degil
    EF_CHECK(cpu_verdict(series(29, 100.0, 100.0)) == CpuVerdict::Insufficient);
    EF_CHECK(cpu_verdict({}) == CpuVerdict::Insufficient);
    EF_CHECK(cpu_verdict(series(60, 0.0, 0.0)) == CpuVerdict::Insufficient);  // ilk 10 sn olculemedi
}

EF_TEST(health_cpu_perf_limit_share) {
    auto v = series(60, 100.0, 100.0, 100.0);
    for (int i = 0; i < 6; ++i) v[static_cast<std::size_t>(i)].perf_limit_pct = 70.0;  // %10: esik asilmadi
    EF_CHECK(cpu_verdict(v) == CpuVerdict::Stable);
    v[6].perf_limit_pct = 70.0;  // 7/60 > %10
    EF_CHECK(cpu_verdict(v) == CpuVerdict::Throttled);
    // sinir sayaci hic okunamadiysa tek basina Throttled denmez
    EF_CHECK(cpu_verdict(series(60, 100.0, 100.0)) == CpuVerdict::Stable);
}

EF_TEST(health_cpu_temp_only_when_present) {
    auto v = series(40, 100.0, 100.0);
    EF_CHECK(!cpu_summary(v)->max_temp_c);
    v[3].temp_c = 71.0;
    v[9].temp_c = 83.0;
    EF_CHECK(cpu_summary(v)->max_temp_c && *cpu_summary(v)->max_temp_c == 83.0);
}

EF_TEST(health_ram_pattern_clean_and_flipped_bit) {
    constexpr std::size_t kWords = 4096;
    std::vector<std::uint64_t> buf(kWords);
    for (int p = 0; p < kRamPatternCount; ++p) {
        const auto pat = static_cast<RamPattern>(p);
        ram_fill(buf.data(), kWords, pat, 12345);
        EF_CHECK(ram_verify(buf.data(), kWords, pat, 12345) == 0);  // temiz tampon: 0 hata
        buf[1000] ^= (1ull << 17);                                   // tek bit cevrildi
        EF_CHECK(ram_verify(buf.data(), kWords, pat, 12345) == 1);
        buf[3000] = ~buf[3000];
        EF_CHECK(ram_verify(buf.data(), kWords, pat, 12345) == 2);
    }
}

EF_TEST(health_ram_patterns_differ) {
    std::vector<std::uint64_t> a(256), b(256);
    ram_fill(a.data(), 256, RamPattern::WalkingOnes, 1);
    ram_fill(b.data(), 256, RamPattern::WalkingOnesInverse, 1);
    EF_CHECK(a[0] == ~b[0] && a[5] == ~b[5]);
    ram_fill(a.data(), 256, RamPattern::Prng, 1);
    ram_fill(b.data(), 256, RamPattern::Prng, 2);
    EF_CHECK(a[0] != b[0]);  // farkli tohum farkli desen
    EF_CHECK(ram_verify(a.data(), 256, RamPattern::Prng, 2) > 0);
}

EF_TEST(health_ram_budget_and_verdict) {
    constexpr std::uint64_t GiB = 1ull << 30;
    EF_CHECK(ram_test_bytes(0) == 0);
    EF_CHECK(ram_test_bytes(2 * GiB) == 0);                  // test sonrasi 2 GiB bos kalmali
    EF_CHECK(ram_test_bytes(2 * GiB + (100ull << 20)) == (100ull << 20));  // serbest-2GiB siniri: 100 MiB
    EF_CHECK(ram_test_bytes(2 * GiB + (32ull << 20)) == 0);                // 64 MiB'den az: calistirilmaz
    EF_CHECK(ram_test_bytes(16 * GiB) == 2 * GiB);           // min(16/4=4, 2) GiB
    EF_CHECK(ram_test_bytes(6 * GiB) == (6 * GiB) / 4);      // 1.5 GiB: serbest/4
    EF_CHECK(ram_test_bytes(64 * GiB) == 2 * GiB);
    EF_CHECK(ram_verdict({0, 0, false}) == RamVerdict::NotRun);
    EF_CHECK(ram_verdict({1ull << 30, 0, true}) == RamVerdict::Ok);
    EF_CHECK(ram_verdict({1ull << 30, 0, false}) == RamVerdict::Partial);
    EF_CHECK(ram_verdict({1ull << 30, 3, true}) == RamVerdict::Errors);
}

EF_TEST(health_gpu_verdict_mapping) {
    EF_CHECK(gpu_verdict(false, false, false) == GpuVerdict::Unavailable);
    EF_CHECK(gpu_verdict(false, true, true) == GpuVerdict::Unavailable);  // yuk yoksa hata iddia edilmez
    EF_CHECK(gpu_verdict(true, false, false) == GpuVerdict::Stable);
    EF_CHECK(gpu_verdict(true, true, false) == GpuVerdict::Faulted);
    EF_CHECK(gpu_verdict(true, false, true) == GpuVerdict::Faulted);
}

EF_TEST(health_readiness_rows) {
    // Rapor yok: tek Info satiri, Saglik testi eylemi, sayi yok.
    const auto idle = health_test_checks(false, 0, 0, std::nullopt);
    EF_CHECK(idle.size() == 1 && idle[0].id == "hardware.health_test" && idle[0].action == cr::ActionKind::RunHealthTest);
    EF_CHECK(idle[0].section == cr::Section::Hardware && idle[0].level == cr::Level::Info && idle[0].args.empty());
    // Suruyor: dugme yok.
    const auto run = health_test_checks(true, 2, 12, std::nullopt);
    EF_CHECK(run.size() == 1 && run[0].action == cr::ActionKind::None && run[0].args.at("sec") == "12" && run[0].args.at("what") == "RAM");
    // Pil tasarrufu reddi.
    HealthReport refused;
    refused.refusal = HealthRefusal::BatterySaver;
    const auto rf = health_test_checks(false, 0, 0, refused);
    EF_CHECK(rf.size() == 1 && rf[0].text_key == "chk.hardware.health_test.refused_battery_saver" && rf[0].action == cr::ActionKind::RunHealthTest);
    // Tam rapor: sicaklik sensoru yok satiri, GPU "yuk uygulanmadi", RAM temiz, puan yok.
    HealthReport rep;
    rep.cpu = series(60, 100.0, 70.0);
    rep.ram = {1ull << 30, 0, true};
    rep.ram_run = true;
    const auto rows = health_test_checks(false, 0, 0, rep);
    auto find = [&](const char* id) -> const cr::Check* {
        for (const auto& c : rows)
            if (c.id == id) return &c;
        return nullptr;
    };
    EF_CHECK(find("hardware.health_test.cpu") && find("hardware.health_test.cpu")->text_key == "chk.hardware.health_test.cpu_throttled");
    EF_CHECK(find("hardware.health_test.temp") && find("hardware.health_test.temp")->text_key == "chk.hardware.health_test.no_sensor");
    EF_CHECK(find("hardware.health_test.ram") && find("hardware.health_test.ram")->text_key == "chk.hardware.health_test.ram_ok");
    EF_CHECK(find("hardware.health_test.ram")->args.at("mib") == "1024");
    EF_CHECK(find("hardware.health_test.gpu") && find("hardware.health_test.gpu")->text_key == "chk.hardware.health_test.gpu_unavailable");
    // RAM hatasi Problem.
    rep.ram.errors = 4;
    for (const auto& c : health_test_checks(false, 0, 0, rep))
        if (c.id == "hardware.health_test.ram") EF_CHECK(c.level == cr::Level::Problem && c.text_key == "chk.hardware.health_test.ram_errors");
    // Sicaklik yalniz sensor degeri varsa.
    rep.cpu[2].temp_c = 80.0;
    for (const auto& c : health_test_checks(false, 0, 0, rep))
        if (c.id == "hardware.health_test.temp") EF_CHECK(c.text_key == "chk.hardware.health_test.temp" && c.args.at("temp") == "80");
}
