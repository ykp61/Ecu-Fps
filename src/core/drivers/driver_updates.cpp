#include "core/drivers/driver_updates.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "core/adapters/windows/update_check.h"

namespace ef::core::drivers {

namespace {

std::string lower(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}
std::string strip_braces(std::string s) {
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}') s = s.substr(1, s.size() - 2);
    return s;
}
bool starts_with_ci(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && lower(s.substr(0, prefix.size())) == lower(prefix);
}
bool contains(std::string_view s, std::string_view n) { return s.find(n) != std::string_view::npos; }

// Aygit sinifi: ad (WUA DriverClass) ya da GUID (suslu parantezsiz, kucuk harf).
struct ClassName {
    std::string_view name, guid;
    DeviceKind kind;
};
constexpr std::array<ClassName, 9> kClasses{{
    {"display", "4d36e968-e325-11ce-bfc1-08002be10318", DeviceKind::Display},
    {"media", "4d36e96c-e325-11ce-bfc1-08002be10318", DeviceKind::Audio},
    {"audioendpoint", "c166523c-fe0c-4a94-a586-f1a80cfbbf3e", DeviceKind::Audio},
    {"net", "4d36e972-e325-11ce-bfc1-08002be10318", DeviceKind::Network},
    {"bluetooth", "e0cbf06c-cd8b-4647-bb8a-263b43f0f974", DeviceKind::Network},
    {"system", "4d36e97d-e325-11ce-bfc1-08002be10318", DeviceKind::Chipset},
    {"scsiadapter", "4d36e97b-e325-11ce-bfc1-08002be10318", DeviceKind::Storage},
    {"hdc", "4d36e96a-e325-11ce-bfc1-08002be10318", DeviceKind::Storage},
    {"firmware", "f2e7dd72-6468-4e36-b6f1-6488f42c1b52", DeviceKind::Firmware},
}};

}  // namespace

bool is_firmware(const DriverUpdate& u) {
    const std::string c = strip_braces(lower(u.driver_class));
    if (c == "firmware" || c == strip_braces(std::string(kFirmwareClassGuid))) return true;
    return starts_with_ci(u.hardware_id, "UEFI\\RES_");
}

DeviceKind device_kind(const DriverUpdate& u) {
    if (is_firmware(u)) return DeviceKind::Firmware;
    const std::string c = strip_braces(lower(u.driver_class));
    for (const auto& k : kClasses)
        if (c == k.name || c == k.guid) return k.kind;
    return DeviceKind::Other;
}

std::string version_from_title(std::string_view title) {
    while (!title.empty() && title.back() == ' ') title.remove_suffix(1);
    std::string_view v;
    if (!title.empty() && title.back() == ')') {
        // "Apple, Inc. USBDevice Driver Update (552.0.0.0)" (gercek WUA basligi, 2026-11-13)
        const auto open = title.rfind('(');
        if (open == std::string_view::npos) return {};
        v = title.substr(open + 1, title.size() - open - 2);
    } else {
        const auto p = title.rfind(" - ");
        if (p == std::string_view::npos) return {};
        v = title.substr(p + 3);
    }
    if (v.empty() || v.size() > 32) return {};
    bool dot = false;
    for (const char ch : v) {
        if (ch == '.') dot = true;
        else if (ch < '0' || ch > '9') return {};
    }
    if (!dot || v.front() == '.' || v.back() == '.') return {};
    return std::string(v);
}

std::string display_name(const DriverUpdate& u) {
    if (u.title.empty()) return u.model;
    const std::string v = version_from_title(u.title);
    if (v.empty()) return u.title;
    std::string_view t = u.title;
    while (!t.empty() && t.back() == ' ') t.remove_suffix(1);
    if (!t.empty() && t.back() == ')') t = t.substr(0, t.rfind('('));
    else t = t.substr(0, t.rfind(" - "));
    while (!t.empty() && (t.back() == ' ' || t.back() == '-')) t.remove_suffix(1);
    return t.empty() ? u.title : std::string(t);
}

ScanFailure classify_hresult(std::int32_t hr) {
    if (hr >= 0) return ScanFailure::None;
    switch (static_cast<std::uint32_t>(hr)) {
        // Baglanti yok / ad cozulemedi / sunucuya ulasilamadi
        case 0x8024402Cu:  // WU_E_PT ailesi: sunucu adi cozulemedi
        case 0x80072EE7u:  // ERROR_INTERNET_NAME_NOT_RESOLVED
        case 0x80072EFDu:  // ERROR_INTERNET_CANNOT_CONNECT
        case 0x80072EFEu:  // ERROR_INTERNET_CONNECTION_ABORTED
        case 0x80072EE2u:  // ERROR_INTERNET_TIMEOUT
        case 0x8024401Bu:  // WU_E_PT_HTTP_STATUS_PROXY_AUTH_REQ
        case 0x80244022u:  // WU_E_PT_HTTP_STATUS_SERVICE_UNAVAIL
            return ScanFailure::Offline;
        // Windows Update hizmeti kapali / durdu
        case 0x80070422u:  // ERROR_SERVICE_DISABLED
        case 0x80240001u:  // WU_E_NO_SERVICE
        case 0x8024001Eu:  // WU_E_SERVICE_STOP
        case 0x8024A000u:  // WU_E_AU_NOSERVICE
            return ScanFailure::ServiceDisabled;
        // Ilke / WSUS aramayi kapatiyor
        case 0x8024002Eu:  // WU_E_WU_DISABLED
        case 0x80240025u:  // WU_E_USER_ACCESS_DISABLED
        case 0x8024500Cu:  // WU_E_REDIRECTOR_CONNECT_POLICY
            return ScanFailure::Policy;
        case 0x800705B4u:  // ERROR_TIMEOUT
            return ScanFailure::Timeout;
        case 0x8024000Bu:  // WU_E_CALL_CANCELLED
        case 0x800704C7u:  // ERROR_CANCELLED
            return ScanFailure::Cancelled;
        default: return ScanFailure::Error;
    }
}

