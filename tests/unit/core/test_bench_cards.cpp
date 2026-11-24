#include <cstdint>
#include <string>

#include "core/i18n/catalog.h"
#include "core/measure/bench_cards.h"
#include "ef_test.h"

using namespace ef::core::measure;
namespace i18n = ef::core::i18n;

EF_TEST(bench_cards_memory_mt_contention_notes) {
    // BM-03: olculmus bellek MT cekismesi kartta gosterilir; olculmemisse iddia uretilmez.
    const auto c = component_cards({{"mem_mt_read_gbps", 61.0}, {"mem_mt_contention_known", 1}, {"mem_mt_contended", 1}}, {})[2];
    EF_CHECK(c.metrics[0].note_key == "bench.note.contended");
    EF_CHECK(i18n::id_of(c.metrics[0].note_key).has_value());
    // Buyuk tampon + cekisme: birlesik not; kart basina bir kez (M09).
    const auto both = component_cards({{"mem_mt_read_gbps", 61.0}, {"mem_mt_write_gbps", 35.0}, {"mem_latency_ns", 80.0},
                                       {"mem_mt_buffer_l3_ratio", 4.0}, {"mem_mt_contention_known", 1}, {"mem_mt_contended", 1}},
                                      {})[2];
    int notes = 0;
    for (const auto& x : both.metrics) notes += x.note_key.empty() ? 0 : 1;
    EF_CHECK(notes == 1 && both.metrics[3].note_key == "bench.note.large_buffer_contended");
    EF_CHECK(i18n::id_of("bench.note.large_buffer_contended").has_value());
    // Cekisme bilinmiyor (known = 0): "cekisme yok" da denmez, not da dusulmez.
    const auto unknown = component_cards({{"mem_mt_read_gbps", 61.0}, {"mem_mt_contention_known", 0}, {"mem_mt_contended", 1}}, {})[2];
    for (const auto& x : unknown.metrics) EF_CHECK(x.note_key.empty());
}

EF_TEST(bench_cards_real_metrics_and_missing_not_zero) {
    std::map<std::string, double> m{{"scene_avg_fps", 93.6}, {"scene_low1_fps", 88.1}, {"cpu_st_units_per_s", 861.0},
                                    {"cpu_mt_units_per_s", 5235.0}, {"mem_mt_read_gbps", 61.0}, {"mem_mt_buffer_l3_ratio", 16.0}};
    std::map<std::string, SubtestInfo> st{{"scene", {"completed", ""}}, {"cpu", {"completed", ""}}, {"memory", {"completed", ""}},
                                          {"storage", {"skipped", "no_volume_selected"}}};
    const auto cards = component_cards(m, st, {{"tier", "high"}, {"res", "1920x1080"}});
    EF_CHECK(cards.size() == 4 && cards[0].id == "gpu" && cards[0].args.at("tier") == "high");
    EF_CHECK(cards[0].metrics[0].value == 93.6 && !cards[0].metrics[2].value);  // p99 olculmedi: 0 degil, bos
    EF_CHECK(cards[2].metrics[0].note_key.empty());                              // tampon yeterli: DRAM notu yok
    EF_CHECK(cards[3].status == "skipped" && cards[3].reason == "no_volume_selected");
}

EF_TEST(bench_cards_small_buffer_and_burst_notes) {
    std::map<std::string, double> m{{"mem_mt_read_gbps", 82.0}, {"mem_mt_buffer_l3_ratio", 4.0}, {"storage_seq_1m_qd8_write_mbps", 2564},
                                    {"storage_seq_1m_qd8_write_burst", 1}};
    const auto cards = component_cards(m, {});
    EF_CHECK(cards[2].metrics[0].note_key == "bench.note.large_buffer");
    EF_CHECK(cards[3].metrics[1].note_key == "bench.note.burst");
}

