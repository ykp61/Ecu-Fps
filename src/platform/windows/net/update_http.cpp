// Zorunlu guncelleme: WinHTTP ile manifest/EXE indirme. Kurallar update_http.h basliginda. Govde yalniz bellekte
// toplanir; dosyaya yazma, dogrulama ve degisim App'tedir (imzali manifest boyutu/SHA-256 ile karsilastirilir).
#include "platform/windows/net/update_http.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

#include "core/update/update_check.h"

namespace ef::platform::net {

namespace {

using Clock = std::chrono::steady_clock;
constexpr long long kSendTimeoutMs = 5000, kReceiveTimeoutMs = 15000;
constexpr std::size_t kIoBytes = 64 * 1024;

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

// Bekleyen tek guncelleme istegi (manifest ve EXE ayni is parcaciginda sirayla). abort_update_requests tutamaci baska
// is parcacigindan kapatir; sahibi kapanmis tutamaci ikinci kez kapatmaz (kilit altinda tek kapatan).
struct ActiveSlot {
    std::mutex mu;
    HINTERNET req = nullptr;
    bool aborted = false;
};
ActiveSlot& active_slot() {
    static ActiveSlot s;
    return s;
}

class ActiveRequest {
public:
    explicit ActiveRequest(HINTERNET h) : h_(h) {
        if (!h_) return;
        auto& s = active_slot();
        std::lock_guard lk(s.mu);
        s.req = h_;
        s.aborted = false;
    }
    ActiveRequest(const ActiveRequest&) = delete;
    ActiveRequest& operator=(const ActiveRequest&) = delete;
    ~ActiveRequest() {
        if (!h_) return;
        auto& s = active_slot();
        std::lock_guard lk(s.mu);
        if (s.req == h_) {
            if (!s.aborted) WinHttpCloseHandle(h_);
            s.req = nullptr;
            s.aborted = false;
        } else {
            WinHttpCloseHandle(h_);
        }
    }
    HINTERNET get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }

private:
    HINTERNET h_ = nullptr;
};

FetchFail map_error(DWORD e) {
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT: return FetchFail::Timeout;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR:
        case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT:
        case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
            return FetchFail::Offline;
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
            return FetchFail::Tls;
        default: return FetchFail::Http;
    }
}

// Yalniz yazdirilabilir ASCII adres (kullanici verisi yok; manifest url'si ayrica onek denetiminden gecti).
std::optional<std::wstring> ascii_wide(std::string_view s) {
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) {
        if (c < 0x21 || c > 0x7E) return std::nullopt;
        w += static_cast<wchar_t>(c);
    }
    return w;
}

std::optional<std::string> narrow_ascii(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) {
        if (c < 0x21 || c > 0x7E) return std::nullopt;
        s += static_cast<char>(c);
    }
    return s;
}

DWORD status_code(HINTERNET req) {
    DWORD code = 0, len = sizeof(code);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len,
                             WINHTTP_NO_HEADER_INDEX))
        return 0;
    return code;
}

std::optional<std::wstring> location(HINTERNET req) {
    DWORD len = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &len, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0 || len > 8192) return std::nullopt;
    std::wstring w(len / sizeof(wchar_t) + 1, L'\0');
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, w.data(), &len, WINHTTP_NO_HEADER_INDEX))
        return std::nullopt;
    w.resize(len / sizeof(wchar_t));
    return w;
}

// Sunucunun bildirdigi govde boyu; yoksa/bicimsizse 0 (sinir okuma sirasinda yine uygulanir).
std::uint64_t content_length(HINTERNET req) {
    wchar_t buf[32] = {};
    DWORD len = sizeof(buf);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buf, &len, WINHTTP_NO_HEADER_INDEX)) return 0;
    std::uint64_t v = 0;
    for (const wchar_t* p = buf; *p; ++p) {
        if (*p < L'0' || *p > L'9') return 0;
        v = v * 10 + static_cast<std::uint64_t>(*p - L'0');
        if (v > (1ull << 40)) return v;  // asiri buyuk: cagiran sinirla reddeder
    }
    return v;
}

}  // namespace

