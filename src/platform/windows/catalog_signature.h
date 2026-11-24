#pragma once
// EF-X05: katalog verisi icin ECDSA P-256 / SHA-256 imza dogrulamasi (Windows CNG). Yalniz genel anahtar tutulur;
// ozel anahtar EXE'ye girmez. Urun genel anahtari henuz yok (anahtar saklama karari bekliyor): bos anahtar her
// imzayi reddeder.
#include <string>
#include <string_view>

namespace ef::platform {

// public_blob: BCRYPT_ECCPUBLIC_BLOB (P-256). signature: r||s (64 bayt).
bool verify_ecdsa_p256(std::string_view public_blob, std::string_view data, std::string_view signature);

}  // namespace ef::platform
