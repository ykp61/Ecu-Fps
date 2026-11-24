#include "core/adapters/windows/display_process.h"

#include <cstdio>

namespace ef::core::adapters::windows {

using contracts::ErrorCode;
using contracts::Status;

namespace {
std::optional<std::uint64_t> parse_u64(std::string_view s, std::size_t max_len) {
    if (s.empty() || s.size() > max_len) return std::nullopt;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return v;
}
using OptStr = std::optional<std::string>;
}  // namespace

// ---------------- W08 ----------------
std::optional<std::uint32_t> highest_refresh(const std::vector<DisplayMode>& modes, const DisplayMode& cur) {
    std::optional<std::uint32_t> best;
    for (const auto& m : modes)
        if (m.width == cur.width && m.height == cur.height && (!best || m.hz > *best)) best = m.hz;
    return best;
}

std::string gdi_display_token(std::string_view g) {
    constexpr std::string_view pre = "\\\\.\\DISPLAY";
    if (g.size() <= pre.size() || g.size() > pre.size() + 3 || g.substr(0, pre.size()) != pre) return {};
    const auto k = g.substr(pre.size());
    for (char c : k)
        if (c < '0' || c > '9') return {};
    return "DISPLAY" + std::string(k);
}

namespace {
bool valid_token(std::string_view t) {
    constexpr std::string_view pre = "DISPLAY";
    if (t.size() <= pre.size() || t.size() > pre.size() + 3 || t.substr(0, pre.size()) != pre) return false;
    for (char c : t.substr(pre.size()))
        if (c < '0' || c > '9') return false;
    return true;
}
}  // namespace

contracts::Result<DisplayRef> parse_display_target(std::string_view t) {
    using R = contracts::Result<DisplayRef>;
    if (t.rfind("display:", 0) != 0) return R::fail(ErrorCode::ParseError, "display target");
    t.remove_prefix(8);
    const auto c = t.find(':');
    if (c == std::string_view::npos) return R::fail(ErrorCode::ParseError, "display target");
    auto n = parse_u64(t.substr(0, c), 2);
    if (!n) return R::fail(ErrorCode::ParseError, "display index");
    auto rest = t.substr(c + 1);
    DisplayRef ref;
    ref.index = static_cast<std::uint32_t>(*n);
    if (rest == "refresh") return ref;
    constexpr std::string_view kw = "refresh:";
    if (rest.substr(0, kw.size()) != kw || !valid_token(rest.substr(kw.size())))
        return R::fail(ErrorCode::ParseError, "display target");
    ref.gdi = "\\\\.\\" + std::string(rest.substr(kw.size()));
    return ref;
}

std::string format_display_target(std::uint32_t index, std::string_view token) {
    if (!valid_token(token)) return {};
    return "display:" + std::to_string(index) + ":refresh:" + std::string(token);
}

contracts::Result<recovery::Snapshot> RefreshRateAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto d = parse_display_target(op.target);
    if (!d.ok()) return R(d.error());
    auto cur = api_.current(d.value());
    if (!cur.ok()) return R(cur.error());
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = std::to_string(cur.value().hz);
    s.raw_type = "hz";
    s.source_version = std::to_string(cur.value().width) + "x" + std::to_string(cur.value().height);
    return s;
}

Status RefreshRateAdapter::apply(const contracts::SettingOperation& op) {
    auto d = parse_display_target(op.target);
    if (!d.ok()) return Status(d.error());
    const auto hz = parse_u64(op.new_value, 4);
    if (!hz || *hz < 24 || *hz > 1000) return Status::fail(ErrorCode::OutOfRange, "refresh rate");
    auto cur = api_.current(d.value());
    auto modes = api_.modes(d.value());
    if (!cur.ok() || !modes.ok()) return Status::fail(ErrorCode::IoError, "display query");
    DisplayMode want{cur.value().width, cur.value().height, static_cast<std::uint32_t>(*hz)};
    bool listed = false;
    for (const auto& m : modes.value()) listed = listed || m == want;
    if (!listed) return Status::fail(ErrorCode::Unsupported, "mode not offered by display/driver at this resolution");
    if (auto t = api_.test(d.value(), want); !t.ok()) return Status::fail(ErrorCode::Unsupported, "driver rejected mode test");
    return (persistent_ ? api_.set_persistent(d.value(), want) : api_.set(d.value(), want));
}

