// DIAG-03: RAM / VRAM / G-C ayrimi.
#include <algorithm>

#include "core/diagnosis/memory_storage.h"
#include "ef_test.h"

using namespace ef::core::diagnosis;
using namespace ef::core::measure;

namespace {
constexpr double kGiB = 1024.0 * 1024 * 1024;
constexpr std::uint32_t kGame = 4242;

FrameStats smooth() { return compute_frame_stats(std::vector<double>(100, 10.0)); }
FrameStats spiky() {
    std::vector<double> v(98, 10.0);
    v.push_back(40.0);
    v.push_back(45.0);
    return compute_frame_stats(v);
}

// Saglikli taban: RAM bol, fault dusuk, disk dusuk, VRAM butce icinde.
MemoryWindow base(double start, bool spike) {
    MemoryWindow w;
    w.start_ms = start;
    w.end_ms = start + 2000;
    w.frames = spike ? spiky() : smooth();
    w.ram_available_bytes = {8 * kGiB, true};
    w.ram_total_bytes = {16 * kGiB, true};
    w.commit_bytes = {10 * kGiB, true};
    w.commit_limit_bytes = {24 * kGiB, true};
    w.hard_faults_per_s = {5, true};
    w.game_disk_read_bps = {1024 * 1024, true};
    w.game_vram_dedicated = VramReading{VramScope::ProcessUsage, kGame, static_cast<std::uint64_t>(3 * kGiB)};
    return w;
}

AdapterMemory discrete8() { return {AdapterKind::Discrete, false, static_cast<std::uint64_t>(8 * kGiB)}; }

bool has(const std::vector<EvidenceCode>& v, EvidenceCode c) { return std::find(v.begin(), v.end(), c) != v.end(); }
}  // namespace

EF_TEST(diag_memory_spike_detector) {
    EF_CHECK(is_frame_spike(spiky(), 2.0));
    EF_CHECK(!is_frame_spike(smooth(), 2.0));
    EF_CHECK(!is_frame_spike(compute_frame_stats({10.0, 40.0}), 2.0));  // yetersiz N: sivri sayilmaz
}

EF_TEST(diag_memory_hard_faults_are_paging_not_hardware_fault) {
    std::vector<MemoryWindow> ws;
    for (int i = 0; i < 4; ++i) {
        auto w = base(i * 2000.0, true);
        w.ram_available_bytes = {0.6 * kGiB, true};  // %3.75 bos
        w.commit_bytes = {23.5 * kGiB, true};
        w.hard_faults_per_s = {600, true};
        ws.push_back(w);
    }
    ws.push_back(base(8000, false));
    const auto d = diagnose_memory_storage(ws, discrete8(), kGame);
    EF_CHECK(d.cls == MemStorageClass::MemoryPressure);
    EF_CHECK(d.is_hardware_fault == false);  // hard fault = sayfalama, fiziksel RAM arizasi degil
    EF_CHECK(d.ram.verdict == DomainVerdict::Limiting && d.ram.correlated_windows == 4);
    EF_CHECK(has(d.supporting, EvidenceCode::HardFaultsWithSpikes) && has(d.supporting, EvidenceCode::RamAvailableLow) &&
              has(d.supporting, EvidenceCode::CommitNearLimit));
    EF_CHECK(d.reason == MemoryConfidence::CorrelatedAcrossManyWindows);
    EF_CHECK(d.vram.verdict == DomainVerdict::NotLimiting && has(d.counter, EvidenceCode::VramWithinBudget));
    EF_CHECK(d.io.verdict == DomainVerdict::NotLimiting && has(d.counter, EvidenceCode::SpikesWithoutDiskRead));
}

EF_TEST(diag_memory_hard_faults_without_capacity_or_spikes_not_pressure) {
    std::vector<MemoryWindow> ws;
    for (int i = 0; i < 3; ++i) {
        auto w = base(i * 2000.0, true);
        w.hard_faults_per_s = {900, true};  // kapasite bol: dosya sayfalama olabilir, RAM baskisi degil
        ws.push_back(w);
    }
    auto calm = base(6000, false);
    calm.ram_available_bytes = {0.5 * kGiB, true};
    calm.hard_faults_per_s = {900, true};  // takilma yok
    ws.push_back(calm);
    const auto d = diagnose_memory_storage(ws, discrete8(), kGame);
    EF_CHECK(d.ram.verdict == DomainVerdict::NotLimiting);
    EF_CHECK(has(d.ram.counter, EvidenceCode::HardFaultsWithoutCapacityPressure));
    EF_CHECK(has(d.ram.counter, EvidenceCode::HardFaultsWithoutSpikes));
    EF_CHECK(d.cls != MemStorageClass::MemoryPressure && d.is_hardware_fault == false);
}

