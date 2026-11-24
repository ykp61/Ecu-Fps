#include "contracts/inventory.h"

namespace ef::contracts {

std::string_view to_string(Provenance p) noexcept {
    switch (p) {
        case Provenance::None: return "none";
        case Provenance::Api: return "api";
        case Provenance::Registry: return "registry";
        case Provenance::Wmi: return "wmi";
        case Provenance::Smbios: return "smbios";
        case Provenance::Derived: return "derived";
    }
    return "?";
}

std::string_view to_string(PolicyState s) noexcept {
    switch (s) {
        case PolicyState::Absent: return "absent";
        case PolicyState::Present: return "present";
        case PolicyState::Unknown: return "unknown";
    }
    return "?";
}

void resolve_target_user(UserContext& u) {
    if (!u.session_user_sid.has()) {
        u.target_user_sid = Field<std::string>::unknown("session user unknown: " + u.session_user_sid.reason);
        u.different_admin = Field<bool>::unknown("session user unknown");
        return;
    }
    u.target_user_sid = Field<std::string>::known(*u.session_user_sid.value, Provenance::Derived);
    if (u.process_user_sid.has()) {
        u.different_admin = Field<bool>::known(*u.process_user_sid.value != *u.session_user_sid.value, Provenance::Derived);
    } else {
        u.different_admin = Field<bool>::unknown("process user unknown");
    }
}

}  // namespace ef::contracts
