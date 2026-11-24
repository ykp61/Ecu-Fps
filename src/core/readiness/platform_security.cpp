#include "core/readiness/platform_security.h"

namespace ef::core::readiness {

namespace {

Check row(const char* id, const char* key) {
    Check c;
    c.id = id;
    c.section = Section::Hardware;
    c.level = Level::Info;  // durum bilgisi: "sorun yok" hukmu degil, eylem yok
    c.text_key = key;
    return c;
}

}  // namespace

bool firmware_change_suggestable(const PlatformSecurity& s) noexcept {
    return s.tpm_version.has_value() && s.secure_boot.has_value() && s.bitlocker_on.has_value();
}

std::vector<Check> platform_security_checks(const PlatformSecurity& s) {
    std::vector<Check> v;
    v.push_back(row("hardware.tpm", !s.tpm_version ? "chk.platform.tpm_unknown" : *s.tpm_version == 2 ? "chk.platform.tpm20"
                                    : *s.tpm_version == 1 ? "chk.platform.tpm12" : "chk.platform.tpm_none"));
    v.push_back(row("hardware.secure_boot", !s.secure_boot ? "chk.platform.sb_unknown" : *s.secure_boot ? "chk.platform.sb_on" : "chk.platform.sb_off"));
    v.push_back(row("hardware.bitlocker", !s.bitlocker_on ? "chk.platform.bl_unknown" : *s.bitlocker_on ? "chk.platform.bl_on" : "chk.platform.bl_off"));
    v.push_back(row("hardware.cstate", "chk.platform.cstate_unknown"));
    if (!firmware_change_suggestable(s)) v.push_back(row("hardware.firmware_gate", "chk.platform.firmware_gate"));
    for (auto& c : v)
        if (c.text_key.find("unknown") != std::string::npos) c.level = Level::Unknown;
    return v;
}

std::vector<Check> mpo_checks(std::optional<std::uint32_t> mode, bool display_symptom, bool read_ok) {
    std::vector<Check> v;
    if (!display_symptom) return v;  // MPO yalniz goruntu sorunu baglaminda anlatilir; Boost varsayilani degil
    Check c;
    c.id = "display.mpo";
    c.section = Section::Display;
    c.level = Level::Info;
    if (!read_ok) {
        c.level = Level::Unknown;
        c.text_key = "chk.display.mpo_unknown";
    } else {
        c.text_key = !mode ? "chk.display.mpo_default" : *mode == 5 ? "chk.display.mpo_off" : "chk.display.mpo_other";
    }
    v.push_back(std::move(c));
    return v;
}

}  // namespace ef::core::readiness
