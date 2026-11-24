#pragma once
// T17: Aygit Yoneticisi sorun kodlari (ConfigManager CM_PROB_*) icin saf model + kod -> metin eslemesi.
// Okuma platform katmaninda (salt okuma); burada yalniz kurallar vardir. Hicbir surucu/ayar yazilmaz.
//  - Kod 22 (kullanici tarafindan kapatilmis) ariza degil, bilgi satiridir.
//  - Liste en cok kMaxProblemDevices ogedir; kirpilirsa "truncated" isaretlenir.
//  - "Bulunamadi" ile "listelenemedi" ayridir (enumerated).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::inventory {

struct ProblemDevice {
    std::string name;
    std::string device_class;
    std::string instance_id;
    std::uint32_t problem_code = 0;
};

inline constexpr std::size_t kMaxProblemDevices = 64;
inline constexpr std::uint32_t kProblemDisabledByUser = 22;  // CM_PROB_DISABLED

// "dev.problem.<kod>" (bilinen kodlar); bilinmeyen kod icin "dev.problem.generic" ({code} parametreli, "Kod {n}").
std::string_view problem_text_key(std::uint32_t code) noexcept;
bool problem_is_known(std::uint32_t code) noexcept;
// Kod 22: kullanici kendisi kapatmis; ariza sayilmaz.
bool problem_is_informational(std::uint32_t code) noexcept;

struct ProblemDeviceReport {
    bool enumerated = false;  // liste alinabildi mi (false: "bulunamadi" denmez)
    bool truncated = false;   // kMaxProblemDevices asildi
    std::vector<ProblemDevice> devices;
};

// Sinir korunarak ekler; sinir asilirsa false doner ve truncated isaretlenir.
bool add_problem_device(ProblemDeviceReport& r, ProblemDevice d);
// Bilgi satiri olmayan (gercek ariza) aygit sayisi.
std::size_t problem_fault_count(const ProblemDeviceReport& r) noexcept;

}  // namespace ef::core::inventory
