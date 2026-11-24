// Internet hiz testi: WinHTTP ile core::net::kSpeedHost uzerinde gecikme, indirme, yukleme.
// Kurallar speedtest_http.h basliginda. Indirilen veri 64 KiB'lik karalama tamponuna okunup atilir; yuklenen veri
// sifirdir. Sonuc diske yazilmaz, sunucuya sonuc gonderilmez.
#include "platform/windows/net/speedtest_http.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ef::platform::net {

namespace cn = core::net;
using cn::SpeedFail;
using cn::SpeedPhase;

namespace {

using Clock = std::chrono::steady_clock;
double secs(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }

constexpr DWORD kResolveTimeoutMs = 5000, kConnectTimeoutMs = 5000, kSendTimeoutMs = 5000, kReceiveTimeoutMs = 10000;
constexpr std::size_t kIoBytes = 64 * 1024;
constexpr double kProgressIntervalS = 0.1;  // en fazla 10 Hz

class Handle {
public:
    Handle() = default;
    explicit Handle(HINTERNET h) : h_(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
    Handle& operator=(Handle&& o) noexcept {
        if (this != &o) {
            if (h_) WinHttpCloseHandle(h_);
            h_ = o.h_;
            o.h_ = nullptr;
        }
        return *this;
    }
    ~Handle() {
        if (h_) WinHttpCloseHandle(h_);
    }
    HINTERNET get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }

private:
    HINTERNET h_ = nullptr;
};

SpeedFail map_error(DWORD e) {
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT: return SpeedFail::Timeout;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
        case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
        case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
            return SpeedFail::Offline;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
            return SpeedFail::Tls;
        default: return SpeedFail::Http;
    }
}

// Neden durduk: iptal / oyun kapisi kesin durdurur; sure siniri yalniz asamayi bitirir.
enum class Stop { None, Cancel, Gate, PhaseTime, TotalTime };

struct Xfer {
    bool ok = false;           // istek tamamlandi veya sure siniriyla bilerek kesildi
    SpeedFail fail = SpeedFail::None;
    Stop stop = Stop::None;
    std::uint64_t bytes = 0;   // ornek baytlari (indirme: okunan, yukleme: tamamlanan yuklemede yazilan)
    std::uint64_t used = 0;    // hatta giden/gelen govde baytlari (yarim kalan yukleme dahil; harcanan veri)
    double total_s = 0;        // istek gonderiminden son bayta / yanita
    double floor_s = 0;        // aktarim icin alt sinir (indirme: ilk yanittan son bayta; yukleme: yazma suresi)
    double header_s = 0;       // gonderimden yanit basligina (gecikme olcumu)
};

class Run {
public:
    Run(const std::atomic<bool>& cancel, const std::function<bool()>& gate, SpeedTestRunner::Progress& progress)
        : cancel_(cancel), gate_(gate), progress_(progress), start_(Clock::now()) {}

    ~Run() { stop_probe(); }
    cn::SpeedResult go();

private:
    // R-C1: indirme/yukleme sirasinda yan baglantiyla yuk altinda gecikme (GET /__down?bytes=0, ~4 Hz). Ayni tek host,
    // govde 0 bayt; ilk ornek (baglanti kurulumu) atilir. Iptalde dongu cikar.
    std::thread probe_;
    std::atomic<bool> probe_stop_{false};
    std::mutex probe_mu_;
    std::vector<double> probe_ms_;
    void start_probe();
    std::optional<double> stop_probe();

    const std::atomic<bool>& cancel_;
    const std::function<bool()>& gate_;
    SpeedTestRunner::Progress& progress_;
    Clock::time_point start_, phase_start_{}, last_progress_{};
    SpeedPhase phase_ = SpeedPhase::Idle;
    std::uint64_t total_bytes_ = 0;
    Handle session_, connect_;
    std::wstring host_;

    double total_elapsed() const { return secs(start_, Clock::now()); }
    double phase_elapsed() const { return secs(phase_start_, Clock::now()); }

