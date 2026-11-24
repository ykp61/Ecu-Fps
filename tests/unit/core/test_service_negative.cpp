// P9/B2 (ADR-037): yardimci hizmet ve broker oturumu negatif testleri + istemci dogrulama sahtecilik matrisi.
// Saf durum makineleri; gercek pipe, SCM, hizmet veya UAC yok. Test kimlikleri docs/03_SECURITY_PERMISSIONS.md STRIDE
// tablosundaki satirlarla eslesir (adlarin basindaki svc_neg_ / ipc_neg_ / svc_spoof_ / svc_halt_).
#include <optional>
#include <string>
#include <vector>

#include "core/ipc/protocol.h"
#include "core/recovery/sha256.h"
#include "core/serialization/json.h"
#include "core/serialization/operation_codec.h"
#include "core/service/channel_choice.h"
#include "core/service/client_check.h"
#include "core/service/service_session.h"
#include "core/update/staged_update.h"
#include "core/update/update_check.h"
#include "ef_test.h"

using namespace ef::core;
using ef::contracts::OperationId;
using ef::contracts::OperationScope;
using ef::contracts::SettingOperation;
using service::ServiceSession;

namespace {
constexpr std::uint32_t kPid = 4242;
const std::string kNonce = "0123456789abcdef0123456789abcdef";

SettingOperation hags_op() { return {OperationId::HagsSet, OperationScope::Machine, "hags:hwschmode", "2", true, false}; }

ServiceSession::Handler counter(int& n) {
    return [&n](const std::string&, ipc::RequestAction, const SettingOperation&) {
        ++n;
        return std::string(R"({"ok":true})");
    };
}

ServiceSession ready_service(int& n, ServiceSession::Extra extra = {}) {
    ServiceSession s(kPid, counter(n), std::move(extra));
    (void)s.on_frame(service::make_service_hello(kPid, 1));
    return s;
}

ipc::ServerSession ready_broker(int& n) {
    ipc::ServerSession s(kNonce, kPid, counter(n));
    (void)s.on_frame(ipc::make_hello(kNonce, kPid, 1), kPid);
    return s;
}

std::string request_payload(const std::string& action, const std::string& op_json) {
    return R"({"request_id":"r1","action":")" + action + R"(","op":)" + op_json + "}";
}
}  // namespace

// --- Hizmet oturumu (ServiceSession) ---------------------------------------------------------------------------

// S-1 sira disi cerceve: Hello'dan once Bye/Response/Update; Hello'dan sonra seq atlamasi ve geri sarma.
EF_TEST(svc_neg_out_of_order_frames) {
    for (auto t : {ipc::MsgType::Bye, ipc::MsgType::Response, ipc::MsgType::UpdateBegin, ipc::MsgType::Cancel, ipc::MsgType::Request}) {
        int n = 0;
        ServiceSession s(kPid, counter(n));
        EF_CHECK(s.on_frame(ipc::Frame{t, 1, "{}"}).type == ipc::MsgType::Error);
        EF_CHECK(s.closed() && !s.ready() && n == 0);
    }
    int n = 0;
    auto s = ready_service(n);
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 2)).type == ipc::MsgType::Response);
    EF_CHECK(s.on_frame(ipc::make_request("r2", hags_op(), 1)).type == ipc::MsgType::Error);  // geri sarma
    EF_CHECK(s.closed() && n == 1);
    auto s2 = ready_service(n);
    EF_CHECK(s2.on_frame(ipc::make_request("r1", hags_op(), ~0ull)).type == ipc::MsgType::Error);  // tasma denemesi
}

// T-1 yeniden oynatma: ayni Hello ikinci kez, kapali oturuma gecerli istek; hicbiri isleyiciye ulasmaz.
EF_TEST(svc_neg_replay_after_ready_and_after_close) {
    int n = 0;
    auto s = ready_service(n);
    EF_CHECK(s.on_frame(service::make_service_hello(kPid, 2)).type == ipc::MsgType::Error);
    EF_CHECK(s.closed());
    EF_CHECK(s.on_frame(ipc::make_request("r1", hags_op(), 3)).type == ipc::MsgType::Error);
    EF_CHECK(n == 0);
    auto b = ready_service(n);
    EF_CHECK(b.on_frame(ipc::Frame{ipc::MsgType::Bye, 2, "{}"}).type == ipc::MsgType::Bye);
    for (std::uint64_t q = 1; q < 6; ++q) EF_CHECK(b.on_frame(ipc::make_request("r", hags_op(), q)).type == ipc::MsgType::Error);
    EF_CHECK(n == 0);
}

