#include "core/recovery/journal.h"

#include <algorithm>

#include "core/recovery/sha256.h"
#include "core/roles.h"
#include "core/serialization/operation_codec.h"

namespace ef::core::recovery {

using contracts::ErrorCode;
using contracts::Status;

std::string_view to_string(StepState s) noexcept {
    switch (s) {
        case StepState::Planned: return "planned";
        case StepState::Intent: return "intent";
        case StepState::Applied: return "applied";
        case StepState::Verified: return "verified";
        case StepState::RolledBack: return "rolled_back";
        case StepState::Failed: return "failed";
        case StepState::KeptExternal: return "kept_external";
    }
    return "?";
}

std::optional<StepState> parse_step_state(std::string_view s) noexcept {
    for (auto v : {StepState::Planned, StepState::Intent, StepState::Applied, StepState::Verified, StepState::RolledBack, StepState::Failed, StepState::KeptExternal})
        if (to_string(v) == s) return v;
    return std::nullopt;
}

std::string clamp_utf8(std::string_view s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string(s);
    std::size_t n = max_bytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;  // devam baytinda kesme (gecerli UTF-8 kalir)
    return std::string(s.substr(0, n));
}

json::Value to_json(const JournalTx& tx) {
    auto o = json::Value::object();
    o.set("id", json::Value::string(tx.id));
    o.set("schema_version", json::Value::integer(tx.schema_version));
    o.set("machine_id", json::Value::string(tx.machine_id));
    o.set("actor_sid", json::Value::string(tx.actor_sid));
    o.set("target_sid", json::Value::string(tx.target_sid));
    o.set("created_unix_ms", json::Value::integer(tx.created_unix_ms));
    o.set("state", json::Value::string(std::string(contracts::to_string(tx.state))));
    o.set("revision", json::Value::integer(static_cast<std::int64_t>(tx.revision)));
    auto steps = json::Value::array();
    for (const auto& s : tx.steps) {
        auto j = json::Value::object();
        j.set("op", json::encode_operation(s.op));
        auto snap = json::Value::object();
        snap.set("existed", json::Value::boolean(s.snapshot.existed));
        snap.set("old_value", json::Value::string(s.snapshot.old_value));
        snap.set("raw_type", json::Value::string(s.snapshot.raw_type));
        snap.set("inherited", json::Value::boolean(s.snapshot.inherited));
        snap.set("source_version", json::Value::string(s.snapshot.source_version));
        if (!s.snapshot.raw.empty()) snap.set("raw", json::Value::string(s.snapshot.raw));
        j.set("snapshot", std::move(snap));
        j.set("state", json::Value::string(std::string(to_string(s.state))));
        j.set("last_written", json::Value::string(s.last_written));
        // Saha 2026-11-15: okuma siniri 512 bayt; daha uzun hata metni dosyayi "bozuk" yapar ve kayit hic geri alinamazdi.
        j.set("error", json::Value::string(clamp_utf8(s.error, kStepErrorMax)));
        if (!s.observed.empty()) j.set("observed", json::Value::string(clamp_utf8(s.observed, kStepObservedMax)));
        if (!s.error_code.empty()) j.set("error_code", json::Value::string(clamp_utf8(s.error_code, 32)));
        steps.push(std::move(j));
    }
    o.set("steps", std::move(steps));
    return o;
}

contracts::Result<JournalTx> from_json(const json::Value& v) {
    using R = contracts::Result<JournalTx>;
    JournalTx tx;
    auto id = json::get_string(v, "id", 32);
    auto sv = json::get_int(v, "schema_version", 1, 1000);
    auto mid = json::get_string(v, "machine_id", 64);
    auto act = json::get_string(v, "actor_sid", 184);
    auto tgt = json::get_string(v, "target_sid", 184);
    auto cr = json::get_int(v, "created_unix_ms", 0, INT64_MAX);
    auto st = json::get_string(v, "state", 32);
    auto rev = json::get_int(v, "revision", 0, INT64_MAX);
    if (!id.ok() || !sv.ok() || !mid.ok() || !act.ok() || !tgt.ok() || !cr.ok() || !st.ok() || !rev.ok())
        return R::fail(ErrorCode::ParseError, "journal fields");
    if (!is_valid_nonce(id.value())) return R::fail(ErrorCode::ParseError, "journal id");
    if (sv.value() > kJournalSchema) return R::fail(ErrorCode::SchemaTooNew, "journal schema");
    auto state = contracts::parse_tx_state(st.value());
    if (!state) return R::fail(ErrorCode::InvalidEnum, "tx state");
    tx.id = id.value();
    tx.schema_version = static_cast<int>(sv.value());
    tx.machine_id = mid.value();
    tx.actor_sid = act.value();
    tx.target_sid = tgt.value();
    tx.created_unix_ms = cr.value();
    tx.state = *state;
    tx.revision = static_cast<std::uint64_t>(rev.value());
    const auto* steps = v.find("steps");
    if (!steps || !steps->is(json::Value::Type::Array) || steps->items().size() > 256) return R::fail(ErrorCode::ParseError, "steps");
    for (const auto& j : steps->items()) {
        JournalStep s;
        const auto* opv = j.find("op");
        const auto* snap = j.find("snapshot");
        if (!opv || !snap) return R::fail(ErrorCode::ParseError, "step");
        auto op = json::decode_operation(*opv);
        if (!op.ok()) return R(op.error());
        s.op = op.value();
        auto ex = json::get_bool(*snap, "existed");
        auto ov = json::get_string(*snap, "old_value", 4096);
        auto rt = json::get_string(*snap, "raw_type", 32);
        auto inh = json::get_bool(*snap, "inherited");
        auto srcv = json::get_string(*snap, "source_version", 128);
        auto ss = json::get_string(j, "state", 16);
        auto lw = json::get_string(j, "last_written", 4096);
        // Saha 2026-11-15: tani metinleri (error, observed) okumada KESILIR, kayit reddedilmez. 0.6.0 512 bayttan uzun hata
        // metnini sinirsiz yaziyordu; boyle bir kayit "bozuk" sayilip hic geri alinamiyor, kalici kurtarma ekrani kaliyordu.
        // Anlik goruntu ve deger alanlari kati kalir.
        auto er = json::get_string(j, "error", 4096);
        if (!ex.ok() || !ov.ok() || !rt.ok() || !inh.ok() || !srcv.ok() || !ss.ok() || !lw.ok() || !er.ok())
            return R::fail(ErrorCode::ParseError, "step fields");
        auto stt = parse_step_state(ss.value());
        if (!stt) return R::fail(ErrorCode::InvalidEnum, "step state");
        s.snapshot = Snapshot{ex.value(), ov.value(), rt.value(), inh.value(), srcv.value(), {}};
        if (snap->find("raw")) {  // istege bagli (sonradan eklendi; eski kayitlarda yok)
            auto raw = json::get_string(*snap, "raw", 4096);
            if (!raw.ok()) return R::fail(ErrorCode::ParseError, "snapshot raw");
            s.snapshot.raw = raw.value();
        }
        s.state = *stt;
        s.last_written = lw.value();
        s.error = clamp_utf8(er.value(), kStepErrorMax);
        if (j.find("observed")) {  // istege bagli (saha 2026-11-15; eski kayitlarda yok)
            auto obs = json::get_string(j, "observed", 4096);
            if (!obs.ok()) return R::fail(ErrorCode::ParseError, "step observed");
            s.observed = clamp_utf8(obs.value(), kStepObservedMax);
        }
        if (j.find("error_code")) {
            auto ec = json::get_string(j, "error_code", 32);
            if (!ec.ok()) return R::fail(ErrorCode::ParseError, "step error_code");
            s.error_code = ec.value();
        }
        tx.steps.push_back(std::move(s));
    }
    return tx;
}

std::string serialize(const JournalTx& tx) {
    const std::string body = json::write(to_json(tx));
    // Ozet govdenin kendisi uzerinden: dosyada govde aynen tekrar yazilir.
    return std::string(R"({"schema_version":)") + std::to_string(kJournalSchema) + R"(,"sha256":")" + sha256_hex(body) +
           R"(","tx":)" + body + "}";
}

