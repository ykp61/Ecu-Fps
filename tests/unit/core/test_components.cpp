// DIAG-07: sentetik ve bu PC'ye benzer envanterle bilesen raporu.
#include <optional>
#include <string>

#include "core/diagnosis/components.h"
#include "core/diagnosis/mismatch.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::inventory;

namespace {
const ComponentFinding& get(const std::vector<ComponentFinding>& v, Component c) {
    for (const auto& f : v)
        if (f.component == c) return f;
    return v.front();
}
}  // namespace

EF_TEST(components_no_data_abstains) {
    const auto r = component_report({});
    EF_CHECK(r.size() == 5);
    for (const auto& f : r) EF_CHECK(f.verdict == Verdict::Abstain && !f.abstain_reason.empty());  // veri yok -> "sorun yok" denmez
}

EF_TEST(components_this_pc_like_inventory) {
    // Bu PC: 2x32 GiB DDR5-6000 anma hizinda, RTX 5080 x16 Gen5, oyunlar SSD'de, bol bos alan.
    ComponentInput in;
    in.inventory.ram = {{6000, 6000, "F5-6000J3036F32G", "DDR5"}, {6000, 6000, "F5-6000J3036F32G", "DDR5"}};
    in.inventory.dgpu_pcie = PcieInput{16, 16, 5, 5};
    in.health = evaluate_health(in.inventory);
    in.ram_total_gib = 64;
    in.games = {{"Counter-Strike 2", false, 70.0}};
    in.inventory.volumes = {{"E:\\", 1863, 1300}};
    const auto r = component_report(in);
    EF_CHECK(get(r, Component::Cpu).verdict == Verdict::Abstain);  // calisma verisi yok
    EF_CHECK(get(r, Component::Motherboard).verdict == Verdict::NoIssueFound);
    EF_CHECK(get(r, Component::Ram).verdict == Verdict::NoIssueFound);
    EF_CHECK(get(r, Component::Storage).verdict == Verdict::NoIssueFound);
}

EF_TEST(components_synthetic_limits_with_evidence) {
    ComponentInput in;
    in.inventory.ram = {{6000, 4800, "F5-6000J3038F16G", "DDR5"}};  // tek modul + XMP kapali
    in.inventory.dgpu_pcie = PcieInput{8, 16, 4, 4};
    in.health = evaluate_health(in.inventory);
    in.runtime = RuntimeClass::GpuBound;
    in.diagnosis_release_ready = true;  // BM-02: kapi acik olmadan calisma ani hukmu yayinlanmaz
    in.ram_total_gib = 16;
    in.game_recommended_ram_gib = 32;
    in.games = {{"Big Game", true, 4.0}};
    auto r = component_report(in);
    EF_CHECK(get(r, Component::Gpu).verdict == Verdict::Limiting && get(r, Component::Cpu).verdict == Verdict::NoIssueFound);
    EF_CHECK(get(r, Component::Ram).verdict == Verdict::Limiting && get(r, Component::Ram).fixable_by_setting);
    EF_CHECK(get(r, Component::Ram).evidence.size() == 2);
    EF_CHECK(get(r, Component::Storage).evidence.size() >= 2);
    // PCIe dusumu bostayken okunduysa darbogaz denmez; tek modul kanal olcumu olmadan darbogaz kaniti degil.
    const auto& mb = get(r, Component::Motherboard);
    EF_CHECK(mb.verdict != Verdict::Limiting && mb.evidence.size() == 1 && mb.abstain_reason == "memory_channel_not_measured");
    in.pcie_measured_under_load = true;
    r = component_report(in);
    EF_CHECK(get(r, Component::Motherboard).verdict == Verdict::Limiting && get(r, Component::Motherboard).evidence.size() == 2);
    for (const auto& f : r)
        if (f.verdict != Verdict::Abstain) EF_CHECK(!f.evidence.empty());  // kanitsiz kesin ifade yok
}

