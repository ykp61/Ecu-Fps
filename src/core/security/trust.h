#pragma once
// SEC-04: gelistirme ve yayin guven ilkesi. Yayin derlemesi (EF_RELEASE_TRUST=1) dev muafiyeti icermez.

#include <string_view>

namespace ef::core::security {

enum class TrustPolicy { Dev, Release };

#if defined(EF_RELEASE_TRUST) && EF_RELEASE_TRUST
inline constexpr TrustPolicy kBuildPolicy = TrustPolicy::Release;
#else
inline constexpr TrustPolicy kBuildPolicy = TrustPolicy::Dev;
#endif

std::string_view to_string(TrustPolicy p) noexcept;

enum class SignatureStatus {
    Valid,      // Authenticode gecerli, guvenilen kok
    Unsigned,   // imza yok
    Invalid,    // imza var ama bozuk/degistirilmis (dosya kurcalanmis)
    Untrusted,  // imza gecerli ama zincir guvenilmez/iptal/suresi dolmus
    Unknown,    // dogrulama yapilamadi
};
std::string_view to_string(SignatureStatus s) noexcept;

struct TrustDecision {
    bool allowed = false;
    bool dev_unsigned = false;  // gelistirme muafiyetiyle izin verildi (kanit uretim imzasi sayilmaz)
    std::string_view reason;
    bool unsigned_alpha = false;  // ADR-034: release imzasiz alpha istisnasiyla izin verildi (imza yok)
};

// ADR-034: EF_UNSIGNED_ALPHA ile derlendi mi (release ilkesi imzasiz EXE'yi kabul eder).
#if defined(EF_UNSIGNED_ALPHA) && EF_UNSIGNED_ALPHA
inline constexpr bool kUnsignedAlpha = true;
#else
inline constexpr bool kUnsignedAlpha = false;
#endif

// Yetkili rol (broker) calisabilir mi?
//  Release: Valid; EF_UNSIGNED_ALPHA derlendiyse Unsigned da (unsigned_alpha=true).
//  Dev:     Valid veya Unsigned (dev_unsigned=true). Invalid/Untrusted/Unknown asla.
TrustDecision broker_trust(TrustPolicy policy, SignatureStatus self) noexcept;

// Yalniz dev derlemede var olan isaret (testler release ikilisinde bulunmadigini dogrular).
std::string_view dev_bypass_marker() noexcept;

// ADR-034 imzasiz alpha isareti (dev isaretinden ayri; yalniz EF_UNSIGNED_ALPHA derlemede bos degil).
std::string_view unsigned_alpha_marker() noexcept;

}  // namespace ef::core::security
