// ADR-033 / spec 7: hizmet tarafi guncelleme alicisi (saf). Imza sahte dogrulayiciyla; gercek CNG dogrulamasi
// test_envelope.cpp capraz vektoruyle kanitlidir. Ag, dosya ve hizmet yok.
#include <cctype>
#include <string>

#include "core/recovery/sha256.h"
#include "core/update/staged_update.h"
#include "core/update/update_check.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::update;

namespace {

std::string b64url(std::string_view in) {
    static constexpr char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : in) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(t[(acc >> bits) & 63]);
        }
    }
    if (bits > 0) out.push_back(t[(acc << (6 - bits)) & 63]);
    return out;
}

const std::string kGoodSig(64, '\x01');
const std::string kBadSig(64, '\x02');
const catalog::RawVerifier fake_verify = [](std::string_view blob, std::string_view msg, std::string_view sig) {
    return blob == "K" && sig == kGoodSig && msg.starts_with("EcuFps:v1:release:rel1");
};
const catalog::KeyLookup keys = [](std::string_view d, std::string_view k) { return d == "release" && k == "rel1" ? std::string("K") : std::string(); };

std::string envelope(const std::string& payload, const std::string& sig = kGoodSig) {
    return R"({"domain":"release","key_id":"rel1","payload":")" + b64url(payload) + R"(","sig":")" + b64url(sig) + R"("})";
}

const std::string kExe = std::string("MZ-new-exe-bytes-") + std::string(100000, 'x');
const std::string kPrefix = "https://github.com/ykp61/Ecu-Fps-releases/releases/download/";

struct M {
    std::string ver = "0.3.1", url = kPrefix + "v0.3.1/EcuFps.exe", arch = "x64", channel = "alpha", sha = recovery::sha256_hex(kExe);
    std::uint64_t size = kExe.size();
    int mv = 10;
    unsigned min_os = 19041;
};
std::string manifest(const M& m = {}) {
    return R"({"version":")" + m.ver + R"(","arch":")" + m.arch + R"(","url":")" + m.url + R"(","sha256":")" + m.sha + R"(","channel":")" +
           m.channel + R"(","publisher":"Ecu Fps","min_os_build":)" + std::to_string(m.min_os) + R"(,"size":)" + std::to_string(m.size) +
           R"(,"manifest_version":)" + std::to_string(m.mv) + "}";
}

catalog::ReleaseContext ctx(std::uint64_t highest = 9) {
    catalog::ReleaseContext c;
    c.current_version = "0.3.0";
    c.publisher = "Ecu Fps";
    c.os_build = 26100;
    c.last_manifest_version = replay_floor(highest);
    c.trusted_release_key = true;
    return c;
}

struct FakeSink final : StageSink {
    std::string data, version;
    bool open = false, finished = false, fail_open = false, fail_write = false, lie = false;
    int discards = 0;
    bool open_part(std::string_view v) override {
        if (fail_open) return false;
        version = std::string(v);
        data.clear();
        open = true;
        finished = false;
        return true;
    }
    bool write(std::string_view b) override {
        if (!open || fail_write) return false;
        data += b;
        return true;
    }
    bool finish() override {
        finished = open;
        return open;
    }
    std::string sha256_hex_readback() override {
        // "Yalanci" yazici: diskte baska bayt var (geri okuma farkli ozet verir).
        return recovery::sha256_hex(lie ? data + "!" : data);
    }
    void discard() override {
        ++discards;
        open = false;
        data.clear();
    }
};

StageError send_all(UpdateReceiver& r, const std::string& bytes, std::size_t chunk = kClientChunkBytes) {
    for (std::size_t off = 0; off < bytes.size(); off += chunk) {
        const auto e = r.chunk(off, std::string_view(bytes).substr(off, chunk));
        if (e != StageError::None) return e;
    }
    return StageError::None;
}

}  // namespace

EF_TEST(staged_update_valid_flow) {
    FakeSink sink;
    UpdateReceiver r(ctx(), keys, fake_verify, sink);
    EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
    EF_CHECK(r.receiving() && sink.version == "0.3.1" && r.manifest().size == kExe.size());
    EF_CHECK(send_all(r, kExe) == StageError::None);
    EF_CHECK(r.commit() == StageError::None);
    EF_CHECK(r.committed() && !r.receiving() && sink.finished && sink.data == kExe && sink.discards == 0);
}