void sort_updates(std::vector<DriverUpdate>& v) {
    std::stable_sort(v.begin(), v.end(), [](const DriverUpdate& a, const DriverUpdate& b) {
        const auto ka = device_kind(a), kb = device_kind(b);
        if (ka != kb) return static_cast<int>(ka) < static_cast<int>(kb);
        return a.title < b.title;
    });
}

ScanView scan_view_from(const SearchOutcome& o) {
    ScanView v;
    v.hresult = o.hresult;
    if (!o.ok) {
        v.state = ScanState::Failed;
        v.failure = o.failure == ScanFailure::None ? ScanFailure::Error : o.failure;
        return v;
    }
    v.updates = o.updates;
    sort_updates(v.updates);
    for (const auto& u : v.updates) (is_firmware(u) ? v.list_only : v.installable) += 1;
    v.state = v.updates.empty() ? ScanState::UpToDate : ScanState::Found;
    return v;
}

DriverScanner::DriverScanner(SourceFactory factory, std::chrono::milliseconds timeout) : factory_(std::move(factory)), timeout_(timeout) {}

DriverScanner::~DriverScanner() {
    cancel();
    join();
}

bool DriverScanner::start(Done on_done) {
    std::lock_guard lk(mu_);
    if (running_.load()) return false;
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this, done = std::move(on_done)] {
        const auto deadline = std::chrono::steady_clock::now() + timeout_;
        const auto expired = [deadline] { return std::chrono::steady_clock::now() >= deadline; };
        SearchOutcome o;
        // Kaynak uretimi de is parcacigindadir: firlatan fabrika (bellek / COM) std::terminate yerine Error olur.
        try {
            auto src = factory_ ? factory_() : nullptr;
            if (!src) o.failure = ScanFailure::Error;
            else o = src->search([this, expired] { return cancel_.load() || expired(); });
        } catch (...) {
            o = SearchOutcome{};
            o.failure = ScanFailure::Error;
        }
        // Kullanici iptali her sonucun onundedir (gec gelen basari da gosterilmez); sure dolduysa neden zaman asimidir.
        // Sure dolduktan sonra gelen basari kabul edilmez: sayfa "Denetlenemedi" der. Ne iptal ne sure dolmusken kaynagin
        // kendisi "iptal edildi" dediyse (WU_E_CALL_CANCELLED: hizmet aramayi birakti) bu zaman asimi ya da kullanici
        // durdurmasi DEGILDIR: hata kodu korunarak Error gosterilir.
        if (cancel_.load()) {
            o = SearchOutcome{};
            o.failure = ScanFailure::Cancelled;
        } else if (expired()) {
            o = SearchOutcome{};
            o.failure = ScanFailure::Timeout;
        } else if (!o.ok && o.failure == ScanFailure::Cancelled) {
            o.updates.clear();
            o.failure = ScanFailure::Error;
        }
        const ScanView v = scan_view_from(o);
        running_ = false;
        if (done) done(v);
    });
    return true;
}

void DriverScanner::cancel() { cancel_ = true; }

void DriverScanner::join() {
    std::lock_guard lk(mu_);
    if (worker_.joinable()) worker_.join();
}

VendorPage gpu_vendor_page(std::uint32_t pci_vendor_id) {
    using adapters::windows::GpuVendor;
    const GpuVendor v = adapters::windows::vendor_from_pci(pci_vendor_id);
    const std::string_view url = adapters::windows::official_driver_url(v);
    switch (v) {
        case GpuVendor::Nvidia: return {"NVIDIA", url};
        case GpuVendor::Amd: return {"AMD", url};
        case GpuVendor::Intel: return {"Intel", url};
        case GpuVendor::Other: break;
    }
    return {};
}

VendorPage chipset_vendor_page(std::string_view cpu_vendor) {
    using adapters::windows::GpuVendor;
    const std::string v = lower(cpu_vendor);
    if (contains(v, "genuineintel") || v == "intel") return {"Intel", adapters::windows::official_driver_url(GpuVendor::Intel)};
    if (contains(v, "authenticamd") || v == "amd") return {"AMD", adapters::windows::official_driver_url(GpuVendor::Amd)};
    return {};
}

VendorPage board_vendor_page(std::string_view board_vendor) {
    const std::string v = lower(board_vendor);
    if (contains(v, "asustek") || v.rfind("asus", 0) == 0) return {"ASUS", "https://www.asus.com/support/download-center/"};
    if (contains(v, "micro-star") || v.rfind("msi", 0) == 0) return {"MSI", "https://www.msi.com/support/download"};
    if (contains(v, "gigabyte")) return {"GIGABYTE", "https://www.gigabyte.com/Support"};
    if (contains(v, "asrock")) return {"ASRock", "https://www.asrock.com/support/index.asp"};
    return {};
}

}  // namespace ef::core::drivers
