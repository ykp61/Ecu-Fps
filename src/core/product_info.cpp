#include "core/product_info.h"

#include "ef_build_id.h"  // her derlemede uretilir (cmake/gen_build_id.cmake)

namespace ef::core {

Version product_version() noexcept {
    return {EF_VERSION_MAJOR, EF_VERSION_MINOR, EF_VERSION_PATCH};
}

std::string_view build_id() noexcept { return EF_BUILD_ID_STR; }

std::wstring_view product_name() noexcept { return L"Ecu Fps"; }

std::wstring_view executable_name() noexcept { return L"EcuFps.exe"; }

}  // namespace ef::core
