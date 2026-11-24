#include "core/update/staged_update.h"

#include <algorithm>
#include <array>

#include "core/serialization/json.h"
#include "core/update/update_check.h"

namespace ef::core::update {

using contracts::ErrorCode;

namespace {

constexpr std::int64_t kInt64Max = 0x7FFFFFFFFFFFFFFFll;

struct PhaseName {
    StagePhase p;
    std::string_view n;
};
constexpr std::array<PhaseName, 5> kPhases{{{StagePhase::Idle, "idle"},
                                            {StagePhase::Receiving, "receiving"},
                                            {StagePhase::Swapped, "swapped"},
                                            {StagePhase::UiHealthy, "ui_healthy"},
                                            {StagePhase::Committed, "committed"}}};

struct StatusName {
    StatusState s;
    std::string_view n;
};
constexpr std::array<StatusName, 4> kStatuses{
    {{StatusState::Accepted, "accepted"}, {StatusState::Swapped, "swapped"}, {StatusState::Committed, "committed"}, {StatusState::Error, "error"}}};

// Surum yalniz "a.b.c" (rakam ve nokta): .part dosya adinda kullanilir.
bool safe_version(std::string_view v) {
    if (v.empty() || v.size() > 32 || compare_versions(v, "0.0.0") < 0) return false;
    for (char c : v)
        if (!((c >= '0' && c <= '9') || c == '.')) return false;
    return true;
}

bool hex64(std::string_view s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

json::Limits payload_limits(std::size_t max_string) {
    json::Limits l;
    l.max_bytes = ipc::kMaxPayload;
    l.max_depth = 2;
    l.max_string = max_string;
    l.max_items = 8;
    return l;
}

// Nesnenin anahtarlari tam olarak izin verilenlerin alt kumesi mi (bilinmeyen alan -> red).
bool only_keys(const json::Value& v, std::initializer_list<std::string_view> allowed) {
    if (!v.is(json::Value::Type::Object)) return false;
    for (const auto& k : v.keys()) {
        bool ok = false;
        for (auto a : allowed) ok = ok || k == a;
        if (!ok) return false;
    }
    return true;
}

int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

// ---- durum ----

std::string encode_state(const StagedUpdateState& s) {
    auto o = json::Value::object();
    std::string_view name = "idle";
    for (const auto& p : kPhases)
        if (p.p == s.phase) name = p.n;
    o.set("phase", json::Value::string(std::string(name)));
    o.set("version", json::Value::string(s.version));
    o.set("from_version", json::Value::string(s.from_version));
    o.set("highest_manifest_version", json::Value::integer(static_cast<std::int64_t>(std::min<std::uint64_t>(s.highest_manifest_version, kInt64Max))));
    o.set("swapped_at_ms", json::Value::integer(s.swapped_at_ms));
    return json::write(o);
}

contracts::Result<StagedUpdateState> decode_state(std::string_view text) {
    using R = contracts::Result<StagedUpdateState>;
    json::Limits l;
    l.max_bytes = 4096;
    l.max_depth = 2;
    l.max_string = 64;
    l.max_items = 8;
    auto doc = json::parse(text, l);
    if (!doc.ok()) return R::fail(ErrorCode::ParseError, "state json");
    const auto& o = doc.value();
    if (!only_keys(o, {"phase", "version", "from_version", "highest_manifest_version", "swapped_at_ms"}) || o.keys().size() != 5)
        return R::fail(ErrorCode::ParseError, "state fields");
    const auto ph = json::get_string(o, "phase", 16), v = json::get_string(o, "version", 32), fv = json::get_string(o, "from_version", 32);
    const auto hi = json::get_int(o, "highest_manifest_version", 0, kInt64Max), at = json::get_int(o, "swapped_at_ms", 0, kInt64Max);
    if (!ph.ok() || !v.ok() || !fv.ok() || !hi.ok() || !at.ok()) return R::fail(ErrorCode::ParseError, "state fields");
    StagedUpdateState s;
    bool known = false;
    for (const auto& p : kPhases)
        if (p.n == ph.value()) {
            s.phase = p.p;
            known = true;
        }
    if (!known) return R::fail(ErrorCode::InvalidEnum, "state phase");
    if ((s.phase == StagePhase::Swapped || s.phase == StagePhase::UiHealthy) && !safe_version(v.value()))
        return R::fail(ErrorCode::ParseError, "state version");
    s.version = v.value();
    s.from_version = fv.value();
    s.highest_manifest_version = static_cast<std::uint64_t>(hi.value());
    s.swapped_at_ms = at.value();
    return s;
}

std::string_view to_string(StageError e) noexcept {
    switch (e) {
        case StageError::None: return "none";
        case StageError::BadEnvelope: return "bad_envelope";
        case StageError::NotNewer: return "not_newer";
        case StageError::Replay: return "replay";
        case StageError::BadUrl: return "bad_url";
        case StageError::WrongTarget: return "wrong_target";
        case StageError::TooLarge: return "too_large";
        case StageError::OutOfOrder: return "out_of_order";
        case StageError::Overflow: return "overflow";
        case StageError::SizeMismatch: return "size_mismatch";
        case StageError::HashMismatch: return "hash_mismatch";
        case StageError::WrongPhase: return "wrong_phase";
        case StageError::WriteFailed: return "write_failed";
        case StageError::Halted: return "halted";
    }
    return "error";
}

// ---- alici ----

StageError UpdateReceiver::fail(StageError e) {
    if (receiving_ || used_) sink_.discard();
    receiving_ = false;
    committed_ = false;
    received_ = 0;
    return e;
}

void UpdateReceiver::abort() {
    if (receiving_) (void)fail(StageError::None);
}

StageError UpdateReceiver::begin(std::string_view envelope_json) {
    if (receiving_ || used_) return fail(StageError::WrongPhase);  // ayni anda tek aktarim; alici tek kullanimlik
    used_ = true;
    const auto payload = catalog::open_envelope(envelope_json, kReleaseDomain, keys_, verify_);
    if (!payload) return fail(StageError::BadEnvelope);
    catalog::ReleaseManifest m;
    using V = catalog::ReleaseVerdict;
    switch (catalog::evaluate_release(*payload, ctx_, &m)) {
        case V::Offer: break;
        case V::NotNewer: return fail(StageError::NotNewer);
        case V::Replay: return fail(StageError::Replay);
        case V::BadUrl: return fail(StageError::BadUrl);
        case V::WrongArch:
        case V::WrongChannel:
        case V::WrongPublisher:
        case V::OsTooOld: return fail(StageError::WrongTarget);
        case V::Malformed:
        case V::UnsignedBuild: return fail(StageError::BadEnvelope);
        // P9/B4 (B2 bulgusu): acil fren. Eksik dal kararin Offer gibi gecmesine yol aciyordu; zorunlu degisim reddedilir.
        case V::Halted: return fail(StageError::Halted);
    }
    if (!release_url_allowed(m.url)) return fail(StageError::BadUrl);
    if (!safe_version(m.version) || !hex64(m.sha256) || m.size == 0) return fail(StageError::BadEnvelope);
    if (compare_versions(m.version, ctx_.current_version) <= 0) return fail(StageError::NotNewer);  // ikinci savunma hatti
    if (m.size > kExeMaxBytes) return fail(StageError::TooLarge);
    if (!sink_.open_part(m.version)) return fail(StageError::WriteFailed);
    manifest_ = std::move(m);
    received_ = 0;
    receiving_ = true;
    return StageError::None;
}

StageError UpdateReceiver::chunk(std::uint64_t offset, std::string_view bytes) {
    if (!receiving_) return fail(StageError::WrongPhase);
    if (offset != received_) return fail(StageError::OutOfOrder);  // atlama, tekrar veya ortusme
    if (bytes.empty() || bytes.size() > kMaxChunkBytes || bytes.size() > manifest_.size - received_) return fail(StageError::Overflow);
    if (!sink_.write(bytes)) return fail(StageError::WriteFailed);
    received_ += bytes.size();
    return StageError::None;
}

StageError UpdateReceiver::commit() {
    if (!receiving_) return fail(StageError::WrongPhase);
    if (received_ != manifest_.size) return fail(StageError::SizeMismatch);
    if (!sink_.finish()) return fail(StageError::WriteFailed);
    // Diskten yeniden okunan baytlarin ozeti imzali manifestle (yazici yalan soyleyemez: geri okuma ayri yol).
    if (!download_matches(received_, sink_.sha256_hex_readback(), manifest_)) return fail(StageError::HashMismatch);
    receiving_ = false;
    committed_ = true;
    return StageError::None;
}

// ---- gozetmen ----

bool adopt_swapped_as_healthy(StagedUpdateState& s, std::string_view running_version) {
    if (s.phase != StagePhase::Swapped || running_version.empty() || s.version != running_version) return false;
    s.phase = StagePhase::UiHealthy;
    return true;
}

SupervisorAction on_service_start(const StagedUpdateState& s) {
    if (s.phase == StagePhase::Swapped) return SupervisorAction::RollBack;   // saglik kanitlanmadan yeniden basladi
    if (s.phase == StagePhase::UiHealthy) return SupervisorAction::Commit;   // yeni surum UI'si saglikliydi
    return SupervisorAction::None;
}

SupervisorAction on_tick(const StagedUpdateState& s, std::int64_t now_ms, std::int64_t wait_ms) {
    if (s.phase != StagePhase::Swapped) return SupervisorAction::None;
    const std::int64_t elapsed = now_ms - s.swapped_at_ms;
    // Saat geri alindiysa da sinirsiz beklenmez.
    if (elapsed > wait_ms || elapsed < -wait_ms) return SupervisorAction::RollBack;
    return SupervisorAction::None;
}

// ---- kodekler ----

std::string base64_encode(std::string_view in) {
    static constexpr char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const std::uint32_t v = (static_cast<std::uint8_t>(in[i]) << 16) | (static_cast<std::uint8_t>(in[i + 1]) << 8) | static_cast<std::uint8_t>(in[i + 2]);
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += t[v & 63];
    }
    if (const std::size_t rem = in.size() - i; rem > 0) {
        std::uint32_t v = static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[i])) << 16;
        if (rem == 2) v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[i + 1])) << 8;
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += rem == 2 ? t[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::string> base64_decode(std::string_view s) {
    if (s.size() % 4 != 0) return std::nullopt;
    std::string out;
    out.reserve(s.size() / 4 * 3);
    for (std::size_t i = 0; i < s.size(); i += 4) {
        const bool last = i + 4 == s.size();
        const int pad = last ? (s[i + 3] == '=' ? (s[i + 2] == '=' ? 2 : 1) : 0) : 0;
        int v[4]{};
        for (int k = 0; k < 4 - pad; ++k) {
            v[k] = b64_val(s[i + static_cast<std::size_t>(k)]);
            if (v[k] < 0) return std::nullopt;
        }
        if (!last && (s[i + 2] == '=' || s[i + 3] == '=')) return std::nullopt;
        if (pad == 2 && (v[1] & 0xF) != 0) return std::nullopt;  // kanonik olmayan dolgu
        if (pad == 1 && (v[2] & 0x3) != 0) return std::nullopt;
        const std::uint32_t w = (static_cast<std::uint32_t>(v[0]) << 18) | (static_cast<std::uint32_t>(v[1]) << 12) |
                                (static_cast<std::uint32_t>(v[2]) << 6) | static_cast<std::uint32_t>(v[3]);
        out += static_cast<char>((w >> 16) & 0xFF);
        if (pad < 2) out += static_cast<char>((w >> 8) & 0xFF);
        if (pad < 1) out += static_cast<char>(w & 0xFF);
    }
    return out;
}

ipc::Frame make_update_begin(std::string_view envelope_json, std::uint64_t seq) {
    auto o = json::Value::object();
    o.set("envelope", json::Value::string(std::string(envelope_json)));
    return ipc::Frame{ipc::MsgType::UpdateBegin, seq, json::write(o)};
}

ipc::Frame make_update_chunk(std::uint64_t offset, std::string_view bytes, std::uint64_t seq) {
    auto o = json::Value::object();
    o.set("offset", json::Value::integer(static_cast<std::int64_t>(offset)));
    o.set("data", json::Value::string(base64_encode(bytes)));
    return ipc::Frame{ipc::MsgType::UpdateChunk, seq, json::write(o)};
}

ipc::Frame make_update_commit(std::uint64_t seq) { return ipc::Frame{ipc::MsgType::UpdateCommit, seq, "{}"}; }

ipc::Frame make_update_healthy(std::string_view version, std::uint64_t seq) {
    auto o = json::Value::object();
    o.set("version", json::Value::string(std::string(version)));
    return ipc::Frame{ipc::MsgType::UpdateHealthy, seq, json::write(o)};
}

contracts::Result<std::string> decode_update_begin(std::string_view payload) {
    using R = contracts::Result<std::string>;
    auto doc = json::parse(payload, payload_limits(ipc::kMaxPayload));
    if (!doc.ok() || !only_keys(doc.value(), {"envelope"})) return R::fail(ErrorCode::ParseError, "begin");
    auto e = json::get_string(doc.value(), "envelope", ipc::kMaxPayload);
    if (!e.ok() || e.value().empty()) return R::fail(ErrorCode::ParseError, "begin");
    return e.value();
}

contracts::Result<ChunkMsg> decode_update_chunk(std::string_view payload) {
    using R = contracts::Result<ChunkMsg>;
    auto doc = json::parse(payload, payload_limits(ipc::kMaxPayload));
    if (!doc.ok() || !only_keys(doc.value(), {"offset", "data"}) || doc.value().keys().size() != 2) return R::fail(ErrorCode::ParseError, "chunk");
    auto off = json::get_int(doc.value(), "offset", 0, static_cast<std::int64_t>(kExeMaxBytes));
    auto data = json::get_string(doc.value(), "data", ipc::kMaxPayload);
    if (!off.ok() || !data.ok()) return R::fail(ErrorCode::ParseError, "chunk");
    auto raw = base64_decode(data.value());
    if (!raw) return R::fail(ErrorCode::ParseError, "chunk data");
    return ChunkMsg{static_cast<std::uint64_t>(off.value()), std::move(*raw)};
}

bool decode_update_commit(std::string_view payload) {
    auto doc = json::parse(payload, payload_limits(16));
    return doc.ok() && doc.value().is(json::Value::Type::Object) && doc.value().keys().empty();
}

contracts::Result<std::string> decode_update_healthy(std::string_view payload) {
    using R = contracts::Result<std::string>;
    auto doc = json::parse(payload, payload_limits(64));
    if (!doc.ok() || !only_keys(doc.value(), {"version"})) return R::fail(ErrorCode::ParseError, "healthy");
    auto v = json::get_string(doc.value(), "version", 32);
    if (!v.ok() || !safe_version(v.value())) return R::fail(ErrorCode::ParseError, "healthy");
    return v.value();
}

std::string encode_status(const UpdateStatusMsg& m) {
    auto o = json::Value::object();
    std::string_view name = "error";
    for (const auto& s : kStatuses)
        if (s.s == m.state) name = s.n;
    o.set("state", json::Value::string(std::string(name)));
    if (m.state == StatusState::Error) o.set("error", json::Value::string(m.error.substr(0, 64)));
    return json::write(o);
}

contracts::Result<UpdateStatusMsg> decode_status(std::string_view payload) {
    using R = contracts::Result<UpdateStatusMsg>;
    auto doc = json::parse(payload, payload_limits(64));
    if (!doc.ok() || !only_keys(doc.value(), {"state", "error"})) return R::fail(ErrorCode::ParseError, "status");
    auto st = json::get_string(doc.value(), "state", 16);
    if (!st.ok()) return R::fail(ErrorCode::ParseError, "status");
    UpdateStatusMsg m;
    bool known = false;
    for (const auto& s : kStatuses)
        if (s.n == st.value()) {
            m.state = s.s;
            known = true;
        }
    if (!known) return R::fail(ErrorCode::InvalidEnum, "status state");
    if (doc.value().find("error")) {
        auto e = json::get_string(doc.value(), "error", 64);
        if (!e.ok()) return R::fail(ErrorCode::ParseError, "status error");
        m.error = e.value();
    }
    return m;
}

// ---- koordinator ----

UpdateCoordinator::UpdateCoordinator(catalog::ReleaseContext base, catalog::KeyLookup keys, catalog::RawVerifier verify, StageSink& sink,
                                     UpdateHost& host, catalog::SwapFs fs, catalog::SwapPaths paths)
    : base_(std::move(base)), keys_(std::move(keys)), verify_(std::move(verify)), sink_(sink), host_(host), fs_(std::move(fs)),
      paths_(std::move(paths)) {
    StagedUpdateState s;
    if (host_.load_state(s)) state_ = s;
}

bool UpdateCoordinator::save(const StagedUpdateState& s) {
    StagedUpdateState n = s;
    if (n.highest_manifest_version < state_.highest_manifest_version) n.highest_manifest_version = state_.highest_manifest_version;  // azalmaz
    if (!host_.save_state(n)) return false;
    state_ = n;
    return true;
}

bool UpdateCoordinator::roll_back() {
    // Eski EXE geri; yeni dosya staged.failed olarak kenarda. Basarisizsa durum Swapped KALIR: sonraki tick / hizmet
    // acilisi yeniden dener (Idle'a dusup yeni EXE + yedekle sessizce kalmak geri alinamaz durum olurdu).
    if (!catalog::recover_swap(fs_, paths_, false)) return false;
    if (fs_.exists(paths_.staged)) (void)fs_.remove(paths_.staged);
    host_.remove_part();
    StagedUpdateState idle;
    idle.highest_manifest_version = state_.highest_manifest_version;
    (void)save(idle);
    return true;
}

bool UpdateCoordinator::commit_swap() {
    // Yedek silinemezse UiHealthy kalir; tick / sonraki hizmet acilisi yeniden dener (yeni surum calismaya devam eder).
    if (!catalog::recover_swap(fs_, paths_, true)) return false;
    auto c = state_;
    c.phase = StagePhase::Committed;
    (void)save(c);
    StagedUpdateState idle;
    idle.highest_manifest_version = state_.highest_manifest_version;
    (void)save(idle);
    return true;
}

SupervisorAction UpdateCoordinator::start() {
    StagedUpdateState s;
    if (host_.load_state(s)) state_ = s;
    const auto a = on_service_start(state_);
    if (a == SupervisorAction::RollBack) {
        // Basarisizsa da RollBack: cagiran durur, durum Swapped kalir ve sonraki acilis yeniden dener.
        (void)roll_back();
    } else if (a == SupervisorAction::Commit) {
        if (!commit_swap()) return SupervisorAction::None;
    } else if (state_.phase == StagePhase::Receiving || state_.phase == StagePhase::Committed) {
        // Yarim aktarim (hizmet aktarim sirasinda durdu) veya yarim kalan onay kaydi: parca atilir, Idle.
        host_.remove_part();
        if (fs_.exists(paths_.staged)) (void)fs_.remove(paths_.staged);
        StagedUpdateState idle;
        idle.highest_manifest_version = state_.highest_manifest_version;
        (void)save(idle);
    }
    return a;
}

SupervisorAction UpdateCoordinator::tick() {
    // UiHealthy iken onay (yedek silme) basarisiz kaldiysa tekrar denenir; aksi halde yeni begin'ler UiHealthy'de takili kalirdi.
    if (state_.phase == StagePhase::UiHealthy && !stop_requested_) return commit_swap() ? SupervisorAction::Commit : SupervisorAction::None;
    const auto a = on_tick(state_, host_.now_ms());
    if (a == SupervisorAction::RollBack && !roll_back()) return SupervisorAction::None;  // Swapped kalir; sonraki tick yeniden dener
    return a;
}

void UpdateCoordinator::on_disconnect() {
    if (rx_ && rx_->receiving()) {
        rx_->abort();
        if (state_.phase == StagePhase::Receiving) {
            auto s = state_;
            s.phase = StagePhase::Idle;
            (void)save(s);
        }
    }
    rx_.reset();
}

ipc::Frame UpdateCoordinator::status(StatusState st, std::string err) {
    return ipc::Frame{ipc::MsgType::UpdateStatus, 0, encode_status(UpdateStatusMsg{st, std::move(err)})};
}

ipc::Frame UpdateCoordinator::error(StageError e) {
    if (rx_) rx_->abort();
    rx_.reset();
    if (state_.phase == StagePhase::Receiving) {
        auto s = state_;
        s.phase = StagePhase::Idle;
        (void)save(s);
    }
    return status(StatusState::Error, std::string(to_string(e)));
}

ipc::Frame UpdateCoordinator::finish_commit() {
    const auto m = rx_->manifest();
    rx_.reset();
    auto fail_idle = [this](StageError e) {
        if (fs_.exists(paths_.staged)) (void)fs_.remove(paths_.staged);
        host_.remove_part();
        StagedUpdateState idle;
        idle.highest_manifest_version = state_.highest_manifest_version;
        (void)save(idle);
        return status(StatusState::Error, std::string(to_string(e)));
    };
    // 1) Dogrulanmis .part -> exe.new; kopya diskten yeniden okunup manifestle karsilastirilir.
    if (fs_.exists(paths_.staged) && !fs_.remove(paths_.staged)) return fail_idle(StageError::WriteFailed);
    if (!host_.stage_part(paths_.staged)) return fail_idle(StageError::WriteFailed);
    std::uint64_t size = 0;
    std::string sha;
    if (!host_.file_digest(paths_.staged, size, sha) || !download_matches(size, sha, m)) return fail_idle(StageError::HashMismatch);
    // 2) Geri donus durumu degisimden ONCE kalici.
    StagedUpdateState sw = state_;
    sw.phase = StagePhase::Swapped;
    sw.version = m.version;
    sw.from_version = base_.current_version;
    sw.swapped_at_ms = host_.now_ms();
    if (!save(sw)) return fail_idle(StageError::WriteFailed);
    // 3) Degisim (exe -> exe.old, exe.new -> exe). Basarisizsa eski EXE geri, Idle.
    if (catalog::run_swap(fs_, paths_) != catalog::SwapStep::Replaced) {
        if (!catalog::recover_swap(fs_, paths_, false)) {
            // Eski EXE geri konamadi: Idle'a DUSULMEZ. Durum Swapped kalir ve bekleme suresi dolmus sayilir: ilk tick geri
            // almayi yeniden dener.
            host_.remove_part();
            auto r = state_;
            r.swapped_at_ms = std::max<std::int64_t>(0, host_.now_ms() - kServiceHealthWaitMs - 1);  // durum kodegi negatif kabul etmez
            (void)save(r);
            return status(StatusState::Error, std::string(to_string(StageError::WriteFailed)));
        }
        return fail_idle(StageError::WriteFailed);
    }
    host_.remove_part();
    return status(StatusState::Swapped);
}

std::optional<ipc::Frame> UpdateCoordinator::on_frame(const ipc::Frame& in) {
    using T = ipc::MsgType;
    switch (in.type) {
        case T::UpdateBegin: {
            auto env = decode_update_begin(in.payload);
            if (!env.ok()) return error(StageError::BadEnvelope);
            if (rx_ && rx_->receiving()) return error(StageError::WrongPhase);  // ayni anda tek aktarim
            if (state_.phase == StagePhase::Swapped || state_.phase == StagePhase::UiHealthy) return error(StageError::WrongPhase);
            StagedUpdateState disk;
            if (host_.load_state(disk) && disk.highest_manifest_version > state_.highest_manifest_version)
                state_.highest_manifest_version = disk.highest_manifest_version;
            auto ctx = base_;
            ctx.last_manifest_version = replay_floor(state_.highest_manifest_version);
            rx_.emplace(std::move(ctx), keys_, verify_, sink_);
            if (const auto e = rx_->begin(env.value()); e != StageError::None) return error(e);
            UpdateDecision d;
            d.verified_manifest_version = rx_->manifest().manifest_version;
            StagedUpdateState r;
            r.phase = StagePhase::Receiving;
            r.version = rx_->manifest().version;
            r.from_version = base_.current_version;
            r.highest_manifest_version = next_highest_seen(state_.highest_manifest_version, d);
            if (!save(r)) return error(StageError::WriteFailed);
            return status(StatusState::Accepted);
        }
        case T::UpdateChunk: {
            if (!rx_ || !rx_->receiving()) return error(StageError::WrongPhase);
            auto c = decode_update_chunk(in.payload);
            if (!c.ok()) return error(StageError::Overflow);
            if (const auto e = rx_->chunk(c.value().offset, c.value().data); e != StageError::None) return error(e);
            return status(StatusState::Accepted);
        }
        case T::UpdateCommit: {
            if (!decode_update_commit(in.payload)) return error(StageError::WrongPhase);
            if (!rx_ || !rx_->receiving()) return error(StageError::WrongPhase);
            if (const auto e = rx_->commit(); e != StageError::None) return error(e);
            return finish_commit();
        }
        case T::UpdateHealthy: {
            auto v = decode_update_healthy(in.payload);
            if (!v.ok() || state_.phase != StagePhase::Swapped || v.value() != state_.version) return status(StatusState::Error, "not_swapped");
            auto h = state_;
            h.phase = StagePhase::UiHealthy;
            if (!save(h)) return status(StatusState::Error, std::string(to_string(StageError::WriteFailed)));
            stop_requested_ = true;
            return status(StatusState::Committed);
        }
        default: return std::nullopt;
    }
}

}  // namespace ef::core::update
