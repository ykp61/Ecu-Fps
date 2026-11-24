#include "benchmark/storage/storage_rules.h"
#include "core/measure/address_pattern.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <limits>

namespace ef::benchmark::storage {

namespace {

wchar_t up(wchar_t c) { return (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - L'a' + L'A') : c; }

bool iequals_prefix(std::wstring_view s, std::wstring_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (up(s[i]) != up(prefix[i])) return false;
    return true;
}

bool icontains(std::wstring_view s, std::wstring_view needle) {
    if (needle.size() > s.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= s.size(); ++i)
        if (iequals_prefix(s.substr(i), needle)) return true;
    return false;
}

bool is_letter(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }

std::wstring slashes_to_backslash(std::wstring_view in) {
    std::wstring s(in);
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return s;
}

std::wstring_view strip_long_prefix(std::wstring_view p) {
    if (iequals_prefix(p, L"\\\\?\\")) p.remove_prefix(4);
    return p;
}

constexpr std::uint64_t kSat = std::numeric_limits<std::uint64_t>::max();
std::uint64_t sat_add(std::uint64_t a, std::uint64_t b) { return a > kSat - b ? kSat : a + b; }

}  // namespace

TargetVerdict validate_volume_root(std::wstring_view root_in) {
    if (root_in.empty()) return TargetVerdict::Empty;
    const std::wstring root = slashes_to_backslash(root_in);
    // Ham cihaz/volume ad alanlari: \\.\ , \\?\ , \??\ , \Device\ ve GLOBALROOT her yerde reddedilir.
    if (icontains(root, L"GLOBALROOT") || icontains(root, L"PhysicalDrive") || icontains(root, L"HarddiskVolume") ||
        icontains(root, L"\\Device\\"))
        return TargetVerdict::RawDevice;
    if (iequals_prefix(root, L"\\\\.\\") || iequals_prefix(root, L"\\\\?\\") || iequals_prefix(root, L"\\??\\"))
        return TargetVerdict::RawDevice;
    if (iequals_prefix(root, L"\\\\")) return TargetVerdict::Network;
    for (const wchar_t c : root)
        if (c < 0x20 || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*')
            return TargetVerdict::InvalidChars;
    if (root.size() < 2 || !is_letter(root[0]) || root[1] != L':') return TargetVerdict::Relative;
    if (root.size() == 2) return TargetVerdict::Ok;
    if (root[2] != L'\\') return TargetVerdict::Relative;  // "C:foo": surucu-goreli
    if (root.size() == 3) return TargetVerdict::Ok;
    return TargetVerdict::NotVolumeRoot;
}

const char* verdict_reason(TargetVerdict v) {
    switch (v) {
        case TargetVerdict::Ok: return "ok";
        case TargetVerdict::Empty: return "target_empty";
        case TargetVerdict::RawDevice: return "raw_device_refused";
        case TargetVerdict::Network: return "network_path_refused";
        case TargetVerdict::Relative: return "relative_path_refused";
        case TargetVerdict::NotVolumeRoot: return "not_volume_root";
        case TargetVerdict::InvalidChars: return "invalid_chars";
    }
    return "unknown";
}

std::wstring normalize_volume_root(std::wstring_view root) {
    if (validate_volume_root(root) != TargetVerdict::Ok) return {};
    std::wstring out;
    out += up(root[0]);
    out += L":\\";
    return out;
}

std::uint64_t free_space_floor(std::uint64_t total) { return std::max<std::uint64_t>(total / 20, 5 * kGiB); }

bool within_free_share(std::uint64_t file_bytes, std::uint64_t free_bytes, std::uint32_t permille) {
    if (permille == 0) return true;
    return file_bytes <= free_bytes / 1000 * permille;  // tasma yok; asagi yuvarlama korumaci
}

std::uint64_t share_required_free(std::uint64_t file_bytes, std::uint32_t permille) {
    if (permille == 0) return 0;
    // within_free_share: file <= free/1000*permille. En kucuk free = ceil(file/permille) * 1000.
    const std::uint64_t units = file_bytes / permille + (file_bytes % permille != 0 ? 1 : 0);
    return units > kSat / 1000 ? kSat : units * 1000;
}

std::uint64_t required_free_bytes(std::uint64_t file_bytes, std::uint64_t total) {
    return sat_add(file_bytes, free_space_floor(total));
}

bool free_space_sufficient(std::uint64_t free_bytes, std::uint64_t total, std::uint64_t file_bytes) {
    const std::uint64_t need = required_free_bytes(file_bytes, total);
    return need != kSat && free_bytes >= need;
}

std::wstring bench_folder_name(std::uint64_t id) {
    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring s(kFolderPrefix);
    for (int i = 15; i >= 0; --i) s += kHex[(id >> (i * 4)) & 0xF];
    return s;
}

bool is_bench_folder_name(std::wstring_view name) {
    if (name.size() != kFolderPrefix.size() + 16 || !iequals_prefix(name, kFolderPrefix)) return false;
    for (const wchar_t c : name.substr(kFolderPrefix.size()))
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    return true;
}

bool path_is_under(std::wstring_view final_path, std::wstring_view folder) {
    const std::wstring f = slashes_to_backslash(strip_long_prefix(final_path));
    std::wstring d = slashes_to_backslash(strip_long_prefix(folder));
    if (f.empty() || d.empty()) return false;
    if (d.back() != L'\\') d += L'\\';
    if (f.size() <= d.size() || !iequals_prefix(f, d)) return false;
    // ".." bileseni ile kacis: normalize edilmis son yolda olmamali.
    return f.find(L"\\..\\") == std::wstring::npos && !(f.size() >= 3 && f.compare(f.size() - 3, 3, L"\\..") == 0);
}

std::string make_marker_content(std::uint32_t pid) {
    return std::string(kMarkerMagic) + "\npid=" + std::to_string(pid) + "\n";
}

bool marker_content_valid(std::string_view content) {
    return content.size() >= kMarkerMagic.size() + 1 && content.substr(0, kMarkerMagic.size()) == kMarkerMagic &&
           content[kMarkerMagic.size()] == '\n';
}

OrphanDecision decide_orphan(std::wstring_view folder_name, bool folder_is_reparse, bool marker_valid,
                             const std::vector<OrphanEntry>& entries) {
    if (!is_bench_folder_name(folder_name)) return OrphanDecision::NotOurs;
    if (folder_is_reparse) return OrphanDecision::FolderReparse;
    bool has_marker = false;
    for (const auto& e : entries) {
        const bool known = e.name.size() == kMarkerName.size() && iequals_prefix(e.name, kMarkerName);
        const bool data = (e.name.size() == kTestFileName.size() && iequals_prefix(e.name, kTestFileName)) ||
                          (e.name.size() == kCacheFileName.size() && iequals_prefix(e.name, kCacheFileName));
        if (!known && !data) return OrphanDecision::ForeignContent;
        if (e.is_dir || e.is_reparse || e.link_count != 1) return OrphanDecision::ForeignContent;
        if (known) has_marker = true;
    }
    if (!has_marker || !marker_valid) return OrphanDecision::NoMarker;
    return OrphanDecision::Delete;
}

SpeedLabel label_of(const Profile& p) {
    return p.cache == CacheMode::Buffered ? SpeedLabel::OsCache : SpeedLabel::DiskUnbuffered;
}

const char* label_text(SpeedLabel l) { return l == SpeedLabel::OsCache ? "cache" : l == SpeedLabel::Burst ? "burst" : "disk"; }

SpeedLabel label_after_run(SpeedLabel l, bool byte_cap_hit, double seconds) noexcept {
    if (l == SpeedLabel::DiskUnbuffered && byte_cap_hit && seconds < kMinSustainedSeconds) return SpeedLabel::Burst;
    return l;
}

std::vector<Profile> default_profiles() {
    const auto u = CacheMode::Unbuffered;
    constexpr auto m1 = static_cast<std::uint32_t>(kMiB);
    constexpr auto k4 = static_cast<std::uint32_t>(4 * kKiB);
    return {
        {"seq_1m_qd1_read", Pattern::Sequential, Op::Read, m1, 1, u},
        {"seq_1m_qd8_read", Pattern::Sequential, Op::Read, m1, 8, u},
        {"rnd_4k_qd1_read", Pattern::Random, Op::Read, k4, 1, u},
        {"rnd_4k_qd4_read", Pattern::Random, Op::Read, k4, 4, u},
        {"rnd_4k_qd32_read", Pattern::Random, Op::Read, k4, 32, u},
        {"rnd_4k_qd1_write", Pattern::Random, Op::Write, k4, 1, u},
        {"rnd_4k_qd32_write", Pattern::Random, Op::Write, k4, 32, u},
        {"seq_1m_qd1_write", Pattern::Sequential, Op::Write, m1, 1, u},
        {"seq_1m_qd8_write", Pattern::Sequential, Op::Write, m1, 8, u},
        {"cache_seq_64k_read", Pattern::Sequential, Op::Read, static_cast<std::uint32_t>(64 * kKiB), 1,
         CacheMode::Buffered},
    };
}

std::uint32_t count_write_profiles(const std::vector<Profile>& ps) {
    std::uint32_t n = 0;
    for (const auto& p : ps) n += (p.op == Op::Write && p.cache == CacheMode::Unbuffered) ? 1u : 0u;
    return n;
}

Limits quick_limits() { return Limits{}; }

Limits full_limits() {
    Limits l;
    l.file_bytes = 2 * kGiB;
    l.cache_file_bytes = 32 * kMiB;
    l.write_budget_bytes = 4 * kGiB;
    l.max_free_permille = 100;
    l.profile_ms = 8000;
    l.total_ms = 150000;
    l.verify_bytes = 256 * kMiB;
    return l;
}

std::uint64_t max_host_write_bytes(const Limits& l) { return l.write_budget_bytes; }

std::uint64_t planned_measure_ms(const Limits& l, std::size_t profile_count) {
    const std::uint64_t want = static_cast<std::uint64_t>(l.profile_ms) * profile_count;
    return std::min<std::uint64_t>(want, l.total_ms);
}

std::uint64_t write_share(std::uint64_t budget, std::uint64_t used, std::uint64_t reserved,
                          std::uint32_t remaining_writers, std::uint32_t block) {
    if (remaining_writers == 0 || block == 0) return 0;
    const std::uint64_t taken = sat_add(used, reserved);
    if (taken >= budget) return 0;
    const std::uint64_t share = (budget - taken) / remaining_writers;
    return share - share % block;
}

// ---- LatencyHistogram ----
std::uint32_t LatencyHistogram::bucket_of(std::uint64_t ns) noexcept {
    if (ns < kSub) return static_cast<std::uint32_t>(ns);
    const std::uint32_t m = static_cast<std::uint32_t>(std::bit_width(ns)) - 1;  // en yuksek bit
    if (m > kMaxMagnitude) return kBuckets - 1;
    const std::uint64_t sub = ns >> (m - kSubBits);  // [64, 127]
    return (m - kSubBits + 1) * kSub + static_cast<std::uint32_t>(sub - kSub);
}

std::uint64_t LatencyHistogram::bucket_low_ns(std::uint32_t idx) noexcept {
    if (idx < kSub) return idx;
    const std::uint32_t k = idx / kSub;
    return (static_cast<std::uint64_t>(kSub) + idx % kSub) << (k - 1);
}

std::uint64_t LatencyHistogram::bucket_width_ns(std::uint32_t idx) noexcept {
    return idx < kSub ? 1 : (1ull << (idx / kSub - 1));
}

void LatencyHistogram::add_ns(std::uint64_t ns) noexcept {
    ++counts_[bucket_of(ns)];
    ++count_;
    sum_ns_ = sat_add(sum_ns_, ns);
    max_ns_ = std::max(max_ns_, ns);
}

void LatencyHistogram::merge(const LatencyHistogram& o) noexcept {
    for (std::uint32_t i = 0; i < kBuckets; ++i) counts_[i] += o.counts_[i];
    count_ += o.count_;
    sum_ns_ = sat_add(sum_ns_, o.sum_ns_);
    max_ns_ = std::max(max_ns_, o.max_ns_);
}

double LatencyHistogram::percentile_us(double p) const noexcept {
    if (count_ == 0) return 0.0;
    if (!(p >= 0.0)) p = 0.0;  // NaN dahil
    if (p > 100.0) p = 100.0;
    // Tamsayi yakin-sira: count = q*10000 + r -> ceil(bp*count/10000) = q*bp + ceil(r*bp/10000); tasma yok.
    const std::uint64_t bp = static_cast<std::uint64_t>(std::llround(p * 100.0));  // [0, 10000]
    const std::uint64_t q = count_ / 10000, r = count_ % 10000;
    std::uint64_t rank = q * bp + (r * bp + 9999) / 10000;
    rank = std::clamp<std::uint64_t>(rank, 1, count_);
    std::uint64_t cum = 0;
    for (std::uint32_t i = 0; i < kBuckets; ++i) {
        cum += counts_[i];
        if (cum >= rank) {
            const std::uint64_t w = bucket_width_ns(i);
            double mid = static_cast<double>(bucket_low_ns(i)) + (w > 1 ? static_cast<double>(w) / 2.0 : 0.0);
            mid = std::min(mid, static_cast<double>(max_ns_));  // kova ortasi gercek maksimumu asmaz
            return mid / 1000.0;
        }
    }
    return static_cast<double>(max_ns_) / 1000.0;
}

std::optional<double> fairness_cycle_cv(const std::vector<SlotLatency>& per_slot) {
    if (per_slot.empty()) return std::nullopt;
    // Alt medyan ornek sayisi (tum slotlar, az ornekliler dahil): ac kalan slot(lar) bunun altinda kalir ve c'ye girmez.
    std::vector<std::uint64_t> ns;
    ns.reserve(per_slot.size());
    for (const auto& s : per_slot) ns.push_back(s.n);
    const auto mid = ns.begin() + static_cast<std::ptrdiff_t>(ns.size() / 2);  // ust medyan (cift k: k/2+1. en kucuk)
    std::nth_element(ns.begin(), mid, ns.end());
    const std::uint64_t median_n = *mid;
    std::optional<double> best;
    for (const auto& s : per_slot) {
        if (s.n < kMinCvSamples || s.n < median_n) continue;
        const double n = static_cast<double>(s.n);
        const double mean = s.sum / n;
        if (!(mean > 0.0)) continue;
        const double var = std::max(0.0, s.sum_sq / n - mean * mean);
        const double cv = std::sqrt(var) / mean;
        if (!best || cv > *best) best = cv;
    }
    return best;
}

double slot_starvation_z(std::uint32_t slots) noexcept {
    const double k = static_cast<double>(std::max<std::uint32_t>(slots, 1));
    return std::sqrt(2.0 * std::log(k / (2.0 * kSlotFalseAlarm)));
}

SlotFairness slot_fairness(const std::vector<std::uint64_t>& per_slot_ops, std::optional<double> cycle_cv) {
    SlotFairness f;
    f.cycle_cv = cycle_cv;
    if (per_slot_ops.empty()) return f;
    f.slots = static_cast<std::uint32_t>(per_slot_ops.size());
    f.min_ops = *std::min_element(per_slot_ops.begin(), per_slot_ops.end());
    f.max_ops = *std::max_element(per_slot_ops.begin(), per_slot_ops.end());
    double sum = 0.0;
    for (const auto v : per_slot_ops) sum += static_cast<double>(v);
    f.mean_ops = sum / static_cast<double>(per_slot_ops.size());
    f.ratio = f.mean_ops > 0.0 ? static_cast<double>(f.min_ops) / f.mean_ops : 0.0;
    f.z = slot_starvation_z(f.slots);
    f.known = f.slots > 1 && f.mean_ops >= kMinOpsPerSlot && cycle_cv.has_value() && *cycle_cv >= 0.0;
    if (f.known) f.floor_ops = f.mean_ops - f.z * *cycle_cv * std::sqrt(f.mean_ops);
    f.starved = f.known && static_cast<double>(f.min_ops) < f.floor_ops && f.ratio < kMinSlotFairness;
    return f;
}

void ProfileResult::set_latency(const LatencyHistogram& h) {
    lat_samples = h.count();
    lat_mean_us = h.mean_us();
    lat_p50_us = h.percentile_us(50.0);
    lat_p95_us = h.percentile_us(95.0);
    lat_p99_us = h.percentile_us(99.0);
    lat_max_us = h.max_us();
}

LimitsVerdict validate_limits(const Limits& l) {
    if (l.file_bytes < 16 * kMiB) return LimitsVerdict::FileTooSmall;
    if (l.file_bytes % kMiB != 0) return LimitsVerdict::FileNotAligned;
    if (l.cache_file_bytes < kMiB || l.cache_file_bytes % kMiB != 0 || l.cache_file_bytes > l.file_bytes)
        return LimitsVerdict::CacheFileInvalid;
    if (l.verify_bytes % kMiB != 0 || l.verify_bytes > l.file_bytes) return LimitsVerdict::VerifyInvalid;
    if (l.write_budget_bytes < sat_add(preparation_write_bytes(l), l.verify_bytes)) return LimitsVerdict::BudgetBelowPreparation;
    if (l.profile_ms == 0 || l.total_ms < l.profile_ms) return LimitsVerdict::TimeInvalid;
    return LimitsVerdict::Ok;
}

const char* limits_reason(LimitsVerdict v) {
    switch (v) {
        case LimitsVerdict::Ok: return "ok";
        case LimitsVerdict::FileTooSmall: return "file_too_small";
        case LimitsVerdict::FileNotAligned: return "file_not_mib_aligned";
        case LimitsVerdict::CacheFileInvalid: return "cache_file_invalid";
        case LimitsVerdict::BudgetBelowPreparation: return "write_budget_below_preparation";
        case LimitsVerdict::TimeInvalid: return "time_limit_invalid";
        case LimitsVerdict::VerifyInvalid: return "verify_size_invalid";
    }
    return "unknown";
}

std::uint64_t preparation_write_bytes(const Limits& l) { return sat_add(l.file_bytes, l.cache_file_bytes); }

std::uint64_t required_free_for(const Limits& l, std::uint64_t total) {
    return std::max(required_free_bytes(preparation_write_bytes(l), total), share_required_free(l.file_bytes, l.max_free_permille));
}

IoWaitOutcome bounded_io_wait(const std::function<bool(std::uint32_t)>& wait, const std::function<void()>& cancel,
                              std::uint32_t first_ms, std::uint32_t cancel_ms) {
    if (wait(first_ms)) return IoWaitOutcome::Completed;
    cancel();
    return wait(cancel_ms) ? IoWaitOutcome::CancelledDone : IoWaitOutcome::Stuck;
}

std::uint64_t clamp_write(std::uint64_t budget, std::uint64_t used, std::uint64_t request, std::uint32_t block) {
    if (used >= budget || block == 0) return 0;
    std::uint64_t n = std::min(request, budget - used);
    return n - n % block;
}

const char* status_text(Status s) {
    switch (s) {
        case Status::Ok: return "ok";
        case Status::Partial: return "partial";
        case Status::Skipped: return "skipped";
        case Status::Cancelled: return "cancelled";
        case Status::Refused: return "refused";
        case Status::Failed: return "failed";
    }
    return "unknown";
}

void fill_random(void* data, std::size_t bytes, std::uint64_t& state) {
    if (state == 0) state = 0x9E3779B97F4A7C15ull;
    auto* p = static_cast<unsigned char*>(data);
    while (bytes > 0) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        const std::uint64_t v = state * 0x2545F4914F6CDD1Dull;
        const std::size_t n = bytes < 8 ? bytes : 8;
        std::memcpy(p, &v, n);
        p += n;
        bytes -= n;
    }
}

void stamp_sectors(void* data, std::size_t bytes, std::uint64_t counter) {
    auto* p = static_cast<unsigned char*>(data);
    for (std::size_t off = 0; off + 8 <= bytes; off += 4096) {
        const std::uint64_t v = counter * 0x100000001B3ull + off;
        std::memcpy(p + off, &v, 8);
    }
}

VerifyResult run_verify(BlockDevice& dev, std::uint64_t bytes, std::uint32_t block, std::uint64_t seed,
                        std::uint64_t* write_buf, std::uint64_t* read_buf, const std::atomic<bool>& cancel,
                        const std::function<bool()>& time_up) {
    VerifyResult r;
    if (bytes == 0 || block == 0 || block % 8 != 0 || bytes % block != 0 || !write_buf || !read_buf) {
        r.status = VerifyStatus::Invalid;
        return r;
    }
    const std::size_t words = block / 8;
    r.blocks_planned = bytes / block;
    auto stop = [&]() -> bool {
        if (cancel.load(std::memory_order_relaxed)) r.status = VerifyStatus::Cancelled;
        else if (time_up && time_up()) r.status = VerifyStatus::TimeLimit;
        else return false;
        if (r.mismatched_blocks) r.status = VerifyStatus::Mismatch;  // bulunan uyari kaybolmaz
        return true;
    };
    for (std::uint64_t b = 0; b < r.blocks_planned; ++b) {
        if (stop()) return r;
        core::measure::fill_address_pattern(write_buf, words, seed + b * words);
        if (!dev.write_block(b * block, write_buf, block)) {
            r.status = VerifyStatus::IoError;
            r.io_error = dev.last_error();
            return r;
        }
        ++r.blocks_written;
        r.bytes_written += block;
    }
    for (std::uint64_t b = 0; b < r.blocks_planned; ++b) {
        if (stop()) return r;
        std::memset(read_buf, 0, block);
        if (!dev.read_block(b * block, read_buf, block)) {
            r.status = r.mismatched_blocks ? VerifyStatus::Mismatch : VerifyStatus::IoError;
            r.io_error = dev.last_error();
            return r;
        }
        if (const auto bad = core::measure::verify_address_pattern(read_buf, words, seed + b * words)) {
            ++r.mismatched_blocks;
            if (!r.first_mismatch_offset) r.first_mismatch_offset = b * block + static_cast<std::uint64_t>(*bad) * 8;
        } else {
            ++r.blocks_verified;
        }
    }
    r.status = r.mismatched_blocks ? VerifyStatus::Mismatch : VerifyStatus::Ok;
    return r;
}

}  // namespace ef::benchmark::storage
