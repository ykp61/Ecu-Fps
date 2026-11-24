#include "platform/windows/metrics/pdh_sampler.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "platform/windows/win_env.h"

namespace ef::platform::metrics {

using contracts::Measurement;
using contracts::MissingReason;
using contracts::Unit;
namespace cm = core::metrics;

namespace {

constexpr std::int64_t kGpuReexpandMs = 30'000;  // UI-09 olcumu: 10 sn'de yeniden genisletme gorunur CPU'yu artiriyordu  // surec ornekleri gelip gider
constexpr std::int64_t kVolumeRefreshMs = 10'000;
constexpr DWORD kFmt = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;  // kirpma live.cpp'de yapilir

const wchar_t* const kCpuUtility = L"\\Processor Information(_Total)\\% Processor Utility";
const wchar_t* const kCpuTime = L"\\Processor(_Total)\\% Processor Time";
// Uyumluluk B3 (bosluk 3): cekirdek doygunlugu SURE tabanli sayactan okunur (Utility frekansla olceklenir).
const wchar_t* const kCpuTimeCoreInfo = L"\\Processor Information(*)\\% Processor Time";  // >64 islemci gruplari dahil
const wchar_t* const kCpuTimeCore = L"\\Processor(*)\\% Processor Time";
const wchar_t* const kGpuEngine = L"\\GPU Engine(*)\\Utilization Percentage";
const wchar_t* const kDiskIdle = L"\\PhysicalDisk(*)\\% Idle Time";
const wchar_t* const kDiskQueue = L"\\PhysicalDisk(*)\\Current Disk Queue Length";
const wchar_t* const kDiskIo = L"\\PhysicalDisk(*)\\Avg. Disk sec/Transfer";
const wchar_t* const kCommitUsed = L"\\Memory\\% Committed Bytes In Use";
const wchar_t* const kHardFaults = L"\\Memory\\Pages Input/sec";
const wchar_t* const kDiskRead = L"\\PhysicalDisk(*)\\Disk Read Bytes/sec";
const wchar_t* const kDiskWrite = L"\\PhysicalDisk(*)\\Disk Write Bytes/sec";

std::int64_t now_ms() noexcept { return static_cast<std::int64_t>(GetTickCount64()); }

PDH_HCOUNTER add_counter(PDH_HQUERY q, const wchar_t* path) noexcept {
    PDH_HCOUNTER c = nullptr;
    if (PdhAddEnglishCounterW(q, path, 0, &c) != ERROR_SUCCESS) return nullptr;
    return c;
}

bool valid_status(DWORD s) noexcept { return s == PDH_CSTATUS_VALID_DATA || s == PDH_CSTATUS_NEW_DATA; }

// Ornek adlari ASCII'dir (pid/luid/disk numarasi); ASCII disi karakter '?' olur. Kapasite yeniden kullanilir.
void narrow_into(const wchar_t* w, std::string& out) {
    out.clear();
    for (; w && *w; ++w) out.push_back(*w < 0x80 ? static_cast<char>(*w) : '?');
}

// Joker sayacin bicimlenmis dizisini okur. Basarisizsa PDH durum kodunu dondurur.
template <typename Fn>
PDH_STATUS read_array(PDH_HCOUNTER c, std::vector<unsigned char>& buf, Fn&& each) {
    DWORD bytes = static_cast<DWORD>(buf.size());
    DWORD count = 0;
    PDH_STATUS st = PDH_MORE_DATA;
    for (int attempt = 0; attempt < 3 && st == PDH_MORE_DATA; ++attempt) {
        bytes = static_cast<DWORD>(buf.size());
        st = PdhGetFormattedCounterArrayW(c, kFmt, &bytes, &count,
                                          buf.empty() ? nullptr
                                                      : reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buf.data()));
        if (st == PDH_MORE_DATA) buf.resize(static_cast<std::size_t>(bytes) + 1024);
    }
    if (st != ERROR_SUCCESS) return st;
    const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    for (DWORD i = 0; i < count; ++i) {
        if (!valid_status(items[i].FmtValue.CStatus)) continue;
        each(items[i].szName, items[i].FmtValue.doubleValue);
    }
    return st;
}

MissingReason reason_for(PDH_STATUS st) noexcept {
    return (st == PDH_INVALID_DATA || st == PDH_CSTATUS_INVALID_DATA || st == PDH_NO_DATA)
               ? MissingReason::NotSampled
               : MissingReason::SourceError;
}

}  // namespace

