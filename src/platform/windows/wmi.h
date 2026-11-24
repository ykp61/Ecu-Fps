#pragma once
// Minimal salt okuma WMI sorgu yardimcisi (COM). Yalniz SELECT; yazma/metot cagrisi yok.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "contracts/result.h"

namespace ef::platform::wmi {

struct Value {
    std::optional<std::int64_t> i;
    std::optional<std::string> s;
    std::vector<std::int64_t> ints;  // tamsayi dizileri (ornek: SecurityServicesRunning)
    bool is_null = true;
};
using Row = std::map<std::string, Value>;

// namespace_path: L"ROOT\\CIMV2" gibi. properties: okunacak alanlar.
contracts::Result<std::vector<Row>> query(const wchar_t* namespace_path, const wchar_t* wql,
                                          const std::vector<std::wstring>& properties);

}  // namespace ef::platform::wmi
