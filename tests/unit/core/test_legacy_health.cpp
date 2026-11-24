// T3b (ADR-043 ek): eski hizmete saglik sinyali. Altin baytlar 0.9.5 kaynagindan (059b3ee3) elle turetildi:
// protocol.cpp encode = magic 0x50585449 LE | surum 1 LE u16 | tur LE u16 | uzunluk LE u32 | seq LE u64 | yuk;
// service_session.cpp make_service_hello {"proto":1,"pid":N}; staged_update.cpp make_update_healthy {"version":V}.
// Sahte port; gercek pipe / hizmet yok.
#include <deque>
#include <string>

#include "core/ipc/protocol.h"
#include "core/legacy/legacy_health.h"
#include "core/legacy/legacy_install.h"
#include "core/legacy/legacy_names.h"
#include "core/update/staged_update.h"
#include "ef_test.h"

using namespace ef::core::legacy;
namespace ipc = ef::core::ipc;
namespace setup = ef::core::setup;
namespace upd = ef::core::update;

namespace {

setup::KnownRoots health_roots() {
    setup::KnownRoots r;
    r.program_files = L"C:\\Program Files";
    r.program_data = L"C:\\ProgramData";
    r.common_programs = L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs";
    r.local_app_data = L"C:\\Users\\oyuncu\\AppData\\Local";
    r.common_desktop = L"C:\\Users\\Public\\Desktop";
    return r;
}

std::string hdr(std::uint16_t type, std::uint32_t len, std::uint64_t seq) {
    std::string o = "ITXP";  // 0x50585449 little-endian
    o += '\x01';
    o += '\x00';
    o += static_cast<char>(type & 0xFF);
    o += static_cast<char>(type >> 8);
    for (int i = 0; i < 4; ++i) o += static_cast<char>((len >> (8 * i)) & 0xFF);
    for (int i = 0; i < 8; ++i) o += static_cast<char>((seq >> (8 * i)) & 0xFF);
    return o;
}

class FakePort final : public LegacyHealthPort {
public:
    LegacyServiceView svc;
    bool pipe_ok = true;
    std::uint32_t connected_pid = 0;
    std::vector<ipc::Frame> sent;
    std::deque<ipc::Frame> replies;
    LegacyServiceView query_old_service() override { return svc; }
    bool connect(std::uint32_t pid) override {
        connected_pid = pid;
        return pipe_ok;
    }
    bool write(const ipc::Frame& f) override {
        sent.push_back(f);
        return true;
    }
    std::optional<ipc::Frame> read(std::uint32_t) override {
        if (replies.empty()) return std::nullopt;
        auto f = replies.front();
        replies.pop_front();
        return f;
    }
};

FakePort running_port(const setup::InstallLayout& legacy) {
    FakePort p;
    p.svc.exists = true;
    p.svc.running = true;
    p.svc.pid = 900;
    p.svc.image_path = setup::service_image_path(legacy);
    return p;
}

ipc::Frame status(upd::StatusState s) {
    upd::UpdateStatusMsg m;
    m.state = s;
    return ipc::Frame{ipc::MsgType::UpdateStatus, 2, upd::encode_status(m)};
}

}  // namespace

EF_TEST(legacy_health_golden_bytes_match_old_wire) {
    const auto frames = legacy_health_frames(4660, "0.9.6");
    EF_CHECK(frames.size() == 2);
    const std::string hello_json = "{\"proto\":1,\"pid\":4660}";
    const std::string healthy_json = "{\"version\":\"0.9.6\"}";
    EF_CHECK(ipc::encode(frames[0]) == hdr(1, static_cast<std::uint32_t>(hello_json.size()), 1) + hello_json);
    EF_CHECK(ipc::encode(frames[1]) == hdr(11, static_cast<std::uint32_t>(healthy_json.size()), 2) + healthy_json);
    // Bilinen ilk baytlar (onaltilik): 49 54 58 50 01 00 01 00 16 00 00 00 01 00 ...
    const auto b = ipc::encode(frames[0]);
    EF_CHECK(b.size() == 20 + 22 && b[8] == '\x16' && b[12] == '\x01' && b[6] == '\x01' && b[7] == '\x00');
}

EF_TEST(legacy_health_targets_old_pipe_and_service_names) {
    EF_CHECK(legacy_health_pipe() == kOldHelperPipeName);
    EF_CHECK(legacy_health_service() == kOldServiceName);
    EF_CHECK(legacy_health_pipe() != setup::kPipeName);
}

EF_TEST(legacy_health_sequence_and_refusals) {
    const auto legacy = legacy_layout(health_roots());
    {  // tam akis: Hello -> HelloAck -> UpdateHealthy -> committed
        auto p = running_port(legacy);
        p.replies.push_back(ipc::Frame{ipc::MsgType::HelloAck, 1, "{}"});
        p.replies.push_back(status(upd::StatusState::Committed));
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::Confirmed);
        EF_CHECK(p.connected_pid == 900);  // pipe sunucu PID'i SCM PID'ine bagli
        EF_CHECK(p.sent.size() == 2 && p.sent[0].type == ipc::MsgType::Hello && p.sent[1].type == ipc::MsgType::UpdateHealthy);
        EF_CHECK(p.sent[1].seq == 2);
    }
    {  // eski hizmet yok: hicbir sey gonderilmez (yeni kurulum normal acilis)
        FakePort p;
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::NoOldService);
        EF_CHECK(p.sent.empty() && p.connected_pid == 0);
    }
    {  // durmus
        auto p = running_port(legacy);
        p.svc.running = false;
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::NotRunning);
        EF_CHECK(p.sent.empty());
    }
    {  // ImagePath baska EXE: ad gaspi / yabanci hizmet -> veri yok
        auto p = running_port(legacy);
        p.svc.image_path = L"\"C:\\Temp\\x.exe\" --service";
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::ImageMismatch);
        EF_CHECK(p.sent.empty() && p.connected_pid == 0);
    }
    {  // pipe yok / sunucu PID'i uyusmadi
        auto p = running_port(legacy);
        p.pipe_ok = false;
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::PipeUnavailable);
        EF_CHECK(p.sent.empty());
    }
    {  // Hello reddi (eski hizmet istemciyi dogrulamadi): UpdateHealthy gonderilmez
        auto p = running_port(legacy);
        p.replies.push_back(ipc::Frame{ipc::MsgType::Error, 1, "{}"});
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::HelloRejected);
        EF_CHECK(p.sent.size() == 1);
    }
    {  // yanit yok (eski hizmet gitti): cokmez, Rejected
        auto p = running_port(legacy);
        p.replies.push_back(ipc::Frame{ipc::MsgType::HelloAck, 1, "{}"});
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::Rejected);
    }
    {  // hata durumu
        auto p = running_port(legacy);
        p.replies.push_back(ipc::Frame{ipc::MsgType::HelloAck, 1, "{}"});
        p.replies.push_back(status(upd::StatusState::Error));
        EF_CHECK(send_legacy_health(p, legacy, 77, "0.9.6") == LegacyHealthOutcome::Rejected);
    }
}