PdhSampler::~PdhSampler() { close(); }

bool PdhSampler::open() {
    close();
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return false;
    query_ = q;
    cpu_ = add_counter(q, kCpuUtility);
    if (!cpu_) cpu_ = add_counter(q, kCpuTime);
    if (!cpu_) {
        close();
        return false;
    }
    gpu_ = add_counter(q, kGpuEngine);
    disk_ = add_counter(q, kDiskIdle);
    // Drop raporu sayaclari opsiyoneldir; yoksa deger NotSupported kalir (0 degil).
    cpu_cores_ = add_counter(q, kCpuTimeCoreInfo);
    if (!cpu_cores_) cpu_cores_ = add_counter(q, kCpuTimeCore);
    disk_queue_ = add_counter(q, kDiskQueue);
    disk_io_ = add_counter(q, kDiskIo);
    commit_used_ = add_counter(q, kCommitUsed);
    hard_faults_ = add_counter(q, kHardFaults);
    disk_read_ = add_counter(q, kDiskRead);
    disk_write_ = add_counter(q, kDiskWrite);
    gpu_expanded_ms_ = now_ms();
    return true;
}

void PdhSampler::close() {
    if (query_) PdhCloseQuery(static_cast<PDH_HQUERY>(query_));  // sayaclari da kapatir
    query_ = cpu_ = gpu_ = gpu_pending_ = disk_ = nullptr;
    cpu_cores_ = disk_queue_ = disk_io_ = commit_used_ = hard_faults_ = nullptr;
    disk_read_ = disk_write_ = nullptr;
    collects_ = pending_collects_ = 0;
    gpu_reexpansions_ = 0;
    volumes_ms_ = -1;
    // Kapanista tamponlarin kapasitesi de birakilir (pencere kapali / gorunmezken bellekte tutulmaz).
    std::vector<core::metrics::VolumeUse>().swap(volumes_);
    std::vector<unsigned char>().swap(buf_);
    std::vector<core::metrics::GpuEngineSample>().swap(gpu_tmp_);
    std::vector<core::metrics::DiskIdleSample>().swap(disk_tmp_);
    std::vector<core::metrics::DiskRateSample>().swap(disk_rate_tmp_);
    std::vector<core::metrics::CoreCpuSample>().swap(core_tmp_);
}

void PdhSampler::maybe_reexpand_gpu(std::int64_t now) {
    if (!gpu_ || gpu_pending_ || now - gpu_expanded_ms_ < kGpuReexpandMs) return;
    // Yeni sayac eskisiyle birlikte toplanir; iki ornek alinca eskisinin yerine gecer (bosluk yok).
    gpu_pending_ = add_counter(static_cast<PDH_HQUERY>(query_), kGpuEngine);
    pending_collects_ = 0;
    if (!gpu_pending_) gpu_expanded_ms_ = now;  // sonra tekrar dene; eski sayac surer
}

Measurement PdhSampler::read_gpu() {
    if (!gpu_) return Measurement::missing(Unit::Percent, MissingReason::NotSupported);
    if (gpu_pending_ && pending_collects_ >= 2) {
        PdhRemoveCounter(static_cast<PDH_HCOUNTER>(gpu_));
        gpu_ = gpu_pending_;
        gpu_pending_ = nullptr;
        gpu_expanded_ms_ = now_ms();
        ++gpu_reexpansions_;
    }
    std::size_t n = 0;
    const PDH_STATUS st = read_array(static_cast<PDH_HCOUNTER>(gpu_), buf_, [&](const wchar_t* name, double v) {
        if (std::wcsncmp(name, L"pid_", 4) != 0) return;
        if (gpu_tmp_.size() <= n) gpu_tmp_.emplace_back();
        narrow_into(name, gpu_tmp_[n].instance);
        gpu_tmp_[n].utilization = v;
        ++n;
    });
    if (st != ERROR_SUCCESS) return Measurement::missing(Unit::Percent, reason_for(st));
    gpu_tmp_.resize(n);  // ilk n eleman bu ornege ait; dize kapasiteleri sonraki ornekte yeniden kullanilir
    return cm::aggregate_gpu(gpu_tmp_);
}

