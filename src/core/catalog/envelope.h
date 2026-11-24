#pragma once
// EF-X26/X27/X30: imzali zarf (CONTRACTS_TR.md). Mesaj = "EcuFps:v1:" + domain + ":" + key_id + NUL + payload.
// ECDSA P-256/SHA-256, imza r||s 64 bayt, base64url. Alan/anahtar degisirse dogrulama bozulur. Istemci yalniz genel
// anahtar tutar; ozel anahtar, Shopier tokeni veya posta parolasi EXE'ye girmez.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ef::core::catalog {

std::optional<std::string> base64url_decode(std::string_view s);
std::string envelope_message(std::string_view domain, std::string_view key_id, std::string_view payload);

// key_lookup(domain, key_id) -> genel anahtar blobu; bos = izinli degil. verify(blob, message, sig).
using KeyLookup = std::function<std::string(std::string_view domain, std::string_view key_id)>;
using RawVerifier = std::function<bool(std::string_view key_blob, std::string_view message, std::string_view sig)>;
// Zarf JSON'unu dogrular; basariliysa ozgun payload baytlari.
std::optional<std::string> open_envelope(std::string_view envelope_json, std::string_view domain, const KeyLookup& keys, const RawVerifier& verify);

// ---- X27: lisans hakki ----
struct Entitlement {
    std::int64_t license_id = 0;
    std::string install_key_id, product;
    std::vector<std::string> entitlements;
};
enum class EntitlementState { Valid, WrongDevice, Invalid };
EntitlementState check_entitlement(std::string_view payload, std::string_view my_install_key, Entitlement* out = nullptr);
// Lisans ne olursa olsun geri alma, kurtarma ve kendi verisini disa aktarma acik kalir.
inline constexpr bool rollback_requires_license() noexcept { return false; }

// ---- X26: surum manifesti ----
struct ReleaseManifest {
    std::string version, arch, url, sha256, channel, publisher;
    std::uint32_t min_os_build = 0;
    std::uint64_t size = 0, manifest_version = 0;
    bool halt = false;  // acil fren (istege bagli alan; yoksa false)
};
// Halted (denetim 3.4, acil fren): imzali manifestte "halt": true. Zorunlu guncelleme sunulmaz; manifest_version yine
// kaydedilir (eski, durdurulmamis manifest yeniden oynatilamaz). Devam icin daha yuksek manifest_version yayinlanir.
enum class ReleaseVerdict { Offer, NotNewer, WrongArch, WrongPublisher, WrongChannel, OsTooOld, BadUrl, Replay, Malformed, UnsignedBuild, Halted };
struct ReleaseContext {
    std::string current_version, arch = "x64", channel = "alpha", publisher;
    std::uint32_t os_build = 0;
    std::uint64_t last_manifest_version = 0;
    bool trusted_release_key = false;  // ADR-028: gomulu ECDSA yayin anahtari yoksa guncelleme calistirilmaz (B-003)
};
ReleaseVerdict evaluate_release(std::string_view payload, const ReleaseContext& ctx, ReleaseManifest* out = nullptr);

// Ayni birimde hazirlik -> yedek -> degistir -> saglik isareti -> onay. Her adimdan sonra kesilse de
// recover() son calisan surume veya tamamlanmis yeni surume doner. Yalniz EXE yolu degisir.
struct SwapFs {
    std::function<bool(const std::string& from, const std::string& to)> move;   // atomik ayni birim
    std::function<bool(const std::string& path)> exists;
    std::function<bool(const std::string& path)> remove;
};
enum class SwapStep { None, Staged, BackedUp, Replaced, Committed };
struct SwapPaths {
    std::string exe, staged, backup, marker;  // marker: "yeni surum calisti" isareti
};
// fail_after: test icin "guc kaybi" (bu adimdan sonra durur). Donus: tamamlanan son adim.
SwapStep run_swap(const SwapFs& fs, const SwapPaths& p, SwapStep fail_after = SwapStep::Committed);
// Acilista: yeni surum calismadiysa (isaret yok) eski surum geri getirilir.
bool recover_swap(const SwapFs& fs, const SwapPaths& p, bool new_version_healthy);

// ---- X30: duyurular ----
struct Notice {
    std::string id, title, text, link, min_product_version;
    std::int64_t expires_at = 0;
    std::uint32_t min_os_build = 0;
};
std::optional<std::vector<Notice>> parse_notices(std::string_view payload);
struct NoticeContext {
    bool opted_in = false, game_or_test_running = false;
    std::int64_t now_unix = 0;
    std::uint32_t os_build = 0;
    std::string product_version;
    std::vector<std::string> dismissed;
};
// Opt-in kapaliysa, oyun/test suruyorsa, suresi dolduysa, hedef disindaysa veya kapatildiysa gosterilmez.
bool notice_visible(const Notice& n, const NoticeContext& ctx);

}  // namespace ef::core::catalog