// D-1 asiri uzunluk: kMaxPayload ustu yuk (cozumleyiciyi atlatan cerceve dahil) ve dev request_id.
EF_TEST(svc_neg_oversize_payloads) {
    int n = 0;
    {
        ServiceSession s(kPid, counter(n));
        EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Hello, 1, std::string(ipc::kMaxPayload + 1, ' ')}).type == ipc::MsgType::Error);
        EF_CHECK(s.closed());
    }
    {
        ServiceSession s(kPid, counter(n));
        EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Hello, 1, R"({"proto":1,"pid":4242})" + std::string(300, ' ')}).type ==
                  ipc::MsgType::Error);  // Hello 256 bayt siniri
    }
    auto s = ready_service(n);
    EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Request, 2, std::string(ipc::kMaxPayload + 1, 'x')}).type == ipc::MsgType::Error);
    auto s2 = ready_service(n);
    const auto huge_id = R"({"request_id":")" + std::string(65, 'a') + R"(","op":{}})";
    EF_CHECK(s2.on_frame(ipc::Frame{ipc::MsgType::Request, 2, huge_id}).type == ipc::MsgType::Error);
    EF_CHECK(n == 0);
}

// D-2 oturum limiti: istek (64) ve uzanti cercevesi (4096) sinirlari baglanti basina.
EF_TEST(svc_neg_extra_frame_limit) {
    int n = 0, hook = 0;
    auto s = ready_service(n, [&hook](const ipc::Frame&) -> std::optional<ipc::Frame> {
        ++hook;
        return ipc::Frame{ipc::MsgType::UpdateStatus, 0, R"({"state":"accepted"})"};
    });
    std::uint64_t seq = 1;
    for (int i = 0; i < service::kMaxServiceExtraFrames; ++i)
        EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::UpdateChunk, ++seq, "{}"}).type == ipc::MsgType::UpdateStatus);
    EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::UpdateChunk, ++seq, "{}"}).type == ipc::MsgType::Error);
    EF_CHECK(s.closed() && hook == service::kMaxServiceExtraFrames);
}

// E-1 izin listesi disi islem: bilinmeyen action, bilinmeyen op, DNS degistirme, surucu/uretici kurulumu.
EF_TEST(svc_neg_outside_allowlist) {
    const std::string hags = json::write(json::encode_operation(hags_op()));
    std::string shell = hags;
    shell.replace(shell.find("hags_set"), 8, "shell_exec");  // izin listesinde olmayan islem kimligi
    {
        int n = 0;
        auto s = ready_service(n);
        EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Request, 2, request_payload("fix", hags)}).type == ipc::MsgType::Response);  // kontrol
        EF_CHECK(n == 1);
    }
    const std::vector<std::string> bad = {
        request_payload("format_disk", hags),
        request_payload("Apply", hags),  // buyuk/kucuk harf duyarli
        R"({"request_id":"r1","op":)" + shell + "}",
        R"({"request_id":"r1","op":)" + hags + R"(,"cmd":"x"})",
        R"({"request_id":"","op":)" + hags + "}",
        R"({"request_id":"r1","request_id":"r2","op":)" + hags + "}",
    };
    for (const auto& p : bad) {
        int n = 0;
        auto s = ready_service(n);
        EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Request, 2, p}).type == ipc::MsgType::Error);
        EF_CHECK(s.closed() && n == 0);
    }
    const SettingOperation dns{OperationId::DnsServersSet, OperationScope::Machine, "dns:{00000000-0000-0000-0000-000000000001}", "1.1.1.1", false, false};
    const SettingOperation drv{OperationId::DriverInstall, OperationScope::Machine, "driver:x", "1", false, false};
    const SettingOperation ven{OperationId::VendorDriverInstall, OperationScope::Machine, "vendor:x", "1", false, false};
    struct Case { SettingOperation op; ipc::RequestAction a; };
    for (const auto& c : {Case{dns, ipc::RequestAction::Apply}, Case{dns, ipc::RequestAction::Fix}, Case{drv, ipc::RequestAction::UndoFix},
                          Case{ven, ipc::RequestAction::Restore}, Case{hags_op(), ipc::RequestAction::DriverInstall},
                          Case{hags_op(), ipc::RequestAction::VendorInstall}}) {
        int n = 0;
        auto s = ready_service(n);
        EF_CHECK(s.on_frame(ipc::make_request("r1", c.op, 2, c.a)).type == ipc::MsgType::Error);
        EF_CHECK(n == 0);
    }
}