Measurement PdhSampler::read_disk() {
    if (!disk_) return Measurement::missing(Unit::Percent, MissingReason::NotSupported);
    disk_tmp_.clear();
    const PDH_STATUS st = read_array(static_cast<PDH_HCOUNTER>(disk_), buf_, [&](const wchar_t* name, double v) {
        cm::DiskIdleSample d;
        narrow_into(name, d.instance);
        d.idle_pct = v;
        disk_tmp_.push_back(std::move(d));
    });
    if (st != ERROR_SUCCESS) return Measurement::missing(Unit::Percent, reason_for(st));
    return cm::aggregate_disk(disk_tmp_);
}

Measurement PdhSampler::read_cpu_max_core() {
    if (!cpu_cores_) return Measurement::missing(Unit::Percent, MissingReason::NotSupported);
    std::size_t n = 0;
    const PDH_STATUS st = read_array(static_cast<PDH_HCOUNTER>(cpu_cores_), buf_, [&](const wchar_t* name, double v) {
        if (core_tmp_.size() <= n) core_tmp_.emplace_back();
        narrow_into(name, core_tmp_[n].instance);
        core_tmp_[n].value = v;
        ++n;
    });
    if (st != ERROR_SUCCESS) return Measurement::missing(Unit::Percent, reason_for(st));
    core_tmp_.resize(n);
    return cm::max_core_percent(core_tmp_);  // "_Total" / "<grup>,_Total" toplamlari cekirdek sayilmaz
}

Measurement PdhSampler::read_disk_queue() {
    if (!disk_queue_) return Measurement::missing(Unit::Count, MissingReason::NotSupported);
    double best = 0;
    bool any = false;
    const PDH_STATUS st = read_array(static_cast<PDH_HCOUNTER>(disk_queue_), buf_, [&](const wchar_t* name, double v) {
        if (std::wcscmp(name, L"_Total") == 0) return;
        if (!std::isfinite(v) || v < 0.0) return;
        any = true;
        best = std::max(best, v);
    });
    if (st != ERROR_SUCCESS) return Measurement::missing(Unit::Count, reason_for(st));
    if (!any) return Measurement::missing(Unit::Count, MissingReason::NotSampled);
    return Measurement::of(best, Unit::Count);
}

Measurement PdhSampler::read_disk_io_ms() {
    if (!disk_io_) return Measurement::missing(Unit::Milliseconds, MissingReason::NotSupported);
    double best = 0;
    bool any = false;
    const PDH_STATUS st = read_array(static_cast<PDH_HCOUNTER>(disk_io_), buf_, [&](const wchar_t* name, double v) {
        if (std::wcscmp(name, L"_Total") == 0) return;
        if (!std::isfinite(v) || v < 0.0) return;
        any = true;
        best = std::max(best, v * 1000.0);  // sayac saniye/aktarim; rapor ms bekler
    });
    if (st != ERROR_SUCCESS) return Measurement::missing(Unit::Milliseconds, reason_for(st));
    if (!any) return Measurement::missing(Unit::Milliseconds, MissingReason::NotSampled);
    if (!std::isfinite(best)) return Measurement::missing(Unit::Milliseconds, MissingReason::Invalid);
    return Measurement::of(best, Unit::Milliseconds);
}

Measurement PdhSampler::read_disk_rate(void* counter) {
    if (!counter) return Measurement::missing(Unit::MBps, MissingReason::NotSupported);
    disk_rate_tmp_.clear();
    const PDH_STATUS st = read_array(static_cast<PDH_HCOUNTER>(counter), buf_, [&](const wchar_t* name, double v) {
        cm::DiskRateSample d;
        narrow_into(name, d.instance);
        d.bytes_per_sec = v;
        disk_rate_tmp_.push_back(std::move(d));
    });
    if (st != ERROR_SUCCESS) return Measurement::missing(Unit::MBps, reason_for(st));
    return cm::aggregate_disk_rate(disk_rate_tmp_);
}