EF_TEST(components_benchmark_scene_runtime_class) {
    // DIAG-11: Uygulama Testi sahnesi (bilinen yuk) -> calisma ani sinifi. Kapi acik varsayilir.
    ReleaseStatus open;
    open.ready = true;
    open.reason_key = "diag.gate.ready";
    const auto run = [&](std::optional<double> avg, std::optional<double> p99, std::optional<double> gpu,
                         std::optional<double> cpu) {
        BenchmarkSceneEvidence e;
        e.avg_fps = avg;
        e.p99_ms = p99;
        e.gpu_util_pct = gpu;
        e.cpu_max_core_pct = cpu;
        e.sample_interval_ms = 50.0;  // ~20 Hz sahne penceresi
        return runtime_class_from_benchmark_scene(e, open);
    };
    EF_CHECK(run(120.0, 9.0, 97.0, 45.0).cls == RuntimeClass::GpuBound);
    EF_CHECK(run(100.0, 12.0, 45.0, 98.0).cls == RuntimeClass::CpuOrEngineBound);
    EF_CHECK(run(100.0, 12.0, 97.0, 98.0).cls == RuntimeClass::Mixed);
    // GPU ne yuksek ne dusuk (70-90 arasi), CPU doygun degil: sinirlayici isaret yok -> Healthy.
    EF_CHECK(run(60.0, 16.0, 80.0, 30.0).cls == RuntimeClass::Healthy);
    // Dusuk GPU tek basina CPU hukmu degildir (CPU doygun degilse Mixed).
    EF_CHECK(run(60.0, 16.0, 40.0, 30.0).cls == RuntimeClass::Mixed);
    EF_CHECK(run(120.0, 9.0, std::nullopt, std::nullopt).cls == RuntimeClass::Insufficient);  // eksik kanit -> cekimser
    EF_CHECK(run(std::nullopt, std::nullopt, 97.0, 45.0).cls == RuntimeClass::Insufficient);   // kare skoru yok
    // MEAS-03: aralik 250 ms'den buyukse kullanim degerleri kanit sayilmaz (1 Hz ornekleme gibi).
    {
        BenchmarkSceneEvidence slow;
        slow.avg_fps = 120.0;
        slow.gpu_util_pct = 97.0;
        slow.cpu_max_core_pct = 45.0;
        slow.sample_interval_ms = 1000.0;
        const auto r = runtime_class_from_benchmark_scene(slow, open);
        EF_CHECK(r.cls == RuntimeClass::Insufficient);
        bool code = false;
        for (const auto& c : r.counter) code = code || c == "sampling_resolution_insufficient";
        EF_CHECK(code);
        slow.sample_interval_ms = std::nullopt;  // aralik bilinmiyor: kanit sayilmaz
        EF_CHECK(runtime_class_from_benchmark_scene(slow, open).cls == RuntimeClass::Insufficient);
    }
    // Yayin kapisi kapali: sinif uretilmez, "sorun yok" da denmez.
    ReleaseStatus closed;
    BenchmarkSceneEvidence e;
    e.avg_fps = 120.0;
    e.gpu_util_pct = 97.0;
    e.cpu_max_core_pct = 45.0;
    e.sample_interval_ms = 50.0;
    const auto g = runtime_class_from_benchmark_scene(e, closed);
    EF_CHECK(g.cls == RuntimeClass::Insufficient);
    bool gate_code = false;
    for (const auto& c : g.counter) gate_code = gate_code || c == "diagnosis_calibration_unverified";
    EF_CHECK(gate_code);

    // DIAG-10: GPU zaman damgasi (gpu_busy_ms) kare olcekli kanittir; PDH coznurluk kapisina tabi degil.
    {
        BenchmarkSceneEvidence busy;
        busy.avg_fps = 100.0;               // kare 10 ms
        busy.gpu_busy_ms = 9.8;             // kendi komut araligimiz kareyi dolduruyor (ust sinir)
        const auto r = runtime_class_from_benchmark_scene(busy, open);
        EF_CHECK(r.cls == RuntimeClass::GpuBound);
        bool near = false;
        for (const auto& c : r.supporting) near = near || c == "gpu_busy_near_frame_time";
        EF_CHECK(near);
        // Kisa GPU araligi + doygun cekirdek: CPU/motor darboğazı.
        BenchmarkSceneEvidence short_busy;
        short_busy.avg_fps = 100.0;
        short_busy.gpu_busy_ms = 2.0;
        short_busy.cpu_max_core_pct = 96.0;
        short_busy.sample_interval_ms = 50.0;
        EF_CHECK(runtime_class_from_benchmark_scene(short_busy, open).cls == RuntimeClass::CpuOrEngineBound);
        // 0 = olcum yok: kanit sayilmaz, "sorun yok" da denmez.
        BenchmarkSceneEvidence zero_busy;
        zero_busy.avg_fps = 100.0;
        zero_busy.gpu_busy_ms = 0.0;
        EF_CHECK(runtime_class_from_benchmark_scene(zero_busy, open).cls == RuntimeClass::Insufficient);
    }
}

