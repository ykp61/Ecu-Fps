#include "core/security/trust.h"
#include "ef_test.h"

using namespace ef::core::security;

EF_TEST(release_policy_requires_valid_signature) {
    const auto valid = broker_trust(TrustPolicy::Release, SignatureStatus::Valid);
    EF_CHECK(valid.allowed);
    EF_CHECK(!valid.unsigned_alpha);
    EF_CHECK(!valid.dev_unsigned);
    // Kurcalanmis / guvenilmez / dogrulanamayan imza her derlemede reddedilir (alpha istisnasi dahil).
    for (const auto s : {SignatureStatus::Invalid, SignatureStatus::Untrusted, SignatureStatus::Unknown}) {
        const auto d = broker_trust(TrustPolicy::Release, s);
        EF_CHECK(!d.allowed);
        EF_CHECK(!d.unsigned_alpha);
    }
}

// ADR-034: release + imzasiz yalniz EF_UNSIGNED_ALPHA derlendiyse ve etiketli olarak kabul edilir.
EF_TEST(release_unsigned_only_with_unsigned_alpha_flag) {
    const auto d = broker_trust(TrustPolicy::Release, SignatureStatus::Unsigned);
#if defined(EF_UNSIGNED_ALPHA) && EF_UNSIGNED_ALPHA
    EF_CHECK(kUnsignedAlpha);
    EF_CHECK(d.allowed);
    EF_CHECK(d.unsigned_alpha);
    EF_CHECK(!d.dev_unsigned);  // dev muafiyeti degil
    EF_CHECK(d.reason == "release unsigned alpha: no signature present");
    EF_CHECK(unsigned_alpha_marker() == "EF-UNSIGNED-ALPHA");
#else
    EF_CHECK(!kUnsignedAlpha);
    EF_CHECK(!d.allowed);
    EF_CHECK(!d.unsigned_alpha);
    EF_CHECK(unsigned_alpha_marker().empty());
#endif
    EF_CHECK(unsigned_alpha_marker() != dev_bypass_marker() || unsigned_alpha_marker().empty());
}

EF_TEST(dev_policy_allows_only_unsigned_never_tampered) {
    const auto d = broker_trust(TrustPolicy::Dev, SignatureStatus::Unsigned);
    if constexpr (kBuildPolicy == TrustPolicy::Dev) {
        EF_CHECK(d.allowed);
        EF_CHECK(d.dev_unsigned);  // kanit "dev" olarak etiketlenir
        EF_CHECK(!dev_bypass_marker().empty());
    } else {
        EF_CHECK(!d.allowed);  // release derlemede muafiyet kodu yok (alpha istisnasi Dev ilkesine uygulanmaz)
        EF_CHECK(dev_bypass_marker().empty());
    }
    EF_CHECK(!broker_trust(TrustPolicy::Dev, SignatureStatus::Invalid).allowed);
    EF_CHECK(!broker_trust(TrustPolicy::Dev, SignatureStatus::Untrusted).allowed);
    EF_CHECK(!broker_trust(TrustPolicy::Dev, SignatureStatus::Valid).dev_unsigned);
}