EF_TEST(bench_cards_latency_only_and_contention_notes) {
    // MT yok, yalniz gecikme: L3 biliniyor ve oran < 8 -> "onbellege gore kucuk" notu.
    {
        std::map<std::string, double> m{{"mem_latency_ns", 80.0}, {"mem_large_buffer_l3_known", 1},
                                        {"mem_large_buffer_dram", 0}, {"mem_large_buffer_l3_ratio", 4}};
        const auto cards = component_cards(m, {});
        EF_CHECK(cards[2].metrics[3].value == 80.0 && cards[2].metrics[3].note_key == "bench.note.large_buffer");
    }
    // L3 bilinmiyor: oran yok -> ne DRAM ne onbellek iddiasi (veri yetersiz), not yok. Ara surum kaydi
    // (dram = 0, oran yok) da ayni sekilde iddia uretmez.
    {
        std::map<std::string, double> m{{"mem_latency_ns", 80.0}, {"mem_large_buffer_l3_known", 0}};
        EF_CHECK(component_cards(m, {})[2].metrics[3].note_key.empty());
        std::map<std::string, double> mid{{"mem_latency_ns", 80.0}, {"mem_large_buffer_dram", 0}};
        for (const auto& x : component_cards(mid, {})[2].metrics) EF_CHECK(x.note_key.empty());
    }
    {
        std::map<std::string, double> m{{"mem_latency_ns", 80.0}, {"mem_large_buffer_dram", 1}, {"mem_large_buffer_l3_ratio", 16}};
        EF_CHECK(component_cards(m, {})[2].metrics[3].note_key.empty());
    }
    // CPU: yalniz olculen cekisme not dusurur; bilinmeyen pay (anahtar yok) iddia uretmez.
    {
        std::map<std::string, double> m{{"cpu_st_units_per_s", 800}, {"cpu_mt_units_per_s", 4000}, {"cpu_st_contended", 0},
                                        {"cpu_mt_contended", 1}};
        const auto cards = component_cards(m, {});
        EF_CHECK(cards[1].metrics[0].note_key.empty() && cards[1].metrics[1].note_key == "bench.note.contended");
        const auto none = component_cards({{"cpu_st_units_per_s", 800}}, {});
        EF_CHECK(none[1].metrics[0].note_key.empty() && none[1].metrics[1].note_key.empty());
    }
}

EF_TEST(bench_cards_scene_utilization) {
    // DIAG-11: sahne penceresi isaretliyse satirlar gorunur; deger yoksa "Olculemedi" (0 yazilmaz).
    std::map<std::string, double> m{{"scene_avg_fps", 90.0}, {"scene_util_sampled", 1}, {"scene_gpu_pct", 97.0},
                                    {"scene_cpu_pct", 42.0}, {"scene_cpu_max_core_pct", 81.0}};
    const auto cards = component_cards(m, {});
    EF_CHECK(cards[0].metrics.size() == 4 && cards[0].metrics[3].value == 97.0);
    EF_CHECK(i18n::id_of(cards[0].metrics[3].label_key).has_value());
    EF_CHECK(i18n::id_of(cards[0].metrics[3].unit_key).has_value());  // "unit.percent" katalogda var
    EF_CHECK(i18n::id_of(cards[0].metrics[3].note_key).has_value());
    EF_CHECK(cards[1].metrics.size() == 4 && cards[1].metrics[2].value == 42.0 && cards[1].metrics[3].value == 81.0);
    // PDH degeri okunamadi: satir kalir, deger bos (desteklenmiyor ile 0 karismaz).
    std::map<std::string, double> miss{{"scene_util_sampled", 1}};
    const auto mc = component_cards(miss, {});
    EF_CHECK(mc[0].metrics.size() == 4 && !mc[0].metrics[3].value);
    EF_CHECK(mc[1].metrics.size() == 4 && !mc[1].metrics[2].value);
    // Isaret yok (eski kayit, sahne kosmadi): satir yok.
    const auto old = component_cards({{"scene_avg_fps", 90.0}}, {});
    EF_CHECK(old[0].metrics.size() == 3 && old[1].metrics.size() == 2);
}