    Stop check() const {
        if (cancel_.load()) return Stop::Cancel;
        if (gate_ && gate_()) return Stop::Gate;
        if (total_elapsed() >= cn::kSpeedMaxSeconds) return Stop::TotalTime;
        if ((phase_ == SpeedPhase::Download || phase_ == SpeedPhase::Upload) && phase_elapsed() >= cn::kPhaseSeconds)
            return Stop::PhaseTime;
        return Stop::None;
    }

    void enter(SpeedPhase p) {
        phase_ = p;
        phase_start_ = Clock::now();
        last_progress_ = phase_start_;
        if (progress_) progress_(p, 0.0, total_bytes_);
    }

    void tick(double live_mbps, std::uint64_t extra_bytes) {
        const auto now = Clock::now();
        if (secs(last_progress_, now) < kProgressIntervalS) return;
        last_progress_ = now;
        if (progress_) progress_(phase_, live_mbps, total_bytes_ + extra_bytes);
    }

    bool open(SpeedFail& fail);
    Handle request(const wchar_t* verb, const std::wstring& path, SpeedFail& fail);
    Xfer get(std::uint64_t n);
    Xfer post(std::uint64_t n);
};

bool Run::open(SpeedFail& fail) {
    session_ = Handle(WinHttpOpen(L"EcuFps", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session_) {
        fail = map_error(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session_.get(), static_cast<int>(kResolveTimeoutMs), static_cast<int>(kConnectTimeoutMs),
                       static_cast<int>(kSendTimeoutMs), static_cast<int>(kReceiveTimeoutMs));
    host_.assign(cn::kSpeedHost.begin(), cn::kSpeedHost.end());  // ASCII derleme sabiti
    connect_ = Handle(WinHttpConnect(session_.get(), host_.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect_) {
        fail = map_error(GetLastError());
        return false;
    }
    return true;
}

Handle Run::request(const wchar_t* verb, const std::wstring& path, SpeedFail& fail) {
    Handle req(WinHttpOpenRequest(connect_.get(), verb, path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE));
    if (!req) {
        fail = map_error(GetLastError());
        return req;
    }
    // Yonlendirme izlenmez: tek host kurali baska adrese tasinamaz.
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    return req;
}

bool status_ok(HINTERNET req) {
    DWORD code = 0, len = sizeof(code);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                             WINHTTP_NO_HEADER_INDEX))
        return false;
    return code == 200;
}

void Run::start_probe() {
    stop_probe();
    probe_stop_ = false;
    {
        std::lock_guard<std::mutex> lk(probe_mu_);
        probe_ms_.clear();
    }
    probe_ = std::thread([this] {
        Handle conn(WinHttpConnect(session_.get(), host_.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!conn) return;
        std::wstring path(cn::kDownPath.begin(), cn::kDownPath.end());
        path += L"?bytes=0";
        bool first = true;
        while (!probe_stop_.load() && !cancel_.load()) {
            Handle req(WinHttpOpenRequest(conn.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE));
            if (!req) break;
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
            const auto t0 = Clock::now();
            if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(req.get(), nullptr))
                break;  // hata: o ana kadarki orneklerle devam edilir (az ise "veri yetersiz")
            const double ms = secs(t0, Clock::now()) * 1000.0;
            if (!status_ok(req.get())) break;
            if (first) first = false;  // ilk istek baglanti/TLS kurulumunu icerir
            else {
                std::lock_guard<std::mutex> lk(probe_mu_);
                probe_ms_.push_back(ms);
            }
            for (int s = 0; s < cn::kLoadedProbeIntervalMs / 25 && !probe_stop_.load() && !cancel_.load(); ++s) Sleep(25);
        }
    });
}

std::optional<double> Run::stop_probe() {
    probe_stop_ = true;
    if (probe_.joinable()) probe_.join();
    std::lock_guard<std::mutex> lk(probe_mu_);
    return cn::loaded_median_ms(probe_ms_);
}

Xfer Run::get(std::uint64_t n) {
    Xfer x;
    std::wstring path(cn::kDownPath.begin(), cn::kDownPath.end());
    path += L"?bytes=" + std::to_wstring(n);  // yalniz sayi; kullanici verisi yok
    auto req = request(L"GET", path, x.fail);
    if (!req) return x;
    const auto t0 = Clock::now();
    if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.get(), nullptr)) {
        x.fail = map_error(GetLastError());
        return x;
    }
    const auto th = Clock::now();
    x.header_s = secs(t0, th);
    if (!status_ok(req.get())) {
        x.fail = SpeedFail::Http;
        return x;
    }
    std::vector<char> buf(kIoBytes);  // karalama: okunan veri atilir
    for (;;) {
        if ((x.stop = check()) != Stop::None) break;
        DWORD got = 0;
        if (!WinHttpReadData(req.get(), buf.data(), static_cast<DWORD>(buf.size()), &got)) {
            x.fail = map_error(GetLastError());
            return x;
        }
        if (got == 0) break;
        x.bytes += got;
        x.used += got;
        const double el = secs(th, Clock::now());
        tick(el > 0 ? static_cast<double>(x.bytes) * 8.0 / el / 1e6 : 0.0, x.bytes);
        if (x.bytes >= n) break;  // sunucu fazlasini gonderse de butce asilmaz
    }
    const auto te = Clock::now();
    x.total_s = secs(t0, te);
    x.floor_s = secs(th, te);
    x.ok = true;
    return x;
}