EF_TEST(diag_memory_vram_only_game_process_usage) {
    auto make = [](VramReading r) {
        std::vector<MemoryWindow> ws;
        for (int i = 0; i < 3; ++i) {
            auto w = base(i * 2000.0, true);
            w.game_vram_dedicated = r;
            ws.push_back(w);
        }
        return ws;
    };
    const auto full = static_cast<std::uint64_t>(7.9 * kGiB);
    // Kendi DXGI butcemiz: oyun icin asla kullanilmaz.
    auto d = diagnose_memory_storage(make({VramScope::OwnProcessDxgiBudget, 1, full}), discrete8(), kGame);
    EF_CHECK(d.vram.verdict == DomainVerdict::Insufficient && has(d.notes, EvidenceCode::VramOwnProcessBudgetIgnored));
    EF_CHECK(d.cls != MemStorageClass::VramPressure);
    // Adaptor toplami yalniz baglam.
    d = diagnose_memory_storage(make({VramScope::AdapterTotal, 0, full}), discrete8(), kGame);
    EF_CHECK(d.vram.verdict == DomainVerdict::Insufficient && has(d.notes, EvidenceCode::VramAdapterTotalContextOnly));
    // Baska PID.
    d = diagnose_memory_storage(make({VramScope::ProcessUsage, 777, full}), discrete8(), kGame);
    EF_CHECK(d.vram.verdict == DomainVerdict::Insufficient && has(d.notes, EvidenceCode::VramPidMismatch));
    // Oyun PID'i, butceye yakin, sivrilerle ayni pencerelerde.
    d = diagnose_memory_storage(make({VramScope::ProcessUsage, kGame, full}), discrete8(), kGame);
    EF_CHECK(d.cls == MemStorageClass::VramPressure && d.vram.correlated_windows == 3);
    EF_CHECK(has(d.supporting, EvidenceCode::VramNearBudgetWithSpikes));
    // Butce bilinmiyor: karar yok.
    d = diagnose_memory_storage(make({VramScope::ProcessUsage, kGame, full}), {AdapterKind::Discrete, false, std::nullopt}, kGame);
    EF_CHECK(d.vram.verdict == DomainVerdict::Insufficient && has(d.counter, EvidenceCode::VramBudgetMissing));
    EF_CHECK(d.cls == MemStorageClass::Insufficient);
}

EF_TEST(diag_memory_igpu_shared_memory_not_double_counted) {
    const AdapterMemory igpu{AdapterKind::Integrated, true, static_cast<std::uint64_t>(0.5 * kGiB)};
    std::vector<MemoryWindow> ws;
    for (int i = 0; i < 3; ++i) {
        auto w = base(i * 2000.0, true);
        w.game_vram_dedicated = VramReading{VramScope::ProcessUsage, kGame, static_cast<std::uint64_t>(0.49 * kGiB)};
        w.game_vram_shared_bytes = static_cast<std::uint64_t>(6 * kGiB);  // sistem RAM'inden; zaten RAM sayaclarinda
        ws.push_back(w);
    }
    auto d = diagnose_memory_storage(ws, igpu, kGame);
    EF_CHECK(d.vram.verdict == DomainVerdict::NotApplicable && has(d.notes, EvidenceCode::SharedMemoryCountedOnceAsRam));
    EF_CHECK(d.cls != MemStorageClass::VramPressure && d.cls != MemStorageClass::MemoryPressure);  // RAM bol: eklenmedi
    // Gercek RAM baskisi: yalniz RAM alaninda, VRAM ile "Mixed" degil.
    for (auto& w : ws) {
        w.ram_available_bytes = {0.4 * kGiB, true};
        w.hard_faults_per_s = {400, true};
    }
    d = diagnose_memory_storage(ws, igpu, kGame);
    EF_CHECK(d.cls == MemStorageClass::MemoryPressure && d.vram.verdict == DomainVerdict::NotApplicable);
}

