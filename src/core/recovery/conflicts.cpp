#include "core/recovery/conflicts.h"

#include <sstream>

namespace ef::core::recovery {

using contracts::ErrorCode;
using contracts::TxState;

std::vector<Conflict> find_conflicts(const JournalTx& tx, const AdapterRegistry& adapters) {
    std::vector<Conflict> out;
    for (std::size_t i = 0; i < tx.steps.size(); ++i) {
        const auto& s = tx.steps[i];
        if (s.state != StepState::Failed || s.error != kExternalChange) continue;
        Conflict c{i, s.op.target, s.snapshot, s.last_written.empty() ? s.op.new_value : s.last_written, std::nullopt};
        if (auto* a = adapters.find(s.op.id)) {
            auto cur = a->read_for_restore(s.op, s.snapshot);
            if (cur.ok()) c.current = cur.value();
        }
        out.push_back(std::move(c));
    }
    return out;
}

namespace {
contracts::Status advance(JournalTx& tx, TxState s, JournalStore& store) {
    if (tx.state == s) return contracts::ok_status();
    if (!contracts::can_transition(tx.state, s)) return contracts::Status::fail(ErrorCode::InvalidTransition, "tx state");
    tx.state = s;
    return store.persist(tx);
}
// Tum adimlar cozulduyse (geri alindi / kullanici degeri korundu / hic yazilmadi) islemi kapat.
contracts::Status close_if_resolved(JournalTx& tx, JournalStore& store) {
    for (const auto& x : tx.steps)
        if (x.state != StepState::RolledBack && x.state != StepState::KeptExternal && x.state != StepState::Planned)
            return contracts::ok_status();
    if (tx.state == TxState::RecoveryRequired)
        if (auto st = advance(tx, TxState::RollingBack, store); !st.ok()) return st;
    return advance(tx, TxState::Restored, store);
}
}  // namespace

contracts::Result<JournalTx> resolve_conflict(JournalTx tx, std::size_t idx, ConflictDecision d, const AdapterRegistry& adapters,
                                              JournalStore& store) {
    using R = contracts::Result<JournalTx>;
    if (idx >= tx.steps.size()) return R::fail(ErrorCode::OutOfRange, "step");
    auto& s = tx.steps[idx];
    if (s.state != StepState::Failed || s.error != kExternalChange) return R::fail(ErrorCode::InvalidTransition, "no conflict on step");
    auto* a = adapters.find(s.op.id);
    if (!a) return R::fail(ErrorCode::Unsupported, "adapter");
    if (d == ConflictDecision::KeepCurrent) {
        s.state = StepState::KeptExternal;
        s.error = "user kept the externally changed value";
    } else {
        auto r = a->restore(s.op, s.snapshot);
        auto now = a->read_for_restore(s.op, s.snapshot);
        const bool ok = r.ok() && now.ok() && a->matches_snapshot(s.op, s.snapshot, now.value());
        if (!ok) {
            s.error = "forced restore failed";
            s.observed = now.ok() ? observed_text(now.value()) : std::string();
            s.error_code = !r.ok() ? std::string(contracts::to_string(r.error().code))
                                   : !now.ok() ? std::string(contracts::to_string(now.error().code)) : std::string();
            (void)store.persist(tx);
            return tx;
        }
        s.state = StepState::RolledBack;
        s.error.clear();
        s.observed.clear();
        s.error_code.clear();
    }
    if (auto p = store.persist(tx); !p.ok()) return R(p.error());
    if (auto st = close_if_resolved(tx, store); !st.ok()) return R(st.error());
    return tx;
}

bool has_external_conflict(const JournalTx& tx) noexcept {
    if (tx.state != TxState::RecoveryRequired) return false;
    for (const auto& s : tx.steps)
        if (s.state == StepState::Failed && s.error == kExternalChange) return true;
    return false;
}

contracts::Result<JournalTx> keep_external_values(JournalTx tx, JournalStore& store,
                                                  const std::function<bool(const contracts::SettingOperation&)>& match) {
    using R = contracts::Result<JournalTx>;
    if (tx.state != TxState::RecoveryRequired) return R::fail(ErrorCode::InvalidTransition, "tx not recovery_required");
    std::size_t kept = 0;
    for (auto& s : tx.steps) {
        if (s.state != StepState::Failed || s.error != kExternalChange) continue;
        if (match && !match(s.op)) continue;
        s.state = StepState::KeptExternal;  // ayar yazilmaz: kullanicinin mevcut degeri kalir
        s.error = "user kept the externally changed value";
        ++kept;
    }
    if (kept == 0) return R::fail(ErrorCode::InvalidTransition, "no external conflict");
    if (auto p = store.persist(tx); !p.ok()) return R(p.error());
    if (auto st = close_if_resolved(tx, store); !st.ok()) return R(st.error());
    return tx;
}

RestoreOutcome restore_outcome(const JournalTx& tx) noexcept {
    if (tx.state != TxState::Restored) return RestoreOutcome::NotRestored;
    for (const auto& s : tx.steps)
        if (s.state == StepState::KeptExternal) return RestoreOutcome::PartialKeptUserChanges;
    return RestoreOutcome::Full;
}

// ---- Alan birlestirme ----
namespace {
struct Line {
    std::string raw;
    std::string key;    // bos: alan satiri degil
    std::string value;  // tirnaksiz
    char sep = ' ';     // ' ' veya '='
    bool quoted = false;
};

Line parse_line(const std::string& raw) {
    Line l{raw, {}, {}, ' ', false};
    std::size_t i = 0;
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;
    if (i >= raw.size() || raw[i] == '/' || raw[i] == '#' || raw[i] == ';' || raw[i] == '[') return l;
    std::size_t j = i;
    while (j < raw.size() && raw[j] != ' ' && raw[j] != '\t' && raw[j] != '=') ++j;
    l.key = raw.substr(i, j - i);
    std::size_t k = j;
    while (k < raw.size() && (raw[k] == ' ' || raw[k] == '\t')) ++k;
    if (k < raw.size() && raw[k] == '=') {
        l.sep = '=';
        ++k;
        while (k < raw.size() && (raw[k] == ' ' || raw[k] == '\t')) ++k;
    }
    std::string v = raw.substr(k);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        l.quoted = true;
        v = v.substr(1, v.size() - 2);
    }
    l.value = v;
    return l;
}

std::vector<std::string> split_lines(const std::string& t) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : t) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty() || (!t.empty() && t.back() != '\n')) out.push_back(cur);
    return out;
}

