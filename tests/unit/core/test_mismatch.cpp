// Spec 2026-11-12 C (D6): parca ve ayar uyumu kurallari (saf). Her esik: hemen alti / tam / hemen ustu.
//  C1 CPU hiz siniri, C2 PCIe linki yuk altinda, C3 oyunun cizdigi bagdastirici.
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/diagnosis/mismatch.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

// 2 Hz ornekler: [begin, end] penceresinde ilk yari `first`, ikinci yari `last` yuzdesi; sinir sabit.
std::vector<CpuSpeedSample> ramp(double begin, double end, double first, double last, std::optional<double> limit = 100.0) {
    std::vector<CpuSpeedSample> out;
    const double mid = (begin + end) / 2.0;
    for (double t = begin; t <= end + 1e-9; t += 0.5) out.push_back({t, t <= mid ? first : last, limit});
    return out;
}

PcieLoadReading link(std::uint32_t w, std::uint32_t wmax, std::uint32_t g, std::uint32_t gmax, std::optional<double> load = 97.0) {
    PcieLoadReading r;
    r.cur_width = w, r.max_width = wmax, r.cur_gen = g, r.max_gen = gmax;
    r.slot_max_width = wmax, r.slot_max_gen = gmax;
    r.gpu_load_pct = load;
    return r;
}

GameAdapterInput adapters(std::string game, std::string high, std::optional<bool> laptop, double pct = 80.0, std::size_t n = 2) {
    GameAdapterInput in;
    in.game_luid = std::move(game);
    in.high_perf_luid = std::move(high);
    in.laptop = laptop;
    in.game_3d_pct = pct;
    in.hardware_adapters = n;
    return in;
}

}  // namespace

static_assert(kCpuSpeedDropRatio == 0.15 && kCpuSpeedSegmentS == 3.0 && kCpuPerfLimitFull == 100.0);
static_assert(kGameAdapterMinLoadPct == 5.0);

// ---- C1 ----

EF_TEST(cpu_speed_drop_threshold_edges) {
    const CpuSpeedWindow w{10.0, 18.0};  // Tam adim: 8 sn -> dilim 3 sn
    // %15 tam: dusus DEGIL (kural "> %15").
    auto f = evaluate_cpu_speed(ramp(10.0, 18.0, 120.0, 102.0), std::vector<CpuSpeedWindow>{w});
    EF_CHECK(f.measured && !f.drop && !f.limited && !f.noted());
    EF_CHECK(f.first_pct && near(*f.first_pct, 120.0) && f.last_pct && near(*f.last_pct, 102.0) && f.drop_ratio && near(*f.drop_ratio, 0.15));
    // Hemen ustu: dusus.
    f = evaluate_cpu_speed(ramp(10.0, 18.0, 120.0, 101.9), std::vector<CpuSpeedWindow>{w});
    EF_CHECK(f.measured && f.drop && f.noted() && !f.limited);
    // Hemen alti: degil.
    f = evaluate_cpu_speed(ramp(10.0, 18.0, 120.0, 102.1), std::vector<CpuSpeedWindow>{w});
    EF_CHECK(f.measured && !f.drop);
    // Hiz artti: oran negatif, dusus yok.
    f = evaluate_cpu_speed(ramp(10.0, 18.0, 100.0, 130.0), std::vector<CpuSpeedWindow>{w});
    EF_CHECK(f.measured && !f.drop && f.drop_ratio && *f.drop_ratio < 0.0);
}

