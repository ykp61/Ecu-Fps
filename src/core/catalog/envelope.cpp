#include "core/catalog/envelope.h"

#include <algorithm>

#include "core/catalog/catalog.h"
#include "core/serialization/json.h"

namespace ef::core::catalog {

namespace {

constexpr std::size_t kMaxPayload = 256 * 1024;

int b64v(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

bool https_allowed(std::string_view url) {
    if (url.substr(0, 8) != "https://" || url.size() > 512) return false;
    for (char c : url)
        if (c <= ' ' || c == '"' || c == '<' || c == '>' || c == '\\') return false;
    const auto host_end = url.find('/', 8);
    const auto host = url.substr(8, host_end == std::string_view::npos ? std::string_view::npos : host_end - 8);
    return !host.empty() && host.find('@') == std::string_view::npos;
}

std::vector<std::string> string_array(const json::Value& o, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* a = o.find(key); a && a->type() == json::Value::Type::Array)
        for (const auto& v : a->items())
            if (v.type() == json::Value::Type::String) out.push_back(v.as_string());
    return out;
}

}  // namespace

std::optional<std::string> base64url_decode(std::string_view s) {
    if (s.size() % 4 == 1 || s.size() > kMaxPayload * 2) return std::nullopt;
    std::string out;
    out.reserve(s.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        const int v = b64v(c);
        if (v < 0) return std::nullopt;  // dolgu, bosluk, standart base64 karakterleri kabul edilmez
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    if (bits > 0 && (acc & ((1u << bits) - 1)) != 0) return std::nullopt;  // kanonik olmayan kodlama
    return out;
}

std::string envelope_message(std::string_view domain, std::string_view key_id, std::string_view payload) {
    std::string m = "EcuFps:v1:";
    m += domain;
    m += ':';
    m += key_id;
    m.push_back('\0');
    m += payload;
    return m;
}

std::optional<std::string> open_envelope(std::string_view text, std::string_view domain, const KeyLookup& keys, const RawVerifier& verify) {
    if (text.size() > kMaxPayload * 2 || !keys || !verify) return std::nullopt;
    const auto doc = json::parse(text);
    if (!doc.ok()) return std::nullopt;
    const auto d = json::get_string(doc.value(), "domain", 16);
    const auto k = json::get_string(doc.value(), "key_id", 64);
    const auto p = json::get_string(doc.value(), "payload", kMaxPayload * 2);
    const auto s = json::get_string(doc.value(), "sig", 128);
    if (!d.ok() || !k.ok() || !p.ok() || !s.ok() || d.value() != domain) return std::nullopt;
    const auto blob = keys(domain, k.value());
    if (blob.empty()) return std::nullopt;  // izin listesinde olmayan anahtar
    auto payload = base64url_decode(p.value());
    const auto sig = base64url_decode(s.value());
    if (!payload || !sig || sig->size() != 64 || payload->size() > kMaxPayload) return std::nullopt;
    if (!verify(blob, envelope_message(domain, k.value(), *payload), *sig)) return std::nullopt;
    return payload;  // imza ozgun baytlar uzerinde; yeniden serilestirilmez
}

EntitlementState check_entitlement(std::string_view payload, std::string_view mine, Entitlement* out) {
    const auto doc = json::parse(payload);
    if (!doc.ok()) return EntitlementState::Invalid;
    const auto id = json::get_int(doc.value(), "license_id", 1, 0x7FFFFFFFFFFFFFFFll);
    const auto inst = json::get_string(doc.value(), "install_key_id", 64);
    const auto prod = json::get_string(doc.value(), "product", 64);
    if (!id.ok() || !inst.ok() || !prod.ok()) return EntitlementState::Invalid;
    if (inst.value() != mine) return EntitlementState::WrongDevice;  // baska cihazin tokeni kabul edilmez
    if (out) *out = {id.value(), inst.value(), prod.value(), string_array(doc.value(), "entitlements")};
    return EntitlementState::Valid;
}

ReleaseVerdict evaluate_release(std::string_view payload, const ReleaseContext& ctx, ReleaseManifest* out) {
    const auto doc = json::parse(payload);
    if (!doc.ok()) return ReleaseVerdict::Malformed;
    const auto& o = doc.value();
    const auto ver = json::get_string(o, "version", 32), arch = json::get_string(o, "arch", 16), url = json::get_string(o, "url", 512),
               sha = json::get_string(o, "sha256", 64), channel = json::get_string(o, "channel", 16), pub = json::get_string(o, "publisher", 128);
    const auto min_os = json::get_int(o, "min_os_build", 0, 0xFFFFFFFF), size = json::get_int(o, "size", 1, 1ll << 31),
               mver = json::get_int(o, "manifest_version", 1, 0x7FFFFFFFFFFFFFFFll);
    if (!ver.ok() || !arch.ok() || !url.ok() || !sha.ok() || !channel.ok() || !pub.ok() || !min_os.ok() || !size.ok() || !mver.ok() ||
        sha.value().size() != 64)
        return ReleaseVerdict::Malformed;
    ReleaseManifest m{ver.value(), arch.value(), url.value(), sha.value(), channel.value(), pub.value(),
                      static_cast<std::uint32_t>(min_os.value()), static_cast<std::uint64_t>(size.value()), static_cast<std::uint64_t>(mver.value())};
    if (o.find("halt")) {  // istege bagli; varsa bool olmali
        const auto hb = json::get_bool(o, "halt");
        if (!hb.ok()) return ReleaseVerdict::Malformed;
        m.halt = hb.value();
    }
    if (out) *out = m;
    if (m.manifest_version <= ctx.last_manifest_version) return ReleaseVerdict::Replay;
    if (m.publisher != ctx.publisher) return ReleaseVerdict::WrongPublisher;  // guvenlik reddi: mimari farkiyla karismaz
    if (m.channel != ctx.channel) return ReleaseVerdict::WrongChannel;
    if (m.arch != ctx.arch) return ReleaseVerdict::WrongArch;
    if (m.min_os_build > ctx.os_build) return ReleaseVerdict::OsTooOld;
    if (!https_allowed(m.url)) return ReleaseVerdict::BadUrl;
    if (m.halt) return ReleaseVerdict::Halted;  // imza + yayinci + kanal dogrulandi: guncelleme durduruldu
    const auto newer = compare_vendor_versions(m.version, ctx.current_version);
    if (!newer) return ReleaseVerdict::Malformed;  // karsilastirilamayan surum "daha yeni degil" diye gizlenmez
    if (*newer <= 0) return ReleaseVerdict::NotNewer;
    if (!ctx.trusted_release_key) return ReleaseVerdict::UnsignedBuild;  // gomulu yayin anahtari yokken otomatik calistirma yok
    return ReleaseVerdict::Offer;
}

SwapStep run_swap(const SwapFs& fs, const SwapPaths& p, SwapStep fail_after) {
    if (!fs.exists(p.staged)) return SwapStep::None;
    SwapStep done = SwapStep::Staged;
    if (done == fail_after) return done;
    if (!fs.move(p.exe, p.backup)) return done;  // eski surum yedege (silinmez)
    done = SwapStep::BackedUp;
    if (done == fail_after) return done;
    if (!fs.move(p.staged, p.exe)) {
        (void)fs.move(p.backup, p.exe);
        return SwapStep::Staged;
    }
    done = SwapStep::Replaced;
    if (done == fail_after) return done;
    return SwapStep::Replaced;  // onay yeni surum calisip isaret yazinca recover_swap'ta
}

bool recover_swap(const SwapFs& fs, const SwapPaths& p, bool healthy) {
    const bool have_exe = fs.exists(p.exe), have_backup = fs.exists(p.backup);
    if (healthy && have_exe) {  // yeni surum calisti: yedek kaldirilir, isaret yalniz yedek gercekten silinince temizlenir
        if (have_backup && !fs.remove(p.backup)) return false;  // yedek duruyor: saglik isareti korunur (sonraki deneme onaylar)
        (void)fs.remove(p.marker);
        return true;
    }
    if (have_backup) {  // yeni surum yok/calismadi: son calisan surum geri
        if (have_exe) {
            const std::string failed = p.staged + ".failed";
            if (fs.exists(failed)) (void)fs.remove(failed);  // onceki basarisiz denemenin kalintisi
            if (!fs.move(p.exe, failed)) return false;
        }
        return fs.move(p.backup, p.exe);
    }
    return have_exe;  // degisim baslamamis
}

std::optional<std::vector<Notice>> parse_notices(std::string_view payload) {
    const auto doc = json::parse(payload);
    if (!doc.ok()) return std::nullopt;
    const auto* items = doc.value().find("items");
    if (!items || items->type() != json::Value::Type::Array || items->items().size() > 20) return std::nullopt;
    std::vector<Notice> out;
    for (const auto& it : items->items()) {
        const auto id = json::get_string(it, "id", 64), text = json::get_string(it, "text", 2000), link = json::get_string(it, "link", 512);
        const auto title = json::get_string(it, "title", 200), minv = json::get_string(it, "min_product_version", 32);
        const auto exp = json::get_int(it, "expires_at", 1, 0x7FFFFFFFFFFFFFFFll), os = json::get_int(it, "min_os_build", 0, 0xFFFFFFFF);
        if (!id.ok() || id.value().empty() || !text.ok() || !link.ok() || !title.ok() || !minv.ok() || !exp.ok() || !os.ok()) return std::nullopt;
        for (const auto* s : {&text.value(), &title.value()})
            if (s->find_first_of("<>") != std::string::npos) return std::nullopt;  // betik/HTML yok
        if (!https_allowed(link.value())) return std::nullopt;
        if (std::any_of(out.begin(), out.end(), [&](const Notice& n) { return n.id == id.value(); })) return std::nullopt;
        out.push_back({id.value(), title.value(), text.value(), link.value(), minv.value(), exp.value(), static_cast<std::uint32_t>(os.value())});
    }
    return out;
}

bool notice_visible(const Notice& n, const NoticeContext& c) {
    if (!c.opted_in || c.game_or_test_running || c.now_unix >= n.expires_at || c.os_build < n.min_os_build) return false;
    const auto v = compare_vendor_versions(c.product_version, n.min_product_version);
    if (!v || *v < 0) return false;  // hedef surum disi veya karsilastirilamaz
    return std::find(c.dismissed.begin(), c.dismissed.end(), n.id) == c.dismissed.end();
}

}  // namespace ef::core::catalog
