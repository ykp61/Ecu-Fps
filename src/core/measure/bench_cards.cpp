#include "core/measure/bench_cards.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "core/measure/verify_state.h"

namespace ef::core::measure {

std::string_view to_string(TestMode m) noexcept { return m == TestMode::Full ? "full" : "quick"; }

std::vector<std::pair<std::string, std::string>> storage_free_space_args(bool skipped, std::string_view reason, std::uint64_t volume_total_bytes,
                                                                         std::uint64_t required_free_bytes, std::uint64_t volume_free_bytes) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!skipped || volume_total_bytes == 0 || (reason != "free_space_floor" && reason != "free_share_limit")) return out;
    out.emplace_back(kStorageNeedFreeBytesArg, std::to_string(required_free_bytes));
    out.emplace_back(kStorageAvailFreeBytesArg, std::to_string(volume_free_bytes));
    return out;
}

namespace {
std::optional<double> get(const std::map<std::string, double>& m, const std::string& k) {
    const auto it = m.find(k);
    if (it == m.end()) return std::nullopt;
    return it->second;
}
ComponentCard card(std::string id, std::string title, const std::map<std::string, SubtestInfo>& st, const std::string& subtest) {
    ComponentCard c;
    c.id = std::move(id);
    c.title_key = std::move(title);
    if (const auto it = st.find(subtest); it != st.end()) c.status = it->second.status, c.reason = it->second.reason;
    return c;
}
void add(ComponentCard& c, const std::map<std::string, double>& m, const std::string& key, std::string label, std::string unit, int dec,
         std::string note = {}) {
    c.metrics.push_back({std::move(label), get(m, key), std::move(unit), dec, std::move(note)});
}
// Ondalik byte metni -> sayi (yalniz rakam; bos/tasma/yabanci karakter -> yok).
std::optional<std::uint64_t> parse_bytes(const std::map<std::string, std::string>& a, const char* key) {
    const auto it = a.find(key);
    if (it == a.end() || it->second.empty() || it->second.size() > 20) return std::nullopt;
    std::uint64_t v = 0;
    for (const char ch : it->second) {
        if (ch < '0' || ch > '9') return std::nullopt;
        const auto d = static_cast<std::uint64_t>(ch - '0');
        if (v > (UINT64_MAX - d) / 10) return std::nullopt;
        v = v * 10 + d;
    }
    return v;
}
// Bosluk 7: bos alan atlamasinda gereken / mevcut alan GB (2^30) olarak; gereken yukari, mevcut asagi yuvarlanir
// (gosterilen fark gercek farktan buyuk ya da esit; "yeterli gorunuyor ama atlandi" celiskisi olmaz).
void storage_free_space_detail(ComponentCard& c, const std::map<std::string, SubtestInfo>& st) {
    if (c.status != "skipped" || (c.reason != "free_space_floor" && c.reason != "free_share_limit")) return;
    const auto it = st.find("storage");
    if (it == st.end()) return;
    const auto need = parse_bytes(it->second.args, kStorageNeedFreeBytesArg);
    const auto avail = parse_bytes(it->second.args, kStorageAvailFreeBytesArg);
    if (!need || !avail || *need <= *avail) return;  // tutarsiz veri: genel metin kalir
    constexpr std::uint64_t kGiB = 1ull << 30;
    c.reason = "free_space_short";
    c.args["need"] = std::to_string(*need / kGiB + (*need % kGiB != 0 ? 1 : 0));
    c.args["free"] = std::to_string(*avail / kGiB);
}
std::string count_text(double v) {
    if (!std::isfinite(v) || v < 0) return "0";
    return std::to_string(static_cast<unsigned long long>(std::llround(v)));
}

// Dogrulama satiri: durum kodu -> metin + ton; tamamlanmadi/desteklenmiyor icin neden notu.
// reasons[kod]: kod basina katalog anahtari (bos = not yok), kod 1..11.
CardMetric verify_row(std::string label, std::optional<double> state, bool compute, const char* const* reasons) {
    CardMetric x;
    x.label_key = std::move(label);
    const auto code = verify_code(state);
    switch (verify_view(code)) {
        case VerifyView::Passed: x.value_key = "bench.verify.passed", x.value_tone = "success"; break;
        case VerifyView::Mismatch:
            x.value_key = compute ? "bench.verify.mismatch_compute" : "bench.verify.mismatch_data", x.value_tone = "error";
            break;
        case VerifyView::Unsupported: x.value_key = "bench.verify.unsupported"; break;
        case VerifyView::Incomplete: x.value_key = "bench.verify.incomplete", x.value_tone = "warning"; break;
        case VerifyView::NotRun: x.value_key = "bench.verify.not_run"; break;
    }
    if (code) {
        const int i = static_cast<int>(*code);
        if (reasons[i] && *reasons[i]) x.note_key = reasons[i];
    }
    return x;
}
}  // namespace

