#include "core/service/service_session.h"

#include "core/serialization/json.h"
#include "core/service/channel_choice.h"
#include "core/update/staged_update.h"
#include "core/update/update_check.h"

namespace ef::core::service {

using ipc::Frame;
using ipc::MsgType;

namespace {
json::Limits hello_limits() {
    json::Limits l;
    l.max_bytes = 256;
    l.max_depth = 2;
    l.max_string = 32;
    l.max_items = 4;
    return l;
}
}  // namespace

Frame ServiceSession::reply(MsgType t, std::string payload) { return Frame{t, ++out_seq_, std::move(payload)}; }

Frame ServiceSession::fail(std::string why) {
    state_ = State::Closed;
    error_ = why;
    auto o = json::Value::object();
    o.set("error", json::Value::string(why));
    return reply(MsgType::Error, json::write(o));
}

Frame ServiceSession::on_frame(const Frame& in) {
    if (state_ == State::Closed) return fail("session closed");
    if (in.seq != last_seq_ + 1) return fail("sequence violation (replay or reorder)");
    last_seq_ = in.seq;
    if (in.payload.size() > ipc::kMaxPayload) return fail("payload too large");

    if (state_ == State::AwaitHello) {
        if (in.type != MsgType::Hello) return fail("hello required");
        auto doc = json::parse(in.payload, hello_limits());
        if (!doc.ok()) return fail("malformed hello");
        const auto& keys = doc.value().keys();
        if (keys.size() != 2) return fail("hello fields");
        for (const auto& k : keys)
            if (k != "proto" && k != "pid") return fail("hello fields");
        auto proto = json::get_int(doc.value(), "proto", 0, 0xFFFF);
        auto pid = json::get_int(doc.value(), "pid", 1, 0xFFFFFFFFll);
        if (!proto.ok() || !pid.ok()) return fail("hello fields");
        if (proto.value() != kServiceProto) return fail("protocol mismatch");
        if (pid_ == 0 || static_cast<std::uint32_t>(pid.value()) != pid_) return fail("pid mismatch");
        state_ = State::Ready;
        return reply(MsgType::HelloAck, "{}");
    }

    switch (in.type) {
        case MsgType::Bye:
            state_ = State::Closed;
            error_.clear();
            return reply(MsgType::Bye, "{}");
        case MsgType::Request: {
            if (++requests_ > kMaxServiceRequests) return fail("too many requests");
            auto req = ipc::decode_request(in.payload);
            if (!req.ok()) return fail(req.error().detail);
            const auto& r = req.value();
            if (!service_handles_op(r.action, r.op.id)) return fail("action not served by helper service");
            if (!handler_) return fail("no handler");
            return reply(MsgType::Response, handler_(r.request_id, r.action, r.op));
        }
        case MsgType::Hello:
        case MsgType::HelloAck:
        case MsgType::Response:
        case MsgType::Error: return fail("unexpected message type");
        default: break;
    }
    // Gorev 5 uzanti noktasi (guncelleme cerceveleri). Ayri cerceve sinirina tabidir; yoksa/nullopt -> red.
    // ADR-037: uzanti YALNIZ istemci->hizmet guncelleme turlerini (8..11) gorur; Cancel, UpdateStatus ve bilinmeyen tur reddedilir.
    const bool update_frame = in.type == MsgType::UpdateBegin || in.type == MsgType::UpdateChunk || in.type == MsgType::UpdateCommit ||
                              in.type == MsgType::UpdateHealthy;
    if (!extra_ || !update_frame) return fail("unexpected message type");
    if (++extra_frames_ > kMaxServiceExtraFrames) return fail("too many frames");
    auto out = extra_(in);
    if (!out) return fail("unexpected message type");
    return reply(out->type, std::move(out->payload));
}

Frame make_service_hello(std::uint32_t pid, std::uint64_t seq) {
    auto o = json::Value::object();
    o.set("proto", json::Value::integer(kServiceProto));
    o.set("pid", json::Value::integer(pid));
    return Frame{MsgType::Hello, seq, json::write(o)};
}

bool update_begin_halted(std::string_view begin_payload, const catalog::KeyLookup& keys, const catalog::RawVerifier& verify) {
    const auto env = update::decode_update_begin(begin_payload);
    if (!env.ok()) return false;
    const auto payload = catalog::open_envelope(env.value(), update::kReleaseDomain, keys, verify);
    if (!payload) return false;
    const auto doc = json::parse(*payload);
    if (!doc.ok()) return false;
    if (!doc.value().find("halt")) return false;
    const auto h = json::get_bool(doc.value(), "halt");
    return !h.ok() || h.value();  // bool olmayan halt da durdurur (fail-closed)
}

ServiceSession::Extra halt_guarded(ServiceSession::Extra inner, catalog::KeyLookup keys, catalog::RawVerifier verify) {
    return [inner = std::move(inner), keys = std::move(keys), verify = std::move(verify)](const Frame& in) -> std::optional<Frame> {
        if (in.type == MsgType::UpdateBegin && update_begin_halted(in.payload, keys, verify)) return std::nullopt;
        if (!inner) return std::nullopt;
        return inner(in);
    };
}

}  // namespace ef::core::service
