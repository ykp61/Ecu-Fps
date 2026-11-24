// UI-09: canli metrik veri modeli (saf mantik). Gercek PDH degerleri windows.live_metrics testindedir.
#include <cmath>
#include <limits>

#include "core/metrics/live.h"
#include "ef_test.h"

using namespace ef;
using contracts::Measurement;
using contracts::MissingReason;
using contracts::Unit;
namespace m = core::metrics;

namespace {
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

EF_TEST(live_clamp_and_percent) {
    EF_CHECK(m::clamp_pct(-5).value() == 0.0);
    EF_CHECK(m::clamp_pct(140).value() == 100.0);
    EF_CHECK(m::clamp_pct(42.5).value() == 42.5);
    EF_CHECK(!m::clamp_pct(kNaN));
    EF_CHECK(!m::clamp_pct(kInf));
    const auto bad = m::percent(kNaN);
    EF_CHECK(!bad.has_value());
    EF_CHECK(bad.missing_reason() == MissingReason::Invalid);
    EF_CHECK(m::percent(120).value().value() == 100.0);
    EF_CHECK(m::percent(120).unit() == Unit::Percent);
}

EF_TEST(live_ring_fraction_missing_is_empty_not_zero_value) {
    const auto miss = Measurement::missing(Unit::Percent, MissingReason::NotSupported);
    EF_CHECK(m::ring_fraction(miss) == 0.0f);
    EF_CHECK(!miss.has_value());  // cagiran "Olculemedi" gosterir; deger 0 olarak uydurulmaz
    EF_CHECK(m::ring_fraction(Measurement::of(50, Unit::Percent)) == 0.5f);
    EF_CHECK(m::ring_fraction(Measurement::of(250, Unit::Percent)) == 1.0f);
    EF_CHECK(m::ring_fraction(Measurement::of(-3, Unit::Percent)) == 0.0f);
    EF_CHECK(m::ring_fraction(Measurement::of(kNaN, Unit::Percent)) == 0.0f);
    EF_CHECK(m::ring_fraction(Measurement::of(50, Unit::MiB)) == 0.0f);  // yanlis birim
}

EF_TEST(live_ram_percent) {
    EF_CHECK(near(m::ram_percent(8, 32).value().value(), 25.0));
    EF_CHECK(m::ram_percent(1, 0).missing_reason() == MissingReason::Invalid);
    EF_CHECK(m::ram_percent(33, 32).missing_reason() == MissingReason::Invalid);
    EF_CHECK(near(m::ram_percent(0, 32).value().value(), 0.0));
}

EF_TEST(live_parse_gpu_engine_instance) {
    const auto id = m::parse_gpu_engine_instance("pid_1234_luid_0x00000000_0x0000C2E3_phys_0_eng_3_engtype_3D");
    EF_CHECK(id.has_value());
    EF_CHECK(id->pid == 1234);
    EF_CHECK(id->luid == "0x00000000_0x0000C2E3");
    EF_CHECK(id->phys == 0);
    EF_CHECK(id->eng == 3);
    EF_CHECK(id->engtype == "3D");
    const auto vd = m::parse_gpu_engine_instance("pid_8_luid_0x00000000_0x0001ABCD_phys_1_eng_12_engtype_VideoDecode");
    EF_CHECK(vd && vd->engtype == "VideoDecode" && vd->phys == 1 && vd->eng == 12);
    const auto noty = m::parse_gpu_engine_instance("pid_8_luid_0x00000000_0x0001ABCD_phys_0_eng_1");
    EF_CHECK(noty && noty->engtype.empty());
    EF_CHECK(!m::parse_gpu_engine_instance(""));
    EF_CHECK(!m::parse_gpu_engine_instance("_Total"));
    EF_CHECK(!m::parse_gpu_engine_instance("pid_x_luid_0x0_0x1_phys_0_eng_0_engtype_3D"));
    EF_CHECK(!m::parse_gpu_engine_instance("pid_1_luid_0x0_phys_0_eng_0_engtype_3D"));
    EF_CHECK(!m::parse_gpu_engine_instance("pid_1_luid_0x0_0x1_phys__eng_0_engtype_3D"));
}

EF_TEST(live_aggregate_gpu_sums_processes_per_engine_max_over_engines_and_adapters) {
    const char* a = "0x00000000_0x0000AAAA";
    const char* b = "0x00000000_0x0000BBBB";
    auto inst = [](int pid, const char* luid, int eng, const char* type) {
        return "pid_" + std::to_string(pid) + "_luid_" + luid + "_phys_0_eng_" + std::to_string(eng) + "_engtype_" + type;
    };
    std::vector<m::GpuEngineSample> s = {
        {inst(10, a, 0, "3D"), 30},      // A 3D: 30 + 25 = 55
        {inst(11, a, 0, "3D"), 25},
        {inst(10, a, 2, "Copy"), 40},    // A Copy: 40 (3D ile toplanmaz)
        {inst(12, b, 0, "3D"), 70},      // B 3D: 70 + 50 = 120 -> 100'e kirpilir
        {inst(13, b, 0, "3D"), 50},
        {inst(14, b, 1, "Compute"), kNaN},  // atlanir
        {"_Total", 99},                     // ayrisamaz, atlanir
    };
    const auto g = m::aggregate_gpu(s);
    EF_CHECK(g.has_value());
    EF_CHECK(near(g.value().value(), 100.0));

    s.erase(s.begin() + 3, s.begin() + 5);  // B 3D kaldirildi -> A = max(55, 40) = 55
    EF_CHECK(near(m::aggregate_gpu(s).value().value(), 55.0));

    // Bosta iGPU + yuklu dGPU: genel deger yuklu adaptordur (ortalama degil)
    std::vector<m::GpuEngineSample> two = {{inst(1, a, 0, "3D"), 2}, {inst(2, b, 0, "3D"), 80}};
    EF_CHECK(near(m::aggregate_gpu(two).value().value(), 80.0));

    // Negatif deger 0 sayilir (kayma), toplam negatif olmaz
    std::vector<m::GpuEngineSample> neg = {{inst(1, a, 0, "3D"), -5}};
    EF_CHECK(near(m::aggregate_gpu(neg).value().value(), 0.0));
}

EF_TEST(live_aggregate_gpu_empty_is_missing) {
    const auto e = m::aggregate_gpu({});
    EF_CHECK(!e.has_value());
    EF_CHECK(e.missing_reason() == MissingReason::NotSampled);
    const auto bad = m::aggregate_gpu({{"garbage", 50}});
    EF_CHECK(!bad.has_value());
}

EF_TEST(live_aggregate_disk) {
    std::vector<m::DiskIdleSample> d = {{"0 C:", 90}, {"1 D:", 35}, {"_Total", 10}};
    EF_CHECK(near(m::aggregate_disk(d).value().value(), 65.0));  // _Total yok sayilir
    std::vector<m::DiskIdleSample> over = {{"0 C:", 104}, {"1 D:", -2}};  // PDH kaymasi
    EF_CHECK(near(m::aggregate_disk(over).value().value(), 100.0));
    std::vector<m::DiskIdleSample> idle = {{"0 C:", 104}};
    EF_CHECK(near(m::aggregate_disk(idle).value().value(), 0.0));
    std::vector<m::DiskIdleSample> only_total = {{"_Total", 50}};
    EF_CHECK(m::aggregate_disk(only_total).missing_reason() == MissingReason::NotSampled);
    std::vector<m::DiskIdleSample> nan = {{"0 C:", kNaN}};
    EF_CHECK(!m::aggregate_disk(nan).has_value());
    EF_CHECK(!m::aggregate_disk({}).has_value());
}

EF_TEST(live_smoother) {
    m::Smoother sm(0.5);
    EF_CHECK(near(sm.push(Measurement::of(40, Unit::Percent)).value().value(), 40.0));  // ilk deger
    EF_CHECK(near(sm.push(Measurement::of(80, Unit::Percent)).value().value(), 60.0));
    EF_CHECK(near(sm.push(Measurement::of(80, Unit::Percent)).value().value(), 70.0));
    // Eksik giris: eksik doner, bayat 70 tasinmaz; sonra yeniden baslar
    const auto miss = sm.push(Measurement::missing(Unit::Percent, MissingReason::SourceError));
    EF_CHECK(!miss.has_value());
    EF_CHECK(miss.missing_reason() == MissingReason::SourceError);
    EF_CHECK(near(sm.push(Measurement::of(10, Unit::Percent)).value().value(), 10.0));
    const auto nan = sm.push(Measurement::of(kNaN, Unit::Percent));
    EF_CHECK(!nan.has_value() && nan.missing_reason() == MissingReason::Invalid);
    m::Smoother raw(1.0);  // alpha 1 = yumusatma yok
    raw.push(Measurement::of(10, Unit::Percent));
    EF_CHECK(near(raw.push(Measurement::of(90, Unit::Percent)).value().value(), 90.0));
    m::Smoother badalpha(kNaN);
    EF_CHECK(near(badalpha.push(Measurement::of(5, Unit::Percent)).value().value(), 5.0));
}

// Denge testi karistiricisi (spec 2026-11-12 A3): baska sureclerin CPU'su CPU SURESI biriminde, birikimli
// sayac farklarindan. Frekans olcekli toplam (% Processor Utility) ile sure tabanli surec zamani karistirilmaz.
EF_TEST(live_other_cpu_time_percent_uses_one_unit) {
    using m::CpuTimes;
    // 16 mantiksal islemci, 1 sn: toplam 16 sn; surec 9 cekirdek (dokuz is parcacigi), baska surecler 0,8 cekirdek.
    constexpr std::uint64_t s = 10'000'000;  // 100 ns biriminde 1 sn
    const CpuTimes a{100 * s, 40 * s, 20 * s};
    const CpuTimes b{a.total + 16 * s, a.busy + 9 * s + 8 * s / 10, a.own + 9 * s};
    const auto other = m::other_cpu_time_percent(a, b);
    EF_CHECK(other.has_value() && other.unit() == Unit::Percent && near(*other.value(), 5.0));  // 0,8 / 16
    // Surec butun cekirdekleri doldurdu, baska surec yok: 0 (turbo carpani sahte yuk uretmez).
    EF_CHECK(near(*m::other_cpu_time_percent(a, {a.total + 16 * s, a.busy + 16 * s, a.own + 16 * s}).value(), 0.0));
    // Sayac taneligi: surec farki mesgul farkini asarsa negatif uretilmez.
    EF_CHECK(near(*m::other_cpu_time_percent(a, {a.total + 16 * s, a.busy + 8 * s, a.own + 9 * s}).value(), 0.0));
    // Esik kenari (%10): 1,6 / 16.
    EF_CHECK(near(*m::other_cpu_time_percent(a, {a.total + 16 * s, a.busy + 10 * s + 6 * s / 10, a.own + 9 * s}).value(), 10.0));
    // Toplam ilerlemedi ya da bir sayac geri gitti: deger uydurulmaz.
    for (const CpuTimes& bad : {a, CpuTimes{a.total - 1, a.busy, a.own}, CpuTimes{a.total + s, a.busy - 1, a.own}, CpuTimes{a.total + s, a.busy, a.own - 1}}) {
        const auto x = m::other_cpu_time_percent(a, bad);
        EF_CHECK(!x.has_value() && x.missing_reason() == MissingReason::NotSampled);
    }
}

EF_TEST(live_other_cpu_time_percent_counts_only_measure_windows) {
    using m::CpuTimeReading;
    using m::CpuTimes;
    using m::TimeWindow;
    constexpr std::uint64_t s = 10'000'000;
    // 8 islemci; 100 ms'de bir okuma. [0, 1 sn) isinma: baska surecler 4 cekirdek (pencere DISI, sayilmaz).
    // [1, 2] sn olcum: surec 6 cekirdek, baska 0,4 cekirdek. [3, 4] sn olcum: surec 6, baska 1,2 cekirdek.
    std::vector<CpuTimeReading> r;
    CpuTimes t{};
    const auto other_cores_at = [](int ms) { return ms < 1000 ? 4.0 : ms < 2000 ? 0.4 : ms < 3000 ? 4.0 : 1.2; };
    for (int ms = 0; ms <= 4000; ms += 100) {
        r.push_back({static_cast<std::int64_t>(ms) * 1000, t});
        const double other = other_cores_at(ms);
        t.total += 8 * s / 10;
        t.own += 6 * s / 10;
        t.busy += 6 * s / 10 + static_cast<std::uint64_t>(std::llround(other * static_cast<double>(s) / 10.0));
    }
    const std::vector<TimeWindow> windows{{1'000'000, 2'000'000}, {3'000'000, 4'000'000}};
    const auto v = m::other_cpu_time_percent_in(r, windows);
    EF_CHECK(v.has_value() && near(*v.value(), 100.0 * (0.4 + 1.2) / 16.0));  // sure agirlikli: 1,6 cekirdek-sn / 16
    // Tek pencere.
    EF_CHECK(near(*m::other_cpu_time_percent_in(r, {{1'000'000, 2'000'000}}).value(), 100.0 * 0.4 / 8.0));
    // Pencereye tek okuma dusuyor ya da hic dusmuyor: bilinmiyor (0 degil).
    for (const auto& w : {std::vector<TimeWindow>{{1'010'000, 1'090'000}}, std::vector<TimeWindow>{{1'000'000, 1'050'000}},
                          std::vector<TimeWindow>{{9'000'000, 9'500'000}}, std::vector<TimeWindow>{{2'000'000, 1'000'000}}, std::vector<TimeWindow>{}}) {
        const auto x = m::other_cpu_time_percent_in(r, w);
        EF_CHECK(!x.has_value() && x.missing_reason() == MissingReason::NotSampled);
    }
    EF_CHECK(!m::other_cpu_time_percent_in({}, windows).has_value());
    // Bir pencerede sayac geri gittiyse o pencere atlanir, digeri sayilir.
    auto broken = r;
    for (auto& x : broken)
        if (x.t_us >= 3'500'000) x.times.total = 0;
    EF_CHECK(near(*m::other_cpu_time_percent_in(broken, windows).value(), 100.0 * 0.4 / 8.0));
}

EF_TEST(live_other_process_percent) {
    const auto total = Measurement::of(70.0, Unit::Percent);
    const auto game = Measurement::of(25.0, Unit::Percent);
    const auto other = m::other_process_percent(total, game);
    EF_CHECK(other.has_value() && near(*other.value(), 45.0));
    // Hedef toplamdan buyuk (ornekleme kaymasi): negatif uretilmez.
    const auto shifted = m::other_process_percent(Measurement::of(20.0, Unit::Percent),
                                                  Measurement::of(35.0, Unit::Percent));
    EF_CHECK(shifted.has_value() && near(*shifted.value(), 0.0));
    // Eksik taraf: deger uydurulmaz.
    const auto missing_total = m::other_process_percent(Measurement::missing(Unit::Percent, MissingReason::NotSampled), game);
    EF_CHECK(!missing_total.has_value() && missing_total.missing_reason() == MissingReason::NotSampled);
    const auto missing_game = m::other_process_percent(total, Measurement::missing(Unit::Percent, MissingReason::PermissionDenied));
    EF_CHECK(!missing_game.has_value() && missing_game.missing_reason() == MissingReason::NotSampled);
    // Birim yanlissa hesaplanmaz.
    const auto wrong_unit = m::other_process_percent(Measurement::of(70.0, Unit::Fps), game);
    EF_CHECK(!wrong_unit.has_value() && wrong_unit.missing_reason() == MissingReason::Invalid);
}

EF_TEST(live_mean_measured) {
    using m::mean_measured;
    const std::vector<Measurement> pct{Measurement::of(70.0, Unit::Percent),
                                       Measurement::missing(Unit::Percent, MissingReason::NotSampled),
                                       Measurement::of(90.0, Unit::Percent)};
    const auto avg = mean_measured(pct);
    EF_CHECK(avg.has_value() && avg.unit() == Unit::Percent && near(*avg.value(), 80.0));
    EF_CHECK(!mean_measured({}).has_value());
    EF_CHECK(mean_measured({}).missing_reason() == MissingReason::NotSampled);
    const std::vector<Measurement> mixed{Measurement::of(10.0, Unit::Percent), Measurement::of(5.0, Unit::Fps)};
    EF_CHECK(!mean_measured(mixed).has_value() && mean_measured(mixed).missing_reason() == MissingReason::Invalid);
    // Yuzde araligi kirpilir; sonsuz deger ortalamaya girmez.
    const std::vector<Measurement> clamped{Measurement::of(120.0, Unit::Percent), Measurement::of(-20.0, Unit::Percent),
                                           Measurement::of(std::numeric_limits<double>::infinity(), Unit::Percent)};
    const auto c = mean_measured(clamped);
    EF_CHECK(c.has_value() && near(*c.value(), 50.0));
}

EF_TEST(live_snapshot_defaults_are_missing) {
    const m::LiveSnapshot s;
    EF_CHECK(!s.cpu_pct.has_value() && s.cpu_pct.missing_reason() == MissingReason::NotSampled);
    EF_CHECK(!s.gpu_pct.has_value());
    EF_CHECK(!s.ram_pct.has_value());
    EF_CHECK(!s.disk_pct.has_value());
    EF_CHECK(!s.other_process_cpu_pct.has_value() &&
             s.other_process_cpu_pct.missing_reason() == MissingReason::NotSampled);
    EF_CHECK(s.volumes.empty());
    // Task 9: disk akisi ve ag hizi alanlari da olculmeden 0 degil NotSampled ile baslar.
    for (const auto* f : {&s.disk_read_mbs, &s.disk_write_mbs, &s.net_down_mbps, &s.net_up_mbps, &s.net_link_mbps})
        EF_CHECK(!f->has_value() && f->missing_reason() == MissingReason::NotSampled);
    EF_CHECK(s.disk_read_mbs.unit() == Unit::MBps && s.disk_write_mbs.unit() == Unit::MBps);
    EF_CHECK(s.net_down_mbps.unit() == Unit::Mbps && s.net_up_mbps.unit() == Unit::Mbps &&
              s.net_link_mbps.unit() == Unit::Mbps);
}

EF_TEST(octet_rate_basic) {
    const auto r = m::octet_rate_mbps(0, 1'250'000, 1.0);
    EF_CHECK(r && near(*r, 10.0));
    const auto half = m::octet_rate_mbps(1000, 1000 + 625'000, 0.5);
    EF_CHECK(half && near(*half, 10.0));
    EF_CHECK(m::octet_rate_mbps(5, 5, 1.0) && near(*m::octet_rate_mbps(5, 5, 1.0), 0.0));
}

EF_TEST(octet_rate_wrap_is_missing) {
    // Sayac geri gitti (bagdastirici sifirlandi / tasma): negatif hiz uretilmez.
    EF_CHECK(!m::octet_rate_mbps(2'000'000, 1'000, 1.0));
}

EF_TEST(octet_rate_zero_time) {
    EF_CHECK(!m::octet_rate_mbps(0, 1'250'000, 0.0));
    EF_CHECK(!m::octet_rate_mbps(0, 1'250'000, -1.0));
    EF_CHECK(!m::octet_rate_mbps(0, 1'250'000, kNaN));
}

EF_TEST(link_fraction_zero_link) {
    EF_CHECK(!m::link_fraction(10.0, 0.0));
    EF_CHECK(!m::link_fraction(10.0, -5.0));
    EF_CHECK(!m::link_fraction(kNaN, 1000.0));
    EF_CHECK(!m::link_fraction(10.0, kInf));
}

// Spec 2026-11-11 §4: ag kutucugu degeri MB/s = Mbit/s / 8.
EF_TEST(mbit_to_mbyte_conversion) {
    EF_CHECK(near(*m::mbit_to_mbyte_per_s(1000.0), 125.0));
    EF_CHECK(near(*m::mbit_to_mbyte_per_s(8.0), 1.0));
    EF_CHECK(near(*m::mbit_to_mbyte_per_s(0.0), 0.0));
    EF_CHECK(near(*m::mbit_to_mbyte_per_s(912.4), 114.05));
    EF_CHECK(!m::mbit_to_mbyte_per_s(-1.0));
    EF_CHECK(!m::mbit_to_mbyte_per_s(kNaN));
    EF_CHECK(!m::mbit_to_mbyte_per_s(kInf));
}

EF_TEST(link_fraction_clamps) {
    EF_CHECK(near(*m::link_fraction(2000.0, 1000.0), 1.0));
    EF_CHECK(near(*m::link_fraction(-3.0, 1000.0), 0.0));
    EF_CHECK(near(*m::link_fraction(250.0, 1000.0), 0.25));
}

EF_TEST(net_rates_adapter_change_wrap_and_vpn_link) {
    // Review focus 2: bagdastirici degisimi, sayac tasmasi, baglanti hizi 0 olan VPN -> "-" (eksik),
    // asla negatif ya da sonsuz Mbps.
    m::NetCounters a{7, 0, 0, 1'000'000'000, 1000};
    const auto first = m::net_rates(std::nullopt, a);
    EF_CHECK(!first.down.has_value() && first.down.missing_reason() == MissingReason::NotSampled);
    EF_CHECK(!first.up.has_value());
    EF_CHECK(first.link.has_value() && near(*first.link.value(), 1000.0) && first.link.unit() == Unit::Mbps);

    m::NetCounters b{7, 1'250'000, 625'000, 1'000'000'000, 2000};
    const auto ok = m::net_rates(a, b);
    EF_CHECK(ok.down.has_value() && near(*ok.down.value(), 10.0) && ok.down.unit() == Unit::Mbps);
    EF_CHECK(ok.up.has_value() && near(*ok.up.value(), 5.0));

    m::NetCounters other{9, 9'000'000, 9'000'000, 100'000'000, 3000};  // yeni bagdastirici
    const auto changed = m::net_rates(b, other);
    EF_CHECK(!changed.down.has_value() && changed.down.missing_reason() == MissingReason::NotSampled);
    EF_CHECK(!changed.up.has_value());

    m::NetCounters wrapped{7, 10, 10, 1'000'000'000, 3000};
    const auto w = m::net_rates(b, wrapped);
    EF_CHECK(!w.down.has_value() && !w.up.has_value());

    m::NetCounters same_time{7, 2'500'000, 1'000'000, 1'000'000'000, 2000};  // ayni ms: sonsuz yok
    EF_CHECK(!m::net_rates(b, same_time).down.has_value());

    m::NetCounters vpn{7, 2'500'000, 1'250'000, 0, 3000};
    const auto v = m::net_rates(b, vpn);
    EF_CHECK(v.down.has_value() && near(*v.down.value(), 10.0));
    EF_CHECK(!v.link.has_value() && v.link.missing_reason() == MissingReason::NotSupported);
    m::NetCounters unknown_link{7, 2'500'000, 1'250'000, ~0ull, 3000};
    EF_CHECK(!m::net_rates(b, unknown_link).link.has_value());
}

EF_TEST(disk_rate_sums_physical_disks) {
    const std::vector<m::DiskRateSample> s{{"_Total", 9e9}, {"0 C:", 1'500'000.0}, {"1 D:", 500'000.0},
                                           {"2 E:", kNaN}, {"3 F:", -10.0}};
    const auto r = m::aggregate_disk_rate(s);
    EF_CHECK(r.has_value() && near(*r.value(), 2.0) && r.unit() == Unit::MBps);
    const std::vector<m::DiskRateSample> only_total{{"_Total", 5e6}};
    EF_CHECK(!m::aggregate_disk_rate(only_total).has_value() &&
              m::aggregate_disk_rate(only_total).missing_reason() == MissingReason::NotSampled);
}

// denge_v2: muaf sure (bilesimci + kesme/DPC) "baska surec" CPU'sundan dusulur; muaf sayac geri giderse olcum yok.
EF_TEST(live_other_cpu_time_excludes_exempt_time) {
    namespace m = ef::core::metrics;
    constexpr std::uint64_t s = 10'000'000;  // 1 sn (100 ns)
    m::CpuTimes a{100 * s, 10 * s, 2 * s, 1 * s};
    // 12 cekirdek x 1 sn = 12 s toplam; mesgul 3 s; kendi 1 s; muaf 1,2 s -> baska 0,8 s = %6,67
    m::CpuTimes b{a.total + 12 * s, a.busy + 3 * s, a.own + 1 * s, a.exempt + 12 * s / 10};
    const auto o = m::other_cpu_time_percent(a, b);
    EF_CHECK(o.has_value() && std::fabs(*o.value() - 100.0 * 0.8 / 12.0) < 1e-6);
    m::CpuTimes back = b;
    back.exempt = a.exempt - 1;
    EF_CHECK(!m::other_cpu_time_percent(a, back).has_value());
}

// ---- Uyumluluk B3 (bosluk 2, 3): sure tabanli CPU olcumu, sentetik turbo oranlari ----
namespace {
// 16 mantiksal islemci, 50 ms'lik okumalar. Cekirdek cinsinden yukler: oyun, baska programlar, DPC/kesme, dwm.
struct SynthCpu {
    double game = 6.0, other = 1.6, dpc = 0.16, dwm = 0.32;
    m::CpuTimes now{};
    void tick(double seconds = 0.05) {
        constexpr double unit = 10'000'000.0;  // 100 ns / sn
        const auto ticks = [&](double cores) { return static_cast<std::uint64_t>(cores * seconds * unit + 0.5); };
        now.total += ticks(16.0);
        now.busy += ticks(game + other + dpc + dwm);
        now.own += ticks(game);
        now.exempt += ticks(dpc + dwm);
    }
    double busy_pct() const { return 100.0 * (game + other + dpc + dwm) / 16.0; }
};
}  // namespace

EF_TEST(live_other_cpu_time_is_turbo_invariant) {
    for (const double ratio : {1.0, 1.26, 2.0}) {
        SynthCpu cpu;
        m::CpuTimeWindow w;
        contracts::Measurement last = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
        for (int i = 0; i <= 40; ++i) {  // 2 sn
            if (i) cpu.tick();
            last = w.push(i * 50'000, cpu.now);
        }
        // Sure tabanli: baska programlar 1,6 / 16 = %10, turbo oranindan bagimsiz (DPC ve dwm sayilmaz).
        EF_CHECK(last.has_value() && std::fabs(*last.value() - 10.0) < 0.05);
        // Eski yontem (frekans olcekli Utility toplami - oyunun sure payi) turboyla sisiyordu: 1,26'da %25 esigini
        // (drops other_process_cpu_high) gecip temiz sistemde sahte "arka plan yuku" uretirdi.
        const auto utility = Measurement::of(std::min(100.0, cpu.busy_pct() * ratio), Unit::Percent);
        const auto old = m::other_process_percent(utility, Measurement::of(100.0 * cpu.game / 16.0, Unit::Percent));
        if (ratio == 1.0) EF_CHECK(*old.value() < 25.0);
        if (ratio >= 1.26) EF_CHECK(*old.value() > 25.0);
    }
}

EF_TEST(live_cpu_time_window_span_and_gaps) {
    SynthCpu cpu;
    m::CpuTimeWindow w;  // 1 sn pencere, en az 250 ms
    EF_CHECK(!w.push(0, cpu.now).has_value());  // tek okuma: fark yok
    for (int i = 1; i <= 4; ++i) {
        cpu.tick();
        const auto v = w.push(i * 50'000, cpu.now);
        EF_CHECK(!v.has_value() && v.missing_reason() == MissingReason::NotSampled);  // < 250 ms: tik gurultusu
    }
    cpu.tick();
    EF_CHECK(w.push(250'000, cpu.now).has_value());
    // Agir arka plan yuku 1 sn surer, sonra 1,5 sn hafif: deger yalniz son ~1 sn'yi yansitir (eski yuk tasinmaz).
    std::int64_t t = 250'000;
    cpu.other = 8.0;
    for (int i = 0; i < 20; ++i) {
        cpu.tick();
        w.push(t += 50'000, cpu.now);
    }
    cpu.other = 1.6;
    contracts::Measurement v = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    for (int i = 0; i < 30; ++i) {
        cpu.tick();
        v = w.push(t += 50'000, cpu.now);
    }
    EF_CHECK(v.has_value() && std::fabs(*v.value() - 10.0) < 0.05);
    // Okunamayan ornek (oyun sureci erisilemez): eksik, gecmis silinir; yeniden 250 ms beklenir.
    EF_CHECK(!w.push(t += 50'000, std::nullopt).has_value());
    cpu.tick();
    EF_CHECK(!w.push(t += 50'000, cpu.now).has_value());
    cpu.tick();
    EF_CHECK(!w.push(t += 50'000, cpu.now).has_value());
    // Sayac geri gitti (yeni surec): eski okumalarla karsilastirilmaz.
    for (int i = 0; i < 10; ++i) {
        cpu.tick();
        w.push(t += 50'000, cpu.now);
    }
    m::CpuTimes back = cpu.now;
    back.own = 0;
    EF_CHECK(!w.push(t += 50'000, back).has_value());
}

EF_TEST(live_max_core_percent_time_based_and_skips_totals) {
    using S = m::CoreCpuSample;
    // Processor Information adlari: grup toplami ("0,_Total") ve genel toplam cekirdek sayilmaz.
    const auto v = m::max_core_percent({{"0,0", 40.0}, {"0,1", 82.0}, {"0,_Total", 99.0}, {"_Total", 98.0}, {"1,0", 12.0}});
    EF_CHECK(v.has_value() && near(*v.value(), 82.0));
    EF_CHECK(near(*m::max_core_percent({{"0", 10.0}, {"3", 55.5}, {"_Total", 90.0}}).value(), 55.5));  // \Processor(*)
    EF_CHECK(m::is_processor_total_instance("_Total") && m::is_processor_total_instance("1,_Total"));
    EF_CHECK(!m::is_processor_total_instance("0,12"));
    // NaN atlanir, aralik disi kirpilir; gecerli cekirdek yoksa eksik (0 uydurulmaz).
    EF_CHECK(near(*m::max_core_percent({{"0,0", kNaN}, {"0,1", 130.0}}).value(), 100.0));
    const auto none = m::max_core_percent({{"_Total", 50.0}, {"0,0", kInf}});
    EF_CHECK(!none.has_value() && none.missing_reason() == MissingReason::NotSampled);
    EF_CHECK(!m::max_core_percent(std::vector<S>{}).has_value());
    // Sentetik turbo: cekirdek zamanin %80'i mesgul. Sure tabanli deger her oranda 80 (esik 90'in altinda); Utility
    // (sure x turbo) 1,26'da %100'u gecip cekirdegi yanlislikla "doygun" gosterirdi.
    for (const double ratio : {1.0, 1.26, 2.0}) {
        const auto time_based = m::max_core_percent({{"0,0", 80.0}, {"0,1", 20.0}});
        EF_CHECK(near(*time_based.value(), 80.0) && *time_based.value() < 90.0);
        if (ratio >= 1.26) EF_CHECK(80.0 * ratio >= 90.0);
    }
}

// ---- B3 inceleme: kismi muaf okuma ve pencerenin kendi zamani ----
EF_TEST(live_cpu_time_partial_exempt_read_is_not_compared) {
    SynthCpu cpu;
    cpu.now.exempt_parts = 0b11;  // DPC + dwm okundu
    m::CpuTimeWindow w;
    std::int64_t t = 0;
    for (int i = 0; i <= 10; ++i) {
        if (i) cpu.tick();
        w.push(t += 50'000, cpu.now);
    }
    // Bir ornekte DPC okunamadi (NtQuerySystemInformation hatasi): exempt yalniz dwm, maske farkli.
    cpu.tick();
    m::CpuTimes partial = cpu.now;
    partial.exempt = static_cast<std::uint64_t>(0.32 * 0.05 * 10'000'000.0);  // yalniz dwm parcasi
    partial.exempt_parts = 0b10;
    EF_CHECK(!w.push(t += 50'000, partial).has_value());
    // Sonraki tam okuma: kismi okumaya gore muaf fark cok buyuk gorunur; karsilastirilmaz, sahte %0 uretilmez.
    cpu.tick();
    const auto after = w.push(t += 50'000, cpu.now);
    EF_CHECK(!after.has_value() && after.missing_reason() == MissingReason::NotSampled);
    // Dogrudan iki okuma da maske farkinda olcum vermez.
    EF_CHECK(!m::other_cpu_time_percent(partial, cpu.now).has_value());
    // Yeni taban: 250 ms sonra dogru deger (%10) geri gelir.
    contracts::Measurement v = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    for (int i = 0; i < 6; ++i) {
        cpu.tick();
        v = w.push(t += 50'000, cpu.now);
    }
    EF_CHECK(v.has_value() && std::fabs(*v.value() - 10.0) < 0.05);
    // Hep okunamayan parca (maske sabit) olcumu durdurmaz.
    SynthCpu no_dpc;
    no_dpc.dpc = 0.0;
    no_dpc.now.exempt_parts = 0b10;
    m::CpuTimeWindow w2;
    contracts::Measurement v2 = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    for (int i = 0; i <= 10; ++i) {
        if (i) no_dpc.tick();
        v2 = w2.push(i * 50'000, no_dpc.now);
    }
    EF_CHECK(v2.has_value());
}

EF_TEST(live_other_cpu_time_around_window_keeps_short_burst) {
    SynthCpu cpu;
    std::vector<m::CpuTimeReading> r;
    r.push_back({0, cpu.now});
    for (std::int64_t t = 50'000; t <= 2'000'000; t += 50'000) {
        // 950..1150 ms arasi 200 ms'lik arka plan patlamasi: 9,6 cekirdek = %60.
        cpu.other = (t > 950'000 && t <= 1'150'000) ? 9.6 : 1.6;
        cpu.tick();
        r.push_back({t, cpu.now});
    }
    // 60 ms'lik drop penceresi patlamanin icinde: 250 ms'ye genisletilir, deger patlamayi tasir (sulanmaz).
    const auto in_burst = m::other_cpu_time_percent_around(r, {1'000'000, 1'060'000});
    EF_CHECK(in_burst.has_value() && *in_burst.value() >= 50.0);
    // Patlamadan uzak pencere: normal %10.
    const auto calm = m::other_cpu_time_percent_around(r, {1'600'000, 1'660'000});
    EF_CHECK(calm.has_value() && std::fabs(*calm.value() - 10.0) < 0.05);
    // Pencerede yeterli okuma yok: eksik (0 degil).
    EF_CHECK(!m::other_cpu_time_percent_around(r, {5'000'000, 5'060'000}).has_value());
    EF_CHECK(!m::other_cpu_time_percent_around({}, {0, 1'000'000}).has_value());
    // Pencerenin ortasinda kismi okuma: ayni maskeli son okuma kullanilir, deger bozulmaz.
    auto mixed = r;
    for (auto& x : mixed)
        if (x.t_us == 1'600'000) x.times.exempt_parts = 0b1, x.times.exempt /= 2;
    const auto skip = m::other_cpu_time_percent_around(mixed, {1'550'000, 1'650'000});
    EF_CHECK(skip.has_value() && std::fabs(*skip.value() - 10.0) < 0.05);
}

// R-E1: test baslamadan once sakinlik beklemesi.
EF_TEST(live_wait_for_calm_cpu_waits_then_starts) {
    constexpr std::uint64_t s = 10'000'000;  // 1 sn = 10^7 x 100 ns, 1 mantiksal islemci
    // Her okuma 1 sn ilerler; "baska program" payi: ilk iki tur %50, sonra %2.
    const double shares[] = {0.5, 0.5, 0.02, 0.02};
    std::size_t i = 0;
    m::CpuTimes cur;
    int pauses = 0;
    const auto read = [&]() -> std::optional<m::CpuTimes> {
        const double sh = i == 0 ? 0.0 : shares[i - 1];
        cur.total += s;
        cur.busy += static_cast<std::uint64_t>(sh * static_cast<double>(s));
        ++i;
        return cur;
    };
    const auto r = m::wait_for_calm_cpu(read, [&] { ++pauses; }, [] { return false; }, 10.0, 10);
    EF_CHECK(r.calm && !r.unreadable && !r.cancelled && r.polls == 3 && pauses == 3);
    EF_CHECK(r.last_other_pct && *r.last_other_pct < 10.0);
}

EF_TEST(live_wait_for_calm_cpu_gives_up_cancels_and_never_invents) {
    constexpr std::uint64_t s = 10'000'000;
    m::CpuTimes cur;
    const auto busy = [&]() -> std::optional<m::CpuTimes> {  // surekli %60 baska program
        cur.total += s;
        cur.busy += 6 * s / 10;
        return cur;
    };
    auto r = m::wait_for_calm_cpu(busy, [] {}, [] { return false; }, 10.0, 4);
    EF_CHECK(!r.calm && !r.unreadable && !r.cancelled && r.polls == 4 && r.last_other_pct && *r.last_other_pct > 50.0);

    cur = {};
    r = m::wait_for_calm_cpu(busy, [] {}, [] { return true; }, 10.0, 4);  // iptal: ilk beklemeden sonra doner
    EF_CHECK(r.cancelled && !r.calm && r.polls == 0);

    r = m::wait_for_calm_cpu([]() -> std::optional<m::CpuTimes> { return std::nullopt; }, [] {}, [] { return false; }, 10.0, 4);
    EF_CHECK(r.unreadable && !r.calm && r.polls == 0 && !r.last_other_pct);  // okunamayan "sakin" sayilmaz, deger uydurulmaz

    int n = 0;  // ikinci okuma basarisiz
    r = m::wait_for_calm_cpu([&]() -> std::optional<m::CpuTimes> { return n++ == 0 ? std::optional<m::CpuTimes>(m::CpuTimes{s, 0, 0, 0, 0}) : std::nullopt; },
                             [] {}, [] { return false; }, 10.0, 4);
    EF_CHECK(r.unreadable && !r.calm);
}
