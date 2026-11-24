// Ecu Fps shim: ToNarrow gerceklemesi (WideCharToMultiByte, UTF-8).
#include "String.h"
#include "../win/WinAPI.h"

namespace pmon::util::str
{
    std::string ToNarrow(const std::wstring& wide) noexcept
    {
        try {
            if (wide.empty()) {
                return {};
            }
            const int need = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                                 nullptr, 0, nullptr, nullptr);
            if (need <= 0) {
                return {};
            }
            std::string out((size_t)need, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                out.data(), need, nullptr, nullptr);
            return out;
        }
        catch (...) {
            return {};
        }
    }
}