Xfer Run::post(std::uint64_t n) {
    Xfer x;
    const std::wstring path(cn::kUpPath.begin(), cn::kUpPath.end());
    auto req = request(L"POST", path, x.fail);
    if (!req) return x;
    static const wchar_t kHeaders[] = L"Content-Type: application/octet-stream\r\n";
    const auto t0 = Clock::now();
    if (!WinHttpSendRequest(req.get(), kHeaders, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, static_cast<DWORD>(n), 0)) {
        x.fail = map_error(GetLastError());
        return x;
    }
    const std::vector<char> zeros(kIoBytes, 0);
    std::uint64_t sent = 0;
    while (sent < n) {
        if ((x.stop = check()) != Stop::None) {
            x.ok = true;  // bilerek kesildi; yarim yukleme ornek sayilmaz (bytes = 0)
            return x;
        }
        const DWORD k = static_cast<DWORD>(std::min<std::uint64_t>(zeros.size(), n - sent));
        DWORD wrote = 0;
        if (!WinHttpWriteData(req.get(), zeros.data(), k, &wrote) || wrote == 0) {
            x.fail = map_error(GetLastError());
            return x;
        }
        sent += wrote;
        x.used = sent;
        const double el = secs(t0, Clock::now());
        tick(el > 0 ? static_cast<double>(sent) * 8.0 / el / 1e6 : 0.0, sent);
    }
    const auto tw = Clock::now();
    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        x.fail = map_error(GetLastError());
        return x;
    }
    const auto te = Clock::now();
    if (!status_ok(req.get())) {
        x.fail = SpeedFail::Http;
        return x;
    }
    // Yanit govdesi (kucuk) okunup atilir; baglanti yeniden kullanilabilsin.
    std::array<char, 4096> sink{};
    for (int guard = 0; guard < 64; ++guard) {
        DWORD got = 0;
        if (!WinHttpReadData(req.get(), sink.data(), static_cast<DWORD>(sink.size()), &got) || got == 0) break;
    }
    x.bytes = sent;
    x.total_s = secs(t0, te);
    x.floor_s = secs(t0, tw);
    x.ok = true;
    return x;
}

// Parca suresi: istek-yanit suresinden bir gidis-donus (gecikme medyani) dusulur; aktarimin kendi suresinin
// altina inilmez (cok kucuk parcada sisik hiz uretilmez).
double sample_seconds(const Xfer& x, double rtt_s) {
    return std::max(x.floor_s, x.total_s - rtt_s);
}

