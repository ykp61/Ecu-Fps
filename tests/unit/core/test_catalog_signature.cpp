// EF-X05 T03: gercek CNG ECDSA P-256 imza yolu. Anahtar test sirasinda uretilir; urun anahtari degildir.
#include <windows.h>
#include <bcrypt.h>

#include <string>
#include <vector>

#include "core/catalog/catalog.h"
#include "ef_test.h"
#include "platform/windows/catalog_signature.h"

namespace {
struct TestKey {
    BCRYPT_KEY_HANDLE h = nullptr;
    std::string pub;
    TestKey() {
        if (!BCRYPT_SUCCESS(BCryptGenerateKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, &h, 256, 0)) || !BCRYPT_SUCCESS(BCryptFinalizeKeyPair(h, 0))) return;
        ULONG n = 0;
        BCryptExportKey(h, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &n, 0);
        pub.resize(n);
        BCryptExportKey(h, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(pub.data()), n, &n, 0);
    }
    ~TestKey() {
        if (h) BCryptDestroyKey(h);
    }
    std::string sign(const std::string& data) const {
        unsigned char d[32];
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), d, 32);
        std::string sig(64, '\0');
        ULONG n = 0;
        BCryptSignHash(h, nullptr, d, 32, reinterpret_cast<PUCHAR>(sig.data()), 64, &n, 0);
        return sig;
    }
};
}  // namespace

EF_TEST(x05_real_ecdsa_signature_accepts_exact_bytes_only) {
    TestKey k;
    EF_CHECK(k.h != nullptr && !k.pub.empty());
    const std::string data = R"({"schema":1,"kind":"bios","version":2,"issued_unix":1,"expires_unix":9999999999})";
    const auto sig = k.sign(data);
    EF_CHECK(ef::platform::verify_ecdsa_p256(k.pub, data, sig));
    std::string tampered = data;
    tampered[tampered.size() - 2] = '8';  // tek bayt degisti
    EF_CHECK(!ef::platform::verify_ecdsa_p256(k.pub, tampered, sig));
    EF_CHECK(!ef::platform::verify_ecdsa_p256({}, data, sig));  // urun anahtari yok: her imza reddedilir
    const ef::core::catalog::SignatureVerifier v = [&k](std::string_view d, std::string_view s) { return ef::platform::verify_ecdsa_p256(k.pub, d, s); };
    EF_CHECK(ef::core::catalog::verify_catalog(data, sig, v, "bios", 100, std::uint64_t{1}) == ef::core::catalog::CatalogStatus::Ok);
    EF_CHECK(ef::core::catalog::verify_catalog(tampered, sig, v, "bios", 100, std::uint64_t{1}) == ef::core::catalog::CatalogStatus::BadSignature);
}
