#pragma once
// Bagimliliksiz SHA-256 (FIPS 180-4). Journal butunlugu ve kimlik ozeti icin.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ef::core::recovery {

std::array<std::uint8_t, 32> sha256(std::string_view data) noexcept;
std::string sha256_hex(std::string_view data);

// R-F1: akis halinde ozet (buyuk dosyalar bellege alinmadan). Sonuc sha256_hex ile ayni.
class Sha256Stream {
public:
    void update(std::string_view data) noexcept;
    std::string hex_final();  // cagrildiktan sonra nesne yeniden kullanilmaz

private:
    std::uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf_[64] = {};
    std::size_t used_ = 0;
    std::uint64_t total_ = 0;
};

}  // namespace ef::core::recovery
