#include "core/adapters/windows/background_process.h"

#include <cctype>
#include <cstdio>

#include "core/recovery/sha256.h"

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;
using OptStr = std::optional<std::string>;

namespace {
std::optional<std::uint64_t> parse_dec(std::string_view s, std::size_t max_len) {
    if (s.empty() || s.size() > max_len) return std::nullopt;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return v;
}

std::string canonical(PriorityClass p, ThrottleState t) {
    char b[48];
    std::snprintf(b, sizeof b, ";thr=%x:%x", t.control, t.state);
    return "prio=" + std::string(to_string(p)) + b;
}

bool eco_on(ThrottleState t) {
    return (t.control & kThrottleExecutionSpeed) != 0 && (t.state & kThrottleExecutionSpeed) != 0;
}

bool parse_canonical(std::string_view v, PriorityClass& p, ThrottleState& t) {
    if (v.rfind("prio=", 0) != 0) return false;
    const auto semi = v.find(";thr=");
    if (semi == std::string_view::npos) return false;
    auto pc = parse_priority(v.substr(5, semi - 5));
    auto th = ProcessAdapter::decode_throttle(v.substr(semi + 5));
    if (!pc || !th) return false;
    p = *pc;
    t = *th;
    return true;
}
}  // namespace

std::string bg_path_hash(std::string_view image_path) {
    std::string n(image_path);
    for (char& c : n) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '/') c = '\\';
    }
    return recovery::sha256_hex(n).substr(0, 16);
}

contracts::Result<BgProcessTarget> parse_bg_process_target(std::string_view s) {
    using R = contracts::Result<BgProcessTarget>;
    if (s.rfind("bgproc:", 0) != 0) return R::fail(ErrorCode::ParseError, "bgproc target");
    s.remove_prefix(7);
    const auto a = s.find(':');
    const auto b = a == std::string_view::npos ? a : s.find(':', a + 1);
    if (b == std::string_view::npos) return R::fail(ErrorCode::ParseError, "bgproc target");
    auto pid = parse_dec(s.substr(0, a), 10);
    auto ct = parse_dec(s.substr(a + 1, b - a - 1), 20);
    const auto h = s.substr(b + 1);
    if (!pid || !ct || *pid == 0 || *pid > 0xFFFFFFFFull || *ct == 0) return R::fail(ErrorCode::ParseError, "bgproc identity");
    if (h.size() != 16) return R::fail(ErrorCode::ParseError, "bgproc path hash");
    for (char c : h)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return R::fail(ErrorCode::ParseError, "bgproc path hash");
    BgProcessTarget t;
    t.id = {static_cast<std::uint32_t>(*pid), *ct};
    t.path_hash = std::string(h);
    return t;
}

std::string format_bg_process_target(const BgProcessTarget& t) {
    return "bgproc:" + std::to_string(t.id.pid) + ":" + std::to_string(t.id.creation) + ":" + t.path_hash;
}

Status BackgroundProcessAdapter::check_identity(const BgProcessTarget& t) {
    if (auto v = api_.verify(t.id); !v.ok()) return v;
    auto p = api_.image_path(t.id);
    if (!p.ok()) return Status(p.error());
    if (bg_path_hash(p.value()) != t.path_hash) return Status::fail(ErrorCode::MissingValue, "image path changed");
    return contracts::ok_status();
}

contracts::Result<recovery::Snapshot> BackgroundProcessAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto t = parse_bg_process_target(op.target);
    if (!t.ok()) return R(t.error());
    if (auto v = check_identity(t.value()); !v.ok()) return R(v.error());
    auto p = api_.get_priority(t.value().id);
    if (!p.ok()) return R(p.error());
    if (p.value() != PriorityClass::Normal && p.value() != PriorityClass::BelowNormal && p.value() != PriorityClass::AboveNormal)
        return R::fail(ErrorCode::Denied, "priority was chosen by the user or already low");
    auto th = api_.get_throttling(t.value().id);
    if (!th.ok()) return R(th.error());  // sorgu desteklenmiyorsa geri alinabilirlik kanitlanamaz: yazma yok
    if (eco_on(th.value())) return R::fail(ErrorCode::Denied, "power throttling already on");
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = canonical(p.value(), th.value());
    s.raw_type = "bg_process";
    return s;
}

Status BackgroundProcessAdapter::apply(const contracts::SettingOperation& op) {
    auto t = parse_bg_process_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (op.new_value != "low") return Status::fail(ErrorCode::InvalidEnum, "bgproc value");
    if (auto v = check_identity(t.value()); !v.ok()) return v;
    const auto& id = t.value().id;
    auto prev_p = api_.get_priority(id);
    if (!prev_p.ok()) return Status(prev_p.error());
    auto prev_t = api_.get_throttling(id);
    if (!prev_t.ok()) return Status(prev_t.error());
    if (auto s = api_.set_priority(id, PriorityClass::Idle); !s.ok()) return s;
    const ThrottleState want{prev_t.value().control | kThrottleExecutionSpeed, prev_t.value().state | kThrottleExecutionSpeed};
    if (auto s = api_.set_throttling(id, want); !s.ok()) {
        (void)api_.set_priority(id, prev_p.value());  // yarim kalan yazimi hemen geri al
        return s;
    }
    return contracts::ok_status();
}

contracts::Result<OptStr> BackgroundProcessAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<OptStr>;
    auto t = parse_bg_process_target(op.target);
    if (!t.ok()) return R(t.error());
    if (!check_identity(t.value()).ok()) return OptStr{};  // bitti ya da baska ornek: hedef yok
    auto p = api_.get_priority(t.value().id);
    if (!p.ok()) return R(p.error());
    auto th = api_.get_throttling(t.value().id);
    if (!th.ok()) return R(th.error());
    if (p.value() == PriorityClass::Idle && eco_on(th.value())) return OptStr{"low"};
    return OptStr{canonical(p.value(), th.value())};
}

bool BackgroundProcessAdapter::target_gone(const contracts::SettingOperation& op) {
    auto t = parse_bg_process_target(op.target);
    if (!t.ok()) return false;
    const auto v = check_identity(t.value());
    return !v.ok() && v.code() == ErrorCode::MissingValue;
}

Status BackgroundProcessAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto t = parse_bg_process_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (!check_identity(t.value()).ok()) return contracts::ok_status();  // yok/baska ornek: dokunulmaz
    PriorityClass p{};
    ThrottleState th{};
    if (!parse_canonical(snap.old_value, p, th)) return Status::fail(ErrorCode::ParseError, "snapshot bgproc");
    const auto& id = t.value().id;
    if (auto s = api_.set_priority(id, p); !s.ok()) return s;
    return api_.set_throttling(id, th);
}

}  // namespace ef::core::adapters::windows