FetchResult update_get(std::string_view url_in, std::uint64_t max_bytes, int deadline_ms, const std::atomic<bool>& cancel,
                       const FetchProgress& progress) {
    FetchResult r;
    const auto start = Clock::now();
    auto left_ms = [&]() -> long long {
        if (deadline_ms <= 0) return kReceiveTimeoutMs;
        return deadline_ms - std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    };
    auto fail = [&r, &cancel](FetchFail f) {
        r.body.clear();
        r.fail = cancel.load() ? FetchFail::Cancelled : f;  // kapanista kapatilan tutamacin hatasi iptal sayilir
        return r;
    };
    Handle session(WinHttpOpen(L"EcuFps", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return fail(map_error(GetLastError()));
    std::string url(url_in);
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        if (cancel.load()) return fail(FetchFail::Cancelled);
        // Her adim: yalniz HTTPS ve yalniz izinli host (ilk adres de ayni kurala tabi).
        if (!core::update::update_host_allowed(url)) return fail(hop == 0 ? FetchFail::Http : FetchFail::Redirect);
        const auto wurl = ascii_wide(url);
        if (!wurl) return fail(FetchFail::Redirect);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwHostNameLength = static_cast<DWORD>(-1);
        uc.dwUrlPathLength = static_cast<DWORD>(-1);
        uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl->c_str(), static_cast<DWORD>(wurl->size()), 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS ||
            uc.nPort != INTERNET_DEFAULT_HTTPS_PORT || !uc.lpszHostName)
            return fail(FetchFail::Redirect);
        const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path = uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : std::wstring();
        if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (path.empty()) path = L"/";

        const long long remain = left_ms();
        if (remain <= 0) return fail(FetchFail::Timeout);
        const int connect_ms = static_cast<int>(std::min<long long>(core::update::kConnectTimeoutMs, remain));
        const int send_ms = static_cast<int>(std::min<long long>(kSendTimeoutMs, remain));
        const int io_ms = static_cast<int>(std::min<long long>(kReceiveTimeoutMs, remain));
        WinHttpSetTimeouts(session.get(), connect_ms, connect_ms, send_ms, io_ms);
        Handle connect(WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connect) return fail(map_error(GetLastError()));
        ActiveRequest req(WinHttpOpenRequest(connect.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE));
        if (!req) return fail(map_error(GetLastError()));
        if (cancel.load()) return fail(FetchFail::Cancelled);  // kayittan once gelen iptal kacmaz
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;  // yonlendirme elle izlenir (host izin listesi)
        WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.get(), nullptr))
            return fail(map_error(GetLastError()));
        const DWORD code = status_code(req.get());
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            const auto loc = location(req.get());
            const auto next = loc ? narrow_ascii(*loc) : std::nullopt;
            if (!next || next->empty()) return fail(FetchFail::Redirect);
            if (next->front() == '/') {  // goreli yonlendirme: ayni sema + host
                const auto sep = url.find("://");
                const auto host_end = url.find('/', sep == std::string::npos ? 0 : sep + 3);
                url = url.substr(0, host_end) + *next;
            } else {
                url = *next;
            }
            continue;
        }
        if (code != 200) return fail(FetchFail::Http);
        const std::uint64_t total = content_length(req.get());
        if (total > max_bytes) return fail(FetchFail::TooLarge);
        if (total) r.body.reserve(static_cast<std::size_t>(total));
        std::vector<char> buf(kIoBytes);
        auto last_progress = Clock::now();
        for (;;) {
            if (cancel.load()) return fail(FetchFail::Cancelled);
            if (deadline_ms > 0 && left_ms() <= 0) return fail(FetchFail::Timeout);
            DWORD got = 0;
            if (!WinHttpReadData(req.get(), buf.data(), static_cast<DWORD>(buf.size()), &got)) return fail(map_error(GetLastError()));
            if (got == 0) break;
            if (r.body.size() + got > max_bytes) return fail(FetchFail::TooLarge);
            r.body.append(buf.data(), got);
            const auto now = Clock::now();
            if (progress && now - last_progress >= std::chrono::milliseconds(100)) {
                last_progress = now;
                progress(r.body.size(), total);
            }
        }
        if (total && r.body.size() != total) return fail(FetchFail::Http);  // yarim govde sonuc sayilmaz
        if (progress) progress(r.body.size(), total);
        return r;
    }
    return fail(FetchFail::Redirect);  // cok fazla yonlendirme
}

void abort_update_requests() {
    auto& s = active_slot();
    std::lock_guard lk(s.mu);
    if (s.req && !s.aborted) {
        WinHttpCloseHandle(s.req);  // bekleyen eszamanli cagri ERROR_WINHTTP_OPERATION_CANCELLED ile doner
        s.aborted = true;
    }
}

}  // namespace ef::platform::net
