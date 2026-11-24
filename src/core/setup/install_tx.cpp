#include "core/setup/install_tx.h"

#include <algorithm>
#include <array>
#include <functional>

#include "core/serialization/json.h"
#include "core/setup/layout.h"

namespace ef::core::setup {

using contracts::ErrorCode;
using contracts::Result;
using contracts::Status;
namespace json = core::json;

namespace {

constexpr std::array<std::string_view, 17> kStepNames = {
    "Probe",     "CreateDir", "CopyExe",        "Shortcut",      "ServiceCreate",     "ServiceStart", "UpdatesDir", "Arp",
    "Commit",    "ServiceStop", "ArpRemove",    "ShortcutRemove", "ServiceDelete",    "MachineDataRemove", "FilesRemove",
    "DesktopShortcut", "DesktopShortcutRemove"};
constexpr std::array<std::string_view, 5> kRecNames = {"Started", "Done", "Undone", "UndoFailed", "Committed"};

constexpr std::size_t kMaxText = 1u << 16;  // bayt; yol/deger alanlari
json::Limits entry_limits() {
    json::Limits l;
    l.max_bytes = 1u << 18;
    l.max_string = 1u << 17;  // prior_json gomulu metin
    l.max_depth = 8;
    l.max_items = 256;
    return l;
}

// ---- UTF-16 <-> UTF-8 (saf; gecersiz dizi reddedilir) ----
void put_utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

std::string to_utf8(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        std::uint32_t c = static_cast<std::uint32_t>(w[i]) & 0xFFFFu;
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            const std::uint32_t d = static_cast<std::uint32_t>(w[i + 1]) & 0xFFFFu;
            if (d >= 0xDC00 && d <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                ++i;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        put_utf8(out, c);
    }
    return out;
}

std::optional<std::wstring> from_utf8(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        std::uint32_t c = 0;
        std::size_t n = 0;
        if (b0 < 0x80) { c = b0; n = 1; }
        else if ((b0 & 0xE0) == 0xC0) { c = b0 & 0x1Fu; n = 2; }
        else if ((b0 & 0xF0) == 0xE0) { c = b0 & 0x0Fu; n = 3; }
        else if ((b0 & 0xF8) == 0xF0) { c = b0 & 0x07u; n = 4; }
        else return std::nullopt;
        if (i + n > s.size()) return std::nullopt;
        for (std::size_t k = 1; k < n; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) return std::nullopt;
            c = (c << 6) | (b & 0x3Fu);
        }
        static constexpr std::uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (c < kMin[n] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return std::nullopt;
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(c));
        }
        i += n;
    }
    return out;
}

json::Value wstr(std::wstring_view w) { return json::Value::string(to_utf8(w)); }

template <typename T>
Result<T> fail(ErrorCode c, std::string d) { return Result<T>::fail(c, std::move(d)); }

Result<std::wstring> get_wstring(const json::Value& o, std::string_view key) {
    auto s = json::get_string(o, key, kMaxText);
    if (!s) return s.error();
    auto w = from_utf8(s.value());
    if (!w) return fail<std::wstring>(ErrorCode::ParseError, std::string(key) + ": utf8");
    return *w;
}

json::Value encode_arp(const ArpValues& a) {
    auto o = json::Value::object();
    o.set("name", wstr(a.display_name));
    o.set("version", wstr(a.display_version));
    o.set("publisher", wstr(a.publisher));
    o.set("icon", wstr(a.display_icon));
    o.set("location", wstr(a.install_location));
    o.set("uninstall", wstr(a.uninstall_string));
    o.set("size_kb", json::Value::integer(a.estimated_size_kb));
    o.set("date", wstr(a.install_date));
    o.set("no_modify", json::Value::boolean(a.no_modify));
    o.set("no_repair", json::Value::boolean(a.no_repair));
    return o;
}

