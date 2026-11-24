// MEAS-01: PresentData ETW tuketicisi sarmalayicisi. Ayrintilar frame_session.h.
#include "measurement/frame/frame_session.h"

#include <windows.h>
#include <evntrace.h>

#include <atomic>
#include <cstddef>
#include <cwchar>
#include <deque>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

// Ucuncu taraf (third_party/presentmon, MIT). Sira onemli: oturum basligi tuketici tiplerine dayanir.
#include "PresentData/PresentMonTraceConsumer.hpp"
#include "PresentData/PresentMonTraceSession.hpp"

namespace ef::measurement {
namespace {

bool has_own_prefix(const wchar_t* name) {
    const std::size_t n = std::wcslen(kFrameSessionPrefix);
    return name != nullptr && std::wcsncmp(name, kFrameSessionPrefix, n) == 0 && std::wcslen(name) > n &&
           std::wcslen(name) < 1024;
}

std::string win32_detail(const char* stage, unsigned long code) {
    std::string s = stage;
    s += " failed: win32=";
    s += std::to_string(code);
    return s;
}

// ControlTrace icin isim alanli EVENT_TRACE_PROPERTIES.
struct QueryProps {
    EVENT_TRACE_PROPERTIES props;
    wchar_t logger_name[1024];
    wchar_t log_file[1024];
};

}  // namespace

struct FrameSession::Impl {
    PMTraceConsumer consumer;
    PMTraceSession session;
    std::thread worker;
    std::atomic<bool> running{false};
    std::mutex dequeue_mutex;
    std::vector<std::shared_ptr<PresentEvent>> scratch;
    std::vector<ProcessEvent> process_scratch;
    std::atomic<std::uint64_t> present_events{0};
    std::uint32_t events_lost = 0;
    std::uint32_t buffers_lost = 0;
    std::uint32_t overflowed = 0;
    static constexpr std::size_t kMaxBuffer = 200000;  // ~10 dk 300 FPS
    std::deque<core::measure::RawPresent> buffer;
    std::uint64_t dropped = 0;

