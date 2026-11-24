#pragma once
// F-79: calisan sureclerin exe adlari (salt okuma; PROCESS_QUERY_LIMITED_INFORMATION).

#include <string>
#include <vector>

namespace ef::platform::readiness {

std::vector<std::string> read_running_exe_names();  // kucuk harf basename; erisilemeyen atlanir

}  // namespace ef::platform::readiness
