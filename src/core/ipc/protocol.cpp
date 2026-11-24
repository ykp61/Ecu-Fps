#include "core/ipc/protocol.h"

#include "core/roles.h"
#include "core/serialization/json.h"
#include "core/serialization/operation_codec.h"

namespace ef::core::ipc {

using contracts::ErrorCode;

namespace {
void put(std::string& o, std::uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) o += static_cast<char>((v >> (8 * i)) & 0xFF);
}
std::uint64_t get(std::string_view s, std::size_t off, int bytes) {
    std::uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(s[off + static_cast<std::size_t>(i)]);
    return v;
}
bool known_type(std::uint16_t t) { return t >= 1 && t <= 12; }

// ADR-037: nonce karsilastirmasi uzunluk disinda veriye bagli erken cikis yapmaz.
bool equal_ct(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

json::Limits ipc_limits() {
    json::Limits l;
    l.max_bytes = kMaxPayload;
    l.max_depth = 8;
    l.max_string = 512;
    l.max_items = 64;
    return l;
}
}  // namespace

std::string encode(const Frame& f) {
    std::string o;
    o.reserve(kHeaderSize + f.payload.size());
    put(o, kMagic, 4);
    put(o, kProtocolVersion, 2);
    put(o, static_cast<std::uint16_t>(f.type), 2);
    put(o, f.payload.size(), 4);
    put(o, f.seq, 8);
    o += f.payload;
    return o;
}

contracts::Result<std::uint32_t> parse_header(std::string_view h, MsgType& type, std::uint64_t& seq) {
    using R = contracts::Result<std::uint32_t>;
    if (h.size() < kHeaderSize) return R::fail(ErrorCode::ParseError, "short header");
    if (get(h, 0, 4) != kMagic) return R::fail(ErrorCode::ParseError, "bad magic");
    const auto ver = static_cast<std::uint16_t>(get(h, 4, 2));
    if (ver != kProtocolVersion) return R::fail(ver > kProtocolVersion ? ErrorCode::SchemaTooNew : ErrorCode::ParseError, "version");
    const auto t = static_cast<std::uint16_t>(get(h, 6, 2));
    if (!known_type(t)) return R::fail(ErrorCode::InvalidEnum, "type");
    const auto len = static_cast<std::uint32_t>(get(h, 8, 4));
    if (len > kMaxPayload) return R::fail(ErrorCode::ParseLimit, "payload too large");
    type = static_cast<MsgType>(t);
    seq = get(h, 12, 8);
    return len;
}

contracts::Result<Frame> decode(std::string_view bytes) {
    using R = contracts::Result<Frame>;
    Frame f;
    auto len = parse_header(bytes, f.type, f.seq);
    if (!len.ok()) return R(len.error());
    if (bytes.size() != kHeaderSize + len.value()) return R::fail(ErrorCode::ParseError, "length mismatch");
    f.payload.assign(bytes.substr(kHeaderSize));
    return f;
}

contracts::Result<DecodedRequest> decode_request(std::string_view payload) {
    using R = contracts::Result<DecodedRequest>;
    auto doc = json::parse(payload, ipc_limits());
    if (!doc.ok()) return R::fail(ErrorCode::ParseError, "malformed request");
    for (const auto& k : doc.value().keys())
        if (k != "request_id" && k != "op" && k != "action") return R::fail(ErrorCode::ParseError, "unknown request field");
    DecodedRequest out;
    if (doc.value().find("action")) {
        auto a = json::get_string(doc.value(), "action", 16);
        if (!a.ok()) return R::fail(ErrorCode::ParseError, "action");
        if (a.value() == "apply") out.action = RequestAction::Apply;
        else if (a.value() == "restore") out.action = RequestAction::Restore;
        else if (a.value() == "fix") out.action = RequestAction::Fix;
        else if (a.value() == "undo_fix") out.action = RequestAction::UndoFix;
        else if (a.value() == "recover") out.action = RequestAction::Recover;
        else if (a.value() == "keep_current") out.action = RequestAction::KeepCurrent;
        else if (a.value() == "restore_point") out.action = RequestAction::RestorePoint;
        else if (a.value() == "maintenance") out.action = RequestAction::Maintenance;
        else if (a.value() == "driver_install") out.action = RequestAction::DriverInstall;
        else if (a.value() == "vendor_install") out.action = RequestAction::VendorInstall;
        else return R::fail(ErrorCode::InvalidEnum, "unknown action");
    }
    auto rid = json::get_string(doc.value(), "request_id", 64);
    if (!rid.ok() || rid.value().empty()) return R::fail(ErrorCode::ParseError, "request_id");
    out.request_id = rid.value();
    const auto* opv = doc.value().find("op");
    if (!opv) return R::fail(ErrorCode::ParseError, "op missing");
    auto op = json::decode_operation(*opv);
    if (!op.ok()) return R::fail(ErrorCode::ParseError, "operation rejected: " + op.error().detail);
    out.op = op.value();
    return out;
}

Frame ServerSession::fail(std::string why) {
    state_ = State::Closed;
    error_ = why;
    Frame e{MsgType::Error, ++out_seq_, {}};
    auto o = json::Value::object();
    o.set("error", json::Value::string(why));
    e.payload = json::write(o);
    return e;
}

Frame ServerSession::on_frame(const Frame& in, std::uint32_t observed_pid) {
    if (state_ == State::Closed) return fail("session closed");
    if (observed_pid != pid_) return fail("unexpected client process");
    if (in.seq != last_seq_ + 1) return fail("sequence violation (replay or reorder)");
    last_seq_ = in.seq;
    if (++frames_ > kMaxSessionFrames) return fail("too many frames");
    if (in.payload.size() > kMaxPayload) return fail("payload too large");

    if (state_ == State::AwaitHello) {
        if (in.type != MsgType::Hello) return fail("hello required");
        auto doc = json::parse(in.payload, ipc_limits());
        if (!doc.ok()) return fail("malformed hello");
        const auto& keys = doc.value().keys();  // ADR-037: tam olarak {"nonce","pid"}
        if (keys.size() != 2) return fail("hello fields");
        for (const auto& k : keys)
            if (k != "nonce" && k != "pid") return fail("hello fields");
        auto nonce = json::get_string(doc.value(), "nonce", 64);
        auto pid = json::get_int(doc.value(), "pid", 1, 0xFFFFFFFFll);
        if (!nonce.ok() || !pid.ok()) return fail("hello fields");
        if (!core::is_valid_nonce(nonce.value()) || !equal_ct(nonce.value(), nonce_)) return fail("nonce mismatch");
        if (static_cast<std::uint32_t>(pid.value()) != pid_) return fail("pid mismatch");
        nonce_.clear();  // tek kullanimlik
        state_ = State::Ready;
        return Frame{MsgType::HelloAck, ++out_seq_, "{}"};
    }

    switch (in.type) {
        case MsgType::Request: {
            auto req = decode_request(in.payload);
            if (!req.ok()) return fail(req.error().detail);
            const auto& r = req.value();
            std::string body = handler_ ? handler_(r.request_id, r.action, r.op) : std::string("{}");
            return Frame{MsgType::Response, ++out_seq_, std::move(body)};
        }
        case MsgType::Cancel: return Frame{MsgType::Response, ++out_seq_, R"({"cancelled":true})"};
        case MsgType::Bye:
            state_ = State::Closed;
            error_.clear();
            return Frame{MsgType::Bye, ++out_seq_, "{}"};
        default: return fail("unexpected message type");
    }
}

Frame make_hello(std::string_view nonce, std::uint32_t pid, std::uint64_t seq) {
    auto o = json::Value::object();
    o.set("nonce", json::Value::string(std::string(nonce)));
    o.set("pid", json::Value::integer(pid));
    return Frame{MsgType::Hello, seq, json::write(o)};
}

std::string_view to_string(RequestAction a) noexcept {
    switch (a) {
        case RequestAction::Apply: return "apply";
        case RequestAction::Restore: return "restore";
        case RequestAction::Fix: return "fix";
        case RequestAction::UndoFix: return "undo_fix";
        case RequestAction::Recover: return "recover";
        case RequestAction::KeepCurrent: return "keep_current";
        case RequestAction::RestorePoint: return "restore_point";
        case RequestAction::Maintenance: return "maintenance";
        case RequestAction::DriverInstall: return "driver_install";
        case RequestAction::VendorInstall: return "vendor_install";
    }
    return "apply";
}

Frame make_request(std::string_view request_id, const contracts::SettingOperation& op, std::uint64_t seq, RequestAction action) {
    auto o = json::Value::object();
    o.set("request_id", json::Value::string(std::string(request_id)));
    o.set("op", json::encode_operation(op));
    if (action != RequestAction::Apply) o.set("action", json::Value::string(std::string(to_string(action))));
    return Frame{MsgType::Request, seq, json::write(o)};
}

}  // namespace ef::core::ipc