EF_TEST(components_benchmark_evidence_from_metrics) {
    const auto ev = benchmark_scene_evidence_from_metrics({{"scene_avg_fps", 93.0}, {"scene_p99_ms", 12.5},
                                                           {"scene_gpu_pct", 88.0}, {"scene_cpu_max_core_pct", 71.0},
                                                           {"scene_sample_interval_ms", 50.0},
                                                           {"scene_gpu_busy_ms", 9.4}});
    EF_CHECK(ev.has_value() && ev->avg_fps == 93.0 && ev->p99_ms == 12.5 && ev->gpu_util_pct == 88.0 &&
              ev->cpu_max_core_pct == 71.0 && ev->sample_interval_ms == 50.0 && ev->gpu_busy_ms == 9.4);
    // gpu_busy_ms 0/negatif = olcum yok; kanit sayilmaz.
    const auto busy_zero = benchmark_scene_evidence_from_metrics({{"scene_avg_fps", 60.0}, {"scene_gpu_busy_ms", 0.0}});
    EF_CHECK(busy_zero.has_value() && !busy_zero->gpu_busy_ms);
    // Sahne skoru yok (eski kayit / sahne kosmadi): kanit yok, deger uydurulmaz.
    EF_CHECK(!benchmark_scene_evidence_from_metrics({{"cpu_st_units_per_s", 800.0}}).has_value());
    EF_CHECK(!benchmark_scene_evidence_from_metrics({{"scene_avg_fps", 0.0}}).has_value());
    // Yalniz sahne skoru varsa kanit kismidir; kullanim degerleri yoksa siniflandirici cekimser kalir.
    const auto partial = benchmark_scene_evidence_from_metrics({{"scene_avg_fps", 60.0}});
    EF_CHECK(partial.has_value() && !partial->gpu_util_pct && !partial->cpu_max_core_pct);
}

EF_TEST(components_closed_release_gate_abstains_on_runtime) {
    // BM-02: calisma verisi var ama yayin kapisi kapali: CPU/GPU "sorun yok" demez, neden ayri.
    ComponentInput in;
    in.runtime = RuntimeClass::GpuBound;
    in.diagnosis_release_ready = false;
    for (const auto& f : component_report(in))
        if (f.component == Component::Cpu || f.component == Component::Gpu) {
            EF_CHECK(f.verdict == Verdict::Abstain && f.abstain_reason == "diagnosis_calibration_unverified");
            EF_CHECK(f.evidence.empty());
        }
    // Kapi acikken ayni girdi normal hukme donusur.
    in.diagnosis_release_ready = true;
    EF_CHECK(get(component_report(in), Component::Gpu).verdict == Verdict::Limiting);
    EF_CHECK(get(component_report(in), Component::Cpu).verdict == Verdict::NoIssueFound);
}