Result<ArpValues> decode_arp(const json::Value& o) {
    if (!o.is(json::Value::Type::Object)) return fail<ArpValues>(ErrorCode::ParseError, "arp");
    ArpValues a;
    std::wstring* const ws[] = {&a.display_name, &a.display_version, &a.publisher, &a.display_icon, &a.install_location,
                                &a.uninstall_string, &a.install_date};
    constexpr std::string_view keys[] = {"name", "version", "publisher", "icon", "location", "uninstall", "date"};
    for (std::size_t i = 0; i < std::size(keys); ++i) {
        auto v = get_wstring(o, keys[i]);
        if (!v) return v.error();
        *ws[i] = std::move(v).value();
    }
    auto size = json::get_int(o, "size_kb", 0, 0xFFFFFFFFll);
    if (!size) return size.error();
    a.estimated_size_kb = static_cast<std::uint32_t>(size.value());
    auto nm = json::get_bool(o, "no_modify");
    if (!nm) return nm.error();
    auto nr = json::get_bool(o, "no_repair");
    if (!nr) return nr.error();
    a.no_modify = nm.value();
    a.no_repair = nr.value();
    return a;
}

// Alan zorunlu; null -> nullopt.
const json::Value* nullable(const json::Value& o, std::string_view key, bool& missing) {
    const json::Value* v = o.find(key);
    missing = v == nullptr;
    if (!v || v->is(json::Value::Type::Null)) return nullptr;
    return v;
}

template <typename E, std::size_t N>
std::optional<E> enum_from(const std::array<std::string_view, N>& names, std::string_view s) {
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == s) return static_cast<E>(i);
    return std::nullopt;
}

// ---- Islem motoru ----

bool point_of_no_return(SetupStep s) { return s == SetupStep::MachineDataRemove || s == SetupStep::FilesRemove; }

struct Tx {
    SetupOps& ops;
    SetupJournal& journal;
    std::string id;
    std::uint64_t seq = 0;
    PriorState prior;
    std::vector<SetupStep> started;  // Started kaydi kalici yazilmis adimlar (sirayla)
    TxResult r;

    bool append(SetupStep s, StepRecord rec, std::string detail = {}, std::string prior_json = {}) {
        JournalEntry e;
        e.seq = ++seq;
        e.tx_id = id;
        e.step = s;
        e.rec = rec;
        e.detail = std::move(detail);
        e.prior_json = std::move(prior_json);
        return journal.append(e).ok();
    }

    // Started (kalici) -> uygula -> Done. Started yazilamazsa adim UYGULANMAZ.
    bool step(SetupStep s, const std::function<Status()>& apply) {
        if (!append(s, StepRecord::Started)) {
            r.failed_at = s;
            return false;
        }
        started.push_back(s);
        const Status st = apply();
        if (!st) {
            r.failed_at = s;  // kismi etkisi de geri alinir (Started kaydi var)
            return false;
        }
        if (!append(s, StepRecord::Done)) {
            r.failed_at = s;
            return false;
        }
        r.done.push_back(s);
        return true;
    }
};

std::uint64_t last_seq(SetupJournal& j) {
    auto rd = j.read();
    if (!rd) return 0;
    std::uint64_t m = 0;
    for (const auto& e : rd.value()) m = std::max(m, e.seq);
    return m;
}

void record_undo(Tx& t, SetupStep s, const Status& st) {
    if (st) {
        t.append(s, StepRecord::Undone);  // en iyi caba: etki zaten geri alindi
    } else {
        t.append(s, StepRecord::UndoFailed, st.error().detail);
        t.r.rollback_clean = false;
        t.r.undo_failed.push_back(s);
    }
}

