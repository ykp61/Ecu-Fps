#include "core/metrics/live.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

#include "core/metrics/process_engine.h"
#include <map>
#include <tuple>

namespace ef::core::metrics {

using contracts::Measurement;
using contracts::MissingReason;
using contracts::Unit;

std::optional<double> clamp_pct(double v) noexcept {
    if (!std::isfinite(v)) return std::nullopt;
    return std::clamp(v, 0.0, 100.0);
}

Measurement percent(double v) noexcept {
    const auto c = clamp_pct(v);
    if (!c) return Measurement::missing(Unit::Percent, MissingReason::Invalid);
    return Measurement::of(*c, Unit::Percent);
}

float ring_fraction(const Measurement& m) noexcept {
    const auto v = m.value();
    if (!v || m.unit() != Unit::Percent) return 0.0f;
    const auto c = clamp_pct(*v);
    if (!c) return 0.0f;
    return static_cast<float>(*c / 100.0);
}

Measurement ram_percent(std::uint64_t used, std::uint64_t total) noexcept {
    if (total == 0 || used > total) return Measurement::missing(Unit::Percent, MissingReason::Invalid);
    return percent(100.0 * static_cast<double>(used) / static_cast<double>(total));
}

Measurement other_process_percent(const Measurement& total, const Measurement& target) noexcept {
    // Eksik taraf varsa cikarma yapilmaz: "bilinmiyor" 0'a donusmemeli.
    if (!total.has_value() || !target.has_value()) return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    if (total.unit() != Unit::Percent || target.unit() != Unit::Percent)
        return Measurement::missing(Unit::Percent, MissingReason::Invalid);
    const double t = std::clamp(*total.value(), 0.0, 100.0);
    const double g = std::clamp(*target.value(), 0.0, 100.0);
    return percent(std::max(0.0, t - g));
}

Measurement other_cpu_time_percent(const CpuTimes& begin, const CpuTimes& end) noexcept {
    if (end.total <= begin.total || end.busy < begin.busy || end.own < begin.own || end.exempt < begin.exempt ||
        end.exempt_parts != begin.exempt_parts)
        return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    const std::uint64_t total = end.total - begin.total, busy = end.busy - begin.busy, own = end.own - begin.own;
    const std::uint64_t exempt = end.exempt - begin.exempt;
    const std::uint64_t not_other = own + exempt;
    const std::uint64_t other = busy > not_other ? busy - not_other : 0;
    return percent(100.0 * static_cast<double>(other) / static_cast<double>(total));
}

Measurement other_cpu_time_percent_in(const std::vector<CpuTimeReading>& readings, const std::vector<TimeWindow>& windows) noexcept {
    CpuTimes sum;  // pencere farklarinin toplami
    for (const auto& w : windows) {
        if (w.end_us <= w.begin_us) continue;
        const CpuTimeReading* first = nullptr;
        const CpuTimeReading* last = nullptr;
        for (const auto& r : readings) {
            if (r.t_us < w.begin_us || r.t_us > w.end_us) continue;
            if (!first) first = &r;
            if (r.times.exempt_parts == first->times.exempt_parts) last = &r;  // kismi okuma ile karsilastirilmaz
        }
        if (!first || last == first) continue;
        const auto& a = first->times;
        const auto& b = last->times;
        if (b.total <= a.total || b.busy < a.busy || b.own < a.own || b.exempt < a.exempt) continue;  // sayac geri gitti
        sum.total += b.total - a.total;
        sum.busy += b.busy - a.busy;
        sum.own += b.own - a.own;
        sum.exempt += b.exempt - a.exempt;
    }
    return other_cpu_time_percent(CpuTimes{}, sum);
}

CalmWaitResult wait_for_calm_cpu(const std::function<std::optional<CpuTimes>()>& read, const std::function<void()>& pause,
                                 const std::function<bool()>& cancelled, double max_pct, std::uint32_t max_polls) {
    CalmWaitResult r;
    auto prev = read();
    if (!prev) {
        r.unreadable = true;
        return r;
    }
    while (r.polls < max_polls) {
        pause();
        if (cancelled()) {
            r.cancelled = true;
            return r;
        }
        const auto now = read();
        if (!now) {
            r.unreadable = true;
            return r;
        }
        ++r.polls;
        const auto other = other_cpu_time_percent(*prev, *now);
        if (!other.has_value()) {  // sayac ilerlemedi / geri gitti: bu tur sayilmaz, yeniden dene
            prev = now;
            continue;
        }
        r.last_other_pct = *other.value();
        if (*r.last_other_pct < max_pct) {
            r.calm = true;
            return r;
        }
        prev = now;
    }
    return r;
}

CpuTimeWindow::CpuTimeWindow(std::int64_t span_us, std::int64_t min_span_us) noexcept
    : span_us_(std::max<std::int64_t>(1, span_us)), min_span_us_(std::clamp<std::int64_t>(min_span_us, 1, std::max<std::int64_t>(1, span_us))) {}

Measurement CpuTimeWindow::push(std::int64_t t_us, const std::optional<CpuTimes>& times) {
    if (!times) {
        history_.clear();
        return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    }
    if (!history_.empty()) {
        const auto& last = history_.back();
        // Saat ya da sayac geri gitti (surec yeniden acildi vb.): eski okumalar bu okumayla karsilastirilmaz.
        if (t_us <= last.t_us || times->total < last.times.total || times->busy < last.times.busy ||
            times->own < last.times.own || times->exempt < last.times.exempt ||
            times->exempt_parts != last.times.exempt_parts)  // B3 inceleme: kismi muaf okuma ile karsilastirilmaz
            history_.clear();
    }
    history_.push_back({t_us, *times});
    // Basi, pencere suresinden eski olmayan en eski okuma olacak sekilde kaydir (en az iki okuma kalir).
    std::size_t drop = 0;
    while (history_.size() - drop > 2 && t_us - history_[drop + 1].t_us >= span_us_) ++drop;
    if (drop) history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
    const auto& first = history_.front();
    if (history_.size() < 2 || t_us - first.t_us < min_span_us_) return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    return other_cpu_time_percent(first.times, *times);
}

Measurement other_cpu_time_percent_around(const std::vector<CpuTimeReading>& readings, TimeWindow window,
                                          std::int64_t min_span_us) noexcept {
    min_span_us = std::max<std::int64_t>(1, min_span_us);
    if (window.end_us < window.begin_us) return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    if (window.end_us - window.begin_us < min_span_us) {
        const std::int64_t mid = window.begin_us + (window.end_us - window.begin_us) / 2;
        window.begin_us = mid - min_span_us / 2;
        window.end_us = window.begin_us + min_span_us;
    }
    const CpuTimeReading* first = nullptr;
    const CpuTimeReading* last = nullptr;
    for (const auto& r : readings) {
        if (r.t_us < window.begin_us || r.t_us > window.end_us) continue;
        if (!first) first = &r;
        if (r.times.exempt_parts == first->times.exempt_parts) last = &r;
    }
    if (!first || last == first || last->t_us - first->t_us < min_span_us / 2)
        return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    return other_cpu_time_percent(first->times, last->times);
}

bool is_processor_total_instance(std::string_view instance) noexcept {
    constexpr std::string_view kTotal = "_Total";
    return instance.size() >= kTotal.size() && instance.substr(instance.size() - kTotal.size()) == kTotal;
}

Measurement max_core_percent(const std::vector<CoreCpuSample>& samples) noexcept {
    double best = 0;
    bool any = false;
    for (const auto& s : samples) {
        if (is_processor_total_instance(s.instance) || !std::isfinite(s.value)) continue;
        any = true;
        best = std::max(best, std::clamp(s.value, 0.0, 100.0));
    }
    if (!any) return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    return percent(best);
}

Measurement mean_measured(const std::vector<Measurement>& values) noexcept {
    std::optional<Unit> unit;
    double sum = 0;
    std::size_t n = 0;
    for (const auto& m : values) {
        if (!m.has_value()) continue;
        if (!unit) unit = m.unit();
        else if (*unit != m.unit()) return Measurement::missing(m.unit(), MissingReason::Invalid);
        double v = *m.value();
        if (!std::isfinite(v)) continue;
        if (m.unit() == Unit::Percent) v = std::clamp(v, 0.0, 100.0);
        sum += v;
        ++n;
    }
    if (n == 0) return Measurement::missing(unit.value_or(Unit::Percent), MissingReason::NotSampled);
    return Measurement::of(sum / static_cast<double>(n), *unit);
}

namespace {

// "key_" onekini bekler, sonraki '_' ye kadar olan parcayi dondurur.
bool take_field(std::string_view& s, std::string_view key, std::string_view& out) noexcept {
    if (s.substr(0, key.size()) != key) return false;
    s.remove_prefix(key.size());
    const auto p = s.find('_');
    out = s.substr(0, p);
    s.remove_prefix(p == std::string_view::npos ? s.size() : p + 1);
    return !out.empty();
}

bool to_u32(std::string_view s, std::uint32_t& v) noexcept {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

}  // namespace

std::optional<GpuEngineId> parse_gpu_engine_instance(std::string_view name) noexcept {
    GpuEngineId id;
    std::string_view s = name, f;
    if (!take_field(s, "pid_", f) || !to_u32(f, id.pid)) return std::nullopt;
    // luid iki parcadir: 0xHHHHHHHH_0xLLLLLLLL
    std::string_view hi, lo;
    if (!take_field(s, "luid_", hi)) return std::nullopt;
    lo = s.substr(0, s.find('_'));
    if (hi.substr(0, 2) != "0x" || lo.substr(0, 2) != "0x" || lo.size() < 3) return std::nullopt;
    s.remove_prefix(std::min(s.size(), lo.size() + 1));
    id.luid = std::string(hi) + "_" + std::string(lo);
    if (!take_field(s, "phys_", f) || !to_u32(f, id.phys)) return std::nullopt;
    if (!take_field(s, "eng_", f) || !to_u32(f, id.eng)) return std::nullopt;
    // engtype son alandir ve '_' icerebilir; yoksa bos kalir.
    if (s.substr(0, 8) == "engtype_") id.engtype = std::string(s.substr(8));
    return id;
}

Measurement aggregate_gpu(const std::vector<GpuEngineSample>& samples) {
    // (luid, phys, eng) -> surec toplami
    std::map<std::tuple<std::string, std::uint32_t, std::uint32_t>, double> engines;
    for (const auto& smp : samples) {
        if (!std::isfinite(smp.utilization)) continue;
        const auto id = parse_gpu_engine_instance(smp.instance);
        if (!id) continue;
        engines[{id->luid, id->phys, id->eng}] += std::max(0.0, smp.utilization);
    }
    if (engines.empty()) return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    std::map<std::string, double> adapters;
    for (const auto& [key, sum] : engines) {
        double& a = adapters[std::get<0>(key)];
        a = std::max(a, std::min(sum, 100.0));
    }
    double overall = 0;
    for (const auto& [luid, v] : adapters) overall = std::max(overall, v);
    return percent(overall);
}

std::vector<GpuAdapterUsage> gpu_usage_by_adapter(const std::vector<GpuEngineSample>& samples) {
    std::map<std::tuple<std::string, std::uint32_t, std::uint32_t>, double> engines;  // (envanter luid, phys, eng)
    for (const auto& smp : samples) {
        if (!std::isfinite(smp.utilization)) continue;
        const auto id = parse_gpu_engine_instance(smp.instance);
        if (!id) continue;
        auto luid = engine_instance_luid(smp.instance);
        if (luid.empty()) continue;
        engines[{std::move(luid), id->phys, id->eng}] += std::max(0.0, smp.utilization);
    }
    std::map<std::string, double> adapters;
    for (const auto& [key, sum] : engines) {
        double& a = adapters[std::get<0>(key)];
        a = std::max(a, std::min(sum, 100.0));
    }
    std::vector<GpuAdapterUsage> out;
    out.reserve(adapters.size());
    for (const auto& [luid, v] : adapters) out.push_back({luid, v});
    return out;
}

namespace {
std::string lower_ascii(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!o.empty() && o.back() == ' ') o.pop_back();
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    return o;
}
bool same_card_name(std::string_view a, std::string_view b) {
    const auto x = lower_ascii(a), y = lower_ascii(b);
    if (x.empty() || y.empty()) return false;
    if (x == y) return true;
    const auto ends = [](const std::string& l, const std::string& s) {  // s, l'nin sonu ve sozcuk sinirinda
        return l.size() > s.size() && l.compare(l.size() - s.size(), s.size(), s) == 0 && l[l.size() - s.size() - 1] == ' ';
    };
    return ends(x, y) || ends(y, x);
}
}  // namespace

LiveGpuPick pick_live_gpu(const std::vector<LiveGpuCard>& cards, const std::vector<GpuAdapterUsage>& usage,
                          std::string_view temp_device, bool temp_present) {
    LiveGpuPick out;
    // Sicakligi ureten kart: ad eslesmesi. Ad yoksa yalniz tek kartli sistemde o karta aittir.
    std::optional<std::size_t> temp_owner;
    if (temp_present) {
        if (!temp_device.empty()) {
            for (std::size_t i = 0; i < cards.size() && !temp_owner; ++i)
                if (same_card_name(cards[i].name, temp_device)) temp_owner = i;
        } else if (cards.size() == 1) {
            temp_owner = 0;
        }
    }
    // En yuklu bagdastirici (aggregate_gpu ile ayni deger).
    const GpuAdapterUsage* busiest = nullptr;
    for (const auto& u : usage)
        if (std::isfinite(u.pct) && (!busiest || u.pct > busiest->pct)) busiest = &u;
    const auto card_of_luid = [&](const std::string& luid) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < cards.size(); ++i)
            if (!cards[i].luid.empty() && cards[i].luid == luid) return i;
        return std::nullopt;
    };
    if (busiest) {
        auto owner = card_of_luid(busiest->luid);
        if (!owner && cards.size() == 1 && cards[0].luid.empty()) owner = 0;  // tek kart, LUID bilinmiyor
        // Esitlikte (0,5 puan) sicakligin kartini sec: bosta iki kart arasinda ad titremez.
        if (temp_owner && owner != temp_owner && !cards[*temp_owner].luid.empty()) {
            for (const auto& u : usage)
                if (u.luid == cards[*temp_owner].luid && std::isfinite(u.pct) && u.pct >= busiest->pct - 0.5) owner = temp_owner;
        }
        out.card = owner;  // cozulemezse hicbir kart adlandirilmaz (yanlis karta deger yazilmaz)
    } else if (!cards.empty()) {
        out.card = temp_owner ? temp_owner : std::optional<std::size_t>(0);
    }
    if (temp_present) {
        if (cards.empty()) out.show_temp = true;  // kart adi yok: deger hicbir karta mal edilmez
        else out.show_temp = temp_owner && out.card == temp_owner;
    }
    return out;
}

