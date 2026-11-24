// Ecu Fps shim: Intel PresentMon CommonUtilities/str/String.h yerine (yalniz ToNarrow).
// Arayuz Intel PresentMon v2.6.0'dan turetilmistir (Copyright (C) 2022 Intel Corporation, MIT).
#pragma once
#include <string>

namespace pmon::util::str
{
    // UTF-16 -> UTF-8. Hata durumunda bos dize doner (noexcept).
    std::string ToNarrow(const std::wstring& wide) noexcept;
}