Status undo_step(Tx& t, SetupStep s, bool& restart_service) {
    SetupOps& o = t.ops;
    const PriorState& p = t.prior;
    switch (s) {
        case SetupStep::CreateDir: return p.dir_existed ? contracts::ok_status() : o.remove_install_dir_if_empty();
        case SetupStep::CopyExe: return o.restore_previous_exe(p);
        case SetupStep::Shortcut: return o.restore_shortcut(p);
        case SetupStep::ServiceCreate: return o.restore_service(p);
        case SetupStep::ServiceStart: {
            Status st = o.stop_service();
            // Onceki hizmet calisiyorduysa EXE ve yapilandirma geri geldikten SONRA (geri almanin sonunda) baslatilir.
            if (st && p.service_running) restart_service = true;
            return st;
        }
        case SetupStep::UpdatesDir: return p.updates_existed ? contracts::ok_status() : o.remove_updates_dir();
        case SetupStep::Arp: return o.restore_arp(p);
        case SetupStep::ServiceStop: return p.service_running ? o.start_service() : contracts::ok_status();
        case SetupStep::ArpRemove: return o.restore_arp(p);
        case SetupStep::ShortcutRemove: return o.restore_shortcut(p);
        case SetupStep::DesktopShortcut:
        case SetupStep::DesktopShortcutRemove: return o.restore_desktop_shortcut(p);
        case SetupStep::ServiceDelete: return o.restore_service(p);
        case SetupStep::Probe:
        case SetupStep::Commit:
        case SetupStep::MachineDataRemove:
        case SetupStep::FilesRemove: break;
    }
    return contracts::ok_status();
}

void rollback(Tx& t) {
    bool restart = false;
    bool restart_pending_record = false;
    for (auto it = t.started.rbegin(); it != t.started.rend(); ++it) {
        const SetupStep s = *it;
        if (s == SetupStep::Probe || s == SetupStep::Commit || point_of_no_return(s)) continue;
        bool want_restart = false;
        const Status st = undo_step(t, s, want_restart);
        if (s == SetupStep::ServiceStart && want_restart) {
            restart = true;
            restart_pending_record = true;  // kayit yeniden baslatmadan sonra
            continue;
        }
        record_undo(t, s, st);
    }
    if (restart) {
        const Status st = t.ops.start_service();
        if (restart_pending_record) record_undo(t, SetupStep::ServiceStart, st);
    }
}

void finish_failed(Tx& t) {
    rollback(t);
    t.r.ok = false;
    if (t.r.rollback_clean) static_cast<void>(t.journal.clear());  // islem temiz geri alindi; journal'da bekleyen is yok
}

bool probe(Tx& t, std::optional<SetupStep> fail_after) {
    if (!t.append(SetupStep::Probe, StepRecord::Started)) {
        t.r.failed_at = SetupStep::Probe;
        return false;
    }
    auto pr = t.ops.probe();
    if (!pr) {
        t.r.failed_at = SetupStep::Probe;
        static_cast<void>(t.journal.clear());  // hicbir sey uygulanmadi
        return false;
    }
    t.prior = pr.value();
    if (!t.append(SetupStep::Probe, StepRecord::Done, {}, encode_prior(t.prior))) {
        t.r.failed_at = SetupStep::Probe;
        return false;
    }
    t.r.done.push_back(SetupStep::Probe);
    return fail_after != SetupStep::Probe;
}

struct StepDef {
    SetupStep s;
    std::function<Status()> apply;
};

// Adimlari sirayla uygular. false: durdu (hata -> geri alindi, veya fail_after -> geri almadan durdu).
bool run_steps(Tx& t, const std::vector<StepDef>& defs, std::optional<SetupStep> fail_after) {
    for (const auto& d : defs) {
        if (!t.step(d.s, d.apply)) {
            finish_failed(t);
            return false;
        }
        if (fail_after == d.s) return false;  // guc kaybi benzetimi
    }
    return true;
}

// Committed -> temizle. Committed yazilamazsa temizleme denenir (islem tamam; geri alma artik dogru olmaz).
void finish_committed(Tx& t, std::optional<SetupStep> fail_after) {
    if (!t.append(SetupStep::Commit, StepRecord::Committed)) {
        if (!t.journal.clear()) {
            t.r.ok = false;
            t.r.failed_at = SetupStep::Commit;
            t.r.rollback_clean = false;
            return;
        }
        t.r.ok = true;
        t.r.done.push_back(SetupStep::Commit);
        return;
    }
    t.r.done.push_back(SetupStep::Commit);
    if (fail_after == SetupStep::Commit) return;
    static_cast<void>(t.journal.clear());
    t.r.ok = true;
}

}  // namespace

