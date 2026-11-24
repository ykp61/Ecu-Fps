#include "core/support/zip_writer.h"

#include <array>

namespace ef::core::support {

namespace {
constexpr std::array<std::uint32_t, 256> make_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
    return t;
}
constexpr auto kTable = make_table();

void u16(std::string& o, std::uint32_t v) {
    o += static_cast<char>(v & 0xFF);
    o += static_cast<char>((v >> 8) & 0xFF);
}
void u32(std::string& o, std::uint32_t v) {
    u16(o, v & 0xFFFF);
    u16(o, v >> 16);
}
}  // namespace

std::uint32_t crc32(std::string_view data) noexcept {
    std::uint32_t c = 0xFFFFFFFFu;
    for (const char ch : data) c = kTable[(c ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool ZipWriter::add(std::string name, std::string data) {
    if (name.empty() || name.size() > 200 || name.front() == '/' || name.front() == '\\') return false;
    if (name.find("..") != std::string::npos || name.find(':') != std::string::npos) return false;
    for (char& c : name)
        if (c == '\\') c = '/';
    if (data.size() > 0xFFFFFFF0u) return false;  // ZIP64 desteklenmez
    for (const auto& f : files_)
        if (f.name == name) return false;
    const auto crc = crc32(data);
    files_.push_back({std::move(name), std::move(data), crc});
    return true;
}

std::string ZipWriter::finish() const {
    // DOS zamani sabit (1980-01-01): icerik deterministik, makine saati sizmaz.
    constexpr std::uint32_t kTime = 0, kDate = (0 << 9) | (1 << 5) | 1;
    constexpr std::uint32_t kUtf8Flag = 1u << 11;
    std::string out, central;
    for (const auto& f : files_) {
        const auto offset = static_cast<std::uint32_t>(out.size());
        const auto size = static_cast<std::uint32_t>(f.data.size());
        const auto nlen = static_cast<std::uint32_t>(f.name.size());
        u32(out, 0x04034b50); u16(out, 20); u16(out, kUtf8Flag); u16(out, 0); u16(out, kTime); u16(out, kDate);
        u32(out, f.crc); u32(out, size); u32(out, size); u16(out, nlen); u16(out, 0);
        out += f.name;
        out += f.data;

        u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, kUtf8Flag); u16(central, 0);
        u16(central, kTime); u16(central, kDate); u32(central, f.crc); u32(central, size); u32(central, size);
        u16(central, nlen); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0);
        u32(central, offset);
        central += f.name;
    }
    const auto cd_offset = static_cast<std::uint32_t>(out.size());
    const auto cd_size = static_cast<std::uint32_t>(central.size());
    out += central;
    const auto n = static_cast<std::uint32_t>(files_.size());
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, n); u16(out, n); u32(out, cd_size); u32(out, cd_offset);
    u16(out, 0);
    return out;
}

}  // namespace ef::core::support