// Spec 2026-11-12 C2 (D6): denge testinin S3 adiminda (GPU yuk altinda) okunan link evaluate_pcie kuralina verilir.
EF_TEST(components_pcie_link_read_under_load_drives_motherboard_row) {
    const auto sample = [](std::uint32_t w, std::uint32_t wmax, std::uint32_t g, std::uint32_t gmax, std::optional<double> load) {
        PcieSample s;
        s.cur_width = w, s.card_max_width = wmax, s.slot_max_width = wmax;
        s.cur_gen = g, s.card_max_gen = gmax, s.slot_max_gen = gmax;
        s.gpu_load_pct = load;
        return s;
    };
    const auto has_ev = [](const ComponentFinding& f, const std::string& e) {
        for (const auto& x : f.evidence)
            if (x == e) return true;
        return false;
    };
    ComponentInput in;
    in.inventory.ram = {{6000, 6000, "F5-6000J3036F32G", "DDR5"}, {6000, 6000, "F5-6000J3036F32G", "DDR5"}};
    in.inventory.dgpu_pcie = PcieInput{16, 16, 5, 5};  // bostaki envanter okumasi: azamide
    in.health = evaluate_health(in.inventory);
    // Yuk altinda x8 / x16: sinirlama kaniti (envanter bostayken azamide gorunse bile); olculen link metni kanitta.
    in.pcie_load = sample(8, 16, 4, 4, 97.0);
    in.pcie_load_text = "x8 / x16, Gen 4 / Gen 4";
    in.pcie_measured_under_load = true;
    auto mb = get(component_report(in), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::Limiting && has_ev(mb, "pcie_load:x8 / x16, Gen 4 / Gen 4"));
    // Metin verilmediyse yalniz anahtar (uydurma deger yok).
    in.pcie_load_text.clear();
    mb = get(component_report(in), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::Limiting && has_ev(mb, "pcie_load"));
    // Yuk altinda azamide: bostaki dusuk okuma (guc tasarrufu) sinirlama sayilmaz.
    in.inventory.dgpu_pcie = PcieInput{16, 16, 1, 5};
    in.health = evaluate_health(in.inventory);
    in.pcie_load = sample(16, 16, 5, 5, 97.0);
    mb = get(component_report(in), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::NoIssueFound && has_ev(mb, "pcie_and_channels_ok") && has_ev(mb, "pcie_load_ok"));
    // Envanter okunamadi ama yuk altinda azamide okundu: "bilinmiyor" degil.
    in.inventory.dgpu_pcie.reset();
    in.health = evaluate_health(in.inventory);
    mb = get(component_report(in), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::NoIssueFound && has_ev(mb, "pcie_load_ok"));
    // Ornek bostayken (yuk < %60) ya da yuk bilinmeden alindi: yuk alti olcum yerine GECMEZ.
    for (const auto& load : {std::optional<double>(30.0), std::optional<double>()}) {
        ComponentInput idle;
        idle.inventory.ram = in.inventory.ram;
        idle.inventory.dgpu_pcie = PcieInput{8, 16, 4, 4};
        idle.health = evaluate_health(idle.inventory);
        idle.pcie_load = sample(8, 16, 4, 4, load);
        mb = get(component_report(idle), Component::Motherboard);
        EF_CHECK(mb.verdict == Verdict::Abstain && mb.abstain_reason == "pcie_not_measured_under_load");
    }
    // Yuva azamisi x8 (kart x16): yuva siniri egitim dususu degildir -> sinirlama denmez.
    ComponentInput slot;
    slot.inventory.ram = in.inventory.ram;
    slot.inventory.dgpu_pcie = PcieInput{8, 16, 4, 4};
    slot.health = evaluate_health(slot.inventory);
    PcieSample s8 = sample(8, 16, 4, 4, 97.0);
    s8.slot_max_width = 8;
    slot.pcie_load = s8;
    mb = get(component_report(slot), Component::Motherboard);
    EF_CHECK(mb.verdict != Verdict::Limiting);
    // Yuva azamisi OKUNAMADI (gercek cihaz: AMD kok portu), Gen 5 kart Gen 4'te calisiyor: kart azamisi yuva yerine
    // yazilmaz -> anakart "sinirliyor" DENMEZ (olagan eslesme); bostaki envanter hukmu aynen kalir.
    ComponentInput gen;
    gen.inventory.ram = in.inventory.ram;
    gen.inventory.dgpu_pcie = PcieInput{16, 16, 4, 5};
    gen.health = evaluate_health(gen.inventory);
    PcieLoadReading gr;
    gr.cur_width = 16, gr.max_width = 16, gr.cur_gen = 4, gr.max_gen = 5, gr.gpu_load_pct = 97.0;
    gen.pcie_load = pcie_sample_from(gr);
    gen.pcie_load_text = pcie_link_text(gr);
    gen.pcie_measured_under_load = evaluate_pcie_under_load(gr).measured_under_load;
    mb = get(component_report(gen), Component::Motherboard);
    EF_CHECK(gen.pcie_measured_under_load && mb.verdict == Verdict::NoIssueFound && has_ev(mb, "pcie_and_channels_ok") && !has_ev(mb, "pcie_load_ok"));
    for (const auto& e : mb.evidence) EF_CHECK(e.rfind("pcie_load:", 0) != 0);
    // Ayni okuma, yuva Gen 5 OKUNDU: kural "dusuk link" der -> sinirlama kaniti + olculen link metni.
    gr.slot_max_width = 16, gr.slot_max_gen = 5;
    gen.pcie_load = pcie_sample_from(gr);
    mb = get(component_report(gen), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::Limiting && has_ev(mb, "pcie_load:x16 / x16, Gen 4 / Gen 5"));
    // Yuva okunamadi, link kart azamisinde: yuk altinda azamide (yuva en az o kadarini destekliyor).
    gr = PcieLoadReading{};
    gr.cur_width = 16, gr.max_width = 16, gr.cur_gen = 5, gr.max_gen = 5, gr.gpu_load_pct = 99.0;
    gen.inventory.dgpu_pcie = PcieInput{16, 16, 1, 5};  // bostayken dusuk hiz (guc tasarrufu)
    gen.health = evaluate_health(gen.inventory);
    gen.pcie_load = pcie_sample_from(gr);
    mb = get(component_report(gen), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::NoIssueFound && has_ev(mb, "pcie_load_ok"));
    // Yuva okunamadi, genislik x8 / x16 yuk altinda: kural hukum vermez; mevcut genislik kurali (genislik dusumu + yuk
    // alti olcum) gecerlidir. Kanit OLCULEN link metnidir (dilden bagimsiz; envanter bulgusunun neden iceren metni degil).
    gr = PcieLoadReading{};
    gr.cur_width = 8, gr.max_width = 16, gr.cur_gen = 4, gr.max_gen = 4, gr.gpu_load_pct = 97.0;
    ComponentInput width;
    width.inventory.ram = in.inventory.ram;
    width.inventory.dgpu_pcie = PcieInput{8, 16, 4, 4};
    width.health = evaluate_health(width.inventory);
    width.pcie_load = pcie_sample_from(gr);
    width.pcie_load_text = pcie_link_text(gr);
    width.pcie_measured_under_load = evaluate_pcie_under_load(gr).measured_under_load;
    mb = get(component_report(width), Component::Motherboard);
    EF_CHECK(mb.verdict == Verdict::Limiting && has_ev(mb, "pcie_load:x8 / x16, Gen 4 / Gen 4"));
    for (const auto& e : mb.evidence) EF_CHECK(e.rfind("pcie:", 0) != 0);
    // Ayni okuma bostayken (yuk < %60): yuk alti olcum degildir -> hukum yok.
    gr.gpu_load_pct = 59.9;
    width.pcie_load = pcie_sample_from(gr);
    width.pcie_measured_under_load = evaluate_pcie_under_load(gr).measured_under_load;
    mb = get(component_report(width), Component::Motherboard);
    EF_CHECK(!width.pcie_measured_under_load && mb.verdict == Verdict::Abstain && mb.abstain_reason == "pcie_not_measured_under_load");
    // Yuk alti ornek yokken eski davranis degismez.
    ComponentInput none;
    none.inventory.ram = in.inventory.ram;
    none.inventory.dgpu_pcie = PcieInput{8, 16, 4, 4};
    none.health = evaluate_health(none.inventory);
    EF_CHECK(get(component_report(none), Component::Motherboard).abstain_reason == "pcie_not_measured_under_load");
}