EF_TEST(bench_cards_cpu_memory_limited_note) {
    // Varyant bazinda: yalniz is yuku RAM butcesiyle degisen varyanta not; cekisme ile birlikteyse ikisi de kalir.
    {
        std::map<std::string, double> m{{"cpu_st_units_per_s", 800}, {"cpu_mt_units_per_s", 4000}, {"cpu_st_memory_limited", 0},
                                        {"cpu_mt_memory_limited", 1}, {"cpu_mt_contended", 1}, {"cpu_bigdata_memory_limited", 1}};
        const auto c = component_cards(m, {})[1];
        EF_CHECK(c.metrics[0].note_key.empty() && c.metrics[1].note_key == "bench.note.memory_limited_contended");
        m["cpu_mt_contended"] = 0;
        EF_CHECK(component_cards(m, {})[1].metrics[1].note_key == "bench.note.memory_limited");
        m["cpu_bigdata_budget_known"] = 1;
        EF_CHECK(component_cards(m, {})[1].metrics[1].note_key == "bench.note.memory_limited");
    }
    // Butce olculemedi: MT tek isci onlemi "bellek yetersiz" diye sunulmaz; ayri (veri yetersiz) not. Cekisme de
    // varsa birlesik not. ST cekismesi kendi satirinda kalir.
    {
        std::map<std::string, double> m{{"cpu_st_units_per_s", 800}, {"cpu_mt_units_per_s", 4000}, {"cpu_st_memory_limited", 0},
                                        {"cpu_mt_memory_limited", 1}, {"cpu_bigdata_budget_known", 0}, {"cpu_st_contended", 1}};
        auto c = component_cards(m, {})[1];
        EF_CHECK(c.metrics[0].note_key == "bench.note.contended" && c.metrics[1].note_key == "bench.note.memory_unknown");
        m["cpu_mt_contended"] = 1;
        c = component_cards(m, {})[1];
        EF_CHECK(c.metrics[1].note_key == "bench.note.memory_unknown_contended");
        // Butce bilinmiyor ama kisitlama yok: bellek notu yok (iddia uretilmez).
        m["cpu_mt_memory_limited"] = 0;
        EF_CHECK(component_cards(m, {})[1].metrics[1].note_key == "bench.note.contended");
    }
    // ST: olculmus butceyle kucultme + cekisme birlikte.
    {
        std::map<std::string, double> m{{"cpu_st_memory_limited", 1}, {"cpu_st_contended", 1}, {"cpu_bigdata_budget_known", 1}};
        EF_CHECK(component_cards(m, {})[1].metrics[0].note_key == "bench.note.memory_limited_contended");
    }
    {
        std::map<std::string, double> m{{"cpu_st_units_per_s", 800}, {"cpu_st_memory_limited", 1}, {"cpu_mt_memory_limited", 1}};
        const auto c = component_cards(m, {})[1];
        // M09: ayni not iki kez yazilmaz; son satirin (MT) altinda bir kez.
        EF_CHECK(c.metrics[0].note_key.empty() && c.metrics[1].note_key == "bench.note.memory_limited");
    }
    // Eski kayit: yalniz birlesik isaret -> MT.
    {
        const auto c = component_cards({{"cpu_mt_units_per_s", 4000}, {"cpu_bigdata_memory_limited", 1}}, {})[1];
        EF_CHECK(c.metrics[0].note_key.empty() && c.metrics[1].note_key == "bench.note.memory_limited");
    }
    // Isaret 0 / yok: not yok.
    {
        const auto c = component_cards({{"cpu_st_memory_limited", 0}, {"cpu_mt_memory_limited", 0}}, {})[1];
        EF_CHECK(c.metrics[0].note_key.empty() && c.metrics[1].note_key.empty());
    }
}

// B3 inceleme (bosluk 25): sahne penceresinde pencere simge durumundaysa sahne degerleri notla isaretlenir.
EF_TEST(bench_cards_scene_window_minimized_note) {
    std::map<std::string, double> m{{"scene_avg_fps", 93.6}, {"scene_low1_fps", 88.1}, {"scene_p99_ms", 14.0},
                                    {"scene_util_sampled", 1},  {"scene_gpu_pct", 90.0},  {"scene_cpu_pct", 30.0},
                                    {"scene_cpu_max_core_pct", 70.0}, {"cpu_st_units_per_s", 861.0}, {"cpu_mt_units_per_s", 5235.0}};
    auto cards = component_cards(m, {});
    for (const auto& x : cards[0].metrics) EF_CHECK(x.note_key != "bench.note.window_minimized");
    m["scene_window_minimized"] = 1;
    cards = component_cards(m, {});
    // M09: ayni not kartta bir kez, son satirin altinda (kartin ortak notu); sahne kartinin tum satirlari kapsanir.
    EF_CHECK(cards[0].metrics.size() == 4 && cards[0].metrics.back().note_key == "bench.note.window_minimized");
    for (std::size_t i = 0; i + 1 < cards[0].metrics.size(); ++i) EF_CHECK(cards[0].metrics[i].note_key.empty());
    // CPU karti: ST/MT skoru sahne disinda olculur, not almaz; not sahne penceresi satirlarinin sonunda.
    EF_CHECK(cards[1].metrics[0].note_key.empty() && cards[1].metrics[1].note_key.empty());
    EF_CHECK(cards[1].metrics.size() == 4 && cards[1].metrics[3].note_key == "bench.note.window_minimized");
    EF_CHECK(i18n::id_of("bench.note.window_minimized").has_value());
    EF_CHECK(i18n::id_of("bench.scene.window_minimized").has_value());
}