std::string render(const Line& l, const std::string& value) {
    const std::string v = l.quoted ? "\"" + value + "\"" : value;
    const bool cr = !l.raw.empty() && l.raw.back() == '\r';
    return l.key + (l.sep == '=' ? "=" : " ") + v + (cr ? "\r" : "");
}

std::string join(const std::vector<std::string>& lines, bool trailing_nl) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || trailing_nl) out += '\n';
    }
    return out;
}
}  // namespace

std::optional<std::string> read_field(const std::string& text, const std::string& key) {
    std::optional<std::string> v;
    for (const auto& raw : split_lines(text)) {
        const auto l = parse_line(raw);
        if (l.key == key) v = l.value;  // son tanim gecerli
    }
    return v;
}

std::string write_field(const std::string& text, const std::string& key, const std::optional<std::string>& value) {
    auto lines = split_lines(text);
    const bool trailing = !text.empty() && text.back() == '\n';
    bool found = false;
    std::vector<std::string> out;
    for (const auto& raw : lines) {
        const auto l = parse_line(raw);
        if (l.key == key) {
            found = true;
            if (value) out.push_back(render(l, *value));
            continue;  // value yoksa satir silinir
        }
        out.push_back(raw);
    }
    if (!found && value) out.push_back(key + " " + (value->find(' ') != std::string::npos ? "\"" + *value + "\"" : *value));
    return join(out, trailing || (!found && value));
}

FieldMergeResult restore_fields(const std::string& current_text, const std::vector<FieldChange>& changes) {
    FieldMergeResult r{current_text, {}, 0};
    for (const auto& c : changes) {
        const auto cur = read_field(r.text, c.key);
        if (cur == c.original) { ++r.restored; continue; }  // zaten orijinalde
        if (!cur || *cur != c.written) {                    // kullanici bizim alanimizi degistirmis
            r.conflicts.push_back(c.key);
            continue;
        }
        r.text = write_field(r.text, c.key, c.original);
        ++r.restored;
    }
    return r;
}

}  // namespace ef::core::recovery
