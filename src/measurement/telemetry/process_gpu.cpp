#include "measurement/telemetry/process_gpu.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <string>
#include <vector>

#include "core/metrics/process_engine.h"

namespace ef::measurement {

namespace {
// Joker ornekli sayacin gecerli orneklerini (ad, deger) olarak verir. Adlar ASCII'dir (pid/luid/eng alanlari).
std::vector<core::metrics::EngineInstance> named_items(PDH_HCOUNTER c, DWORD fmt) {
    std::vector<core::metrics::EngineInstance> out;
    DWORD bytes = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(c, fmt, &bytes, &count, nullptr) != PDH_MORE_DATA || bytes == 0) return out;
    std::vector<unsigned char> buf(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(c, fmt, &bytes, &count, items) != ERROR_SUCCESS) return out;
    for (DWORD i = 0; i < count; ++i) {
        const auto& v = items[i].FmtValue;
        if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        core::metrics::EngineInstance e;
        for (const wchar_t* p = items[i].szName; p && *p; ++p) e.name.push_back(*p < 0x80 ? static_cast<char>(*p) : '?');
        e.pct = v.doubleValue;
        out.push_back(std::move(e));
    }
    return out;
}
// Ornek adlarindan "luid_..._phys_N" parcasi -> deger.
std::vector<std::pair<std::wstring, double>> items_by_luid(PDH_HCOUNTER c, DWORD fmt) {
    std::vector<std::pair<std::wstring, double>> out;
    DWORD bytes = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(c, fmt, &bytes, &count, nullptr) != PDH_MORE_DATA || bytes == 0) return out;
    std::vector<unsigned char> buf(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(c, fmt, &bytes, &count, items) != ERROR_SUCCESS) return out;
    for (DWORD i = 0; i < count; ++i) {
        const auto& v = items[i].FmtValue;
        if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        std::wstring name = items[i].szName ? items[i].szName : L"";
        const auto k = name.find(L"luid_");
        out.push_back({k == std::wstring::npos ? name : name.substr(k), fmt == PDH_FMT_LARGE ? static_cast<double>(v.largeValue) : v.doubleValue});
    }
    return out;
}
// Ingilizce sayac adlari: Windows dilinden bagimsiz (UI-09 ile ayni yontem).
std::wstring mem_path(std::uint32_t pid) { return L"\\GPU Process Memory(pid_" + std::to_wstring(pid) + L"_*)\\Dedicated Usage"; }
// Uyumluluk B4 (bosluk 15): butun motor turleri (3D, Compute_N, Copy, Video...); 3D ayrica ayiklanir.
std::wstring eng_path(std::uint32_t pid) { return L"\\GPU Engine(pid_" + std::to_wstring(pid) + L"_*)\\Utilization Percentage"; }
PDH_HCOUNTER add_counter(PDH_HQUERY q, const std::wstring& path) {
    PDH_HCOUNTER c = nullptr;
    return PdhAddEnglishCounterW(q, path.c_str(), 0, &c) == ERROR_SUCCESS ? c : nullptr;
}
std::int64_t now_ms() { return static_cast<std::int64_t>(GetTickCount64()); }
}  // namespace

ProcessGpuSampler::~ProcessGpuSampler() { close(); }

bool ProcessGpuSampler::open() {
    if (query_) return true;
    PDH_HQUERY q = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return false;
    query_ = q;
    PDH_HCOUNTER a = nullptr;
    if (PdhAddEnglishCounterW(q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &a) == ERROR_SUCCESS) adapter_ = a;
    mem_ = add_counter(q, mem_path(pid_));
    eng_ = add_counter(q, eng_path(pid_));
    PdhCollectQueryData(q);  // yuzde icin taban
    primed_ = false;
    pending_ = false;
    pending_collects_ = 0;
    reexpansions_ = 0;
    reexpand_ = {};
    reexpand_.opened_ms = reexpand_.last_expand_ms = now_ms();
    return mem_ || eng_;
}

void ProcessGpuSampler::start_reexpand() {
    // Bosluk 16: joker sayac eklendigi anda genisletilir. Yeni sayaclar eskileriyle birlikte toplanir; iki toplama
    // sonra (yuzde tabani hazir) eskilerin yerine gecer (sample). Biri eklenemezse eskisi surer.
    auto* q = static_cast<PDH_HQUERY>(query_);
    mem_pending_ = add_counter(q, mem_path(pid_));
    eng_pending_ = add_counter(q, eng_path(pid_));
    pending_ = mem_pending_ || eng_pending_;
    pending_collects_ = 0;
}

