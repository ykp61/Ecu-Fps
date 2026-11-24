#pragma once
// Bagimliliksiz, sikistirmasiz (stored) ZIP yazici. Destek paketi icin yeterli; Windows Gezgini
// ve Expand-Archive ile acilir.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::support {

std::uint32_t crc32(std::string_view data) noexcept;

class ZipWriter {
public:
    // name: ZIP icindeki yol ("logs/app.log"). ".." ve mutlak yol reddedilir.
    bool add(std::string name, std::string data);
    // Tum arsivi bellekte uretir.
    std::string finish() const;
    std::size_t entries() const noexcept { return files_.size(); }

private:
    struct File {
        std::string name;
        std::string data;
        std::uint32_t crc;
    };
    std::vector<File> files_;
};

}  // namespace ef::core::support