Measurement aggregate_disk(const std::vector<DiskIdleSample>& samples) {
    std::optional<double> busiest;
    for (const auto& smp : samples) {
        if (smp.instance == "_Total") continue;
        const auto idle = clamp_pct(smp.idle_pct);
        if (!idle) continue;
        const double busy = 100.0 - *idle;
        busiest = busiest ? std::max(*busiest, busy) : busy;
    }
    if (!busiest) return Measurement::missing(Unit::Percent, MissingReason::NotSampled);
    return percent(*busiest);
}

Measurement aggregate_disk_rate(const std::vector<DiskRateSample>& samples) {
    double sum = 0;
    bool any = false;
    for (const auto& smp : samples) {
        if (smp.instance == "_Total") continue;
        if (!std::isfinite(smp.bytes_per_sec) || smp.bytes_per_sec < 0.0) continue;
        sum += smp.bytes_per_sec;
        any = true;
    }
    if (!any) return Measurement::missing(Unit::MBps, MissingReason::NotSampled);
    const double mbs = sum / 1e6;
    if (!std::isfinite(mbs)) return Measurement::missing(Unit::MBps, MissingReason::Invalid);
    return Measurement::of(mbs, Unit::MBps);
}

std::optional<double> octet_rate_mbps(std::uint64_t prev, std::uint64_t cur, double seconds) noexcept {
    if (!(seconds > 0.0) || !std::isfinite(seconds) || cur < prev) return std::nullopt;
    const double r = static_cast<double>(cur - prev) * 8.0 / seconds / 1e6;
    if (!std::isfinite(r)) return std::nullopt;
    return r;
}