EF_TEST(cpu_speed_uses_first_and_last_three_seconds_only) {
    // 8 sn pencere: ilk 3 sn 120, ortadaki 2 sn cok dusuk (sayilmaz), son 3 sn 118 -> dusus yok.
    std::vector<CpuSpeedSample> s;
    for (double t = 0.0; t <= 8.0 + 1e-9; t += 0.5) s.push_back({t, t <= 3.0 ? 120.0 : t < 5.0 ? 40.0 : 118.0, 100.0});
    auto f = evaluate_cpu_speed(s, std::vector<CpuSpeedWindow>{{0.0, 8.0}});
    EF_CHECK(f.measured && !f.drop && near(*f.first_pct, 120.0) && near(*f.last_pct, 118.0));
    // Hizli adim (4 sn pencere): dilim pencerenin yarisi (2 sn); ortusme yok.
    f = evaluate_cpu_speed(ramp(0.0, 4.0, 130.0, 100.0), std::vector<CpuSpeedWindow>{{0.0, 4.0}});
    EF_CHECK(f.measured && f.drop && near(*f.first_pct, 130.0) && near(*f.last_pct, 100.0));
    // Pencere disindaki ornekler kullanilmaz: dusus pencereden SONRA.
    auto late = ramp(0.0, 8.0, 120.0, 120.0);
    for (double t = 8.5; t < 12.0; t += 0.5) late.push_back({t, 60.0, 50.0});
    f = evaluate_cpu_speed(late, std::vector<CpuSpeedWindow>{{0.0, 8.0}});
    EF_CHECK(f.measured && !f.drop && !f.limited && near(*f.min_limit_pct, 100.0));
    // Birden cok pencere: en buyuk dusus bildirilir (S1 temiz, K1 dustu).
    auto two = ramp(0.0, 8.0, 120.0, 119.0);
    for (const auto& x : ramp(20.0, 28.0, 118.0, 90.0)) two.push_back(x);
    f = evaluate_cpu_speed(two, std::vector<CpuSpeedWindow>{{0.0, 8.0}, {20.0, 28.0}});
    EF_CHECK(f.drop && near(*f.first_pct, 118.0) && near(*f.last_pct, 90.0));
}

EF_TEST(cpu_speed_limit_below_100_and_missing_data) {
    const std::vector<CpuSpeedWindow> w{{0.0, 8.0}};
    // Sinir tam 100: sinirlama yok. 99: var (hiz dusmese de not).
    auto f = evaluate_cpu_speed(ramp(0.0, 8.0, 110.0, 110.0, 100.0), w);
    EF_CHECK(f.measured && !f.limited && !f.noted() && near(*f.min_limit_pct, 100.0));
    f = evaluate_cpu_speed(ramp(0.0, 8.0, 110.0, 110.0, 99.0), w);
    EF_CHECK(f.measured && f.limited && f.noted() && !f.drop && near(*f.min_limit_pct, 99.0));
    // Tek bir ornekte dusuk sinir yeter (en dusuk deger).
    auto one = ramp(0.0, 8.0, 110.0, 110.0, 100.0);
    one[5].limit_pct = 62.0;
    f = evaluate_cpu_speed(one, w);
    EF_CHECK(f.limited && near(*f.min_limit_pct, 62.0));
    // Okunamayan degerler: sinir yok -> yalniz hiz; hiz yok -> yalniz sinir; ikisi de yok -> olculmedi (not yok).
    f = evaluate_cpu_speed(ramp(0.0, 8.0, 120.0, 90.0, std::nullopt), w);
    EF_CHECK(f.measured && f.drop && !f.limited && !f.min_limit_pct);
    std::vector<CpuSpeedSample> limit_only;
    for (double t = 0.0; t <= 8.0; t += 0.5) limit_only.push_back({t, std::nullopt, 80.0});
    f = evaluate_cpu_speed(limit_only, w);
    EF_CHECK(f.measured && f.limited && !f.drop && !f.first_pct && !f.drop_ratio);
    EF_CHECK(!evaluate_cpu_speed({}, w).measured && !evaluate_cpu_speed({}, w).noted());
    EF_CHECK(!evaluate_cpu_speed(ramp(0.0, 8.0, 120.0, 60.0), {}).measured);  // pencere yok: iddia yok
    // Gecersiz ornekler atlanir (NaN, <= 0, sinir [0,100] disi); bozuk pencere yok sayilir.
    std::vector<CpuSpeedSample> bad{{1.0, std::numeric_limits<double>::quiet_NaN(), 250.0}, {2.0, 0.0, -5.0}, {7.0, -3.0, std::nullopt}};
    f = evaluate_cpu_speed(bad, w);
    EF_CHECK(!f.measured && !f.noted());
    EF_CHECK(!evaluate_cpu_speed(ramp(0.0, 8.0, 120.0, 60.0), std::vector<CpuSpeedWindow>{{8.0, 0.0}}).measured);
    // Yalniz ilk dilimde ornek var (son dilim bos): dusus hesaplanmaz.
    std::vector<CpuSpeedSample> head{{0.5, 120.0, std::nullopt}, {1.0, 120.0, std::nullopt}};
    EF_CHECK(!evaluate_cpu_speed(head, w).measured);
}

