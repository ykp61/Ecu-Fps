#pragma once
// Enum <-> kararli metin eslemesi. Bilinmeyen metin nullopt doner; sessiz default yok.

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace ef::contracts {

template <typename E, std::size_t N>
using EnumTable = std::array<std::pair<E, std::string_view>, N>;

template <typename E, std::size_t N>
constexpr std::string_view enum_to_text(const EnumTable<E, N>& t, E v) noexcept {
    for (const auto& [e, s] : t)
        if (e == v) return s;
    return "?";
}

template <typename E, std::size_t N>
constexpr std::optional<E> enum_from_text(const EnumTable<E, N>& t, std::string_view s) noexcept {
    for (const auto& [e, name] : t)
        if (name == s) return e;
    return std::nullopt;
}

}  // namespace ef::contracts