std::optional<double> link_fraction(double down_mbps, double link_mbps) noexcept {
    if (!std::isfinite(link_mbps) || !(link_mbps > 0.0) || !std::isfinite(down_mbps)) return std::nullopt;
    return std::clamp(down_mbps / link_mbps, 0.0, 1.0);
}

std::optional<double> mbit_to_mbyte_per_s(double mbit_per_s) noexcept {
    if (!std::isfinite(mbit_per_s) || mbit_per_s < 0.0) return std::nullopt;
    return mbit_per_s / 8.0;
}

NetRates net_rates(const std::optional<NetCounters>& prev, const NetCounters& cur) noexcept {
    NetRates r;
    if (cur.link_bps > 0 && cur.link_bps != ~0ull)
        r.link = Measurement::of(static_cast<double>(cur.link_bps) / 1e6, Unit::Mbps);
    else
        r.link = Measurement::missing(Unit::Mbps, MissingReason::NotSupported);
    if (!prev || prev->if_index != cur.if_index || cur.mono_ms <= prev->mono_ms) return r;
    const double secs = static_cast<double>(cur.mono_ms - prev->mono_ms) / 1000.0;
    if (const auto d = octet_rate_mbps(prev->in_octets, cur.in_octets, secs)) r.down = Measurement::of(*d, Unit::Mbps);
    if (const auto u = octet_rate_mbps(prev->out_octets, cur.out_octets, secs)) r.up = Measurement::of(*u, Unit::Mbps);
    return r;
}

Smoother::Smoother(double alpha) noexcept
    : alpha_(std::isfinite(alpha) ? std::clamp(alpha, 0.01, 1.0) : 0.5) {}

Measurement Smoother::push(const Measurement& m) noexcept {
    const auto v = m.value();
    if (!v || !std::isfinite(*v)) {
        y_.reset();
        return v ? Measurement::missing(m.unit(), MissingReason::Invalid) : m;
    }
    y_ = y_ ? *y_ + alpha_ * (*v - *y_) : *v;
    return Measurement::of(*y_, m.unit());
}

}  // namespace ef::core::metrics