EF_TEST(staged_update_rejects_bad_envelopes) {
    auto begin = [](const std::string& env, const catalog::ReleaseContext& c = ctx()) {
        FakeSink sink;
        UpdateReceiver r(c, keys, fake_verify, sink);
        const auto e = r.begin(env);
        EF_CHECK(e == StageError::None ? r.receiving() && sink.open : !r.receiving() && !sink.open);
        return e;
    };
    EF_CHECK(begin(envelope(manifest(), kBadSig)) == StageError::BadEnvelope);  // imza
    EF_CHECK(begin(R"({"domain":"license","key_id":"rel1","payload":"e30","sig":"AQ"})") == StageError::BadEnvelope);
    EF_CHECK(begin("not json") == StageError::BadEnvelope);
    M m;
    m.url = "https://github.com/evil/repo/releases/download/v0.3.1/EcuFps.exe";  // onek disi
    EF_CHECK(begin(envelope(manifest(m))) == StageError::BadUrl);
    m = {};
    m.url = "http://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.3.1/x.exe";
    EF_CHECK(begin(envelope(manifest(m))) == StageError::BadUrl);
    m = {};
    m.url = kPrefix + "v0.3.1/../x.exe";
    EF_CHECK(begin(envelope(manifest(m))) == StageError::BadUrl);
    m = {};
    m.ver = "0.3.0";  // esit
    EF_CHECK(begin(envelope(manifest(m))) == StageError::NotNewer);
    m.ver = "0.2.9";  // eski
    EF_CHECK(begin(envelope(manifest(m))) == StageError::NotNewer);
    m = {};
    m.mv = 7;  // kalici taban 9 -> floor 8: 7 eski (Replay)
    EF_CHECK(begin(envelope(manifest(m))) == StageError::Replay);
    m.mv = 9;  // ayni (en son) manifest kabul
    EF_CHECK(begin(envelope(manifest(m))) == StageError::None);
    m = {};
    m.arch = "arm64";
    EF_CHECK(begin(envelope(manifest(m))) == StageError::WrongTarget);
    m = {};
    m.channel = "stable";
    EF_CHECK(begin(envelope(manifest(m))) == StageError::WrongTarget);
    m = {};
    m.min_os = 30000;
    EF_CHECK(begin(envelope(manifest(m))) == StageError::WrongTarget);
    m = {};
    m.size = kExeMaxBytes + 1;  // > 64 MiB
    EF_CHECK(begin(envelope(manifest(m))) == StageError::TooLarge);
    m = {};
    m.ver = "0.3.1-evil";  // dosya adinda kullanilir: yalniz rakam/nokta
    EF_CHECK(begin(envelope(manifest(m))) != StageError::None);
    // Gomulu anahtar yok -> calistirilmaz.
    auto c = ctx();
    c.trusted_release_key = false;
    EF_CHECK(begin(envelope(manifest()), c) == StageError::BadEnvelope);
}

EF_TEST(staged_update_chunk_order_and_overflow) {
    {
        FakeSink sink;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(r.chunk(0, std::string_view(kExe).substr(0, 1000)) == StageError::None);
        EF_CHECK(r.chunk(500, std::string_view(kExe).substr(500, 1000)) == StageError::OutOfOrder);  // ortusen
        EF_CHECK(!r.receiving() && sink.discards == 1 && sink.data.empty());
        EF_CHECK(r.chunk(1000, "x") == StageError::WrongPhase);  // Idle'a dondu
    }
    {
        FakeSink sink;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(r.chunk(10, "abc") == StageError::OutOfOrder);  // atlama
    }
    {
        FakeSink sink;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(r.chunk(0, std::string(kMaxChunkBytes + 1, 'a')) == StageError::Overflow);  // parca siniri
    }
    {
        FakeSink sink;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(send_all(r, kExe) == StageError::None);
        EF_CHECK(r.chunk(kExe.size(), "z") == StageError::Overflow);  // manifest boyutunu asar
        EF_CHECK(sink.discards == 1);
    }
    {
        FakeSink sink;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(r.chunk(0, "") == StageError::Overflow);
    }
    {
        FakeSink sink;
        sink.fail_write = true;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(r.chunk(0, "abc") == StageError::WriteFailed);
    }
}