// B3 inceleme (bosluk 25): simge durumunda olculen sahne bilesen kaniti degildir.
EF_TEST(components_benchmark_evidence_ignores_minimized_scene) {
    EF_CHECK(benchmark_scene_evidence_from_metrics({{"scene_avg_fps", 93.0}, {"scene_window_minimized", 0.0}}).has_value());
    EF_CHECK(!benchmark_scene_evidence_from_metrics({{"scene_avg_fps", 93.0}, {"scene_gpu_pct", 88.0}, {"scene_window_minimized", 1.0}}).has_value());
}

// B10: kapi kapaliyken CPU/GPU satiri "Olculmedi" demez: yuk altinda olculduyse "olculdu, karar yok" + kalibrasyon
// dogrulanmadi nedeni. Kapi acilmaz, hukum (Limiting / NoIssueFound) uretilmez.
EF_TEST(b10_measured_runtime_without_gate_is_measured_no_verdict) {
    ComponentInput in;
    in.runtime_measured = true;  // sinif yok (kapi kapali)
    auto r = component_report(in);
    for (auto c : {Component::Cpu, Component::Gpu}) {
        const auto& f = get(r, c);
        EF_CHECK(f.verdict == Verdict::Abstain && f.abstain_reason == "diagnosis_calibration_unverified" && f.evidence.empty());
        EF_CHECK(verdict_key(f) == "diag.verdict_measured_no_verdict");
    }
    in.runtime_measured = false;  // olcum yok: eski "Olculmedi" durumu
    r = component_report(in);
    EF_CHECK(get(r, Component::Cpu).abstain_reason == "no_runtime_evidence" && verdict_key(get(r, Component::Gpu)) == "diag.verdict_not_measured");
    // Kapi acik ve sinif varsa hukum eskisi gibi siniftan gelir (measured bayragi hukmu degistirmez).
    in.runtime_measured = true;
    in.runtime = RuntimeClass::GpuBound;
    in.diagnosis_release_ready = true;
    r = component_report(in);
    EF_CHECK(get(r, Component::Gpu).verdict == Verdict::Limiting && get(r, Component::Cpu).verdict == Verdict::NoIssueFound);
    // Kapi kapali + (bir sekilde) sinif verilmis olsa bile hukum yayinlanmaz.
    in.diagnosis_release_ready = false;
    r = component_report(in);
    EF_CHECK(get(r, Component::Gpu).verdict == Verdict::Abstain && get(r, Component::Gpu).abstain_reason == "diagnosis_calibration_unverified");
}

