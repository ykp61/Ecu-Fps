#include "core/setup/acl_rules.h"

#include <string_view>

namespace ef::core::setup {

namespace {
bool privileged(std::string_view sid) {
    return sid == kSidSystem || sid == kSidAdministrators || sid == kSidTrustedInstaller;
}
}  // namespace

bool install_dir_acl_ok(const AclFacts& f) {
    if (!f.dacl_present || !f.dacl_protected) return false;
    if (!privileged(f.owner_sid)) return false;
    for (const auto& a : f.aces) {
        if (!a.allow) continue;  // reddeden ACE erisimi yalniz daraltir
        if ((a.mask & kWriteAccessMask) != 0 && !privileged(a.sid)) return false;
    }
    return true;
}

}  // namespace ef::core::setup
