#pragma once
// EF-X22: bellek dogrulamasi icin adrese bagli desen (yalniz baslik; benchmark kutuphanesi core'a baglanmaz).
// Tekduze desen adres karisikligini ve tek bit hatasinin yerini goremez.
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ef::core::measure {

inline std::uint64_t address_pattern(std::size_t index, std::uint64_t seed) noexcept {
    std::uint64_t z = static_cast<std::uint64_t>(index) + seed + 0x9E3779B97F4A7C15ull;  // splitmix64
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline void fill_address_pattern(std::uint64_t* w, std::size_t n, std::uint64_t seed) noexcept {
    for (std::size_t i = 0; i < n; ++i) w[i] = address_pattern(i, seed);
}

// Ilk hatali kelimenin indeksi; hata yoksa nullopt.
inline std::optional<std::size_t> verify_address_pattern(const std::uint64_t* w, std::size_t n, std::uint64_t seed) noexcept {
    for (std::size_t i = 0; i < n; ++i)
        if (w[i] != address_pattern(i, seed)) return i;
    return std::nullopt;
}

}  // namespace ef::core::measure
