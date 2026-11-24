#include "platform/windows/background/process_scan.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <memory>

#include "platform/windows/signature.h"

namespace ef::platform::background {

namespace {
using core::perf::BgProcessInfo;

std::string narrow(const wchar_t* w, std::size_t n) {
    if (n == 0) return {};
    const int m = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    if (m <= 0) return {};
    std::string s(static_cast<std::size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), s.data(), m, nullptr, nullptr);
    return s;
}
std::wstring lower_w(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

struct HandleCloser {
    using pointer = HANDLE;
    void operator()(HANDLE h) const {
        if (h) CloseHandle(h);
    }
};
using Handle = std::unique_ptr<void, HandleCloser>;

std::uint64_t ft_u64(const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

// Tek ornek: CPU zamani (100 ns), I/O bayti, olusturma zamani. Erisim yoksa ok=false.
struct Sample {
    bool ok = false;
    std::uint64_t creation = 0, cpu100ns = 0, io_bytes = 0;
};
Sample sample_of(HANDLE h) {
    Sample s;
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(h, &c, &e, &k, &u)) return s;
    s.creation = ft_u64(c);
    s.cpu100ns = ft_u64(k) + ft_u64(u);
    IO_COUNTERS io{};
    if (GetProcessIoCounters(h, &io)) s.io_bytes = io.ReadTransferCount + io.WriteTransferCount + io.OtherTransferCount;
    s.ok = true;
    return s;
}

// Token: ayni kullanici mi, yukseltilmis mi. Okunamazsa muhafazakar (baska kullanici, yukseltilmis).
void token_facts(HANDLE proc, const std::vector<BYTE>& self_user, bool& same_user, bool& elevated) {
    same_user = false;
    elevated = true;
    HANDLE t = nullptr;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &t)) return;
    Handle tok(t);
    DWORD n = 0;
    GetTokenInformation(t, TokenUser, nullptr, 0, &n);
    if (n && !self_user.empty()) {
        std::vector<BYTE> buf(n);
        if (GetTokenInformation(t, TokenUser, buf.data(), n, &n))
            same_user = EqualSid(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid,
                                 reinterpret_cast<const TOKEN_USER*>(self_user.data())->User.Sid) != FALSE;
    }
    TOKEN_ELEVATION el{};
    DWORD rn = 0;
    if (GetTokenInformation(t, TokenElevation, &el, sizeof el, &rn)) elevated = el.TokenIsElevated != 0;
}

std::vector<BYTE> self_token_user() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return {};
    Handle tok(t);
    DWORD n = 0;
    GetTokenInformation(t, TokenUser, nullptr, 0, &n);
    if (!n) return {};
    std::vector<BYTE> buf(n);
    if (!GetTokenInformation(t, TokenUser, buf.data(), n, &n)) return {};
    return buf;
}

std::wstring image_path_of(HANDLE h) {
    wchar_t path[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(h, 0, path, &n)) return {};
    return std::wstring(path, n);
}

// Gomulu Authenticode imzalayicisinin goruntu adi (ornek "Discord Inc."). Alinamazsa bos.
std::string signer_name(const std::wstring& file) {
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, file.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr))
        return {};
    std::string out;
    DWORD n = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &n) && n) {
        std::vector<BYTE> buf(n);
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &n)) {
            auto* si = reinterpret_cast<CMSG_SIGNER_INFO*>(buf.data());
            CERT_INFO ci{};
            ci.Issuer = si->Issuer;
            ci.SerialNumber = si->SerialNumber;
            if (PCCERT_CONTEXT cc = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT,
                                                               &ci, nullptr)) {
                const DWORD len = CertGetNameStringW(cc, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
                if (len > 1) {
                    std::wstring w(len, L'\0');
                    CertGetNameStringW(cc, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, w.data(), len);
                    w.resize(len - 1);
                    out = narrow(w.data(), w.size());
                }
                CertFreeCertificateContext(cc);
            }
        }
    }
    if (store) CertCloseStore(store, 0);
    if (msg) CryptMsgClose(msg);
    return out;
}
}  // namespace

ProcessScanner::SigInfo ProcessScanner::signature_of(const std::wstring& path) {
    const std::wstring key = lower_w(path);
    if (auto it = sig_cache_.find(key); it != sig_cache_.end()) return it->second;
    SigInfo s;
    if (core::security::SignatureStatus::Valid == verify_file_signature(std::filesystem::path(path))) {
        s.publisher = signer_name(path);
        s.valid = !s.publisher.empty();  // yayinci alinamayan imza "dogrulanmis yayinci" sayilmaz
    }
    if (sig_cache_.size() > 256) sig_cache_.clear();
    sig_cache_[key] = s;
    return s;
}