contracts::Result<JournalTx> deserialize(std::string_view bytes) {
    using R = contracts::Result<JournalTx>;
    json::Limits lim;
    lim.max_bytes = 256 * 1024;
    lim.max_string = 4096;
    lim.max_items = 20000;
    lim.max_depth = 12;
    auto doc = json::parse(bytes, lim);
    if (!doc.ok()) return R(doc.error());
    if (auto s = json::check_schema(doc.value(), kJournalSchema); !s.ok()) return R(s.error());
    auto sum = json::get_string(doc.value(), "sha256", 64);
    const auto* txv = doc.value().find("tx");
    if (!sum.ok() || !txv) return R::fail(ErrorCode::ParseError, "envelope");
    if (sha256_hex(json::write(*txv)) != sum.value()) return R::fail(ErrorCode::ParseError, "integrity check failed");
    return from_json(*txv);
}

Status JournalStore::begin(JournalTx& tx) {
    if (!is_valid_nonce(tx.id)) return Status::fail(ErrorCode::ParseError, "tx id");
    if (tx.state != contracts::TxState::Prepared) return Status::fail(ErrorCode::InvalidTransition, "begin requires prepared");
    // CORE-03: bilinmeyen yeni sema varken yeni degisiklik BASLAMAZ (docs/04). Broker disi yol da (UI kullanici
    // journal'i, startup/fixes kapsamlari) ayni sozlesmeyi uygular: eski EXE yeni kaydi tahmin ederek yazmaz.
    const auto current = scan();
    if (!current.newer.empty())
        return Status::fail(ErrorCode::SchemaTooNew, "journal written by a newer version; new changes are blocked");
    // CORE-05: onceki cokmeden kalan *.json.tmp artiklari commit edilmemis gecici veridir; yeni yazimdan once atilir.
    (void)discard_stale_temp();
    if (auto q = enforce_quota(); !q.ok()) return q;
    auto existing = fs_.list();
    if (!existing.ok()) return Status(existing.error());
    if (existing.value().size() >= lim_.max_transactions) return Status::fail(ErrorCode::ParseLimit, "journal quota full");
    for (const auto& n : existing.value())
        if (n == file_name(tx.id)) return Status::fail(ErrorCode::ParseError, "duplicate tx id");
    tx.machine_id = machine_id_;
    tx.revision = 0;
    return persist(tx);
}

