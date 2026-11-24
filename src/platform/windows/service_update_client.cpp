#include "platform/windows/service_update_client.h"

#include <windows.h>

#include <algorithm>
#include <memory>

#include "core/service/channel_choice.h"
#include "core/update/staged_update.h"
#include "platform/windows/pipe.h"
#include "platform/windows/service_channel.h"

namespace ef::platform {

namespace {

constexpr std::uint32_t kSendMs = 5000;
constexpr std::uint32_t kReplyMs = 30000;
constexpr std::uint32_t kCommitReplyMs = 180000;  // hizmet .part'i ve kopyasini yeniden hash'ler (<= 64 MiB) + degisim

namespace upd = core::update;

// Tek cerceve gonder + UpdateStatus yanitini oku. Baska tur veya bozuk yanit -> nullopt.
std::optional<upd::UpdateStatusMsg> round_trip(PipeClient& c, const core::ipc::Frame& f, std::uint32_t reply_ms) {
    if (!c.write_frame(f, kSendMs).ok()) return std::nullopt;
    auto r = c.read_frame(reply_ms);
    if (!r.ok() || r.value().type != core::ipc::MsgType::UpdateStatus) return std::nullopt;
    auto d = upd::decode_status(r.value().payload);
    if (!d.ok()) return std::nullopt;
    return d.value();
}

void bye(PipeClient& c, std::uint64_t seq) {
    if (c.write_frame(core::ipc::Frame{core::ipc::MsgType::Bye, seq, "{}"}, kSendMs).ok()) (void)c.read_frame(kSendMs);
}

}  // namespace

ServiceUpdateOutcome service_update_send(std::string_view envelope_json, std::string_view exe_bytes,
                                         const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    ServiceUpdateOutcome out;
    core::service::ServiceProbe probe;
    auto c = connect_helper_service(probe, static_cast<std::uint32_t>(GetCurrentProcessId()));
    if (!c) return out;  // Unavailable: hicbir sey gonderilmedi
    out.result = ServiceUpdateResult::Error;
    std::uint64_t seq = 1;  // Hello
    auto st = round_trip(*c, upd::make_update_begin(envelope_json, ++seq), kReplyMs);
    if (!st || st->state != upd::StatusState::Accepted) {
        out.error = st ? st->error : "no reply";
        return out;
    }
    for (std::size_t off = 0; off < exe_bytes.size(); off += upd::kClientChunkBytes) {
        const auto part = exe_bytes.substr(off, upd::kClientChunkBytes);
        st = round_trip(*c, upd::make_update_chunk(off, part, ++seq), kReplyMs);
        if (!st || st->state != upd::StatusState::Accepted) {
            out.error = st ? st->error : "no reply";
            return out;  // baglanti kapaninca hizmet yarim parcayi atar
        }
        if (progress) progress(off + part.size(), exe_bytes.size());
    }
    st = round_trip(*c, upd::make_update_commit(++seq), kCommitReplyMs);
    if (!st || st->state != upd::StatusState::Swapped) {
        out.error = st ? st->error : "no reply";
        return out;
    }
    bye(*c, ++seq);
    out.result = ServiceUpdateResult::Swapped;
    return out;
}

bool service_update_healthy(std::string_view version) {
    core::service::ServiceProbe probe;
    auto c = connect_helper_service(probe, static_cast<std::uint32_t>(GetCurrentProcessId()));
    if (!c) return false;
    const auto st = round_trip(*c, upd::make_update_healthy(version, 2), kReplyMs);
    // Hizmet yanittan sonra kendini durdurur (yeni EXE ile yeniden baslar); Bye beklenmez.
    return st && st->state == upd::StatusState::Committed;
}

}  // namespace ef::platform