// E-2 uzanti noktasi yalniz guncelleme cerceveleri (8..11) gorur; Cancel, UpdateStatus ve bilinmeyen tur reddedilir.
EF_TEST(svc_neg_extra_hook_only_update_frames) {
    for (auto t : {ipc::MsgType::Cancel, ipc::MsgType::UpdateStatus, static_cast<ipc::MsgType>(42), static_cast<ipc::MsgType>(0)}) {
        int n = 0, hook = 0;
        auto s = ready_service(n, [&hook](const ipc::Frame&) -> std::optional<ipc::Frame> {
            ++hook;
            return ipc::Frame{ipc::MsgType::UpdateStatus, 0, "{}"};
        });
        EF_CHECK(s.on_frame(ipc::Frame{t, 2, "{}"}).type == ipc::MsgType::Error);
        EF_CHECK(s.closed() && hook == 0);
    }
    for (auto t : {ipc::MsgType::UpdateBegin, ipc::MsgType::UpdateChunk, ipc::MsgType::UpdateCommit, ipc::MsgType::UpdateHealthy}) {
        int n = 0, hook = 0;
        auto s = ready_service(n, [&hook](const ipc::Frame&) -> std::optional<ipc::Frame> {
            ++hook;
            return ipc::Frame{ipc::MsgType::UpdateStatus, 0, "{}"};
        });
        EF_CHECK(s.on_frame(ipc::Frame{t, 2, "{}"}).type == ipc::MsgType::UpdateStatus);
        EF_CHECK(hook == 1);
    }
}

// --- Halt bayragi (acil fren): imzali "halt": true manifest hizmet alicisinda degisime baslatmaz -------------------
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
const std::string kSig(64, '\x01');
const catalog::RawVerifier halt_verify = [](std::string_view blob, std::string_view, std::string_view sig) { return blob == "K" && sig == kSig; };
const catalog::KeyLookup halt_keys = [](std::string_view d, std::string_view k) { return d == "release" && k == "rel1" ? std::string("K") : std::string(); };
const std::string kHaltExe(1000, 'x');

std::string halt_manifest(const std::string& halt_field) {
    return R"({"version":"0.3.1","arch":"x64","url":"https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.3.1/EcuFps.exe","sha256":")" +
           recovery::sha256_hex(kHaltExe) + R"(","channel":"alpha","publisher":"Ecu Fps","min_os_build":19041,"size":1000,"manifest_version":10)" +
           halt_field + "}";
}
std::string halt_envelope(const std::string& payload) {
    return R"({"domain":"release","key_id":"rel1","payload":")" + b64url(payload) + R"(","sig":")" + b64url(kSig) + R"("})";
}
struct NullSink final : update::StageSink {
    int opens = 0;
    bool open_part(std::string_view) override { return ++opens, true; }
    bool write(std::string_view) override { return true; }
    bool finish() override { return true; }
    std::string sha256_hex_readback() override { return {}; }
    void discard() override {}
};
catalog::ReleaseContext halt_ctx(bool trusted_key) {
    catalog::ReleaseContext c;
    c.current_version = "0.3.0";
    c.publisher = "Ecu Fps";
    c.os_build = 26100;
    c.last_manifest_version = 0;
    c.trusted_release_key = trusted_key;
    return c;
}
}  // namespace

