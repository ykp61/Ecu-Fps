#include "measurement/frame/game_capture.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace ef::measurement {

namespace cm = core::measure;

namespace {
// EF-X18/T04: hedef surecin cikisini dongu icinde izler. Yalniz SYNCHRONIZE + sinirli sorgu hakki
// (query_identity ile ayni sinif); bellek okuma/enjeksiyon yok. Tanitici olusturma zamaniyla dogrulanir
// (PID tekrar kullanimi); acilamaz/eslesmezse izleme kapali kalir ve son kimlik kontrolu gecerlidir.
class ExitWatch {
public:
    explicit ExitWatch(const cm::ProcessIdentity& t) {
        h_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, t.pid);
        if (!h_) return;
        FILETIME c{}, e{}, k{}, u{};
        const bool same = GetProcessTimes(h_, &c, &e, &k, &u) &&
                          ((static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime) == t.create_time;
        if (!same) {
            CloseHandle(h_);
            h_ = nullptr;
        }
    }
    ~ExitWatch() {
        if (h_) CloseHandle(h_);
    }
    ExitWatch(const ExitWatch&) = delete;
    ExitWatch& operator=(const ExitWatch&) = delete;
    bool exited() const noexcept { return h_ && WaitForSingleObject(h_, 0) == WAIT_OBJECT_0; }

private:
    HANDLE h_ = nullptr;
};
}  // namespace

cm::ProcessIdentity query_identity(std::uint32_t pid) {
    cm::ProcessIdentity id;
    id.pid = pid;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return id;
    wchar_t path[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(h, 0, path, &n) && n > 0) {
        const int m = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(m), '\0');
        WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(n), s.data(), m, nullptr, nullptr);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c); });
        id.image_path = std::move(s);
    }
    FILETIME c{}, e{}, k{}, u{};
    if (GetProcessTimes(h, &c, &e, &k, &u)) id.create_time = (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    CloseHandle(h);
    return id;
}

GameCaptureResult capture_game(const cm::ProcessIdentity& target, double seconds, const std::atomic<bool>& cancel, const std::string& app,
                               double min_seconds) {
    GameCaptureResult r;
    r.target = cm::check_target(target, query_identity(target.pid));
    // BM-01: erken hatalarda da gorunum gecersizdir (varsayilan "gecerli" gorunum sizmaz).
    if (r.target != cm::TargetError::None) {
        r.view = cm::ingame_result_view(app, r.manifest, r.stats, 0);
        return r;  // yanlis/overlay/bilinmeyen surec olculmez
    }
    FrameSession fs;
    r.start = fs.start_realtime();
    if (!r.start.ok()) {
        r.view = cm::ingame_result_view(app, r.manifest, r.stats, 0);
        return r;  // AccessDenied vb. acik hata
    }
    // Ayni sistem QPC'si: kare zamanlari (qpc - origin) * ms_per_tick(); istemci ornekleri de
    // QueryPerformanceCounter ms cinsindendir. Origin 0 ise hiza kurulmaz.
    if (fs.qpc_origin() > 0) r.capture_origin_ms = static_cast<double>(fs.qpc_origin()) * fs.ms_per_tick();
    const ExitWatch watch(target);
    std::vector<cm::FrameEvent> events;
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed_s = [&t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    bool cancelled = false;
    std::optional<double> exited_after_s;
    while (elapsed_s() < seconds) {
        if (cancel.load()) {
            cancelled = true;
            break;
        }
        fs.poll();
        for (const auto& p : fs.take_presents())
            if (p.pid == target.pid) cm::append_frame_events(p, fs.ms_per_tick(), fs.qpc_origin(), events);
        if (watch.exited()) {  // oyun kapandi/coktu: sure dolana kadar beklenmez
            exited_after_s = elapsed_s();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const double loop_end_s = elapsed_s();
    fs.stop();
    for (const auto& p : fs.take_presents())
        if (p.pid == target.pid) cm::append_frame_events(p, fs.ms_per_tick(), fs.qpc_origin(), events);
    // Dogrulama bulgusu 2: durdurma/son bosaltma sirasinda gozlenen cikis yalniz pencere dolmadan dongu
    // bittiyse erken cikistir; tam pencereden sonraki cikis bilgi olarak tasinir (GameExited degil).
    const auto exit_obs = cm::resolve_target_exit(exited_after_s, watch.exited(), loop_end_s, seconds, elapsed_s());
    exited_after_s = exit_obs.early_exit_s;
    r.target_exited_after_window = exit_obs.exited_after_window;
    const auto st = fs.stats();
    r.dropped_presents = fs.dropped_presents();
    r.session_stats = st;
    r.loss.events_lost = st.events_lost;
    r.loss.buffers_lost = st.buffers_lost;
    r.loss.overflowed_presents = st.overflowed_presents;
    r.loss.dropped_presents = r.dropped_presents;
    // Yakalama sonrasi kimlik tekrar: surec bu arada degistiyse sonuc gecersiz.
    // Tam pencereden sonra cikan surec yeniden sorgulanamaz (goruntu yolu okunamaz); kimligi acilista
    // olusturma zamaniyla dogrulanan ve pencere boyunca acik tutulan tanitici kanitlar (PID yeniden kullanimi yok).
    if (!exit_obs.exited_after_window && cm::check_target(target, query_identity(target.pid)) != cm::TargetError::None)
        r.target = cm::TargetError::IdentityMismatch;
    cm::StreamHealth health;
    health.events_lost = cm::stream_events_lost(r.loss);
    health.buffers_lost = st.buffers_lost;
    r.summary = cm::summarize(events, target.pid, health);
    r.stats = cm::compute_frame_stats(r.summary.render_intervals_ms);
    r.manifest = cm::make_manifest("in_game", app, {}, r.summary, r.stats, cm::manifest_loss_total(r.loss), min_seconds, cancelled);
    r.outcome = cm::classify_capture_outcome(cancelled, exited_after_s.has_value(), r.target == cm::TargetError::None);
    r.target_exited_after_s = exited_after_s;
    cm::apply_capture_outcome(r.manifest, r.outcome);  // TargetChanged -> TargetMismatch (onceki davranis), GameExited ayri
    r.view = cm::ingame_result_view(app, r.manifest, r.stats, 0);
    return r;
}

}  // namespace ef::measurement