EF_TEST(diag_memory_loading_time_not_sustained_fps_limit) {
    std::vector<MemoryWindow> ws;
    // 0-6 s yukleme: agir disk okuma ve dusuk FPS, ama MenuOrLoading.
    for (int i = 0; i < 3; ++i) {
        auto w = base(i * 2000.0, true);
        w.scope = WindowScope::MenuOrLoading;
        w.game_disk_read_bps = {400.0 * 1024 * 1024, true};
        ws.push_back(w);
    }
    for (int i = 3; i < 8; ++i) ws.push_back(base(i * 2000.0, false));
    auto d = diagnose_memory_storage(ws, discrete8(), kGame);
    EF_CHECK(d.cls == MemStorageClass::Healthy && d.io.verdict == DomainVerdict::NotLimiting);
    EF_CHECK(d.loading.size() == 1 && d.loading_total_ms == 6000.0);  // "yukleme suresi" ayri
    EF_CHECK(has(d.notes, EvidenceCode::LoadingReportedSeparately));
    EF_CHECK(!has(d.supporting, EvidenceCode::DiskReadWithSpikes));
    // Oyun icinde disk okumasi ile esli takilma: IoStutter (yukleme yine ayri).
    for (int i = 3; i < 6; ++i) {
        ws[static_cast<std::size_t>(i)].frames = spiky();
        ws[static_cast<std::size_t>(i)].game_disk_read_bps = {120.0 * 1024 * 1024, true};
    }
    d = diagnose_memory_storage(ws, discrete8(), kGame);
    EF_CHECK(d.cls == MemStorageClass::IoStutter && d.io.correlated_windows == 3 && d.loading_total_ms == 6000.0);
}

EF_TEST(diag_memory_missing_metrics_insufficient) {
    EF_CHECK(diagnose_memory_storage({}, discrete8(), kGame).cls == MemStorageClass::Insufficient);
    std::vector<MemoryWindow> ws{base(0, true), base(2000, true)};
    for (auto& w : ws) w.hard_faults_per_s = {};
    auto d = diagnose_memory_storage(ws, discrete8(), kGame);
    EF_CHECK(d.cls == MemStorageClass::Insufficient && d.ram.verdict == DomainVerdict::Insufficient);
    EF_CHECK(has(d.counter, EvidenceCode::RamMetricsMissing) && d.reason == MemoryConfidence::MissingMetrics);
    // Yalniz yukleme pencereleri: olculmus pencere yok.
    auto l = base(0, false);
    l.scope = WindowScope::MenuOrLoading;
    d = diagnose_memory_storage({l}, discrete8(), kGame);
    EF_CHECK(d.cls == MemStorageClass::Insufficient && d.reason == MemoryConfidence::NoMeasuredWindows && d.loading_total_ms == 2000.0);
}

EF_TEST(diag_memory_coarse_counter_not_root_cause) {
    std::vector<MemoryWindow> ws;
    for (int i = 0; i < 3; ++i) {
        auto w = base(i * 2000.0, true);
        w.game_disk_read_bps = {200.0 * 1024 * 1024, false};  // coznurluk yetmiyor
        ws.push_back(w);
    }
    const auto d = diagnose_memory_storage(ws, discrete8(), kGame);
    EF_CHECK(d.io.verdict == DomainVerdict::Insufficient && d.io.reason == MemoryConfidence::CoarseResolution);
    EF_CHECK(d.cls == MemStorageClass::Insufficient && d.reason == MemoryConfidence::CoarseResolution);
}

EF_TEST(diag_memory_window_from_evidence) {
    EvidenceWindow e;
    e.start_ms = 100;
    e.end_ms = 2100;
    e.frames = spiky();
    e.metrics.push_back({"hard_faults_per_s", 250.0, Attribution::RootCandidate});
    e.metrics.push_back({"game_disk_read_bps", 5.0, Attribution::ContextOnly});
    const auto m = memory_window_from(e);
    EF_CHECK(m.hard_faults_per_s.value == 250.0 && m.hard_faults_per_s.attributable);
    EF_CHECK(m.game_disk_read_bps.value == 5.0 && !m.game_disk_read_bps.attributable);
    EF_CHECK(!m.ram_available_bytes.value && !m.game_vram_dedicated);
}