contracts::Result<OptStr> RefreshRateAdapter::read_back(const contracts::SettingOperation& op) {
    // Ayni hedef (GDI aygit adi) okunur: baska ekranin degeri "basarili" sayilmaz.
    auto d = parse_display_target(op.target);
    if (!d.ok()) return contracts::Result<OptStr>(d.error());
    auto cur = api_.current(d.value());
    if (!cur.ok()) return contracts::Result<OptStr>(cur.error());
    return OptStr{std::to_string(cur.value().hz)};
}

Status RefreshRateAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto d = parse_display_target(op.target);
    if (!d.ok()) return Status(d.error());
    const auto hz = parse_u64(snap.old_value, 4);
    auto cur = api_.current(d.value());
    if (!hz || !cur.ok()) return Status::fail(ErrorCode::ParseError, "snapshot");
    const DisplayMode back{cur.value().width, cur.value().height, static_cast<std::uint32_t>(*hz)};
    return persistent_ ? api_.set_persistent(d.value(), back) : api_.set(d.value(), back);
}

bool DisplayConfirmation::should_revert(MonoTime now) const {
    if (!active_ || confirmed_) return false;
    return now.ns - started_.ns >= timeout_.ns;
}

std::int64_t DisplayConfirmation::remaining_ms(MonoTime now) const {
    if (!active_) return 0;
    const auto left = timeout_.ns - (now.ns - started_.ns);
    return left > 0 ? left / 1'000'000 : 0;
}

// ---------------- W09 ----------------
std::string_view to_string(VisualEffect e) noexcept {
    switch (e) {
        case VisualEffect::ClientAreaAnimation: return "client_animation";
        case VisualEffect::MenuAnimation: return "menu_animation";
        case VisualEffect::ComboBoxAnimation: return "combobox_animation";
        case VisualEffect::ListBoxSmoothScrolling: return "listbox_smooth_scrolling";
        case VisualEffect::CursorShadow: return "cursor_shadow";
        case VisualEffect::DropShadow: return "drop_shadow";
        case VisualEffect::Transparency: return "transparency";
    }
    return "?";
}

std::optional<VisualEffect> parse_visual_effect(std::string_view s) noexcept {
    for (auto e : {VisualEffect::ClientAreaAnimation, VisualEffect::MenuAnimation, VisualEffect::ComboBoxAnimation,
                   VisualEffect::ListBoxSmoothScrolling, VisualEffect::CursorShadow, VisualEffect::DropShadow, VisualEffect::Transparency})
        if (to_string(e) == s) return e;
    return std::nullopt;  // yazi yumusatma (fontsmoothing) bilerek listede yok
}

namespace {
contracts::Result<VisualEffect> vfx_target(std::string_view t) {
    if (t.rfind("vfx:", 0) != 0) return contracts::Result<VisualEffect>::fail(ErrorCode::ParseError, "vfx target");
    auto e = parse_visual_effect(t.substr(4));
    if (!e) return contracts::Result<VisualEffect>::fail(ErrorCode::Denied, "effect not allowed");
    return *e;
}
}  // namespace

contracts::Result<recovery::Snapshot> VisualEffectAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto e = vfx_target(op.target);
    if (!e.ok()) return R(e.error());
    auto v = api_.get(e.value());
    if (!v.ok()) return R(v.error());
    recovery::Snapshot s;
    s.existed = true;
    s.old_value = v.value() ? "1" : "0";
    s.raw_type = "bool";
    return s;
}

Status VisualEffectAdapter::apply(const contracts::SettingOperation& op) {
    auto e = vfx_target(op.target);
    if (!e.ok()) return Status(e.error());
    if (op.new_value != "0" && op.new_value != "1") return Status::fail(ErrorCode::ParseError, "bool");
    return api_.set(e.value(), op.new_value == "1");
}

