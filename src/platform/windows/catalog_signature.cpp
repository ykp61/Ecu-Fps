#include "platform/windows/catalog_signature.h"

#include <windows.h>
#include <bcrypt.h>

#include <vector>

namespace ef::platform {

namespace {

bool sha256(std::string_view data, unsigned char out[32]) {
    return BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                     static_cast<ULONG>(data.size()), out, 32));
}

}  // namespace

bool verify_ecdsa_p256(std::string_view public_blob, std::string_view data, std::string_view signature) {
    if (public_blob.size() != sizeof(BCRYPT_ECCKEY_BLOB) + 64 || signature.size() != 64 || data.size() > 16u * 1024 * 1024) return false;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                            reinterpret_cast<PUCHAR>(const_cast<char*>(public_blob.data())), static_cast<ULONG>(public_blob.size()), 0)))
        return false;
    unsigned char digest[32];
    bool ok = sha256(data, digest);
    if (ok)
        ok = BCRYPT_SUCCESS(BCryptVerifySignature(key, nullptr, digest, 32, reinterpret_cast<PUCHAR>(const_cast<char*>(signature.data())),
                                                  static_cast<ULONG>(signature.size()), 0));
    BCryptDestroyKey(key);
    return ok;
}

}  // namespace ef::platform
