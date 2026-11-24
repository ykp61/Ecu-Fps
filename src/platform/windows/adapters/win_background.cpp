#include "platform/windows/adapters/win_background.h"

#include <windows.h>
#include <winspool.h>
#include <versionhelpers.h>

#include <cctype>
#include <memory>

namespace ef::platform::adapters {

using contracts::ErrorCode;

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Yerel yazicilar: herhangi bir kuyrukta is var mi, ya da yazici agda paylasiliyor mu (sunucu rolu).
contracts::Result<bool> print_queue_busy() {
    using R = contracts::Result<bool>;
    DWORD need = 0, count = 0;
    if (EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, nullptr, 0, &need, &count)) return false;  // yerel yazici yok
    const DWORD e = GetLastError();
    // Belirsizlik (Spooler'a ulasilamadi vb.) mesgul degil diye YORUMLANMAZ: hata -> adaptor reddeder.
    if (e != ERROR_INSUFFICIENT_BUFFER || need == 0) return R::fail(ErrorCode::IoError, "EnumPrinters (" + std::to_string(e) + ")");
    auto buf = std::make_unique<unsigned char[]>(need);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, buf.get(), need, &need, &count))
        return R::fail(ErrorCode::IoError, "EnumPrinters (" + std::to_string(GetLastError()) + ")");
    const auto* p = reinterpret_cast<const PRINTER_INFO_2W*>(buf.get());
    for (DWORD i = 0; i < count; ++i) {
        if (p[i].cJobs > 0) return true;
        if (p[i].Attributes & PRINTER_ATTRIBUTE_SHARED) return true;
    }
    return false;
}
}  // namespace

contracts::Result<bool> WinServiceBusyProbe::busy(const std::string& service) {
    const std::string n = lower(service);
    if (n != "spooler" && n != "fax") return false;
    // Sunucu SKU: yazdirma sunucusu veya etki alani denetleyicisi olabilir (Microsoft: durdurma).
    if (IsWindowsServer()) return true;
    // Fax isleri Fax yazicisi kuyrugundan gecer; giden kutusu kuyrugu ayrica okunmaz (belgede acik konu).
    return print_queue_busy();
}

}  // namespace ef::platform::adapters