std::vector<ComponentCard> component_cards(const std::map<std::string, double>& m, const std::map<std::string, SubtestInfo>& st,
                                           const std::map<std::string, std::string>& args) {
    std::vector<ComponentCard> out;
    // B3 inceleme (bosluk 25): sahne olcum penceresinde pencere simge durumundaysa sahne degerleri (FPS ve sahne
    // penceresi kullanimi) isaretlenir; deger silinmez ama guvenilir sayilmaz (gecmiste "scene." dislamasi).
    const bool scene_minimized = get(m, "scene_window_minimized").value_or(0) > 0.5;
    const auto mark_minimized = [scene_minimized](ComponentCard& c, std::size_t from) {
        if (!scene_minimized) return;
        for (std::size_t i = from; i < c.metrics.size(); ++i) c.metrics[i].note_key = "bench.note.window_minimized";
    };
    {
        auto c = card("gpu", "bench.card.gpu", st, "scene");
        add(c, m, "scene_avg_fps", "bench.metric.avg_fps", "unit.fps", 1);
        add(c, m, "scene_low1_fps", "bench.metric.low1_fps", "unit.fps", 1);
        add(c, m, "scene_p99_ms", "bench.metric.p99_ms", "unit.ms", 2);
        // DIAG-11: sahne penceresi GPU kullanimi. Isaret varsa satir gorunur (deger yoksa "Olculemedi").
        if (get(m, "scene_util_sampled").value_or(0) > 0.5)
            add(c, m, "scene_gpu_pct", "bench.metric.gpu_util_scene", "unit.percent", 0, "bench.note.scene_window");
        mark_minimized(c, 0);
        for (const auto* k : {"tier", "res"})
            if (const auto it = args.find(k); it != args.end()) c.args[k] = it->second;
        out.push_back(std::move(c));
    }
    {
        auto c = card("cpu", "bench.card.cpu", st, "cpu");
        add(c, m, "cpu_st_units_per_s", "bench.metric.cpu_st", "unit.units_s", 0);
        add(c, m, "cpu_mt_units_per_s", "bench.metric.cpu_mt", "unit.units_s", 0);
        // DIAG-11: sahne penceresi CPU kullanimi (toplam + en yuklu cekirdek). Ornekleyici acilamadiysa
        // satirlar "Olculemedi" der; 0 yazilmaz.
        if (get(m, "scene_util_sampled").value_or(0) > 0.5) {
            add(c, m, "scene_cpu_pct", "bench.metric.cpu_util_scene", "unit.percent", 0, "bench.note.scene_window");
            add(c, m, "scene_cpu_max_core_pct", "bench.metric.cpu_max_core_scene", "unit.percent", 0, "bench.note.scene_window");
        }
        // Iki bilgi ayri ayri tasinir ve satir basina tek not alaninda BIRLESIK anahtarla gosterilir (biri digerini
        // silmez):
        //  - Rakip yuk olculduyse (isci CPU payi < %90) cekisme; pay bilinmiyorsa iddia yok.
        //  - RAM butcesi is yukunu degistirdiyse (bigdata isci/eleman sayisi) skor varsayilan kosuyla
        //    karsilastirilamaz. Varyant anahtarlari yoksa (eski kayit) birlesik isaret yalniz MT'ye uygulanir.
        //  - Butce OLCULEMEDIYSE (cpu_bigdata_budget_known = 0) MT kisitlamasi guvenli taraf onlemidir, olculmus bir
        //    bellek yetersizligi degildir: ayri "bellek durumu okunamadi" notu (veri yetersiz). Butce bilinmezken
        //    eleman sayisi kucultulmez, yani ST'nin kisitlamasi her zaman olculmus butceden gelir. Anahtar yoksa
        //    (eski kayit) ayrim yapilamaz ve onceki not korunur.
        const auto st_lim = get(m, "cpu_st_memory_limited");
        const auto mt_lim = get(m, "cpu_mt_memory_limited");
        const bool legacy = !st_lim && !mt_lim && get(m, "cpu_bigdata_memory_limited").value_or(0) > 0.5;
        const bool budget_unknown = get(m, "cpu_bigdata_budget_known").value_or(1) < 0.5;
        const auto note = [](bool limited, bool unknown, bool contended) -> std::string {
            if (limited) {
                if (unknown) return contended ? "bench.note.memory_unknown_contended" : "bench.note.memory_unknown";
                return contended ? "bench.note.memory_limited_contended" : "bench.note.memory_limited";
            }
            return contended ? "bench.note.contended" : "";
        };
        c.metrics[0].note_key = note(st_lim.value_or(0) > 0.5, false, get(m, "cpu_st_contended").value_or(0) > 0.5);
        c.metrics[1].note_key =
            note(mt_lim.value_or(0) > 0.5 || legacy, budget_unknown, get(m, "cpu_mt_contended").value_or(0) > 0.5);
        mark_minimized(c, 2);  // yalniz sahne penceresi satirlari (ST/MT skoru sahne disinda olculur)
        out.push_back(std::move(c));
    }
    {
        auto c = card("memory", "bench.card.memory", st, "memory");
        add(c, m, "mem_mt_read_gbps", "bench.metric.mem_read", "unit.gbps", 1);
        add(c, m, "mem_mt_write_gbps", "bench.metric.mem_write", "unit.gbps", 1);
        add(c, m, "mem_mt_copy_gbps", "bench.metric.mem_copy", "unit.gbps", 1);
        add(c, m, "mem_latency_ns", "bench.metric.mem_latency", "unit.ns", 1);
        // Tampon L3'un 8 katindan kucuk olculduyse "onbellege gore kucuk" notu dusulur. L3 bilinmiyorsa (oran yok)
        // ne DRAM ne de onbellek iddiasi yapilir: veri yetersiz, not yok. Acik isaret mem_large_buffer_dram + oran;
        // eski kayitlarda yalniz MT orani.
        const auto dram = get(m, "mem_large_buffer_dram");
        const auto big_ratio = get(m, "mem_large_buffer_l3_ratio");
        const auto ratio = get(m, "mem_mt_buffer_l3_ratio");
        const bool large = (dram && big_ratio && *dram < 0.5) || (!dram && ratio && *ratio < 8.0);
        // BM-03: bellek MT cekismesi olculduyse kartta nedeniyle birlikte gosterilir (tek satir notu; M09).
        const bool mem_contended = get(m, "mem_mt_contention_known").value_or(0) > 0.5 &&
                                   get(m, "mem_mt_contended").value_or(0) > 0.5;
        if (large || mem_contended) {
            const char* note = large ? (mem_contended ? "bench.note.large_buffer_contended" : "bench.note.large_buffer")
                                     : "bench.note.contended";
            for (auto& x : c.metrics)
                if (x.value) x.note_key = note;  // olculmeyen satira not dusulmez
        }
        out.push_back(std::move(c));
    }
    {
        auto c = card("storage", "bench.card.storage", st, "storage");
        const bool wburst = get(m, "storage_seq_1m_qd8_write_burst").value_or(0) > 0.5;
        add(c, m, "storage_seq_1m_qd8_read_mbps", "bench.metric.seq_read", "unit.mbps", 0);
        add(c, m, "storage_seq_1m_qd8_write_mbps", "bench.metric.seq_write", "unit.mbps", 0, wburst ? "bench.note.burst" : "");
        add(c, m, "storage_rnd_4k_qd1_read_iops", "bench.metric.rnd_read_qd1", "unit.iops", 0);
        add(c, m, "storage_rnd_4k_qd32_read_iops", "bench.metric.rnd_read_qd32", "unit.iops", 0);
        add(c, m, "storage_rnd_4k_qd1_read_p99_us", "bench.metric.rnd_read_p99", "unit.us", 0);
        for (const auto* k : {"volume", "media"})
            if (const auto it = args.find(k); it != args.end()) c.args[k] = it->second;
        storage_free_space_detail(c, st);
        out.push_back(std::move(c));
    }
    // M10: birlesik yuk (sahne + ayni anda disk okumasi) kosulduysa ayri kart; eski kayitta (alt test/metrik yok) kart yok.
    if (st.count("combined") || get(m, "combined_avg_fps")) {
        auto c = card("combined", "bench.card.combined", st, "combined");
        add(c, m, "combined_avg_fps", "bench.metric.avg_fps", "unit.fps", 1);
        add(c, m, "combined_low1_fps", "bench.metric.low1_fps", "unit.fps", 1);
        add(c, m, "combined_p99_ms", "bench.metric.p99_ms", "unit.ms", 2);
        out.push_back(std::move(c));
    }
    // EF-X22: dogrulama karti (gercek durum kodlari + sayaclar). Eski kayitta (hicbir dogrulama anahtari yok) kart yok.
    {
        const auto gs = get(m, std::string(kGpuVerifyStateMetric));
        const auto cs = get(m, std::string(kCpuVerifyStateMetric));
        const auto ss = get(m, std::string(kStorageVerifyStateMetric));
        const bool any = gs || cs || ss || get(m, "scene_gpu_verify_repeats") || get(m, "cpu_verify_blocks") ||
                         get(m, "storage_verify_blocks");
        if (any) {
            ComponentCard c;
            c.id = "verify";
            c.title_key = "bench.card.verify";
            // Kod -> neden anahtari (indeks 0 kullanilmaz). Gecti/uyusmazlik satirinda not gercek sayaclardir.
            static const char* const gpu_r[12] = {"", "", "", "bench.reason.gpu_verify_unsupported", "bench.reason.gpu_verify_setup_failed",
                                                  "bench.reason.device_removed", "bench.reason.gpu_timeout", "bench.reason.gpu_verify_readback",
                                                  "bench.reason.gpu_verify_incomplete", "", "bench.reason.gpu_verify_invalid", ""};
            static const char* const cpu_r[12] = {"", "", "", "", "", "", "", "", "bench.reason.cpu_verify_incomplete", "",
                                                  "bench.reason.cpu_verify_invalid", ""};
            static const char* const sto_r[12] = {"", "", "", "", "bench.reason.storage_verify_setup_failed", "", "", "",
                                                  "bench.reason.storage_verify_incomplete", "", "bench.reason.storage_verify_invalid",
                                                  "bench.reason.storage_verify_io_error"};
            {
                auto x = verify_row("bench.verify.gpu", gs, true, gpu_r);
                const auto rep = get(m, "scene_gpu_verify_repeats");
                const auto bad = get(m, "scene_gpu_verify_mismatch_words");
                if (rep && bad && x.note_key.empty()) {
                    x.note_key = "bench.verify.note.gpu";
                    x.note_args = {{"repeats", count_text(*rep)}, {"bad", count_text(*bad)}};
                }
                c.metrics.push_back(std::move(x));
            }
            {
                auto x = verify_row("bench.verify.cpu", cs, true, cpu_r);
                const auto blocks = get(m, "cpu_verify_blocks");
                const auto bad = get(m, "cpu_verify_mismatch_blocks");
                if (blocks && bad && x.note_key.empty()) {
                    // Komut seti etiketleri: dogrulanan SIMD varyantlari ve desteklenmeyenler ayri listelenir.
                    std::string simd, unsup;
                    for (const auto& [k, name] : std::initializer_list<std::pair<const char*, const char*>>{
                             {"sse2", "SSE2"}, {"avx2", "AVX2"}, {"avx512", "AVX-512"}}) {
                        const auto sup = get(m, std::string("cpu_verify_") + k + "_supported");
                        if (!sup) continue;
                        auto& dst = *sup > 0.5 ? simd : unsup;
                        dst += (dst.empty() ? "" : ", ") + std::string(name);
                    }
                    x.note_key = simd.empty() ? "bench.verify.note.cpu_scalar"
                                 : unsup.empty() ? "bench.verify.note.cpu"
                                                 : "bench.verify.note.cpu_unsupported";
                    x.note_args = {{"blocks", count_text(*blocks)}, {"bad", count_text(*bad)}};
                    if (!simd.empty()) x.note_args["isas"] = simd;
                    if (!unsup.empty()) x.note_args["unsupported"] = unsup;
                }
                c.metrics.push_back(std::move(x));
            }
            {
                auto x = verify_row("bench.verify.storage", ss, false, sto_r);
                const auto blocks = get(m, "storage_verify_blocks");
                const auto bad = get(m, "storage_verify_mismatch_blocks");
                if (blocks && bad && x.note_key.empty()) {
                    x.note_key = "bench.verify.note.storage";
                    x.note_args = {{"blocks", count_text(*blocks)}, {"bad", count_text(*bad)}};
                }
                c.metrics.push_back(std::move(x));
            }
            out.push_back(std::move(c));
        }
    }
    // M09: ayni kartta ayni not bir kez gosterilir: son gectigi satirin altinda (kartin ortak notu gibi okunur);
    // onceki satirlardaki tekrarlar silinir.
    for (auto& c : out)
        for (std::size_t i = 0; i < c.metrics.size() && c.id != "verify"; ++i)
            for (std::size_t j = i + 1; j < c.metrics.size() && !c.metrics[i].note_key.empty(); ++j)
                if (c.metrics[j].note_key == c.metrics[i].note_key) c.metrics[i].note_key.clear();
    return out;
}

}  // namespace ef::core::measure
