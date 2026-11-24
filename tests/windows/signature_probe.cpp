// SEC-04: ef_signature_probe <file>  -> imza durumunu yazar; cikis: 0 valid,1 unsigned,2 invalid,3 untrusted,4 unknown
// ADR-034: ef_signature_probe --trust <file> -> bu derlemenin ilkesiyle (kBuildPolicy) broker_trust karari;
//          cikis: 10 izinli (imzali), 11 izinli imzasiz alpha, 12 izinli dev muafiyeti, 13 reddedildi.
#include <cstdio>
#include <cwchar>

#include "core/security/trust.h"
#include "platform/windows/signature.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 9;
    if (std::wcscmp(argv[1], L"--trust") == 0) {
        if (argc < 3) return 9;
        using namespace ef::core::security;
        const auto d = broker_trust(kBuildPolicy, ef::platform::verify_file_signature(argv[2]));
        std::printf("policy=%.*s allowed=%d unsigned_alpha=%d dev_unsigned=%d reason=%.*s\n",
                    static_cast<int>(to_string(kBuildPolicy).size()), to_string(kBuildPolicy).data(), d.allowed ? 1 : 0,
                    d.unsigned_alpha ? 1 : 0, d.dev_unsigned ? 1 : 0, static_cast<int>(d.reason.size()), d.reason.data());
        if (!d.allowed) return 13;
        if (d.unsigned_alpha) return 11;
        if (d.dev_unsigned) return 12;
        return 10;
    }
    const auto s = ef::platform::verify_file_signature(argv[1]);
    std::printf("%.*s\n", static_cast<int>(ef::core::security::to_string(s).size()), ef::core::security::to_string(s).data());
    return static_cast<int>(s);
}
