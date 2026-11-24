#include "core/inventory/problem_devices.h"

#include <utility>

namespace ef::core::inventory {

std::string_view problem_text_key(std::uint32_t code) noexcept {
    switch (code) {
        case 1: return "dev.problem.1";
        case 10: return "dev.problem.10";
        case 12: return "dev.problem.12";
        case 14: return "dev.problem.14";
        case 18: return "dev.problem.18";
        case 22: return "dev.problem.22";
        case 24: return "dev.problem.24";
        case 28: return "dev.problem.28";
        case 31: return "dev.problem.31";
        case 37: return "dev.problem.37";
        case 39: return "dev.problem.39";
        case 43: return "dev.problem.43";
        case 52: return "dev.problem.52";
        default: return "dev.problem.generic";
    }
}

bool problem_is_known(std::uint32_t code) noexcept { return problem_text_key(code) != "dev.problem.generic"; }

bool problem_is_informational(std::uint32_t code) noexcept { return code == kProblemDisabledByUser; }

bool add_problem_device(ProblemDeviceReport& r, ProblemDevice d) {
    if (r.devices.size() >= kMaxProblemDevices) {
        r.truncated = true;
        return false;
    }
    r.devices.push_back(std::move(d));
    return true;
}

std::size_t problem_fault_count(const ProblemDeviceReport& r) noexcept {
    std::size_t n = 0;
    for (const auto& d : r.devices)
        if (!problem_is_informational(d.problem_code)) ++n;
    return n;
}

}  // namespace ef::core::inventory