// ---- C2 ----

EF_TEST(pcie_under_load_compares_width_and_generation) {
    // x8 / x16 yuk altinda: azaminin altinda, mevcut kural "dusuk link".
    auto f = evaluate_pcie_under_load(link(8, 16, 4, 4));
    EF_CHECK(f.measured_under_load && f.width_below_max && !f.gen_below_max && f.below_max() && f.reduced());
    EF_CHECK(f.platform.cls == PlatformClass::PcieLinkReduced && f.platform.reason == PlatformConfidence::DirectLinkReading && !f.platform.hardware_fault());
    // Hiz nesli dusuk (Gen 3 / Gen 4), genislik tam.
    f = evaluate_pcie_under_load(link(16, 16, 3, 4));
    EF_CHECK(f.measured_under_load && !f.width_below_max && f.gen_below_max && f.reduced());
    // Azamide: bulgu yok.
    f = evaluate_pcie_under_load(link(16, 16, 5, 5));
    EF_CHECK(f.measured_under_load && !f.below_max() && !f.reduced() && f.platform.cls == PlatformClass::NotLimiting);
    EF_CHECK(pcie_link_text(link(8, 16, 3, 4)) == "x8 / x16, Gen 3 / Gen 4");
    PcieLoadReading only_width;
    only_width.cur_width = 8, only_width.max_width = 16;
    EF_CHECK(pcie_link_text(only_width) == "x8 / x16" && pcie_link_text(PcieLoadReading{}).empty());
}

EF_TEST(pcie_idle_or_unknown_load_is_not_measured_under_load) {
    // Yuk esigi (kGpuLoadPct = 60): alti bosta sayilir -> bostaki dusuk link darbogaz ilan edilmez.
    auto f = evaluate_pcie_under_load(link(8, 16, 1, 4, 59.9));
    EF_CHECK(!f.measured_under_load && !f.below_max() && !f.reduced() && f.platform.cls == PlatformClass::Insufficient);
    f = evaluate_pcie_under_load(link(8, 16, 1, 4, 60.0));
    EF_CHECK(f.measured_under_load && f.reduced());
    // Yuk bilinmiyor: karar yok.
    f = evaluate_pcie_under_load(link(8, 16, 4, 4, std::nullopt));
    EF_CHECK(!f.measured_under_load && !f.reduced());
    // Link okunamadi: karar yok.
    PcieLoadReading none;
    none.gpu_load_pct = 99.0;
    EF_CHECK(!evaluate_pcie_under_load(none).measured_under_load);
}