// T-3: hizmet oturumu halt'li UpdateBegin'i koordinatore (burada gercek UpdateReceiver) ulastirmaz. Kok neden
// (UpdateReceiver::begin switch'inde Halted dali yok -> Offer gibi devam) B4 sahipligindedir; istek branch-notes'ta.
EF_TEST(svc_halt_flag_blocks_staged_update) {
    const auto halted = halt_envelope(halt_manifest(R"(,"halt":true)"));
    const auto normal = halt_envelope(halt_manifest(R"(,"halt":false)"));
    EF_CHECK(service::update_begin_halted(update::make_update_begin(halted, 2).payload, halt_keys, halt_verify));
    EF_CHECK(!service::update_begin_halted(update::make_update_begin(normal, 2).payload, halt_keys, halt_verify));
    EF_CHECK(service::update_begin_halted(update::make_update_begin(halt_envelope(halt_manifest(R"(,"halt":"yes")")), 2).payload,
                                           halt_keys, halt_verify));  // bool olmayan halt: fail-closed
    EF_CHECK(!service::update_begin_halted("{", halt_keys, halt_verify));
    for (bool trusted : {true, false}) {
        for (const auto* env : {&halted, &normal}) {
            NullSink sink;
            update::UpdateReceiver r(halt_ctx(trusted), halt_keys, halt_verify, sink);
            int n = 0, inner_calls = 0;
            auto s = ready_service(n, service::halt_guarded(
                                          [&](const ipc::Frame& in) -> std::optional<ipc::Frame> {
                                              ++inner_calls;
                                              const auto e = update::decode_update_begin(in.payload);
                                              const bool ok = e.ok() && r.begin(e.value()) == update::StageError::None;
                                              return ipc::Frame{ipc::MsgType::UpdateStatus, 0, ok ? "accepted" : "error"};
                                          },
                                          halt_keys, halt_verify));
            const auto out = s.on_frame(update::make_update_begin(*env, 2));
            if (env == &halted) {
                EF_CHECK(out.type == ipc::MsgType::Error && s.closed());
                EF_CHECK(inner_calls == 0 && sink.opens == 0);  // .part acilmaz, degisim baslamaz
            } else {
                EF_CHECK(out.type == ipc::MsgType::UpdateStatus && inner_calls == 1);
                EF_CHECK(out.payload == (trusted ? "accepted" : "error"));  // gomulu anahtar yoksa alici reddeder
            }
        }
    }
}

// --- Broker oturumu (ipc::ServerSession) -----------------------------------------------------------------------

// D-3 oturum limiti: broker oturumu da cerceve sayisini sinirlar (platform dongusune guvenmez).
EF_TEST(ipc_neg_broker_frame_limit) {
    int n = 0;
    auto s = ready_broker(n);
    std::uint64_t seq = 1;
    for (int i = 1; i < ipc::kMaxSessionFrames; ++i)
        EF_CHECK(s.on_frame(ipc::make_request("r" + std::to_string(i), hags_op(), ++seq), kPid).type == ipc::MsgType::Response);
    EF_CHECK(s.on_frame(ipc::make_request("rX", hags_op(), ++seq), kPid).type == ipc::MsgType::Error);
    EF_CHECK(s.closed() && n == ipc::kMaxSessionFrames - 1);
    // Cancel seli de ayni sinira tabidir.
    auto c = ready_broker(n);
    seq = 1;
    bool cut = false;
    for (int i = 0; i < 200 && !cut; ++i) cut = c.on_frame(ipc::Frame{ipc::MsgType::Cancel, ++seq, "{}"}, kPid).type == ipc::MsgType::Error;
    EF_CHECK(cut && c.closed());
}

// S-2 broker Hello'su tam olarak {"nonce","pid"}: fazladan alan, yanlis nonce, ikinci Hello, PID degisimi.
EF_TEST(ipc_neg_broker_hello_shape_and_pid) {
    int n = 0;
    {
        ipc::ServerSession s(kNonce, kPid, counter(n));
        const auto p = R"({"nonce":")" + kNonce + R"(","pid":4242,"admin":true})";
        EF_CHECK(s.on_frame(ipc::Frame{ipc::MsgType::Hello, 1, p}, kPid).type == ipc::MsgType::Error);
        EF_CHECK(s.closed());
    }
    {
        ipc::ServerSession s(kNonce, kPid, counter(n));
        EF_CHECK(s.on_frame(ipc::make_hello("0123456789abcdef0123456789abcdee", kPid, 1), kPid).type == ipc::MsgType::Error);
    }
    {
        ipc::ServerSession s(kNonce, kPid, counter(n));
        EF_CHECK(s.on_frame(ipc::make_hello(kNonce, kPid, 1), kPid + 1).type == ipc::MsgType::Error);  // gozlenen PID
    }
    auto s = ready_broker(n);
    EF_CHECK(s.on_frame(ipc::make_hello(kNonce, kPid, 2), kPid).type == ipc::MsgType::Error);  // nonce tek kullanimlik
    auto s2 = ready_broker(n);
    EF_CHECK(s2.on_frame(ipc::make_request("r1", hags_op(), 2), kPid + 1).type == ipc::MsgType::Error);  // oturum ortasi PID
    EF_CHECK(s2.on_frame(ipc::make_request("r1", hags_op(), 3), kPid).type == ipc::MsgType::Error);      // kapali kalir
    EF_CHECK(n == 0);
}

