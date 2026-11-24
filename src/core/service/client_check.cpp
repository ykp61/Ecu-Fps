#include "core/service/client_check.h"

#include "core/security/dispatch.h"
#include "core/setup/layout.h"

namespace ef::core::service {

std::string_view to_string(ClientVerdict v) noexcept {
    switch (v) {
        case ClientVerdict::Allow: return "allow";
        case ClientVerdict::InstallDirInsecure: return "install_dir_insecure";
        case ClientVerdict::QueryFailed: return "query_failed";
        case ClientVerdict::WrongImage: return "wrong_image";
        case ClientVerdict::WrongFile: return "wrong_file";
        case ClientVerdict::SessionZero: return "session_zero";
        case ClientVerdict::NotInteractive: return "not_interactive";
        case ClientVerdict::NotAdmin: return "not_admin";
    }
    return "query_failed";
}

ClientVerdict check_client(const ClientFacts& c, const ServiceSelf& s) {
    if (!s.install_dir_acl_ok) return ClientVerdict::InstallDirInsecure;
    if (!c.image_ok || !c.file_ok || !c.token_ok || !s.installed_file_ok || c.user_sid.empty() || c.image.empty() ||
        s.installed_exe.empty() || c.pid == 0)
        return ClientVerdict::QueryFailed;
    // ADR-037: hedef oyuncu bir kullanici SID'i olmali (SYSTEM/hizmet hesabi/bozuk metin degil); sifir dosya kimligi
    // yarim kalmis sorgudur, iki taraf esit olsa bile kanit sayilmaz.
    if (!security::looks_like_user_sid(c.user_sid)) return ClientVerdict::QueryFailed;
    if (c.file == FileId{} || s.installed_file == FileId{}) return ClientVerdict::QueryFailed;
    if (!setup::same_path(c.image, s.installed_exe)) return ClientVerdict::WrongImage;
    if (!(c.file == s.installed_file)) return ClientVerdict::WrongFile;
    if (c.session == 0) return ClientVerdict::SessionZero;
    if (!c.interactive) return ClientVerdict::NotInteractive;
    if (!c.admin_member) return ClientVerdict::NotAdmin;  // K27: standart hesaplar UAC aracisina duser
    return ClientVerdict::Allow;
}

}  // namespace ef::core::service
