#pragma once
// ADR-028: zorunlu guncelleme yayin anahtari (ECDSA P-256 genel anahtari, BCRYPT_ECCPUBLIC_BLOB). Bu dosyayi
// scripts/release_sign.py keygen uretir; ozel anahtar EXE'ye ve depoya girmez. Elle duzenleme.
#include <string_view>

namespace ef::core::update {

inline constexpr std::string_view kReleaseKeyId{"rel-7e5df9066e6c47ad"};  // zarf key_id
inline constexpr std::string_view kReleasePublicBlob{"\x45\x43\x53\x31\x20\x00\x00\x00\x39\x4f\xff\x7c\xa8\xf4\x25\x1c\x94\xd9\xe0\xbd\x6a\x58\x74\x63\x5f\x47\x7c\xbc\x98\x71\x9b\xc8\xf4\xf4\x53\x9b\x51\x54\x6b\xe2\x5f\xe1\x54\xe5\x5d\xb6\xa5\xa7\x38\x8a\xb2\x51\x61\xd5\xbc\xe1\xec\xe0\xdf\x75\x39\xb0\x31\x7f\x42\x47\x9d\x9d\x73\x8c\x35\xcf", 72};  // BCRYPT_ECCPUBLIC_BLOB (8 + 64 bayt)

inline constexpr bool has_release_key() noexcept { return !kReleaseKeyId.empty() && !kReleasePublicBlob.empty(); }

}  // namespace ef::core::update