EF_TEST(ipc_neg_broker_update_frames_rejected) {
    for (auto t : {ipc::MsgType::UpdateBegin, ipc::MsgType::UpdateChunk, ipc::MsgType::UpdateCommit, ipc::MsgType::UpdateHealthy,
                   ipc::MsgType::UpdateStatus, ipc::MsgType::HelloAck, ipc::MsgType::Response, ipc::MsgType::Error}) {
        int n = 0;
        auto s = ready_broker(n);
        EF_CHECK(s.on_frame(ipc::Frame{t, 2, "{}"}, kPid).type == ipc::MsgType::Error);
        EF_CHECK(s.closed());
    }
}

// --- Istemci dogrulama sahtecilik matrisi (check_client) ---------------------------------------------------------
namespace {
const std::wstring kExe = L"C:\\Program Files\\Ecu Fps\\EcuFps.exe";
const service::FileId kFile{0x1234ABCDull, 0x0000000500000042ull};

service::ClientFacts good_client() {
    service::ClientFacts c;
    c.pid = kPid;
    c.session = 1;
    c.image_ok = c.file_ok = c.token_ok = true;
    c.image = kExe;
    c.file = kFile;
    c.interactive = true;
    c.admin_member = true;
    c.user_sid = "S-1-5-21-1111111111-2222222222-3333333333-1001";
    return c;
}
service::ServiceSelf good_self() {
    service::ServiceSelf s;
    s.installed_exe = kExe;
    s.installed_file_ok = true;
    s.installed_file = kFile;
    s.install_dir_acl_ok = true;
    return s;
}
}  // namespace