std::string_view to_string(SetupStep s) noexcept {
    const auto i = static_cast<std::size_t>(s);
    return i < kStepNames.size() ? kStepNames[i] : std::string_view{};
}
std::string_view to_string(StepRecord r) noexcept {
    const auto i = static_cast<std::size_t>(r);
    return i < kRecNames.size() ? kRecNames[i] : std::string_view{};
}

std::string encode_entry(const JournalEntry& e) {
    auto o = json::Value::object();
    o.set("seq", json::Value::integer(static_cast<std::int64_t>(e.seq & 0x7FFFFFFFFFFFFFFFull)));
    o.set("tx", json::Value::string(e.tx_id));
    o.set("step", json::Value::string(std::string(to_string(e.step))));
    o.set("rec", json::Value::string(std::string(to_string(e.rec))));
    o.set("detail", json::Value::string(e.detail));
    o.set("prior", json::Value::string(e.prior_json));
    return json::write(o);
}

Result<JournalEntry> decode_entry(std::string_view line) {
    auto v = json::parse(line, entry_limits());
    if (!v) return v.error();
    const auto& o = v.value();
    if (!o.is(json::Value::Type::Object)) return fail<JournalEntry>(ErrorCode::ParseError, "entry");
    JournalEntry e;
    auto seq = json::get_int(o, "seq", 0, INT64_MAX);
    if (!seq) return seq.error();
    e.seq = static_cast<std::uint64_t>(seq.value());
    auto tx = json::get_string(o, "tx", 128);
    if (!tx) return tx.error();
    if (tx.value().empty()) return fail<JournalEntry>(ErrorCode::MissingValue, "tx");
    e.tx_id = std::move(tx).value();
    auto st = json::get_string(o, "step", 64);
    if (!st) return st.error();
    auto step = enum_from<SetupStep>(kStepNames, st.value());
    if (!step) return fail<JournalEntry>(ErrorCode::InvalidEnum, "step");
    e.step = *step;
    auto rc = json::get_string(o, "rec", 64);
    if (!rc) return rc.error();
    auto rec = enum_from<StepRecord>(kRecNames, rc.value());
    if (!rec) return fail<JournalEntry>(ErrorCode::InvalidEnum, "rec");
    e.rec = *rec;
    auto d = json::get_string(o, "detail", kMaxText);
    if (!d) return d.error();
    e.detail = std::move(d).value();
    auto p = json::get_string(o, "prior", 1u << 17);
    if (!p) return p.error();
    e.prior_json = std::move(p).value();
    return e;
}

LinkAction desktop_remove_action(bool present, const std::optional<std::wstring>& current, const std::filesystem::path& exe) {
    if (!present) return LinkAction::Nothing;
    return current && same_path(*current, exe) ? LinkAction::Delete : LinkAction::Nothing;
}

LinkAction desktop_restore_action(const PriorState& p, bool present, const std::optional<std::wstring>& current,
                                  const std::filesystem::path& exe) {
    if (!p.desktop_existed) return present ? LinkAction::Delete : LinkAction::Nothing;
    if (p.desktop_target) return present && current && same_path(*current, *p.desktop_target) ? LinkAction::Nothing : LinkAction::Write;
    if (present && !(current && same_path(*current, exe))) return LinkAction::Nothing;  // okunamayan onceki dosya yerinde
    return LinkAction::Fail;
}

std::string encode_prior(const PriorState& p) {
    auto o = json::Value::object();
    o.set("dir", json::Value::boolean(p.dir_existed));
    o.set("exe", json::Value::boolean(p.exe_existed));
    o.set("shortcut", json::Value::boolean(p.shortcut_existed));
    o.set("service", json::Value::boolean(p.service_existed));
    o.set("running", json::Value::boolean(p.service_running));
    o.set("updates", json::Value::boolean(p.updates_existed));
    o.set("shortcut_target", p.shortcut_target ? wstr(*p.shortcut_target) : json::Value::null());
    if (p.service_config) {
        auto s = json::Value::object();
        s.set("image", wstr(p.service_config->image_path));
        s.set("delayed", json::Value::boolean(p.service_config->auto_start_delayed));
        o.set("service_config", std::move(s));
    } else {
        o.set("service_config", json::Value::null());
    }
    o.set("arp", p.arp ? encode_arp(*p.arp) : json::Value::null());
    o.set("desktop", json::Value::boolean(p.desktop_existed));
    o.set("desktop_target", p.desktop_target ? wstr(*p.desktop_target) : json::Value::null());
    return json::write(o);
}

