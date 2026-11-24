#pragma once

#include <string_view>

namespace ef::core {

struct Version {
    int major;
    int minor;
    int patch;
};

// Derleme zamaninda CMake proje surumunden gelir.
Version product_version() noexcept;

// Git tabanli derleme kimligi ("abc123def456", degisiklikli agacta "-dirty" eki). Ayni surumun derlemelerini ayirir.
std::string_view build_id() noexcept;

// Kullaniciya gosterilen urun adi.
std::wstring_view product_name() noexcept;

// Dagitim dosyasi adi (bosluksuz).
std::wstring_view executable_name() noexcept;


}  // namespace ef::core