EF_TEST(pcie_slot_maximum_is_read_not_assumed) {
    // Yuva x8, kart x16, link x8: yuva siniri -> mevcut kural "sinirlamiyor" der; duz karsilastirma yine "x8 / x16".
    PcieLoadReading r = link(8, 16, 4, 4);
    r.slot_max_width = 8;
    auto f = evaluate_pcie_under_load(r);
    EF_CHECK(f.measured_under_load && f.width_below_max && f.below_max() && !f.reduced() && f.platform.cls == PlatformClass::NotLimiting);
    // Yuva azamisi okunamadi (gercek cihaz: AMD kok portu link ozelligi vermiyor) ve link kart azamisinde: o hizda
    // calisan link yuvanin en az onu destekledigini kanitlar -> yuva = kart azamisi, "azamide".
    r = link(16, 16, 5, 5);
    r.slot_max_width.reset(), r.slot_max_gen.reset();
    PcieSample s = pcie_sample_from(r);
    EF_CHECK(s.slot_max_width == 16u && s.slot_max_gen == 5u && s.card_max_width == 16u && s.cur_gen == 5u && s.gpu_load_pct == 97.0);
    f = evaluate_pcie_under_load(r);
    EF_CHECK(f.measured_under_load && !f.below_max() && !f.reduced() && f.platform.cls == PlatformClass::NotLimiting);
    // Yuva azamisi okunamadi ve hiz nesli kart azamisinin ALTINDA (Gen 5 kart, Gen 4 yuva olagandir): kart azamisi
    // yuva yerine yazilmaz, kural hukum vermez ("dusuk link" denmez). Olculen duz karsilastirma yine yuk alti okumadir.
    r = link(16, 16, 4, 5);
    r.slot_max_width.reset(), r.slot_max_gen.reset();
    s = pcie_sample_from(r);
    EF_CHECK(s.slot_max_width == 16u && !s.slot_max_gen && s.card_max_gen == 5u && s.cur_gen == 4u);
    f = evaluate_pcie_under_load(r);
    EF_CHECK(f.measured_under_load && f.gen_below_max && !f.width_below_max && f.below_max());
    EF_CHECK(!f.reduced() && f.platform.cls == PlatformClass::Insufficient && !f.platform.hardware_fault());
    EF_CHECK(pcie_link_text(r) == "x16 / x16, Gen 4 / Gen 5");
    // Ayni okuma, yuva OKUNDU ve Gen 5: kart da yuva da Gen 5 iken Gen 4 -> kural "dusuk link" der.
    r.slot_max_width = 16, r.slot_max_gen = 5;
    f = evaluate_pcie_under_load(r);
    EF_CHECK(f.measured_under_load && f.reduced());
    // Yuva okundu ve Gen 4: yuva siniri, dusus degil.
    r.slot_max_gen = 4;
    f = evaluate_pcie_under_load(r);
    EF_CHECK(f.measured_under_load && f.below_max() && !f.reduced() && f.platform.cls == PlatformClass::NotLimiting);
    // Genislik icin de ayni: x8 / x16, yuva okunamadi -> kural hukum vermez; olcum "x8 / x16, yuk altinda" kalir.
    r = link(8, 16, 4, 4);
    r.slot_max_width.reset(), r.slot_max_gen.reset();
    s = pcie_sample_from(r);
    EF_CHECK(!s.slot_max_width && s.slot_max_gen == 4u && s.card_max_width == 16u && s.cur_width == 8u);
    f = evaluate_pcie_under_load(r);
    EF_CHECK(f.measured_under_load && f.width_below_max && f.below_max() && !f.reduced() && f.platform.cls == PlatformClass::Insufficient);
    // Yuk bostayken / bilinmezken yuva cikarimi "yuk altinda olculdu" yapmaz.
    r.gpu_load_pct = 59.9;
    EF_CHECK(!evaluate_pcie_under_load(r).measured_under_load && !evaluate_pcie_under_load(r).below_max());
    r.gpu_load_pct = std::numeric_limits<double>::quiet_NaN();
    EF_CHECK(!evaluate_pcie_under_load(r).measured_under_load);
    // Kart azamisi da bilinmiyor: karsilastirma yapilamaz.
    PcieLoadReading unknown_max;
    unknown_max.cur_width = 8, unknown_max.cur_gen = 3, unknown_max.gpu_load_pct = 98.0;
    f = evaluate_pcie_under_load(unknown_max);
    EF_CHECK(!f.measured_under_load && !f.below_max() && f.platform.cls == PlatformClass::Insufficient);
}

// ---- C3 ----