Result<PriorState> decode_prior(std::string_view text) {
    auto v = json::parse(text, entry_limits());
    if (!v) return v.error();
    const auto& o = v.value();
    if (!o.is(json::Value::Type::Object)) return fail<PriorState>(ErrorCode::ParseError, "prior");
    PriorState p;
    bool* const bs[] = {&p.dir_existed, &p.exe_existed, &p.shortcut_existed, &p.service_existed, &p.service_running,
                        &p.updates_existed};
    constexpr std::string_view keys[] = {"dir", "exe", "shortcut", "service", "running", "updates"};
    for (std::size_t i = 0; i < std::size(keys); ++i) {
        auto b = json::get_bool(o, keys[i]);
        if (!b) return b.error();
        *bs[i] = b.value();
    }
    bool missing = false;
    if (nullable(o, "shortcut_target", missing)) {
        auto w = get_wstring(o, "shortcut_target");
        if (!w) return w.error();
        p.shortcut_target = std::move(w).value();
    }
    if (missing) return fail<PriorState>(ErrorCode::MissingValue, "shortcut_target");
    if (const auto* s = nullable(o, "service_config", missing)) {
        if (!s->is(json::Value::Type::Object)) return fail<PriorState>(ErrorCode::ParseError, "service_config");
        auto img = get_wstring(*s, "image");
        if (!img) return img.error();
        auto del = json::get_bool(*s, "delayed");
        if (!del) return del.error();
        p.service_config = ServiceConfig{std::move(img).value(), del.value()};
    }
    if (missing) return fail<PriorState>(ErrorCode::MissingValue, "service_config");
    if (const auto* a = nullable(o, "arp", missing)) {
        auto arp = decode_arp(*a);
        if (!arp) return arp.error();
        p.arp = std::move(arp).value();
    }
    if (missing) return fail<PriorState>(ErrorCode::MissingValue, "arp");
    // Masaustu alanlari sonradan eklendi: 0.3.0 journal'inda yoksa "yoktu" (geri almada yalniz bizimki silinir).
    if (o.find("desktop")) {
        auto b = json::get_bool(o, "desktop");
        if (!b) return b.error();
        p.desktop_existed = b.value();
    }
    if (nullable(o, "desktop_target", missing)) {
        auto w = get_wstring(o, "desktop_target");
        if (!w) return w.error();
        p.desktop_target = std::move(w).value();
    }
    return p;
}

TxResult run_install(SetupOps& ops, SetupJournal& journal, const ArpValues& arp, std::string tx_id,
                     std::optional<SetupStep> fail_after) {
    Tx t{ops, journal, std::move(tx_id), last_seq(journal), {}, {}, {}};
    if (!probe(t, fail_after)) return t.r;
    const std::vector<StepDef> defs = {
        {SetupStep::CreateDir, [&] { return ops.create_install_dir(); }},
        {SetupStep::CopyExe, [&] { return ops.stage_and_swap_exe(); }},
        {SetupStep::Shortcut, [&] { return ops.write_shortcut(); }},
        {SetupStep::DesktopShortcut, [&] { return ops.write_desktop_shortcut(); }},
        {SetupStep::ServiceCreate, [&] { return ops.write_service(); }},
        {SetupStep::ServiceStart, [&] { return ops.start_service(); }},
        {SetupStep::UpdatesDir, [&] { return ops.create_updates_dir(); }},
        {SetupStep::Arp, [&] { return ops.write_arp(arp); }},
    };
    if (!run_steps(t, defs, fail_after)) return t.r;
    // Commit: .old silinir (hata -> tam geri alma), sonra Committed + temizle.
    if (!t.append(SetupStep::Commit, StepRecord::Started)) {
        t.r.failed_at = SetupStep::Commit;
        finish_failed(t);
        return t.r;
    }
    t.started.push_back(SetupStep::Commit);
    if (!ops.commit_exe()) {
        t.r.failed_at = SetupStep::Commit;
        finish_failed(t);
        return t.r;
    }
    finish_committed(t, fail_after);
    return t.r;
}