    std::size_t drain() {
        std::lock_guard<std::mutex> lock(dequeue_mutex);
        consumer.DequeueProcessEvents(process_scratch);
        process_scratch.clear();
        consumer.DequeuePresentEvents(scratch);
        const std::size_t n = scratch.size();
        for (const auto& pe : scratch) {
            if (!pe) continue;
            core::measure::RawPresent r;
            r.pid = pe->ProcessId;
            r.swapchain = pe->SwapChainAddress;
            r.start_qpc = pe->PresentStartTime;
            r.runtime = static_cast<int>(pe->Runtime);
            r.final_state = static_cast<int>(pe->FinalState);
            for (const auto& [ft, q] : pe->Displayed) r.displayed.push_back({static_cast<int>(ft), q});
            // Spec 2026-11-12 B: kare basi GPU mesgul suresi (QPC tik). Izleme kapaliysa alan anlamsizdir.
            if (consumer.mTrackGPU) {
                r.gpu_busy_qpc = pe->GPUDuration;
                r.gpu_tracked = true;
            }
            if (buffer.size() >= kMaxBuffer) {
                buffer.pop_front();  // sinir: eski olay dusurulur (sayilir; sessiz degil)
                ++dropped;
            }
            buffer.push_back(std::move(r));
        }
        scratch.clear();
        present_events.fetch_add(n, std::memory_order_relaxed);
        return n;
    }
};

FrameSession::FrameSession() : impl_(std::make_unique<Impl>()) {}

FrameSession::~FrameSession() {
    stop();
}

FrameSessionStart FrameSession::start_realtime(const wchar_t* session_name) {
    FrameSessionStart r;
    if (impl_->running.load()) {
        r.error = FrameSessionError::AlreadyRunning;
        r.detail = "frame session already running in this process";
        return r;
    }
    if (!has_own_prefix(session_name)) {
        r.error = FrameSessionError::Other;
        r.win32 = ERROR_INVALID_PARAMETER;
        r.detail = "session name must start with EcuFps-";
        return r;
    }

    // Kare (present -> ekran) + GPU is paketi izleme (spec 2026-11-12 B: kare basi GPU mesgul suresi; DxgKrnl
    // Context/Device/DmaPacket olaylari). Video motoru ayrimi, girdi, NVIDIA PCL ve kare tipi izleri kapali.
    auto& c = impl_->consumer;
    c.mTrackDisplay = true;
    c.mTrackGPU = true;
    c.mTrackGPUVideo = false;
    c.mTrackInput = false;
    c.mTrackFrameType = false;
    c.mTrackPMMeasurements = false;
    c.mTrackAppTiming = false;
    c.mTrackHybridPresent = false;
    c.mTrackPcLatency = false;
    c.mTrackProcessState = false;
    c.mTrackD3D12ShaderCompilation = false;
    c.mFilteredProcessIds = false;
    c.mPaceEvents = false;
    c.mRetimeEvents = false;
    impl_->session.mPMConsumer = &c;

    ULONG status = impl_->session.Start(nullptr, session_name);
    if (status == ERROR_ALREADY_EXISTS) {
        // Ayni adli oturum yalniz bize ait olabilir (onek denetlendi): onceki surecin yetimi.
        StopNamedTraceSession(session_name);
        status = impl_->session.Start(nullptr, session_name);
    }
    r.win32 = status;
    if (status == ERROR_SUCCESS) {
        TRACEHANDLE handle = impl_->session.mTraceHandle;
        impl_->running.store(true);
        impl_->worker = std::thread([handle]() mutable {
            SetThreadDescription(GetCurrentThread(), L"EcuFps ETW frames");
            ProcessTrace(&handle, 1, nullptr, nullptr);
        });
        r.error = FrameSessionError::None;
        return r;
    }
    if (status == ERROR_ACCESS_DENIED) {
        r.error = FrameSessionError::AccessDenied;
        r.detail = "ETW realtime session requires administrator or Performance Log Users membership";
    } else if (status == ERROR_ALREADY_EXISTS) {
        r.error = FrameSessionError::AlreadyRunning;
        r.detail = win32_detail("StartTrace", status);
    } else {
        r.error = FrameSessionError::Other;
        r.detail = win32_detail("StartTrace/EnableProviders/OpenTrace", status);
    }
    return r;
}

std::size_t FrameSession::poll() {
    if (!impl_->running.load()) return 0;
    return impl_->drain();
}

void FrameSession::stop() {
    if (!impl_ || !impl_->running.exchange(false)) return;
    impl_->session.Stop();  // saglayicilari kapatir, oturumu durdurur, CloseTrace
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->drain();
    impl_->events_lost = impl_->session.mNumEventsLost;
    impl_->buffers_lost = impl_->session.mNumBuffersLost;
    impl_->overflowed = impl_->consumer.GetNumOverflowedPresents();
}

std::vector<core::measure::RawPresent> FrameSession::take_presents() {
    std::lock_guard<std::mutex> lock(impl_->dequeue_mutex);
    std::vector<core::measure::RawPresent> out(std::make_move_iterator(impl_->buffer.begin()), std::make_move_iterator(impl_->buffer.end()));
    impl_->buffer.clear();
    return out;
}

std::uint64_t FrameSession::dropped_presents() const noexcept { return impl_->dropped; }

double FrameSession::ms_per_tick() const noexcept {
    const auto f = impl_->session.mTimestampFrequency.QuadPart;
    return f > 0 ? 1000.0 / static_cast<double>(f) : 0.0;
}

std::uint64_t FrameSession::qpc_origin() const noexcept { return static_cast<std::uint64_t>(impl_->session.mStartTimestamp.QuadPart); }

bool FrameSession::running() const noexcept {
    return impl_ && impl_->running.load();
}

FrameSessionStats FrameSession::stats() const noexcept {
    FrameSessionStats s;
    if (!impl_) return s;
    s.present_events = impl_->present_events.load(std::memory_order_relaxed);
    s.events_lost = impl_->events_lost;
    s.buffers_lost = impl_->buffers_lost;
    s.overflowed_presents = impl_->overflowed;
    return s;
}

SessionPresence query_session(const wchar_t* session_name, unsigned long* win32) {
    QueryProps q{};
    q.props.Wnode.BufferSize = sizeof(QueryProps);
    q.props.LoggerNameOffset = offsetof(QueryProps, logger_name);
    q.props.LogFileNameOffset = offsetof(QueryProps, log_file);
    const ULONG status = ControlTraceW(0, session_name, &q.props, EVENT_TRACE_CONTROL_QUERY);
    if (win32 != nullptr) *win32 = status;
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA) return SessionPresence::Present;
    if (status == ERROR_WMI_INSTANCE_NOT_FOUND) return SessionPresence::Absent;
    return SessionPresence::Unknown;
}

}  // namespace ef::measurement
