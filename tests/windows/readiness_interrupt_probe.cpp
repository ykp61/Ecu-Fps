// B5 Windows probu: bu PC'de okunan gercek MSI/kesme durumu ve bellek esik olcumu ile uretilen bulgular.
// SALT OKUMA: kayit defterine/aygita yazmaz, pencere acmaz, yonetici istemez, bakim isi (bosaltma) CALISTIRMAZ.
// ctest'e eklenmez (ortama bagli cikti).
#include <cstdio>
#include <string>

#include "core/readiness/readiness.h"
#include "core/readiness/readiness_interrupts.h"
#include "platform/windows/readiness/interrupt_reader.h"
#include "platform/windows/readiness/memory_reader.h"

using namespace ef;
namespace cr = core::readiness;

namespace {
const char* level_name(cr::Level l) {
    switch (l) {
        case cr::Level::Good: return "Good";
        case cr::Level::Advice: return "Advice";
        case cr::Level::Problem: return "Problem";
        case cr::Level::Unknown: return "Unknown";
        case cr::Level::Info: return "Info";
    }
    return "?";
}
template <typename T>
std::string ou(const std::optional<T>& v) {
    return v ? std::to_string(static_cast<unsigned long long>(*v)) : "null";
}
void print_checks(const std::vector<cr::Check>& v) {
    for (const auto& c : v) {
        std::printf("  check id=%s level=%s key=%s action=%d", c.id.c_str(), level_name(c.level), c.text_key.c_str(), static_cast<int>(c.action));
        for (const auto& [k, val] : c.args) std::printf(" %s=\"%s\"", k.c_str(), val.c_str());
        std::printf("\n");
    }
}
}  // namespace

int main() {
    std::printf("== interrupt devices (read-only) ==\n");
    const auto devs = platform::readiness::read_interrupt_devices();
    for (const auto& d : devs)
        std::printf("  role=%s name=\"%s\" readable=%d msi_supported=%s priority=%s\n",
                    d.role == cr::InterruptDeviceRole::GameGpu ? "gpu" : "net", d.name.c_str(), d.readable ? 1 : 0,
                    ou(d.msi_supported).c_str(), ou(d.priority).c_str());
    print_checks(cr::interrupt_checks(devs));

    std::printf("== memory pressure (read-only) ==\n");
    const auto m = platform::readiness::read_memory_pressure();
    std::printf("  total_bytes=%s standby_bytes=%s free_bytes=%s\n", ou(m.total_bytes).c_str(), ou(m.standby_bytes).c_str(), ou(m.free_bytes).c_str());
    const auto pc = cr::standby_pressure_checks(m);
    std::printf("  finding_count=%zu\n", pc.size());
    print_checks(pc);
    return 0;
}