std::vector<BgProcessInfo> ProcessScanner::scan(bool game_active, int sample_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<BgProcessInfo> out;
    const auto self_user = self_token_user();
    DWORD self_session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &self_session);
    std::wstring self_path;
    {
        wchar_t p[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameW(nullptr, p, static_cast<DWORD>(std::size(p)));
        self_path = lower_w(std::wstring(p, n));
    }

    struct Item {
        BgProcessInfo info;
        Sample first;
        Handle h;  // PROCESS_QUERY_LIMITED_INFORMATION; ikinci ornek ayni tutamakla (kimlik sabit)
    };
    std::vector<Item> items;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4) continue;
        const std::string name = narrow(pe.szExeFile, std::wcslen(pe.szExeFile));
        const auto cls = core::perf::bg_class_for_name(name);
        if (!cls) continue;  // listede olmayan ad: ne okunur ne siniflanir
        Handle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID));
        Item it;
        it.info.pid = pe.th32ProcessID;
        it.info.name = name;
        if (!h) {
            // Erisilemeyen surec: guvenli degil yonunde (yol/imza yok, baska kullanici varsayilir).
            it.info.user_session = false;
            it.info.elevated = true;
            out.push_back(std::move(it.info));
            continue;
        }
        const std::wstring path = image_path_of(h.get());
        it.info.path = narrow(path.data(), path.size());
        it.first = sample_of(h.get());
        it.info.creation = it.first.creation;
        DWORD session = 0;
        const bool have_session = ProcessIdToSessionId(pe.th32ProcessID, &session) != FALSE;
        bool same_user = false, elevated = true;
        token_facts(h.get(), self_user, same_user, elevated);
        it.info.user_session = have_session && session == self_session && session != 0 && same_user;
        it.info.is_service = have_session && session == 0;
        it.info.elevated = elevated;
        it.info.is_self = pe.th32ProcessID == GetCurrentProcessId() || (!path.empty() && lower_w(path) == self_path);
        PROCESS_MEMORY_COUNTERS pmc{};
        if (K32GetProcessMemoryInfo(h.get(), &pmc, sizeof pmc)) it.info.working_set_bytes = pmc.WorkingSetSize;
        if (!path.empty() && it.info.user_session && !it.info.elevated) {
            const auto sig = signature_of(path);
            it.info.publisher = sig.publisher;
            it.info.signature_valid = sig.valid;
        }
        if (game_active && *cls == core::perf::BgClass::Launcher) it.info.is_game_or_launcher = true;
        if (game_active && *cls == core::perf::BgClass::Chat) it.info.overlay_hint = true;  // overlay algilanamaz: muhafazakar
        it.h = std::move(h);
        items.push_back(std::move(it));
    }
    CloseHandle(snap);

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    Sleep(static_cast<DWORD>(std::max(sample_ms, 100)));
    QueryPerformanceCounter(&t1);
    const double wall_s = static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const double cores = std::max<DWORD>(si.dwNumberOfProcessors, 1);

    // Etkilesim: on plan penceresi + kullanicinin son girdisi (GetLastInputInfo). 60 sn girdi yoksa etkilesim yok.
    const std::uint64_t now_ms = GetTickCount64();
    DWORD fg_pid = 0;
    if (HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &fg_pid);
    if (fg_pid) last_fg_ms_[fg_pid] = now_ms;
    LASTINPUTINFO li{sizeof(li), 0};
    const bool input_recent = !GetLastInputInfo(&li) || static_cast<std::uint64_t>(GetTickCount() - li.dwTime) < 60'000u;
    if (last_fg_ms_.size() > 512) last_fg_ms_.clear();

    for (auto& it : items) {
        if (it.h && it.first.ok) {
            const Sample s2 = sample_of(it.h.get());
            if (s2.ok && s2.creation == it.first.creation && wall_s > 0.0) {
                const double cpu_s = static_cast<double>(s2.cpu100ns - std::min(s2.cpu100ns, it.first.cpu100ns)) / 1e7;
                it.info.cpu_pct = std::clamp(cpu_s / (wall_s * cores) * 100.0, 0.0, 100.0);
                const double io_bps = static_cast<double>(s2.io_bytes - std::min(s2.io_bytes, it.first.io_bytes)) / wall_s;
                it.info.busy_transfer = io_bps > 256.0 * 1024.0;  // indirme/yukleme/senkron suruyor sayilir
            } else {
                // Olculemedi: CPU 0 (esigin altinda -> aday olmaz) ve yogun is varsayilir.
                it.info.cpu_pct = 0.0;
                it.info.busy_transfer = true;
            }
        } else {
            it.info.busy_transfer = true;
        }
        it.info.foreground = fg_pid != 0 && it.info.pid == fg_pid;
        const auto lf = last_fg_ms_.find(it.info.pid);
        const bool fg_recent = lf != last_fg_ms_.end() && now_ms - lf->second < 60'000u;
        it.info.recent_interaction = input_recent && fg_recent;
        out.push_back(std::move(it.info));
    }
    return out;
}

}  // namespace ef::platform::background