TxResult run_repair(SetupOps& ops, SetupJournal& journal, std::string tx_id, const RepairOptions& opt) {
    Tx t{ops, journal, std::move(tx_id), last_seq(journal), {}, {}, {}};
    if (!probe(t, std::nullopt)) return t.r;
    std::vector<StepDef> defs = {
        {SetupStep::ServiceCreate, [&] { return ops.write_service(); }},
        {SetupStep::ServiceStart, [&] { return ops.start_service(); }},
        {SetupStep::UpdatesDir, [&] { return ops.create_updates_dir(); }},
        {SetupStep::DesktopShortcut, [&] { return ops.write_desktop_shortcut(); }},
    };
    // Plan probe'daki (journal'a yazilmis) onceki ARP'den: geri alma ayni onceki degeri yazar.
    std::optional<ArpValues> arp_plan;
    if (opt.layout) arp_plan = plan_display_version_refresh(t.prior.arp, *opt.layout, opt.running_version);
    if (arp_plan) defs.push_back({SetupStep::Arp, [&] { return ops.write_arp(*arp_plan); }});
    if (!run_steps(t, defs, std::nullopt)) return t.r;
    if (!t.append(SetupStep::Commit, StepRecord::Started)) {
        t.r.failed_at = SetupStep::Commit;
        finish_failed(t);
        return t.r;
    }
    t.started.push_back(SetupStep::Commit);
    finish_committed(t, std::nullopt);
    return t.r;
}

TxResult run_uninstall_program(SetupOps& ops, SetupJournal& journal, std::string tx_id, std::optional<SetupStep> fail_after) {
    Tx t{ops, journal, std::move(tx_id), last_seq(journal), {}, {}, {}};
    if (!probe(t, fail_after)) return t.r;
    const std::vector<StepDef> defs = {
        {SetupStep::ServiceStop, [&] { return ops.stop_service(); }},
        {SetupStep::ArpRemove, [&] { return ops.remove_arp(); }},
        {SetupStep::ShortcutRemove, [&] { return ops.remove_shortcut(); }},
        {SetupStep::DesktopShortcutRemove, [&] { return ops.remove_desktop_shortcut(); }},
        {SetupStep::ServiceDelete, [&] { return ops.delete_service(); }},
    };
    if (!run_steps(t, defs, fail_after)) return t.r;

    // Geri donulmez bolum: Started yazilamazsa henuz hicbir sey silinmedi -> geri al.
    if (!t.append(SetupStep::MachineDataRemove, StepRecord::Started)) {
        t.r.failed_at = SetupStep::MachineDataRemove;
        finish_failed(t);
        return t.r;
    }
    t.started.push_back(SetupStep::MachineDataRemove);
    const auto clean = ops.machine_journals_clean();
    std::string detail;
    if (clean.ok() && clean.value()) {
        if (!ops.remove_machine_data()) {
            t.r.failed_at = SetupStep::MachineDataRemove;
            t.r.rollback_clean = false;
            return t.r;
        }
    } else {
        t.r.machine_data_kept = true;
        detail = clean.ok() ? "kept: machine journals not clean" : "kept: machine journals unreadable";
    }
    t.append(SetupStep::MachineDataRemove, StepRecord::Done, detail);  // geri alinamaz; kayit en iyi caba
    t.r.done.push_back(SetupStep::MachineDataRemove);
    if (fail_after == SetupStep::MachineDataRemove) return t.r;

    if (!t.append(SetupStep::FilesRemove, StepRecord::Started)) {
        t.r.failed_at = SetupStep::FilesRemove;
        t.r.rollback_clean = false;
        return t.r;
    }
    t.started.push_back(SetupStep::FilesRemove);
    auto files = ops.remove_program_files();
    if (!files) {
        t.r.failed_at = SetupStep::FilesRemove;
        t.r.rollback_clean = false;
        return t.r;
    }
    t.r.reboot_pending = files.value();
    t.append(SetupStep::FilesRemove, StepRecord::Done, t.r.reboot_pending ? "reboot_pending" : "");
    t.r.done.push_back(SetupStep::FilesRemove);
    if (fail_after == SetupStep::FilesRemove) return t.r;
    finish_committed(t, fail_after);
    return t.r;
}

