#include "core/setup/setup_protocol.h"

#include <algorithm>
#include <array>
#include <vector>

#include "core/roles.h"
#include "core/serialization/json.h"

namespace ef::core::setup {

using contracts::ErrorCode;
using contracts::Result;
namespace json = core::json;

namespace {

constexpr std::array<SetupStep, 17> kAllSteps = {
    SetupStep::Probe,       SetupStep::CreateDir,  SetupStep::CopyExe,        SetupStep::Shortcut,
    SetupStep::ServiceCreate, SetupStep::ServiceStart, SetupStep::UpdatesDir, SetupStep::Arp,
    SetupStep::Commit,      SetupStep::ServiceStop, SetupStep::ArpRemove,     SetupStep::ShortcutRemove,
    SetupStep::ServiceDelete, SetupStep::MachineDataRemove, SetupStep::FilesRemove, SetupStep::DesktopShortcut,
    SetupStep::DesktopShortcutRemove};
constexpr std::array<StepRecord, 5> kAllRecs = {StepRecord::Started, StepRecord::Done, StepRecord::Undone,
                                                StepRecord::UndoFailed, StepRecord::Committed};

json::Limits request_limits() {
    json::Limits l;
    l.max_bytes = 256;
    l.max_depth = 2;
    l.max_string = 32;
    l.max_items = 4;
    return l;
}

json::Limits response_limits() {
    json::Limits l;
    l.max_bytes = 16 * 1024;
    l.max_depth = 4;
    l.max_string = 1024;
    l.max_items = 128;
    return l;
}

json::Limits hello_limits() {
    json::Limits l;
    l.max_bytes = 1024;
    l.max_depth = 2;
    l.max_string = 128;
    l.max_items = 8;
    return l;
}

template <typename T>
Result<T> fail(std::string d) { return Result<T>::fail(ErrorCode::ParseError, std::move(d)); }

std::optional<SetupStep> step_from(std::string_view s) {
    for (const auto v : kAllSteps)
        if (to_string(v) == s) return v;
    return std::nullopt;
}

std::optional<StepRecord> rec_from(std::string_view s) {
    for (const auto v : kAllRecs)
        if (to_string(v) == s) return v;
    return std::nullopt;
}

bool only_keys(const json::Value& o, std::initializer_list<std::string_view> allowed) {
    for (const auto& k : o.keys()) {
        bool ok = false;
        for (const auto a : allowed) ok = ok || k == a;
        if (!ok) return false;
    }
    return true;
}

json::Value step_array(const std::vector<SetupStep>& v) {
    auto a = json::Value::array();
    for (const auto s : v) a.push(json::Value::string(std::string(to_string(s))));
    return a;
}

Result<std::vector<SetupStep>> get_steps(const json::Value& o, std::string_view key) {
    const auto* a = o.find(key);
    if (!a || !a->is(json::Value::Type::Array)) return fail<std::vector<SetupStep>>(std::string(key));
    std::vector<SetupStep> out;
    for (const auto& it : a->items()) {
        if (!it.is(json::Value::Type::String)) return fail<std::vector<SetupStep>>(std::string(key));
        const auto s = step_from(it.as_string());
        if (!s) return fail<std::vector<SetupStep>>(std::string(key) + ": unknown step");
        out.push_back(*s);
    }
    return out;
}

json::Value encode_result(const TxResult& r) {
    auto o = json::Value::object();
    o.set("ok", json::Value::boolean(r.ok));
    if (r.failed_at) o.set("failed_at", json::Value::string(std::string(to_string(*r.failed_at))));
    o.set("rollback_clean", json::Value::boolean(r.rollback_clean));
    o.set("undo_failed", step_array(r.undo_failed));
    o.set("reboot_pending", json::Value::boolean(r.reboot_pending));
    o.set("done", step_array(r.done));
    o.set("machine_data_kept", json::Value::boolean(r.machine_data_kept));
    return o;
}

Result<TxResult> decode_result(const json::Value& o) {
    if (!o.is(json::Value::Type::Object)) return fail<TxResult>("result");
    if (!only_keys(o, {"ok", "failed_at", "rollback_clean", "undo_failed", "reboot_pending", "done", "machine_data_kept"}))
        return fail<TxResult>("unknown result field");
    TxResult r;
    auto ok = json::get_bool(o, "ok");
    auto clean = json::get_bool(o, "rollback_clean");
    auto reboot = json::get_bool(o, "reboot_pending");
    auto kept = json::get_bool(o, "machine_data_kept");
    if (!ok || !clean || !reboot || !kept) return fail<TxResult>("result flags");
    r.ok = ok.value();
    r.rollback_clean = clean.value();
    r.reboot_pending = reboot.value();
    r.machine_data_kept = kept.value();
    if (o.find("failed_at")) {
        auto s = json::get_string(o, "failed_at", 32);
        if (!s) return fail<TxResult>("failed_at");
        const auto st = step_from(s.value());
        if (!st) return fail<TxResult>("failed_at: unknown step");
        r.failed_at = *st;
    }
    auto undo = get_steps(o, "undo_failed");
    if (!undo) return undo.error();
    auto done = get_steps(o, "done");
    if (!done) return done.error();
    r.undo_failed = std::move(undo).value();
    r.done = std::move(done).value();
    if (r.ok && (r.failed_at || !r.undo_failed.empty())) return fail<TxResult>("inconsistent result");
    return r;
}

}  // namespace

std::string_view to_string(SetupKind k) noexcept {
    switch (k) {
        case SetupKind::Install: return "install";
        case SetupKind::Uninstall: return "uninstall";
        case SetupKind::Repair: return "repair";
    }
    return "install";
}

std::string encode_request(const SetupRequest& r) {
    auto o = json::Value::object();
    o.set("kind", json::Value::string(std::string(to_string(r.kind))));
    return json::write(o);
}

Result<SetupRequest> decode_request(std::string_view payload) {
    auto doc = json::parse(payload, request_limits());
    if (!doc) return doc.error();
    const auto& o = doc.value();
    if (!o.is(json::Value::Type::Object) || o.keys().size() != 1 || o.keys().front() != "kind")
        return fail<SetupRequest>("request must carry only kind");
    auto k = json::get_string(o, "kind", 16);
    if (!k) return k.error();
    SetupRequest r;
    if (k.value() == "install") r.kind = SetupKind::Install;
    else if (k.value() == "uninstall") r.kind = SetupKind::Uninstall;
    else if (k.value() == "repair") r.kind = SetupKind::Repair;
    else return Result<SetupRequest>::fail(ErrorCode::InvalidEnum, "kind");
    return r;
}

std::string encode_progress(const SetupProgress& p) {
    auto in = json::Value::object();
    in.set("step", json::Value::string(std::string(to_string(p.step))));
    in.set("rec", json::Value::string(std::string(to_string(p.rec))));
    auto o = json::Value::object();
    o.set("progress", std::move(in));
    return json::write(o);
}

std::string encode_reply(const SetupReply& r) {
    auto o = json::Value::object();
    o.set("result", encode_result(r.result));
    // Hata metni platform ayrintisi tasir (or. yerel kod sayfasindaki ec.message()): istemci 512 bayttan uzun veya gecersiz
    // UTF-8 metni reddeder ve sonucu "bilinmiyor" sayardi. Yazdirilabilir ASCII'ye indirgenir ve kirpilir.
    std::string err;
    err.reserve(std::min<std::size_t>(r.error.size(), kMaxReplyError));
    for (const char c : r.error) {
        if (err.size() >= kMaxReplyError) break;
        const auto u = static_cast<unsigned char>(c);
        err.push_back(u >= 0x20 && u < 0x7F ? c : '?');
    }
    o.set("error", json::Value::string(std::move(err)));
    return json::write(o);
}

Result<SetupResponse> decode_response(std::string_view payload) {
    auto doc = json::parse(payload, response_limits());
    if (!doc) return doc.error();
    const auto& o = doc.value();
    if (!o.is(json::Value::Type::Object)) return fail<SetupResponse>("response");
    SetupResponse out;
    if (const auto* p = o.find("progress")) {
        if (o.keys().size() != 1 || !p->is(json::Value::Type::Object) || p->keys().size() != 2 || !only_keys(*p, {"step", "rec"}))
            return fail<SetupResponse>("progress shape");
        auto s = json::get_string(*p, "step", 32);
        auto r = json::get_string(*p, "rec", 32);
        if (!s || !r) return fail<SetupResponse>("progress fields");
        const auto st = step_from(s.value());
        const auto rc = rec_from(r.value());
        if (!st || !rc) return Result<SetupResponse>::fail(ErrorCode::InvalidEnum, "progress enum");
        out.progress = SetupProgress{*st, *rc};
        return out;
    }
    if (o.keys().size() != 2 || !only_keys(o, {"result", "error"})) return fail<SetupResponse>("reply shape");
    const auto* rv = o.find("result");
    if (!rv) return fail<SetupResponse>("result missing");
    auto res = decode_result(*rv);
    if (!res) return res.error();
    auto err = json::get_string(o, "error", kMaxReplyError);
    if (!err) return err.error();
    out.reply = SetupReply{std::move(res).value(), err.value()};
    return out;
}

SetupServerSession::Step SetupServerSession::fail(std::string why) {
    state_ = State::Closed;
    error_ = why;
    auto o = json::Value::object();
    o.set("error", json::Value::string(std::move(why)));
    return Step{ipc::Frame{ipc::MsgType::Error, ++out_seq_, json::write(o)}, std::nullopt};
}

SetupServerSession::Step SetupServerSession::on_frame(const ipc::Frame& in, std::uint32_t observed_pid) {
    if (state_ == State::Closed) return fail("session closed");
    if (observed_pid != pid_) return fail("unexpected client process");
    if (in.seq != last_seq_ + 1) return fail("sequence violation (replay or reorder)");
    last_seq_ = in.seq;
    if (in.payload.size() > ipc::kMaxPayload) return fail("payload too large");

    if (state_ == State::AwaitHello) {
        if (in.type != ipc::MsgType::Hello) return fail("hello required");
        auto doc = json::parse(in.payload, hello_limits());
        if (!doc || !doc.value().is(json::Value::Type::Object) || doc.value().keys().size() != 2 ||
            !only_keys(doc.value(), {"nonce", "pid"}))
            return fail("malformed hello");
        auto nonce = json::get_string(doc.value(), "nonce", 64);
        auto pid = json::get_int(doc.value(), "pid", 1, 0xFFFFFFFFll);
        if (!nonce || !pid) return fail("hello fields");
        if (!core::is_valid_nonce(nonce.value()) || nonce.value() != nonce_) return fail("nonce mismatch");
        if (static_cast<std::uint32_t>(pid.value()) != pid_) return fail("pid mismatch");
        nonce_.clear();  // tek kullanimlik
        state_ = State::Ready;
        return Step{ipc::Frame{ipc::MsgType::HelloAck, ++out_seq_, "{}"}, std::nullopt};
    }

    switch (in.type) {
        case ipc::MsgType::Request: {
            if (request_taken_) return fail("one request per session");
            auto req = decode_request(in.payload);
            if (!req) return fail("request rejected: " + req.error().detail);
            request_taken_ = true;
            return Step{std::nullopt, req.value()};
        }
        case ipc::MsgType::Bye:
            state_ = State::Closed;
            error_.clear();
            return Step{ipc::Frame{ipc::MsgType::Bye, ++out_seq_, "{}"}, std::nullopt};
        default: return fail("unexpected message type");
    }
}

}  // namespace ef::core::setup