cn::SpeedResult Run::go() {
    cn::SpeedResult r;
    auto finish = [&](SpeedPhase ph, SpeedFail f) {
        r.phase = ph;
        r.fail = f;
        r.bytes_used = total_bytes_;
        return cn::sanitize_result(r);
    };
    auto stopped = [&](Stop s) {
        return s == Stop::Cancel ? finish(SpeedPhase::Cancelled, SpeedFail::None) : finish(SpeedPhase::Cancelled, SpeedFail::Gated);
    };
    if (const auto s = check(); s == Stop::Cancel || s == Stop::Gate) return stopped(s);

    enter(SpeedPhase::Latency);
    SpeedFail fail = SpeedFail::None;
    if (!open(fail)) return finish(SpeedPhase::Failed, fail);

    // 1) Gecikme: GET /__down?bytes=0, kLatencyProbes kez; yanit basligina kadar gecen sure.
    std::vector<double> rtts;
    for (int i = 0; i < cn::kLatencyProbes; ++i) {
        const auto s = check();
        if (s == Stop::Cancel || s == Stop::Gate) return stopped(s);
        if (s == Stop::TotalTime) return finish(SpeedPhase::Failed, SpeedFail::Budget);
        const auto x = get(0);
        if (x.stop == Stop::Cancel || x.stop == Stop::Gate) return stopped(x.stop);
        if (!x.ok) return finish(SpeedPhase::Failed, x.fail);
        rtts.push_back(x.header_s * 1000.0);
        tick(0.0, 0);
    }
    r.latency_ms = cn::median_ms(rtts);
    if (!r.latency_ms) return finish(SpeedPhase::Failed, SpeedFail::Http);
    const double rtt_s = *r.latency_ms / 1000.0;

    // 2-3) Indirme ve yukleme: kademeli parcalar, 90. yuzdelik.
    const cn::SpeedPlan plan;
    for (const SpeedPhase ph : {SpeedPhase::Download, SpeedPhase::Upload}) {
        enter(ph);
        start_probe();
        std::vector<cn::ChunkSample> chunks;
        bool limited = false;
        for (std::size_t done = 0;; ++done) {
            const auto s = check();
            if (s == Stop::Cancel || s == Stop::Gate) return stopped(s);
            const std::uint64_t n = plan.next_chunk(ph, done, phase_elapsed(), total_bytes_, total_elapsed());
            if (n == 0) {
                limited = true;
                break;
            }
            const auto x = ph == SpeedPhase::Download ? get(n) : post(n);
            total_bytes_ += x.used;
            if (x.stop == Stop::Cancel || x.stop == Stop::Gate) return stopped(x.stop);
            if (!x.ok) return finish(SpeedPhase::Failed, x.fail);
            if (x.bytes > 0) chunks.push_back({x.bytes, sample_seconds(x, rtt_s)});
            if (x.stop == Stop::PhaseTime || x.stop == Stop::TotalTime) {
                limited = true;
                break;
            }
        }
        const auto loaded = stop_probe();
        const auto v = cn::percentile90_mbps(chunks);
        if (!v) return finish(SpeedPhase::Failed, limited ? SpeedFail::Budget : SpeedFail::Http);
        if (ph == SpeedPhase::Download) {
            r.down_mbps = v;
            r.down_loaded_ms = loaded;
        } else {
            r.up_mbps = v;
            r.up_loaded_ms = loaded;
        }
        if (progress_) progress_(ph, *v, total_bytes_);
    }
    if (progress_) progress_(SpeedPhase::Done, r.down_mbps.value_or(0.0), total_bytes_);
    return finish(SpeedPhase::Done, SpeedFail::None);
}

}  // namespace

cn::SpeedResult SpeedTestRunner::run(const std::atomic<bool>& cancel, const std::function<bool()>& gate_blocked, Progress progress) {
    Run r(cancel, gate_blocked, progress);
    return r.go();
}

}  // namespace ef::platform::net