EF_TEST(staged_update_commit_checks) {
    {
        FakeSink sink;
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(send_all(r, kExe.substr(0, kExe.size() - 1)) == StageError::None);
        EF_CHECK(r.commit() == StageError::SizeMismatch);
        EF_CHECK(!r.committed() && sink.discards == 1);
    }
    {
        FakeSink sink;
        M m;
        m.sha = std::string(64, 'a');  // manifest baska ozet
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest(m))) == StageError::None);
        EF_CHECK(send_all(r, kExe) == StageError::None);
        EF_CHECK(r.commit() == StageError::HashMismatch);
    }
    {
        FakeSink sink;
        M m;
        for (auto& c : m.sha) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));  // harf duyarsiz
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest(m))) == StageError::None);
        EF_CHECK(send_all(r, kExe) == StageError::None);
        EF_CHECK(r.commit() == StageError::None);
    }
    {
        FakeSink sink;
        sink.lie = true;  // yazilan baytlar dogru gorunur ama geri okuma farkli
        UpdateReceiver r(ctx(), keys, fake_verify, sink);
        EF_CHECK(r.begin(envelope(manifest())) == StageError::None);
        EF_CHECK(send_all(r, kExe) == StageError::None);
        EF_CHECK(r.commit() == StageError::HashMismatch);
        EF_CHECK(sink.discards == 1);
    }
}

EF_TEST(staged_update_phase_errors) {
    FakeSink sink;
    UpdateReceiver r(ctx(), keys, fake_verify, sink);
    EF_CHECK(r.commit() == StageError::WrongPhase);  // begin'den once
    EF_CHECK(r.chunk(0, "a") == StageError::WrongPhase);
    UpdateReceiver r2(ctx(), keys, fake_verify, sink);
    EF_CHECK(r2.begin(envelope(manifest())) == StageError::None);
    EF_CHECK(r2.begin(envelope(manifest())) == StageError::WrongPhase);  // iki kez begin
    EF_CHECK(!r2.receiving());
    FakeSink bad;
    bad.fail_open = true;
    UpdateReceiver r3(ctx(), keys, fake_verify, bad);
    EF_CHECK(r3.begin(envelope(manifest())) == StageError::WriteFailed);
}

EF_TEST(staged_update_state_codec) {
    StagedUpdateState s;
    s.phase = StagePhase::Swapped;
    s.version = "0.3.1";
    s.from_version = "0.3.0";
    s.highest_manifest_version = 12;
    s.swapped_at_ms = 1700000000000;
    auto d = decode_state(encode_state(s));
    EF_CHECK(d.ok());
    EF_CHECK(d.value().phase == StagePhase::Swapped && d.value().version == "0.3.1" && d.value().from_version == "0.3.0" &&
              d.value().highest_manifest_version == 12 && d.value().swapped_at_ms == 1700000000000);
    for (auto p : {StagePhase::Idle, StagePhase::Receiving, StagePhase::UiHealthy, StagePhase::Committed}) {
        s.phase = p;
        EF_CHECK(decode_state(encode_state(s)).ok() && decode_state(encode_state(s)).value().phase == p);
    }
    EF_CHECK(!decode_state("").ok());
    EF_CHECK(!decode_state("{}").ok());
    EF_CHECK(!decode_state(R"({"phase":"boom","version":"","from_version":"","highest_manifest_version":0,"swapped_at_ms":0})").ok());
    EF_CHECK(!decode_state(R"({"phase":"idle","version":"","from_version":"","highest_manifest_version":-1,"swapped_at_ms":0})").ok());
    EF_CHECK(!decode_state(R"({"phase":"idle","version":"","from_version":"","highest_manifest_version":0,"swapped_at_ms":0,"x":1})").ok());
    EF_CHECK(!decode_state(R"({"phase":"swapped","version":"..\\x","from_version":"","highest_manifest_version":0,"swapped_at_ms":0})").ok());
    EF_CHECK(!decode_state("[1,2]").ok());
}

