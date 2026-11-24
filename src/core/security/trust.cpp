#include "core/security/trust.h"

namespace ef::core::security {

std::string_view to_string(TrustPolicy p) noexcept { return p == TrustPolicy::Release ? "release" : "dev"; }

std::string_view to_string(SignatureStatus s) noexcept {
    switch (s) {
        case SignatureStatus::Valid: return "valid";
        case SignatureStatus::Unsigned: return "unsigned";
        case SignatureStatus::Invalid: return "invalid";
        case SignatureStatus::Untrusted: return "untrusted";
        case SignatureStatus::Unknown: return "unknown";
    }
    return "?";
}

TrustDecision broker_trust(TrustPolicy policy, SignatureStatus self) noexcept {
    if (self == SignatureStatus::Valid) return {true, false, "signed"};
    if (self == SignatureStatus::Invalid) return {false, false, "signature present but invalid (tampered)"};
    if (self == SignatureStatus::Untrusted) return {false, false, "signature chain not trusted"};
    if (self == SignatureStatus::Unknown) return {false, false, "signature could not be verified"};
#if !(defined(EF_RELEASE_TRUST) && EF_RELEASE_TRUST)
    // Gelistirme muafiyeti: yalniz dev derlemede derlenir.
    if (policy == TrustPolicy::Dev) return {true, true, "dev build: unsigned allowed"};
#endif
#if defined(EF_UNSIGNED_ALPHA) && EF_UNSIGNED_ALPHA
    // ADR-034 imzasiz alpha istisnasi: yalniz HIC imza yoksa (Unsigned). Kurcalanmis imza yukarida reddedildi.
    if (policy == TrustPolicy::Release) return {true, false, "release unsigned alpha: no signature present", true};
#endif
    (void)policy;
    return {false, false, "release build requires a valid signature"};
}

std::string_view unsigned_alpha_marker() noexcept {
#if defined(EF_UNSIGNED_ALPHA) && EF_UNSIGNED_ALPHA
    return "EF-UNSIGNED-ALPHA";
#else
    return {};
#endif
}

std::string_view dev_bypass_marker() noexcept {
#if !(defined(EF_RELEASE_TRUST) && EF_RELEASE_TRUST)
    return "EF-DEV-UNSIGNED-BROKER-ALLOWED";
#else
    return {};
#endif
}

}  // namespace ef::core::security