EF_TEST(bench_cards_storage_free_space_args_mapping_round_trips) {
    // Bosluk 7 (B5 inceleme): kosucu eslemesi -> SubtestInfo.args -> kart. Anahtar adlari ortak sabitlerden gelir;
    // biri kayarsa kart genel metne duser ve bu test kirilir.
    constexpr std::uint64_t kGiB = 1ull << 30;
    const std::uint64_t total = 500 * kGiB, need = 30 * kGiB + 5, avail = 12 * kGiB + 7;
    for (const char* reason : {"free_space_floor", "free_share_limit"}) {
        const auto a = storage_free_space_args(true, reason, total, need, avail);
        EF_CHECK(a.size() == 2);
        EF_CHECK(a[0].first == kStorageNeedFreeBytesArg && a[0].second == std::to_string(need));
        EF_CHECK(a[1].first == kStorageAvailFreeBytesArg && a[1].second == std::to_string(avail));
        SubtestInfo si{"skipped", reason};
        for (const auto& [k, v] : a) si.args[k] = v;
        const auto cards = component_cards({}, {{"storage", si}});
        EF_CHECK(cards[3].reason == "free_space_short");
        EF_CHECK(cards[3].args.at("need") == "31" && cards[3].args.at("free") == "12");
    }
    // Atlanmamis, baska neden ya da birim boyutu olculmemis: arg yok (sayi uydurulmaz).
    EF_CHECK(storage_free_space_args(false, "free_space_floor", total, need, avail).empty());
    EF_CHECK(storage_free_space_args(true, "write_budget_exhausted", total, need, avail).empty());
    EF_CHECK(storage_free_space_args(true, "free_space_floor", 0, need, avail).empty());
}

EF_TEST(bench_cards_storage_free_space_skip_shows_need_and_available) {
    // Bosluk 7: bos alan atlamasinda kart gereken / mevcut GB'yi tasir (gereken yukari, mevcut asagi yuvarlanir).
    constexpr unsigned long long kGiB = 1ull << 30;
    SubtestInfo si{"skipped", "free_space_floor"};
    si.args = {{"need_free_bytes", std::to_string(26 * kGiB + 1)}, {"avail_free_bytes", std::to_string(25 * kGiB + kGiB / 2)}};
    auto cards = component_cards({}, {{"storage", si}}, {{"volume", "C:\\"}});
    EF_CHECK(cards[3].id == "storage" && cards[3].reason == "free_space_short");
    EF_CHECK(cards[3].args.at("need") == "27" && cards[3].args.at("free") == "25" && cards[3].args.at("volume") == "C:\\");
    EF_CHECK(i18n::id_of("bench.reason.free_space_short").has_value());
    // Pay kurali da ayni ayrintiyi alir.
    si.reason = "free_share_limit";
    cards = component_cards({}, {{"storage", si}});
    EF_CHECK(cards[3].reason == "free_space_short");
    // Sayi yok / bozuk / celiskili (gereken <= mevcut): genel neden kalir, sayi uydurulmaz.
    SubtestInfo plain{"skipped", "free_space_floor"};
    cards = component_cards({}, {{"storage", plain}});
    EF_CHECK(cards[3].reason == "free_space_floor" && !cards[3].args.count("need"));
    SubtestInfo bad = si;
    bad.args["need_free_bytes"] = "12x";
    EF_CHECK(component_cards({}, {{"storage", bad}})[3].reason == "free_share_limit");
    SubtestInfo odd = si;
    odd.args["need_free_bytes"] = odd.args["avail_free_bytes"];
    EF_CHECK(component_cards({}, {{"storage", odd}})[3].reason == "free_share_limit");
    // Baska atlama nedeni: ayrinti eklenmez.
    SubtestInfo other = si;
    other.reason = "write_budget_exhausted";
    EF_CHECK(component_cards({}, {{"storage", other}})[3].reason == "write_budget_exhausted");
}