void PdhSampler::refresh_volumes(std::int64_t now) {
    if (volumes_ms_ >= 0 && now - volumes_ms_ < kVolumeRefreshMs) return;
    volumes_ms_ = now;
    volumes_.clear();
    wchar_t drives[256] = {};
    const DWORD n = GetLogicalDriveStringsW(255, drives);
    if (n == 0 || n > 255) return;
    DWORD old_mode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &old_mode);  // hazir olmayan surucude pencere acilmasin
    for (const wchar_t* p = drives; *p; p += std::wcslen(p) + 1) {
        if (GetDriveTypeW(p) != DRIVE_FIXED) continue;  // cikarilabilir/ag: uyandirma ve gecikme yok
        cm::VolumeUse v;
        v.root = to_utf8(p);
        ULARGE_INTEGER freeb{}, total{};
        if (GetDiskFreeSpaceExW(p, &freeb, &total, nullptr)) {
            v.total_gib = total.QuadPart >> 30;
            v.free_gib = freeb.QuadPart >> 30;
        }
        volumes_.push_back(std::move(v));
    }
    SetThreadErrorMode(old_mode, nullptr);
}

cm::LiveSnapshot PdhSampler::sample() {
    cm::LiveSnapshot s;
    const std::int64_t now = now_ms();
    s.mono_ms = now;

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0) {
        const std::uint64_t used = ms.ullTotalPhys - ms.ullAvailPhys;
        s.ram_total_mib = ms.ullTotalPhys >> 20;
        s.ram_used_mib = used >> 20;
        s.ram_pct = cm::ram_percent(used, ms.ullTotalPhys);
    } else {
        s.ram_pct = Measurement::missing(Unit::Percent, MissingReason::SourceError);
    }
    refresh_volumes(now);
    s.volumes = volumes_;

    if (!query_) {
        s.cpu_pct = s.gpu_pct = s.disk_pct = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
        return s;
    }
    maybe_reexpand_gpu(now);
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(query_)) != ERROR_SUCCESS) {
        s.cpu_pct = s.gpu_pct = s.disk_pct = Measurement::missing(Unit::Percent, MissingReason::SourceError);
        s.disk_read_mbs = s.disk_write_mbs = Measurement::missing(Unit::MBps, MissingReason::SourceError);
        return s;
    }
    if (collects_ < 2) ++collects_;
    if (gpu_pending_ && pending_collects_ < 2) ++pending_collects_;
    if (collects_ < 2) {  // oran sayaclari ikinci ornekte anlam kazanir
        s.cpu_pct = s.disk_pct = Measurement::missing(Unit::Percent, MissingReason::NotSampled);
        s.gpu_pct = gpu_ ? s.cpu_pct : Measurement::missing(Unit::Percent, MissingReason::NotSupported);
        return s;
    }

    PDH_FMT_COUNTERVALUE v{};
    const PDH_STATUS st = PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(cpu_), kFmt, nullptr, &v);
    if (st == ERROR_SUCCESS && valid_status(v.CStatus))
        s.cpu_pct = cm::percent(v.doubleValue);
    else
        s.cpu_pct = Measurement::missing(Unit::Percent, reason_for(st == ERROR_SUCCESS ? static_cast<PDH_STATUS>(v.CStatus) : st));
    s.gpu_pct = read_gpu();
    // EF-X24.02: ayni ornekten bagdastirici kimlikli kullanim (gpu_tmp_ yalniz basarili okumada bu ornege aittir).
    if (s.gpu_pct.has_value()) s.gpu_adapters = cm::gpu_usage_by_adapter(gpu_tmp_);
    s.disk_pct = read_disk();
    s.cpu_max_core_pct = read_cpu_max_core();
    s.disk_queue_len = read_disk_queue();
    s.disk_io_ms = read_disk_io_ms();
    s.disk_read_mbs = read_disk_rate(disk_read_);
    s.disk_write_mbs = read_disk_rate(disk_write_);
    auto simple = [&](Handle c, Unit u) -> Measurement {
        if (!c) return Measurement::missing(u, MissingReason::NotSupported);
        PDH_FMT_COUNTERVALUE sv{};
        const PDH_STATUS st = PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(c), kFmt, nullptr, &sv);
        if (st != ERROR_SUCCESS || !valid_status(sv.CStatus))
            return Measurement::missing(u, reason_for(st == ERROR_SUCCESS ? static_cast<PDH_STATUS>(sv.CStatus) : st));
        const double v = sv.doubleValue;
        if (!std::isfinite(v) || v < 0.0) return Measurement::missing(u, MissingReason::Invalid);
        return Measurement::of(u == Unit::Percent ? std::clamp(v, 0.0, 100.0) : v, u);
    };
    s.commit_used_pct = simple(commit_used_, Unit::Percent);
    s.hard_fault_rate = simple(hard_faults_, Unit::Count);
    return s;
}

}  // namespace ef::platform::metrics