EF_TEST(game_adapter_pick_is_the_most_loaded_adapter) {
    const std::vector<AdapterLoadSample> s{{"00000000:0000c9e5", 90.0}, {"00000000:0000a1b2", 12.0}, {"00000000:0000c9e5", 70.0},
                                           {"", 99.0}, {"00000000:0000a1b2", std::numeric_limits<double>::quiet_NaN()}, {"00000000:0000a1b2", -1.0}};
    const auto p = pick_game_adapter(s);
    EF_CHECK(p.luid == "00000000:0000c9e5" && p.samples == 2 && p.mean_pct && near(*p.mean_pct, 80.0));
    const auto none = pick_game_adapter({});
    EF_CHECK(none.luid.empty() && !none.mean_pct && none.samples == 0);
    // Esitlikte kucuk LUID (belirlenimci).
    const std::vector<AdapterLoadSample> tie{{"00000000:0000ffff", 50.0}, {"00000000:00000001", 50.0}};
    EF_CHECK(pick_game_adapter(tie).luid == "00000000:00000001");
}

EF_TEST(game_adapter_desktop_advice_laptop_info) {
    const std::string dgpu = "00000000:0000c9e5", igpu = "00000000:0000a1b2";
    // Ayni bagdastirici: bulgu yok.
    auto f = evaluate_game_adapter(adapters(dgpu, dgpu, false));
    EF_CHECK(f.cls == GameAdapterClass::OnHighPerformance && f.level == GameAdapterLevel::None);
    // Farkli: masaustu -> oneri, dizustu -> bilgi, bilinmiyor -> bilgi.
    f = evaluate_game_adapter(adapters(igpu, dgpu, false));
    EF_CHECK(f.cls == GameAdapterClass::OnOtherAdapter && f.level == GameAdapterLevel::Advice);
    f = evaluate_game_adapter(adapters(igpu, dgpu, true));
    EF_CHECK(f.cls == GameAdapterClass::OnOtherAdapter && f.level == GameAdapterLevel::Info);
    f = evaluate_game_adapter(adapters(igpu, dgpu, std::nullopt));
    EF_CHECK(f.cls == GameAdapterClass::OnOtherAdapter && f.level == GameAdapterLevel::Info);
}

EF_TEST(game_adapter_unknown_inputs_make_no_claim) {
    const std::string dgpu = "00000000:0000c9e5", igpu = "00000000:0000a1b2";
    auto f = evaluate_game_adapter(adapters("", dgpu, false));
    EF_CHECK(f.cls == GameAdapterClass::Unknown && f.level == GameAdapterLevel::None && f.reason == "no_game_adapter");
    f = evaluate_game_adapter(adapters(igpu, "", false));
    EF_CHECK(f.cls == GameAdapterClass::Unknown && f.reason == "no_high_perf");
    // Yuk esigi: %5'in alti iddia icin yetersiz; tam %5 yeterli.
    f = evaluate_game_adapter(adapters(igpu, dgpu, false, 4.9));
    EF_CHECK(f.cls == GameAdapterClass::Unknown && f.level == GameAdapterLevel::None && f.reason == "low_load");
    f = evaluate_game_adapter(adapters(igpu, dgpu, false, 5.0));
    EF_CHECK(f.cls == GameAdapterClass::OnOtherAdapter && f.level == GameAdapterLevel::Advice);
    auto in = adapters(igpu, dgpu, false);
    in.game_3d_pct.reset();
    EF_CHECK(evaluate_game_adapter(in).reason == "low_load");
    in.game_3d_pct = std::numeric_limits<double>::infinity();
    EF_CHECK(evaluate_game_adapter(in).cls == GameAdapterClass::Unknown);
    // Tek donanim karti sayilmisken farkli LUID: tutarsiz sayim, iddia yok.
    f = evaluate_game_adapter(adapters(igpu, dgpu, false, 80.0, 1));
    EF_CHECK(f.cls == GameAdapterClass::Unknown && f.reason == "single_adapter");
    EF_CHECK(evaluate_game_adapter(adapters(dgpu, dgpu, false, 80.0, 1)).cls == GameAdapterClass::OnHighPerformance);
}