contracts::Result<OptStr> VisualEffectAdapter::read_back(const contracts::SettingOperation& op) {
    auto e = vfx_target(op.target);
    if (!e.ok()) return contracts::Result<OptStr>(e.error());
    auto v = api_.get(e.value());
    if (!v.ok()) return contracts::Result<OptStr>(v.error());
    return OptStr{v.value() ? "1" : "0"};
}

Status VisualEffectAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto e = vfx_target(op.target);
    if (!e.ok()) return Status(e.error());
    return api_.set(e.value(), snap.old_value == "1");
}

// ---------------- W10 / W11 ----------------
contracts::Result<ProcessTarget> parse_process_target(std::string_view s) {
    using R = contracts::Result<ProcessTarget>;
    if (s.rfind("proc:", 0) != 0) return R::fail(ErrorCode::ParseError, "process target");
    s.remove_prefix(5);
    const auto a = s.find(':');
    const auto b = a == std::string_view::npos ? a : s.find(':', a + 1);
    if (b == std::string_view::npos) return R::fail(ErrorCode::ParseError, "process target");
    auto pid = parse_u64(s.substr(0, a), 10);
    auto ct = parse_u64(s.substr(a + 1, b - a - 1), 20);
    if (!pid || !ct || *pid == 0 || *pid > 0xFFFFFFFFull || *ct == 0) return R::fail(ErrorCode::ParseError, "process identity");
    ProcessTarget t;
    t.id = {static_cast<std::uint32_t>(*pid), *ct};
    const auto kind = s.substr(b + 1);
    if (kind == "priority") t.priority = true;
    else if (kind == "power_throttling") t.priority = false;
    else return R::fail(ErrorCode::InvalidEnum, "process setting");
    return t;
}

std::string format_process_target(const ProcessTarget& t) {
    return "proc:" + std::to_string(t.id.pid) + ":" + std::to_string(t.id.creation) + (t.priority ? ":priority" : ":power_throttling");
}

std::string_view to_string(PriorityClass p) noexcept {
    switch (p) {
        case PriorityClass::Idle: return "idle";
        case PriorityClass::BelowNormal: return "below_normal";
        case PriorityClass::Normal: return "normal";
        case PriorityClass::AboveNormal: return "above_normal";
        case PriorityClass::High: return "high";
        case PriorityClass::Realtime: return "realtime";
    }
    return "?";
}

std::optional<PriorityClass> parse_priority(std::string_view s) noexcept {
    for (auto p : {PriorityClass::Idle, PriorityClass::BelowNormal, PriorityClass::Normal, PriorityClass::AboveNormal,
                   PriorityClass::High, PriorityClass::Realtime})
        if (to_string(p) == s) return p;
    return std::nullopt;
}

bool is_writable_priority(PriorityClass p) noexcept { return p == PriorityClass::Normal || p == PriorityClass::AboveNormal; }

std::string ProcessAdapter::encode(ThrottleState s) {
    char b[24];
    std::snprintf(b, sizeof b, "%x:%x", s.control, s.state);
    return b;
}

std::optional<ThrottleState> ProcessAdapter::decode_throttle(std::string_view v) {
    if (v == "off") return ThrottleState{kThrottleExecutionSpeed, 0};  // kisitlamayi kapat: kontrol et, durum 0
    const auto c = v.find(':');
    if (c == std::string_view::npos || c == 0 || c > 8 || v.size() - c - 1 > 8) return std::nullopt;
    auto hex = [](std::string_view h) -> std::optional<std::uint32_t> {
        std::uint32_t x = 0;
        for (char ch : h) {
            x <<= 4;
            if (ch >= '0' && ch <= '9') x |= static_cast<std::uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') x |= static_cast<std::uint32_t>(ch - 'a' + 10);
            else return std::nullopt;
        }
        return x;
    };
    auto a = hex(v.substr(0, c)), b = hex(v.substr(c + 1));
    if (!a || !b) return std::nullopt;
    return ThrottleState{*a, *b};
}