EF_TEST(staged_update_payload_codecs) {
    for (std::string s : {std::string(), std::string("a"), std::string("ab"), std::string("abc"), std::string("\x00\xff\x10\x80", 4), kExe.substr(0, 999)}) {
        const auto e = base64_encode(s);
        const auto d = base64_decode(e);
        EF_CHECK(d && *d == s);
    }
    EF_CHECK(base64_encode("foobar") == "Zm9vYmFy" && base64_encode("fo") == "Zm8=");
    EF_CHECK(!base64_decode("Zm9").has_value());
    EF_CHECK(!base64_decode("Zm=v").has_value());
    EF_CHECK(!base64_decode("Zm9v!mFy").has_value());
    EF_CHECK(!base64_decode("Zm9=").has_value());  // kanonik olmayan dolgu

    const std::string chunk(kClientChunkBytes, '\xAB');
    const auto f = make_update_chunk(65536, chunk, 3);
    EF_CHECK(f.type == ipc::MsgType::UpdateChunk && f.seq == 3 && f.payload.size() <= ipc::kMaxPayload);
    auto c = decode_update_chunk(f.payload);
    EF_CHECK(c.ok() && c.value().offset == 65536 && c.value().data == chunk);
    EF_CHECK(!decode_update_chunk(R"({"offset":-1,"data":""})").ok());
    EF_CHECK(!decode_update_chunk(R"({"offset":0,"data":"AA==","cmd":"x"})").ok());

    auto b = decode_update_begin(make_update_begin(envelope(manifest()), 2).payload);
    EF_CHECK(b.ok() && b.value() == envelope(manifest()));
    EF_CHECK(!decode_update_begin(R"({"envelope":"x","path":"C:\\x"})").ok());
    EF_CHECK(decode_update_commit(make_update_commit(4).payload));
    EF_CHECK(!decode_update_commit(R"({"x":1})"));
    auto h = decode_update_healthy(make_update_healthy("0.3.1", 1).payload);
    EF_CHECK(h.ok() && h.value() == "0.3.1");
    EF_CHECK(!decode_update_healthy(R"({"version":"../x"})").ok());

    for (auto st : {StatusState::Accepted, StatusState::Swapped, StatusState::Committed}) {
        auto d = decode_status(encode_status({st, {}}));
        EF_CHECK(d.ok() && d.value().state == st);
    }
    auto e = decode_status(encode_status({StatusState::Error, "hash_mismatch"}));
    EF_CHECK(e.ok() && e.value().state == StatusState::Error && e.value().error == "hash_mismatch");
    EF_CHECK(!decode_status(R"({"state":"done"})").ok());
}

// P9/B4 (B2 bulgusu): imzali "halt": true manifest (acil fren) hizmete zorunlu guncelleme olarak AKTARILAMAZ. Eskiden
// begin() switch'inde Halted dali yoktu: karar Offer gibi gecip .part acilirdi (gomulu anahtar denetimi de atlanirdi).
EF_TEST(staged_update_halt_manifest_rejected_by_receiver) {
    auto m = manifest();
    m.insert(m.size() - 1, R"(,"halt":true)");
    for (bool trusted : {true, false}) {
        FakeSink sink;
        auto c = ctx();
        c.trusted_release_key = trusted;
        UpdateReceiver r(c, keys, fake_verify, sink);
        const auto e = r.begin(envelope(m));
        EF_CHECK(e != StageError::None);
        EF_CHECK(!r.receiving() && !sink.open && sink.version.empty());
        EF_CHECK(r.chunk(0, std::string_view(kExe).substr(0, 100)) != StageError::None);
    }
    // Ayni manifest halt'siz: kabul (testin kendisi gecerli zarf uretiyor).
    FakeSink ok_sink;
    UpdateReceiver ok(ctx(), keys, fake_verify, ok_sink);
    EF_CHECK(ok.begin(envelope(manifest())) == StageError::None);
}