Status JournalStore::persist(JournalTx& tx) {
    if (tx.machine_id != machine_id_) return Status::fail(ErrorCode::Denied, "journal belongs to another machine");
    ++tx.revision;
    const std::string bytes = serialize(tx);
    if (bytes.size() > lim_.max_file_bytes) {
        --tx.revision;
        return Status::fail(ErrorCode::ParseLimit, "journal record too large");
    }
    // Saha 2026-11-15: diske yalniz geri okunabilen kayit yazilir. Okuma sinirini asan alan (ornek cok uzun anlik goruntu)
    // dosyayi "bozuk" yapar; bozuk kayit kurtarma gerekli sayilir ama hic geri alinamaz (kalici "Geri donus tamamlanamadi").
    // Ilk yazimda (begin) reddedilirse hicbir ayar uygulanmaz.
    if (const auto back = deserialize(bytes); !back.ok()) {
        --tx.revision;
        return Status::fail(ErrorCode::ParseLimit, "journal record would not read back: " + back.error().detail);
    }
    auto s = fs_.write_atomic(file_name(tx.id), bytes);
    if (!s.ok()) --tx.revision;
    return s;
}

contracts::Result<JournalTx> JournalStore::load(const std::string& id) {
    auto b = fs_.read(file_name(id));
    if (!b.ok()) return contracts::Result<JournalTx>(b.error());
    auto tx = deserialize(b.value());
    if (tx.ok() && tx.value().machine_id != machine_id_)
        return contracts::Result<JournalTx>::fail(ErrorCode::Denied, "foreign machine journal");
    return tx;
}