ProcessGpuReading ProcessGpuSampler::sample() {
    ProcessGpuReading r;
    if (!query_) {
        r.missing_reason = "not_open";
        return r;
    }
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(query_)) != ERROR_SUCCESS) {
        r.missing_reason = "collect_failed";
        return r;
    }
    // Bosluk 16: bekleyen yeni sayaclar iki toplama gorduyse eskilerin yerine gecer (eski kaldirilir; bosluk yok).
    if (pending_ && ++pending_collects_ >= 2) {
        if (mem_pending_) {
            if (mem_) PdhRemoveCounter(static_cast<PDH_HCOUNTER>(mem_));
            mem_ = mem_pending_;
        }
        if (eng_pending_) {
            if (eng_) PdhRemoveCounter(static_cast<PDH_HCOUNTER>(eng_));
            eng_ = eng_pending_;
            primed_ = true;  // yeni sayac iki toplama gordu: yuzde gecerli
        }
        mem_pending_ = eng_pending_ = nullptr;
        pending_ = false;
        ++reexpansions_;
    }
    bool instance_seen = false;
    if (mem_) {
        // Makullik: surecin bir adaptordeki kullanimi o adaptorun toplam kullanimini asamaz. Asiyorsa
        // sayac bu surec icin guvenilmez (ornek: dwm.exe) -> deger atilir, neden yazilir (uydurma yok).
        const auto proc = items_by_luid(static_cast<PDH_HCOUNTER>(mem_), PDH_FMT_LARGE);
        const auto adap = adapter_ ? items_by_luid(static_cast<PDH_HCOUNTER>(adapter_), PDH_FMT_LARGE)
                                   : std::vector<std::pair<std::wstring, double>>{};
        double sum = 0;
        bool any = false, implausible = false;
        for (const auto& [luid, v] : proc) {
            for (const auto& [al, av] : adap)
                if (al == luid && v > av * 1.05 + 64.0 * 1024 * 1024) implausible = true;
            sum += v;
            any = true;
        }
        instance_seen = any;
        if (implausible) r.missing_reason = "process_exceeds_adapter_usage";
        else if (any) r.dedicated_bytes = static_cast<std::uint64_t>(sum);
    }
    if (eng_ && primed_) {
        const auto items = named_items(static_cast<PDH_HCOUNTER>(eng_), PDH_FMT_DOUBLE | PDH_FMT_NOCAP100);
        instance_seen = instance_seen || !items.empty();
        // Spec 2026-11-12 B: LUID bazinda; surecin en cok kullandigi bagdastirici (farkli kartlar toplanmaz).
        const auto pick = core::metrics::pick_process_3d_engine(items);
        r.engine_adapters = pick.adapters;
        if (pick.pct) {
            r.engine_3d_pct = *pick.pct;
            r.engine_luid = pick.luid;
        }
        // Bosluk 15: Gorev Yoneticisi yontemi (bagdastirici basina en yuklu motor turu; 3D yukaridaki ikincil alan).
        const auto top = core::metrics::pick_process_gpu_engine(items);
        if (top.pct) {
            r.engine_max_pct = *top.pct;
            r.engine_max_luid = top.luid;
            r.engine_max_type = top.engine_type;
        }
    } else if (eng_) {
        instance_seen = true;  // ilk toplama yalniz yuzde tabani: "ornek yok" karari bu okumada verilmez
    }
    primed_ = true;
    if (!pending_ && core::metrics::process_gpu_reexpand_due(reexpand_, now_ms(), instance_seen)) start_reexpand();
    r.reexpansions = reexpansions_;
    if (!r.dedicated_bytes && !r.engine_3d_pct && !r.engine_max_pct && r.missing_reason.empty())
        r.missing_reason = mem_ || eng_ ? "no_instance_for_pid" : "counter_unavailable";
    return r;
}

void ProcessGpuSampler::close() {
    if (query_) PdhCloseQuery(static_cast<PDH_HQUERY>(query_));  // sayaclari da kapatir (bekleyenler dahil)
    query_ = mem_ = eng_ = adapter_ = nullptr;
    mem_pending_ = eng_pending_ = nullptr;
    pending_ = false;
    pending_collects_ = 0;
    primed_ = false;
}

}  // namespace ef::measurement