EF_TEST(svc_spoof_image_path_matrix) {
    using V = service::ClientVerdict;
    struct Row { std::wstring image; V want; };
    std::wstring nul_tail = kExe;
    nul_tail += L'\0';
    nul_tail += L"x";
    const std::vector<Row> rows = {
        {kExe, V::Allow},
        {L"c:\\program files\\ecu fps\\ecufps.exe", V::Allow},     // NTFS buyuk/kucuk harf duyarsiz
        {L"\\\\?\\C:\\Program Files\\Ecu Fps\\EcuFps.exe", V::Allow},  // uzun yol oneki
        {L"C:/Program Files/Ecu Fps/EcuFps.exe", V::Allow},
        {L"C:\\Program Files\\Ecu Fps\\EcuFps.exe.", V::WrongImage},       // sondaki nokta
        {L"C:\\Program Files\\Ecu Fps\\EcuFps.exe ", V::WrongImage},       // sondaki bosluk
        {L"C:\\Program Files\\Ecu Fps\\EcuFps.exe:evil", V::WrongImage},   // ADS
        {L"C:\\PROGRA~1\\ECUFPS~1\\ECUFPS~1.EXE", V::WrongImage},              // 8.3 ad (fail-closed)
        {L"\\\\?\\UNC\\evil\\share\\EcuFps.exe", V::WrongImage},
        {L"\\\\.\\C:\\Program Files\\Ecu Fps\\EcuFps.exe", V::WrongImage},  // aygit yolu
        {L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume3\\Program Files\\Ecu Fps\\EcuFps.exe", V::WrongImage},
        {L"C:\\Users\\Public\\Ecu Fps\\EcuFps.exe", V::WrongImage},  // ayni ad, baska klasor
        {L"C:\\Program Files\\Ecu Fps\\..\\Ecu Fps2\\EcuFps.exe", V::WrongImage},
        {L"C:\\Program Files\\Ecu Fps\\sub\\..\\EcuFps.exe", V::Allow},  // sozcuksel olarak ayni dosya; dosya kimligi ayrica
        {L"D:\\Program Files\\Ecu Fps\\EcuFps.exe", V::WrongImage},
        {nul_tail, V::WrongImage},  // gomulu NUL
        {L"", V::QueryFailed},
    };
    for (const auto& r : rows) {
        auto c = good_client();
        c.image = r.image;
        EF_CHECK(service::check_client(c, good_self()) == r.want);
    }
}

EF_TEST(svc_spoof_identity_matrix) {
    using V = service::ClientVerdict;
    const auto self = good_self();
    auto with = [&](auto mutate) {
        auto c = good_client();
        mutate(c);
        return service::check_client(c, self);
    };
    EF_CHECK(with([](service::ClientFacts&) {}) == V::Allow);
    EF_CHECK(with([](service::ClientFacts& c) { c.file.file_index ^= 1; }) == V::WrongFile);      // sert bag / yeniden adlandirma
    EF_CHECK(with([](service::ClientFacts& c) { c.file.volume_serial ^= 1; }) == V::WrongFile);   // baska birim
    EF_CHECK(with([](service::ClientFacts& c) { c.session = 0; }) == V::SessionZero);
    EF_CHECK(with([](service::ClientFacts& c) { c.interactive = false; }) == V::NotInteractive);
    EF_CHECK(with([](service::ClientFacts& c) { c.admin_member = false; }) == V::NotAdmin);       // K27
    EF_CHECK(with([](service::ClientFacts& c) { c.pid = 0; }) == V::QueryFailed);
    EF_CHECK(with([](service::ClientFacts& c) { c.image_ok = false; }) == V::QueryFailed);
    EF_CHECK(with([](service::ClientFacts& c) { c.file_ok = false; }) == V::QueryFailed);
    EF_CHECK(with([](service::ClientFacts& c) { c.token_ok = false; }) == V::QueryFailed);
    EF_CHECK(with([](service::ClientFacts& c) { c.user_sid.clear(); }) == V::QueryFailed);
    // Hedef kullanici bir kullanici SID'i olmali: SYSTEM/LocalService/bozuk SID hedef oyuncu olamaz.
    for (const char* sid : {"S-1-5-18", "S-1-5-19", "S-1-5-20", "S-1-1-0", "garbage", "S-1-5-21-1-2-3-1001;rm"})
        EF_CHECK(with([sid](service::ClientFacts& c) { c.user_sid = sid; }) == V::QueryFailed);
    EF_CHECK(with([](service::ClientFacts& c) { c.user_sid = "S-1-12-1-111-222-333-444"; }) == V::Allow);  // Entra ID
    // Sifir dosya kimligi (sorgu yarim kaldi) her iki tarafta esit olsa da kabul edilmez.
    {
        auto c = good_client();
        auto s = good_self();
        c.file = {};
        s.installed_file = {};
        EF_CHECK(service::check_client(c, s) == V::QueryFailed);
    }
    // Siralama: guvensiz kurulum klasoru her seyden once (istemci ne olursa olsun red).
    auto bad_acl = self;
    bad_acl.install_dir_acl_ok = false;
    EF_CHECK(service::check_client(good_client(), bad_acl) == V::InstallDirInsecure);
    auto no_inst = self;
    no_inst.installed_file_ok = false;
    EF_CHECK(service::check_client(good_client(), no_inst) == V::QueryFailed);
    no_inst = self;
    no_inst.installed_exe.clear();
    EF_CHECK(service::check_client(good_client(), no_inst) == V::QueryFailed);
}

// UI tarafi kanal secimi: herhangi bir kosul eksikse UAC broker'ina duser (ad gaspi: pipe sunucu PID'i != SCM PID'i).
EF_TEST(svc_spoof_channel_choice_matrix) {
    service::ServiceProbe p;
    p.ui_installed = p.admin_member = p.exists = p.running = p.config_ok = p.connected = p.hello_ok = true;
    p.scm_pid = p.pipe_server_pid = 900;
    EF_CHECK(service::choose_path(p) == service::Path::Service);
    bool service::ServiceProbe::*flags[] = {&service::ServiceProbe::ui_installed, &service::ServiceProbe::admin_member,
                                            &service::ServiceProbe::exists,       &service::ServiceProbe::running,
                                            &service::ServiceProbe::config_ok,    &service::ServiceProbe::connected,
                                            &service::ServiceProbe::hello_ok};
    for (auto f : flags) {
        auto q = p;
        q.*f = false;
        EF_CHECK(service::choose_path(q) == service::Path::UacBroker);
    }
    auto q = p;
    q.pipe_server_pid = 901;
    EF_CHECK(service::choose_path(q) == service::Path::UacBroker);
    q = p;
    q.scm_pid = q.pipe_server_pid = 0;
    EF_CHECK(service::choose_path(q) == service::Path::UacBroker);
}
