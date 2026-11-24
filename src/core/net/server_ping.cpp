#include "core/net/server_ping.h"

#include <array>

namespace ef::core::net {

std::optional<std::string> normalize_server_ipv4(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    if (text.empty() || text.size() > kServerAddrMaxLen) return std::nullopt;
    std::array<int, 4> oct{};
    std::size_t n = 0;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t start = i;
        int v = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            v = v * 10 + (text[i] - '0');
            if (v > 255) return std::nullopt;
            ++i;
        }
        const std::size_t len = i - start;
        if (len == 0 || len > 3) return std::nullopt;
        if (len > 1 && text[start] == '0') return std::nullopt;  // "01": sekizli/ondalik belirsizligi
        if (n >= 4) return std::nullopt;
        oct[n++] = v;
        if (i == text.size()) break;
        if (text[i] != '.') return std::nullopt;
        ++i;
        if (i == text.size()) return std::nullopt;  // sondaki nokta
    }
    if (n != 4) return std::nullopt;
    if (oct[0] == 0 || oct[0] == 127 || oct[0] >= 224) return std::nullopt;
    return std::to_string(oct[0]) + "." + std::to_string(oct[1]) + "." + std::to_string(oct[2]) + "." + std::to_string(oct[3]);
}

}  // namespace ef::core::net
