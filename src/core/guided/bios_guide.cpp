#include "core/guided/bios_guide.h"

#include <algorithm>
#include <cctype>

namespace ef::core::guided {

namespace {

std::string lower(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

bool has(const std::string& hay, std::string_view needle) { return hay.find(needle) != std::string::npos; }

// Sozcuk basi eslesmesi ("hp" gibi kisa adlar baska sozcugun icinde eslesmesin).
bool starts_word(const std::string& hay, std::string_view w) {
    for (std::size_t p = hay.find(w); p != std::string::npos; p = hay.find(w, p + 1))
        if (p == 0 || !std::isalnum(static_cast<unsigned char>(hay[p - 1]))) return true;
    return false;
}

enum class Cpu { Unknown, Intel, Amd };

Cpu cpu_of(std::string_view v) {
    const auto l = lower(v);
    if (has(l, "intel")) return Cpu::Intel;
    if (has(l, "amd") || has(l, "advanced micro")) return Cpu::Amd;
    return Cpu::Unknown;
}

const char* profile_key(std::string_view vendor, Cpu cpu) {
    if (vendor == "asus") return cpu == Cpu::Intel ? "bios.guide.profile.xmp" : cpu == Cpu::Amd ? "bios.guide.profile.expo_docp" : "bios.guide.profile.xmp_expo";
    if (vendor == "msi") return cpu == Cpu::Intel ? "bios.guide.profile.xmp_axmp" : cpu == Cpu::Amd ? "bios.guide.profile.expo_axmp" : "bios.guide.profile.xmp_expo";
    if (vendor == "gigabyte" || vendor == "asrock") return cpu == Cpu::Intel ? "bios.guide.profile.xmp" : "bios.guide.profile.xmp_expo";
    return "bios.guide.profile.xmp_expo";
}

}  // namespace

BiosGuide bios_guide_for(std::string_view board_manufacturer, std::string_view cpu_vendor) {
    const auto m = lower(board_manufacturer);
    const Cpu cpu = cpu_of(cpu_vendor);
    BiosGuide g;
    if (has(m, "asus")) g.vendor_key = "asus";
    else if (has(m, "micro-star") || starts_word(m, "msi")) g.vendor_key = "msi";
    else if (has(m, "gigabyte") || has(m, "aorus")) g.vendor_key = "gigabyte";
    else if (has(m, "asrock")) g.vendor_key = "asrock";
    else if (has(m, "dell") || has(m, "alienware") || starts_word(m, "hp") || has(m, "hewlett") || has(m, "lenovo") || has(m, "acer"))
        g.vendor_key = "oem";
    else g.vendor_key = "generic";

    const auto& v = g.vendor_key;
    g.enter_key_text_key = v == "asus" || v == "asrock" ? "bios.guide.enter.del_f2"
                           : v == "msi" || v == "gigabyte" ? "bios.guide.enter.del"
                           : v == "oem"                    ? "bios.guide.enter.oem"
                                                           : "bios.guide.enter.generic";
    g.profile_name_key = profile_key(v, cpu);

    if (v == "oem") {
        // Hazir sistem / dizustu: bellek profili secenegi genellikle gorunmez; olmayan secenek icin adim uydurulmaz.
        g.step_keys = {"bios.guide.step.oem_not_exposed", "bios.guide.step.oem_manual", "bios.guide.step.verify"};
        return g;
    }
    g.step_keys = {"bios.guide.step.note", "bios.guide.step.enter"};
    if (v != "generic") g.step_keys.push_back("bios.guide.step.vendor_" + v);
    for (const char* k : {"bios.guide.step.profile", "bios.guide.step.only", "bios.guide.step.save", "bios.guide.step.fallback",
                          "bios.guide.step.verify", "bios.guide.step.risk"})
        g.step_keys.push_back(k);
    return g;
}

std::string bios_vendor_name_key(const BiosGuide& g) { return "bios.guide.vendor." + g.vendor_key; }

}  // namespace ef::core::guided