// B10: sahne kanitli EN YENI Uygulama Testi kaydi esastir; sahne kaniti olmayan daha yeni kayit (ya da denge / oyun
// kaydi) atlanir. Kapi kapaliyken sinif hic verilmez.
EF_TEST(b10_benchmark_runtime_uses_newest_builtin_with_scene_metrics) {
    using ef::core::insights::HistoryEntry;
    const RuntimeHardware hw{"CPU A", "GPU A", "1.0"};
    const auto entry = [&hw](const char* src, std::map<std::string, double> m) {
        HistoryEntry e;
        e.source = src;
        e.metrics = std::move(m);
        e.conditions = {{"cpu", hw.cpu}, {"gpu", hw.gpu}, {"gpu_driver", hw.gpu_driver}};
        return e;
    };
    const std::map<std::string, double> gpu_bound{{"scene_avg_fps", 120.0}, {"scene_p99_ms", 9.0}, {"scene_gpu_pct", 97.0},
                                                  {"scene_cpu_max_core_pct", 45.0}, {"scene_sample_interval_ms", 50.0}};
    // En yeni -> en eski: denge kaydi, sahnesi olmayan Uygulama Testi (ornek: sahne basarisiz), simge durumunda olculen
    // Uygulama Testi, sahne kanitli Uygulama Testi.
    auto minimized = gpu_bound;
    minimized["scene_window_minimized"] = 1.0;
    const std::vector<HistoryEntry> h{entry("balance", {{"balance.cpu_fps", 200.0}}), entry("builtin", {{"cpu_st_units_per_s", 5.0}}),
                                      entry("builtin", minimized), entry("in_game", {{"avg_fps", 90.0}}), entry("builtin", gpu_bound)};
    ReleaseStatus closed;
    auto rt = benchmark_runtime_from_history(h, closed, hw);
    EF_CHECK(rt.measured && !rt.cls.has_value());
    ReleaseStatus open;
    open.ready = true;
    open.reason_key = "diag.gate.ready";
    rt = benchmark_runtime_from_history(h, open, hw);
    EF_CHECK(rt.measured && rt.cls == RuntimeClass::GpuBound);
    // Kanit yetersizse (kullanim degeri yok) kapi acikken de sinif verilmez ama olcum vardir.
    const std::vector<HistoryEntry> thin{entry("builtin", {{"scene_avg_fps", 120.0}})};
    rt = benchmark_runtime_from_history(thin, open, hw);
    EF_CHECK(rt.measured && !rt.cls.has_value());
    // Sahne kanitli Uygulama Testi kaydi yok: olculmedi.
    const std::vector<HistoryEntry> none{entry("builtin", {{"cpu_st_units_per_s", 5.0}}), entry("balance", {{"balance.cpu_fps", 200.0}})};
    rt = benchmark_runtime_from_history(none, open, hw);
    EF_CHECK(!rt.measured && !rt.cls.has_value());
    EF_CHECK(!benchmark_runtime_from_history({}, closed, hw).measured);
}