JournalStore::Listing JournalStore::scan() {
    Listing l;
    l.stale_temp = fs_.temp_files();  // CORE-05: rapor (commit edilmemis gecici dosyalar)
    auto names = fs_.list();
    if (!names.ok()) return l;
    for (const auto& n : names.value()) {
        if (n.rfind("tx-", 0) != 0) continue;
        auto b = fs_.read(n);
        if (!b.ok()) { l.corrupt.push_back(n); continue; }
        auto tx = deserialize(b.value());
        if (!tx.ok()) {
            (tx.code() == ErrorCode::SchemaTooNew ? l.newer : l.corrupt).push_back(n);
            continue;
        }
        if (tx.value().machine_id != machine_id_) { l.foreign.push_back(n); continue; }
        l.transactions.push_back(std::move(tx.value()));
    }
    std::sort(l.transactions.begin(), l.transactions.end(),
              [](const JournalTx& a, const JournalTx& b) { return a.created_unix_ms < b.created_unix_ms; });
    return l;
}

std::size_t JournalStore::discard_stale_temp() {
    std::size_t removed = 0;
    for (const auto& n : fs_.temp_files())
        if (fs_.remove(n).ok()) ++removed;
    return removed;
}

Status JournalStore::enforce_quota() {
    auto names = fs_.list();
    if (!names.ok()) return Status(names.error());
    if (names.value().size() < lim_.max_transactions) return contracts::ok_status();
    auto l = scan();
    std::size_t count = names.value().size();
    for (const auto& tx : l.transactions) {  // en eskiden
        if (count < lim_.max_transactions) break;
        if (tx.state != contracts::TxState::Restored) continue;  // aktif/kurtarma bekleyen silinmez
        if (fs_.remove(file_name(tx.id)).ok()) --count;
    }
    return contracts::ok_status();
}

// ---- MemoryJournalFs ----
Status MemoryJournalFs::write_atomic(const std::string& name, const std::string& bytes) {
    ++writes;
    if (fail_write_after >= 0 && writes > fail_write_after) return Status::fail(ErrorCode::IoError, "injected write failure");
    if (fail_only_write >= 0 && writes == fail_only_write) return Status::fail(ErrorCode::IoError, "injected single write failure");
    for (auto& f : files)
        if (f.first == name) { f.second = bytes; return contracts::ok_status(); }
    files.emplace_back(name, bytes);
    return contracts::ok_status();
}
contracts::Result<std::string> MemoryJournalFs::read(const std::string& name) {
    for (auto& f : files)
        if (f.first == name) return f.second;
    return contracts::Result<std::string>::fail(ErrorCode::MissingValue, name);
}
contracts::Result<std::vector<std::string>> MemoryJournalFs::list() {
    // WinJournalFs ile ayni gorunum: yalniz *.json kayitlari (gecici *.json.tmp temp_files() ile raporlanir).
    std::vector<std::string> n;
    for (auto& f : files)
        if (f.first.size() > 5 && f.first.compare(f.first.size() - 5, 5, ".json") == 0) n.push_back(f.first);
    return n;
}
Status MemoryJournalFs::remove(const std::string& name) {
    files.erase(std::remove_if(files.begin(), files.end(), [&](const auto& f) { return f.first == name; }), files.end());
    return contracts::ok_status();
}
std::vector<std::string> MemoryJournalFs::temp_files() {
    std::vector<std::string> out;
    for (const auto& f : files)
        if (f.first.size() > 9 && f.first.compare(f.first.size() - 9, 9, ".json.tmp") == 0) out.push_back(f.first);
    return out;
}

}  // namespace ef::core::recovery