TxResult recover_interrupted(SetupOps& ops, SetupJournal& journal) {
    TxResult r;
    auto rd = journal.read();
    if (!rd) {
        r.rollback_clean = false;
        return r;
    }
    const auto& all = rd.value();
    if (all.empty()) {
        r.ok = true;
        return r;
    }
    const std::string id = all.back().tx_id;
    std::uint64_t max_seq = 0;
    std::vector<const JournalEntry*> es;
    for (const auto& e : all) {
        max_seq = std::max(max_seq, e.seq);
        if (e.tx_id == id) es.push_back(&e);
    }
    const bool committed = std::any_of(es.begin(), es.end(), [](const JournalEntry* e) { return e->rec == StepRecord::Committed; });
    if (committed) {
        static_cast<void>(journal.clear());
        r.ok = true;
        return r;
    }
    // Commit Started yalniz tum adimlar Done olduktan sonra yazilir. Geri alma baslamamissa (Undone/UndoFailed yok)
    // islem ileri tamamlanir: commit_exe .old'u silmis olabilir; geri almak eski EXE'siz yarim kurulum birakirdi.
    const auto has = [&](SetupStep s, StepRecord rec) {
        return std::any_of(es.begin(), es.end(), [&](const JournalEntry* e) { return e->step == s && e->rec == rec; });
    };
    const bool undo_began = std::any_of(es.begin(), es.end(), [](const JournalEntry* e) {
        return e->rec == StepRecord::Undone || e->rec == StepRecord::UndoFailed;
    });
    if (has(SetupStep::Commit, StepRecord::Started) && !undo_began) {
        if (has(SetupStep::CopyExe, StepRecord::Done) && !ops.commit_exe()) {
            r.failed_at = SetupStep::Commit;
            r.rollback_clean = false;
            return r;  // journal korunur; sonraki kurtarma yeniden dener
        }
        Tx t{ops, journal, id, max_seq, {}, {}, {}};
        t.r.done.push_back(SetupStep::Commit);
        if (t.append(SetupStep::Commit, StepRecord::Committed) || journal.clear()) {
            static_cast<void>(journal.clear());
            t.r.ok = true;
        } else {
            t.r.failed_at = SetupStep::Commit;
            t.r.rollback_clean = false;
        }
        return t.r;
    }
    const JournalEntry* probe_done = nullptr;
    for (const auto* e : es)
        if (e->step == SetupStep::Probe && e->rec == StepRecord::Done) probe_done = e;
    if (!probe_done) {  // Probe tamamlanmadan hicbir adim baslamaz
        static_cast<void>(journal.clear());
        r.ok = true;
        return r;
    }
    auto prior = decode_prior(probe_done->prior_json);
    if (!prior) {
        r.rollback_clean = false;
        return r;  // journal korunur
    }
    Tx t{ops, journal, id, max_seq, prior.value(), {}, {}};
    for (const auto* e : es) {
        if (e->rec != StepRecord::Started || e->step == SetupStep::Probe) continue;
        if (point_of_no_return(e->step)) {
            r.failed_at = e->step;
            r.rollback_clean = false;
            return r;  // geri alma yok; journal korunur
        }
        if (std::find(t.started.begin(), t.started.end(), e->step) != t.started.end()) continue;
        const bool undone = std::any_of(es.begin(), es.end(), [&](const JournalEntry* x) {
            return x->step == e->step && x->rec == StepRecord::Undone;
        });
        if (!undone) t.started.push_back(e->step);
    }
    rollback(t);
    t.r.ok = t.r.rollback_clean;
    if (t.r.rollback_clean) static_cast<void>(journal.clear());
    return t.r;
}

}  // namespace ef::core::setup