contracts::Result<recovery::Snapshot> ProcessAdapter::capture(const contracts::SettingOperation& op) {
    using R = contracts::Result<recovery::Snapshot>;
    auto t = parse_process_target(op.target);
    if (!t.ok()) return R(t.error());
    if (auto v = api_.verify(t.value().id); !v.ok()) return R(v.error());
    recovery::Snapshot s;
    s.existed = true;
    if (t.value().priority) {
        auto p = api_.get_priority(t.value().id);
        if (!p.ok()) return R(p.error());
        s.old_value = std::string(to_string(p.value()));
        s.raw_type = "priority_class";
    } else {
        auto th = api_.get_throttling(t.value().id);
        if (!th.ok()) return R(th.error());
        s.old_value = encode(th.value());
        s.raw_type = "power_throttling";
    }
    return s;
}

Status ProcessAdapter::apply(const contracts::SettingOperation& op) {
    auto t = parse_process_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (auto v = api_.verify(t.value().id); !v.ok()) return v;
    if (t.value().priority) {
        auto p = parse_priority(op.new_value);
        if (!p) return Status::fail(ErrorCode::InvalidEnum, "priority");
        if (!is_writable_priority(*p)) return Status::fail(ErrorCode::Denied, "only Normal/AboveNormal may be set");
        return api_.set_priority(t.value().id, *p);
    }
    auto th = decode_throttle(op.new_value);
    if (!th) return Status::fail(ErrorCode::ParseError, "throttling value");
    return api_.set_throttling(t.value().id, *th);
}

contracts::Result<OptStr> ProcessAdapter::read_back(const contracts::SettingOperation& op) {
    using R = contracts::Result<OptStr>;
    auto t = parse_process_target(op.target);
    if (!t.ok()) return R(t.error());
    if (!api_.verify(t.value().id).ok()) return OptStr{};  // surec bitti: hedef artik yok
    if (t.value().priority) {
        auto p = api_.get_priority(t.value().id);
        if (!p.ok()) return R(p.error());
        return OptStr{std::string(to_string(p.value()))};
    }
    auto th = api_.get_throttling(t.value().id);
    if (!th.ok()) return R(th.error());
    // Yazdigimiz "off" degeriyle karsilastirilabilmesi icin kanonik bicim
    if (th.value() == ThrottleState{kThrottleExecutionSpeed, 0}) return OptStr{"off"};
    return OptStr{encode(th.value())};
}

bool ProcessAdapter::target_gone(const contracts::SettingOperation& op) {
    auto t = parse_process_target(op.target);
    if (!t.ok()) return false;
    const auto v = api_.verify(t.value().id);
    return !v.ok() && v.code() == ErrorCode::MissingValue;
}

Status ProcessAdapter::restore(const contracts::SettingOperation& op, const recovery::Snapshot& snap) {
    auto t = parse_process_target(op.target);
    if (!t.ok()) return Status(t.error());
    if (!api_.verify(t.value().id).ok()) return contracts::ok_status();  // bitmis surece dokunulmaz
    if (t.value().priority) {
        auto p = parse_priority(snap.old_value);
        if (!p) return Status::fail(ErrorCode::ParseError, "snapshot priority");
        return api_.set_priority(t.value().id, *p);  // onceki sinif aynen (High olsa bile kullanicinin degeri)
    }
    auto th = decode_throttle(snap.old_value);
    if (!th) return Status::fail(ErrorCode::ParseError, "snapshot throttling");
    return api_.set_throttling(t.value().id, *th);
}

ThrottleState execution_speed_opt_out(ThrottleState prev) noexcept {
    return ThrottleState{prev.control | kThrottleExecutionSpeed, prev.state & ~kThrottleExecutionSpeed};
}

ScopedExecutionSpeedOptOut::ScopedExecutionSpeedOptOut(SelfThrottleApi& api) : api_(api) {
    prev_ = api_.get().value_or(ThrottleState{});
    const ThrottleState want = execution_speed_opt_out(prev_);
    if (want == prev_) return;  // zaten kisitlamasiz: yazma yok, geri yazma da yok
    active_ = api_.set(want);
}

ScopedExecutionSpeedOptOut::~ScopedExecutionSpeedOptOut() { restore(); }

bool ScopedExecutionSpeedOptOut::restore() {
    if (!active_) return false;
    active_ = false;
    return api_.set(prev_);
}

}  // namespace ef::core::adapters::windows