// B10 inceleme: daha eski sahne kanitli kayit yalniz BUGUNKU islemci / ekran karti / surucuyle alindiysa kullanilir.
EF_TEST(b10_benchmark_runtime_ignores_entries_from_other_hardware) {
    using ef::core::insights::HistoryEntry;
    const auto entry = [](std::map<std::string, double> m, std::map<std::string, std::string> c) {
        HistoryEntry e;
        e.source = "builtin";
        e.metrics = std::move(m);
        e.conditions = std::move(c);
        return e;
    };
    const std::map<std::string, double> scene{{"scene_avg_fps", 120.0}, {"scene_p99_ms", 9.0}, {"scene_gpu_pct", 97.0},
                                              {"scene_cpu_max_core_pct", 45.0}, {"scene_sample_interval_ms", 50.0}};
    const RuntimeHardware today{"CPU A", "GPU B", "2.0"};
    ReleaseStatus open;
    open.ready = true;
    open.reason_key = "diag.gate.ready";
    // En yeni Uygulama Testi sahnesiz (bugunku donanim); daha eskisi sahne kanitli ama BASKA ekran kartiyla: olculmedi.
    const std::vector<HistoryEntry> other_gpu{entry({{"cpu_st_units_per_s", 5.0}}, {{"cpu", "CPU A"}, {"gpu", "GPU B"}, {"gpu_driver", "2.0"}}),
                                              entry(scene, {{"cpu", "CPU A"}, {"gpu", "GPU A"}, {"gpu_driver", "2.0"}})};
    for (const auto& gate : {ReleaseStatus{}, open}) {
        const auto rt = benchmark_runtime_from_history(other_gpu, gate, today);
        EF_CHECK(!rt.measured && !rt.cls.has_value());
    }
    // Baska islemci ya da surucu de bayat kanittir; kosulu olmayan (eski) kayit da eslesmez.
    EF_CHECK(!benchmark_runtime_from_history({entry(scene, {{"cpu", "CPU X"}, {"gpu", "GPU B"}, {"gpu_driver", "2.0"}})}, open, today).measured);
    EF_CHECK(!benchmark_runtime_from_history({entry(scene, {{"cpu", "CPU A"}, {"gpu", "GPU B"}, {"gpu_driver", "1.0"}})}, open, today).measured);
    EF_CHECK(!benchmark_runtime_from_history({entry(scene, {})}, open, today).measured);
    // Bugunku donanim henuz bilinmiyorsa (envanter okunmadi) hicbir kayit kullanilmaz.
    const std::vector<HistoryEntry> same{entry(scene, {{"cpu", "CPU A"}, {"gpu", "GPU B"}, {"gpu_driver", "2.0"}})};
    EF_CHECK(!benchmark_runtime_from_history(same, open, RuntimeHardware{}).measured);
    // Ayni donanim: kullanilir. Kosul 160 bayta kirpildiysa bugunku uzun ad da ayni sayilir.
    const auto rt = benchmark_runtime_from_history(same, open, today);
    EF_CHECK(rt.measured && rt.cls == RuntimeClass::GpuBound);
    const std::string long_gpu(200, 'G');
    const std::vector<HistoryEntry> trimmed{entry(scene, {{"cpu", "CPU A"}, {"gpu", long_gpu.substr(0, kRuntimeConditionBytes)}, {"gpu_driver", "2.0"}})};
    EF_CHECK(benchmark_runtime_from_history(trimmed, open, RuntimeHardware{"CPU A", long_gpu, "2.0"}).measured);
}

// B10 inceleme: olcum durumu ya da sinif degisince bilesen raporu yeniden toplanir (aksi halde ilk Uygulama Testi'nden
// sonra CPU/GPU yeniden baslatmaya kadar "Olculmedi" kalirdi).
EF_TEST(b10_runtime_change_triggers_component_refresh) {
    BenchmarkRuntime none, measured{true, std::nullopt}, gpu{true, RuntimeClass::GpuBound}, cpu{true, RuntimeClass::CpuOrEngineBound};
    EF_CHECK(benchmark_runtime_changed(none, measured));
    EF_CHECK(benchmark_runtime_changed(measured, none));
    EF_CHECK(benchmark_runtime_changed(measured, gpu));
    EF_CHECK(benchmark_runtime_changed(gpu, cpu));
    EF_CHECK(!benchmark_runtime_changed(measured, BenchmarkRuntime{true, std::nullopt}));
    EF_CHECK(!benchmark_runtime_changed(none, BenchmarkRuntime{}));
}
