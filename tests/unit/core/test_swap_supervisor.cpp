// ADR-033 / spec 7: hizmet tarafi degisim gozetmeni ve koordinatoru (saf). Sahte dosya sistemi, sahte saat, sahte
// durum deposu; gercek hizmet, ProgramData veya Program Files yok.
#include <map>
#include <set>
#include <string>

#include "core/recovery/sha256.h"
#include "core/update/staged_update.h"
#include "core/update/update_check.h"
#include "ef_test.h"

using namespace ef::core;
using namespace ef::core::update;

namespace {

std::string b64url(std::string_view in) {
    static constexpr char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : in) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(t[(acc >> bits) & 63]);
        }
    }
    if (bits > 0) out.push_back(t[(acc << (6 - bits)) & 63]);
    return out;
}

const std::string kSig(64, '\x01');
const catalog::RawVerifier fake_verify = [](std::string_view blob, std::string_view, std::string_view sig) { return blob == "K" && sig == kSig; };
const catalog::KeyLookup keys = [](std::string_view d, std::string_view k) { return d == "release" && k == "rel1" ? std::string("K") : std::string(); };

const std::string kOld = "OLD-EXE", kNew = std::string("NEW-EXE-") + std::string(70000, 'n');

std::string envelope(const std::string& ver = "0.3.1", int mv = 10, const std::string& sha = recovery::sha256_hex(kNew)) {
    const std::string m = R"({"version":")" + ver +
                          R"(","arch":"x64","url":"https://github.com/ykp61/Ecu-Fps-releases/releases/download/v0.3.1/EcuFps.exe","sha256":")" +
                          sha + R"(","channel":"alpha","publisher":"Ecu Fps","min_os_build":19041,"size":)" + std::to_string(kNew.size()) +
                          R"(,"manifest_version":)" + std::to_string(mv) + "}";
    return R"({"domain":"release","key_id":"rel1","payload":")" + b64url(m) + R"(","sig":")" + b64url(kSig) + R"("})";
}

catalog::ReleaseContext base() {
    catalog::ReleaseContext c;
    c.current_version = "0.3.0";
    c.publisher = "Ecu Fps";
    c.os_build = 26100;
    c.trusted_release_key = true;
    return c;
}

// Ayni birimde atomik yeniden adlandirma; "kilitli" yol tasinamaz/silinemez (calisan EXE silinemez benzetimi).
struct FakeSwapFs {
    std::map<std::string, std::string> files;
    std::string locked;
    std::set<std::string> fail_to;  // bu hedeflere tasima basarisiz (ornegin disk hatasi)
    catalog::SwapFs fs() {
        catalog::SwapFs f;
        f.move = [this](const std::string& from, const std::string& to) {
            auto it = files.find(from);
            if (it == files.end() || from == locked || fail_to.count(to)) return false;
            files[to] = it->second;
            files.erase(from);
            return true;
        };
        f.exists = [this](const std::string& p) { return files.count(p) > 0; };
        f.remove = [this](const std::string& p) {
            if (p == locked && files.count(p)) return false;
            files.erase(p);
            return true;
        };
        return f;
    }
};

const catalog::SwapPaths kPaths = update_swap_paths("C:/PF/Ecu Fps/EcuFps.exe");
const std::string kPart = "C:/PD/updates/0.3.1.part";

struct FakeSink final : StageSink {
    FakeSwapFs& disk;
    explicit FakeSink(FakeSwapFs& d) : disk(d) {}
    bool open_part(std::string_view) override {
        disk.files[kPart].clear();
        return true;
    }
    bool write(std::string_view b) override {
        disk.files[kPart] += b;
        return true;
    }
    bool finish() override { return disk.files.count(kPart) > 0; }
    std::string sha256_hex_readback() override { return recovery::sha256_hex(disk.files[kPart]); }
    void discard() override { disk.files.erase(kPart); }
};

struct FakeHost final : UpdateHost {
    FakeSwapFs& disk;
    std::optional<std::string> state_json;
    std::int64_t now = 1000;
    int saves = 0;
    bool fail_save = false, corrupt_copy = false;
    std::vector<StagePhase> history;
    explicit FakeHost(FakeSwapFs& d) : disk(d) {}
    bool load_state(StagedUpdateState& out) override {
        if (!state_json) return false;
        auto d = decode_state(*state_json);
        if (!d.ok()) return false;
        out = d.value();
        return true;
    }
    bool save_state(const StagedUpdateState& s) override {
        if (fail_save) return false;
        ++saves;
        state_json = encode_state(s);
        history.push_back(s.phase);
        return true;
    }
    bool stage_part(const std::string& staged) override {
        auto it = disk.files.find(kPart);
        if (it == disk.files.end()) return false;
        disk.files[staged] = corrupt_copy ? it->second + "?" : it->second;
        return true;
    }
    bool file_digest(const std::string& p, std::uint64_t& size, std::string& sha) override {
        auto it = disk.files.find(p);
        if (it == disk.files.end()) return false;
        size = it->second.size();
        sha = recovery::sha256_hex(it->second);
        return true;
    }
    void remove_part() override { disk.files.erase(kPart); }
    std::int64_t now_ms() override { return now; }
    StagePhase phase() {
        StagedUpdateState s;
        return load_state(s) ? s.phase : StagePhase::Idle;
    }
};

StatusState status_of(const std::optional<ipc::Frame>& f) {
    if (!f || f->type != ipc::MsgType::UpdateStatus) return StatusState::Error;
    auto d = decode_status(f->payload);
    return d.ok() ? d.value().state : StatusState::Error;
}

// Butun aktarim: begin, parcalar, commit. Son yanitin durumu.
StatusState transfer(UpdateCoordinator& c, const std::string& env = envelope(), const std::string& bytes = kNew) {
    std::uint64_t seq = 1;
    if (status_of(c.on_frame(make_update_begin(env, ++seq))) != StatusState::Accepted) return StatusState::Error;
    for (std::size_t off = 0; off < bytes.size(); off += kClientChunkBytes)
        if (status_of(c.on_frame(make_update_chunk(off, std::string_view(bytes).substr(off, kClientChunkBytes), ++seq))) != StatusState::Accepted)
            return StatusState::Error;
    return status_of(c.on_frame(make_update_commit(++seq)));
}

struct Rig {
    FakeSwapFs disk;
    FakeSink sink{disk};
    FakeHost host{disk};
    Rig() { disk.files[kPaths.exe] = kOld; }
    UpdateCoordinator make() { return UpdateCoordinator(base(), keys, fake_verify, sink, host, disk.fs(), kPaths); }
};

}  // namespace

EF_TEST(swap_supervisor_tables) {
    StagedUpdateState s;
    for (auto [p, want] : {std::pair{StagePhase::Idle, SupervisorAction::None}, std::pair{StagePhase::Receiving, SupervisorAction::None},
                           std::pair{StagePhase::Swapped, SupervisorAction::RollBack}, std::pair{StagePhase::UiHealthy, SupervisorAction::Commit},
                           std::pair{StagePhase::Committed, SupervisorAction::None}}) {
        s.phase = p;
        EF_CHECK(on_service_start(s) == want);
    }
    s.phase = StagePhase::Swapped;
    s.swapped_at_ms = 10000;
    EF_CHECK(on_tick(s, 10000, 60000) == SupervisorAction::None);
    EF_CHECK(on_tick(s, 70000, 60000) == SupervisorAction::None);  // tam sinir
    EF_CHECK(on_tick(s, 70001, 60000) == SupervisorAction::RollBack);
    EF_CHECK(on_tick(s, 10000 - 60001, 60000) == SupervisorAction::RollBack);  // saat geri alindi
    for (auto p : {StagePhase::Idle, StagePhase::Receiving, StagePhase::UiHealthy, StagePhase::Committed}) {
        s.phase = p;
        EF_CHECK(on_tick(s, 1'000'000, 60000) == SupervisorAction::None);
    }
}

// Degisim -> UiHealthy -> hizmet yeniden baslar -> Commit: EXE yeni, yedek yok, durum Idle, taban korunur.
EF_TEST(swap_supervisor_swap_healthy_restart_commit) {
    Rig r;
    {
        auto c = r.make();
        EF_CHECK(c.start() == SupervisorAction::None);
        EF_CHECK(transfer(c) == StatusState::Swapped);
        EF_CHECK(r.disk.files[kPaths.exe] == kNew && r.disk.files[kPaths.backup] == kOld && !r.disk.files.count(kPaths.staged));
        EF_CHECK(!r.disk.files.count(kPart));
        EF_CHECK(r.host.phase() == StagePhase::Swapped && c.state().version == "0.3.1" && c.state().from_version == "0.3.0");
        // Durum degisimden ONCE kalici yazildi: Receiving -> Swapped sirasinda.
        EF_CHECK(r.host.history.size() >= 2 && r.host.history[r.host.history.size() - 1] == StagePhase::Swapped);
        // Yanlis surumle saglik reddedilir.
        EF_CHECK(status_of(c.on_frame(make_update_healthy("0.3.2", 1))) == StatusState::Error && !c.stop_requested());
        EF_CHECK(status_of(c.on_frame(make_update_healthy("0.3.1", 2))) == StatusState::Committed);
        EF_CHECK(c.stop_requested() && r.host.phase() == StagePhase::UiHealthy);
    }
    // SCM yeni EXE ile yeniden baslatir.
    auto c2 = r.make();
    EF_CHECK(c2.start() == SupervisorAction::Commit);
    EF_CHECK(r.disk.files[kPaths.exe] == kNew && !r.disk.files.count(kPaths.backup));
    EF_CHECK(r.host.phase() == StagePhase::Idle && c2.state().highest_manifest_version == 10);
    EF_CHECK(c2.tick() == SupervisorAction::None);
}

// Degisim -> saglik gelmez -> 60 sn -> RollBack: eski EXE geri.
EF_TEST(swap_supervisor_timeout_rolls_back) {
    Rig r;
    auto c = r.make();
    (void)c.start();
    EF_CHECK(transfer(c) == StatusState::Swapped);
    r.host.now += 30000;
    EF_CHECK(c.tick() == SupervisorAction::None);
    r.host.now += 30001;
    EF_CHECK(c.tick() == SupervisorAction::RollBack);
    EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup));
    EF_CHECK(r.host.phase() == StagePhase::Idle);
    // Gec gelen saglik sinyali artik kabul edilmez.
    EF_CHECK(status_of(c.on_frame(make_update_healthy("0.3.1", 1))) == StatusState::Error && !c.stop_requested());
    // Ayni manifest (taban 10 -> floor 9) yeniden denenebilir; eski manifest (9) Replay.
    EF_CHECK(transfer(c, envelope("0.3.1", 9)) == StatusState::Error);
}

// Degisimden sonra cokme / guc kaybi (durum Swapped): yeni hizmet acilisi geri alir ve eski ikiliye yeniden baslatir.
EF_TEST(swap_supervisor_crash_after_swap_rolls_back_on_start) {
    Rig r;
    {
        auto c = r.make();
        (void)c.start();
        EF_CHECK(transfer(c) == StatusState::Swapped);
    }  // hizmet sureci oldu
    auto c2 = r.make();
    EF_CHECK(c2.start() == SupervisorAction::RollBack);
    EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup));
    EF_CHECK(r.host.phase() == StagePhase::Idle);
    auto c3 = r.make();
    EF_CHECK(c3.start() == SupervisorAction::None);  // dongu yok
}

EF_TEST(swap_supervisor_rejects_and_cleans_up) {
    {   // Kopya (exe.new) yeniden hash'lenir: bozuk kopya -> degisim yok, EXE eski, Idle.
        Rig r;
        r.host.corrupt_copy = true;
        auto c = r.make();
        EF_CHECK(transfer(c) == StatusState::Error);
        EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.staged) && !r.disk.files.count(kPaths.backup));
        EF_CHECK(r.host.phase() == StagePhase::Idle);
    }
    {   // Durum yazilamazsa degisim yapilmaz.
        Rig r;
        auto c = r.make();
        r.host.fail_save = true;
        EF_CHECK(transfer(c) == StatusState::Error);
        EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup));
    }
    {   // EXE tasinamiyor (kilitli): run_swap basarisiz, eski EXE yerinde, Idle.
        Rig r;
        r.disk.locked = kPaths.exe;
        auto c = r.make();
        EF_CHECK(transfer(c) == StatusState::Error);
        EF_CHECK(r.disk.files[kPaths.exe] == kOld && r.host.phase() == StagePhase::Idle);
    }
    {   // Hash uyusmaz: .part atilir.
        Rig r;
        auto c = r.make();
        EF_CHECK(transfer(c, envelope("0.3.1", 10, std::string(64, 'a'))) == StatusState::Error);
        EF_CHECK(!r.disk.files.count(kPart) && r.disk.files[kPaths.exe] == kOld);
    }
    {   // Baglanti aktarim ortasinda koptu: parca atilir, Idle; Swapped iken yeni begin reddedilir.
        Rig r;
        auto c = r.make();
        EF_CHECK(status_of(c.on_frame(make_update_begin(envelope(), 2))) == StatusState::Accepted);
        EF_CHECK(status_of(c.on_frame(make_update_chunk(0, std::string_view(kNew).substr(0, 100), 3))) == StatusState::Accepted);
        c.on_disconnect();
        EF_CHECK(!r.disk.files.count(kPart) && r.host.phase() == StagePhase::Idle);
        EF_CHECK(transfer(c) == StatusState::Swapped);
        EF_CHECK(status_of(c.on_frame(make_update_begin(envelope("0.3.2", 11), 9))) == StatusState::Error);
    }
    {   // Commit begin'den once; guncelleme disi cerceve hook'a ait degil.
        Rig r;
        auto c = r.make();
        EF_CHECK(status_of(c.on_frame(make_update_commit(2))) == StatusState::Error);
        EF_CHECK(!c.on_frame(ipc::Frame{ipc::MsgType::Cancel, 3, "{}"}).has_value());
        EF_CHECK(!c.on_frame(ipc::Frame{ipc::MsgType::UpdateStatus, 4, "{}"}).has_value());
    }
    {   // Tekrar oynatma tabani kalici durumdan: taban 20 iken manifest 10 reddedilir; taban asla azalmaz.
        Rig r;
        StagedUpdateState s;
        s.highest_manifest_version = 20;
        r.host.state_json = encode_state(s);
        auto c = r.make();
        (void)c.start();
        EF_CHECK(transfer(c) == StatusState::Error);
        EF_CHECK(c.state().highest_manifest_version == 20);
    }
}

// Geri alma basarisiz (yeni EXE tasinamiyor): durum Idle'a DUSMEZ, Swapped kalir ve sonraki tick yeniden dener.
EF_TEST(swap_supervisor_failed_rollback_is_retried) {
    Rig r;
    auto c = r.make();
    (void)c.start();
    EF_CHECK(transfer(c) == StatusState::Swapped);
    r.disk.locked = kPaths.exe;
    r.host.now += 60001;
    EF_CHECK(c.tick() == SupervisorAction::None);
    EF_CHECK(r.host.phase() == StagePhase::Swapped && r.disk.files[kPaths.exe] == kNew && r.disk.files[kPaths.backup] == kOld);
    r.disk.locked.clear();
    EF_CHECK(c.tick() == SupervisorAction::RollBack);
    EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup) && r.host.phase() == StagePhase::Idle);
    // Acilista geri alma basarisizsa da durum Swapped kalir (sonraki acilis yeniden dener).
    Rig r2;
    {
        auto a = r2.make();
        (void)a.start();
        EF_CHECK(transfer(a) == StatusState::Swapped);
    }
    r2.disk.locked = kPaths.exe;
    auto b = r2.make();
    EF_CHECK(b.start() == SupervisorAction::RollBack);
    EF_CHECK(r2.host.phase() == StagePhase::Swapped && r2.disk.files[kPaths.exe] == kNew);
    r2.disk.locked.clear();
    auto d = r2.make();
    EF_CHECK(d.start() == SupervisorAction::RollBack && r2.disk.files[kPaths.exe] == kOld && r2.host.phase() == StagePhase::Idle);
}

// run_swap ortada kaldi ve eski EXE geri konamadi: Idle yerine Swapped (bekleme dolmus) -> ilk tick geri alir.
EF_TEST(swap_supervisor_broken_swap_keeps_rollback_state) {
    Rig r;
    r.host.now = 1'700'000'000'000;  // gercek saat gibi (epoch ms)
    auto c = r.make();
    (void)c.start();
    r.disk.fail_to = {kPaths.exe};
    EF_CHECK(transfer(c) == StatusState::Error);
    EF_CHECK(!r.disk.files.count(kPaths.exe) && r.disk.files[kPaths.backup] == kOld);
    EF_CHECK(r.host.phase() == StagePhase::Swapped && on_tick(c.state(), r.host.now) == SupervisorAction::RollBack);
    EF_CHECK(status_of(c.on_frame(make_update_begin(envelope(), 2))) == StatusState::Error);  // yeni aktarim yok
    r.disk.fail_to.clear();
    EF_CHECK(c.tick() == SupervisorAction::RollBack);
    EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup) && r.host.phase() == StagePhase::Idle);
}

// Yeni hizmet acilisinda yedek silinemedi (UiHealthy kalir): tick onayi yeniden dener; yeni aktarim sonra kabul edilir.
EF_TEST(swap_supervisor_commit_retried_on_tick) {
    Rig r;
    {
        auto c = r.make();
        (void)c.start();
        EF_CHECK(transfer(c) == StatusState::Swapped);
        EF_CHECK(status_of(c.on_frame(make_update_healthy("0.3.1", 2))) == StatusState::Committed);
        EF_CHECK(c.tick() == SupervisorAction::None && r.disk.files.count(kPaths.backup));  // durmadan once onay yok
    }
    r.disk.locked = kPaths.backup;
    auto c2 = r.make();
    EF_CHECK(c2.start() == SupervisorAction::None && r.host.phase() == StagePhase::UiHealthy);
    EF_CHECK(c2.tick() == SupervisorAction::None && r.host.phase() == StagePhase::UiHealthy);
    r.disk.locked.clear();
    EF_CHECK(c2.tick() == SupervisorAction::Commit);
    EF_CHECK(r.disk.files[kPaths.exe] == kNew && !r.disk.files.count(kPaths.backup) && r.host.phase() == StagePhase::Idle);
}

// 0.8.2: hizmet guncelleme sonrasi durmus kaldiysa onarim baslatmadan once durum UiHealthy'ye cekilir (RollBack yeni EXE'yi ezmesin).
EF_TEST(swap_adopt_swapped_as_healthy) {
    StagedUpdateState s;
    s.phase = StagePhase::Swapped;
    s.version = "0.8.2";
    s.from_version = "0.8.1";
    EF_CHECK(!adopt_swapped_as_healthy(s, "0.8.1"));  // farkli surum: degismez (eski EXE kuran onarim)
    EF_CHECK(s.phase == StagePhase::Swapped);
    EF_CHECK(!adopt_swapped_as_healthy(s, ""));
    EF_CHECK(adopt_swapped_as_healthy(s, "0.8.2"));
    EF_CHECK(s.phase == StagePhase::UiHealthy);
    EF_CHECK(on_service_start(s) == SupervisorAction::Commit);
    EF_CHECK(!adopt_swapped_as_healthy(s, "0.8.2"));  // zaten UiHealthy: islem yok
    for (auto p : {StagePhase::Idle, StagePhase::Receiving, StagePhase::Committed}) {
        StagedUpdateState o;
        o.phase = p;
        o.version = "0.8.2";
        EF_CHECK(!adopt_swapped_as_healthy(o, "0.8.2"));
        EF_CHECK(o.phase == p);
    }
}

namespace {
// win_setup_ops adopt_pending_update_state'in saf esi: yukseltilmis onarim hizmeti baslatmadan once kalici durumu okur, uyarlar, yazar.
void repair_adopts(FakeHost& host, std::string_view running_version) {
    StagedUpdateState s;
    if (!host.load_state(s)) return;
    if (adopt_swapped_as_healthy(s, running_version)) host.state_json = encode_state(s);
}
}  // namespace

// P9/B4 regresyon (0.8.x sahasi): guncelleme sonrasi hizmet DURMUS kaldi (SCM yeniden baslatmadi). Uc yol:
//  A) saglik geldi (UiHealthy), hizmet durdu ve hic acilmadi -> onarim (yeni EXE) -> acilis Commit, yeni EXE kalir.
//  B) saglik gelmeden hizmet oldu (Swapped), kullanici yeni surumle onarim yapti -> adopt UiHealthy -> Commit; RollBack
//     calisan yeni EXE'yi eskisiyle EZMEZ (0.8.2'deki tuzak).
//  C) onarimi eski surum yapti (hedef surum calismiyor) -> adopt yok -> acilis RollBack: saglik kanitlanmadi, eski EXE.
// Her yolda sonraki acilis donguye girmez (None).
EF_TEST(swap_supervisor_service_stopped_after_update_regression_08x) {
    {   // A
        Rig r;
        {
            auto c = r.make();
            (void)c.start();
            EF_CHECK(transfer(c) == StatusState::Swapped);
            EF_CHECK(status_of(c.on_frame(make_update_healthy("0.3.1", 2))) == StatusState::Committed && c.stop_requested());
        }  // hizmet sifir olmayan cikisla durdu; SCM yeniden baslatmadi (hata eylemi tukendi): hic tick yok
        EF_CHECK(r.host.phase() == StagePhase::UiHealthy && r.disk.files[kPaths.exe] == kNew);
        repair_adopts(r.host, "0.3.1");
        EF_CHECK(r.host.phase() == StagePhase::UiHealthy);
        auto c2 = r.make();
        EF_CHECK(c2.start() == SupervisorAction::Commit);
        EF_CHECK(r.disk.files[kPaths.exe] == kNew && !r.disk.files.count(kPaths.backup) && r.host.phase() == StagePhase::Idle);
        EF_CHECK(r.make().start() == SupervisorAction::None);
    }
    {   // B
        Rig r;
        {
            auto c = r.make();
            (void)c.start();
            EF_CHECK(transfer(c) == StatusState::Swapped);
        }  // saglik sinyalinden once hizmet oldu, SCM baslatmadi
        r.host.now += 10 * 60 * 1000;  // kullanici dakikalar sonra onarir
        repair_adopts(r.host, "0.3.1");
        EF_CHECK(r.host.phase() == StagePhase::UiHealthy);
        auto c2 = r.make();
        EF_CHECK(c2.start() == SupervisorAction::Commit);
        EF_CHECK(r.disk.files[kPaths.exe] == kNew && !r.disk.files.count(kPaths.backup));
        EF_CHECK(c2.tick() == SupervisorAction::None && r.make().start() == SupervisorAction::None);
    }
    {   // C
        Rig r;
        {
            auto c = r.make();
            (void)c.start();
            EF_CHECK(transfer(c) == StatusState::Swapped);
        }
        repair_adopts(r.host, "0.3.0");
        EF_CHECK(r.host.phase() == StagePhase::Swapped);
        auto c2 = r.make();
        EF_CHECK(c2.start() == SupervisorAction::RollBack);
        EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup) && r.host.phase() == StagePhase::Idle);
        EF_CHECK(r.make().start() == SupervisorAction::None);
    }
}

// P9/B4: bozuk saglik kontrolu (sahte supervizor). Yeni UI bozuk/yanlis/eski surumlu saglik yollar veya hic yollamaz
// (her acilista coker): hicbiri kabul edilmez, hizmet durmaz; bekleme dolunca eski EXE geri gelir ve tekrar dongu olmaz.
EF_TEST(swap_supervisor_broken_health_check_rolls_back) {
    Rig r;
    auto c = r.make();
    (void)c.start();
    EF_CHECK(transfer(c) == StatusState::Swapped);
    std::uint64_t seq = 100;
    for (const std::string& bad : {std::string(""), std::string("{}"), std::string("{\"version\":1}"), std::string("not json"),
                                   std::string("{\"version\":\"\"}"), std::string(70000, 'x')}) {
        const auto rep = c.on_frame(ipc::Frame{ipc::MsgType::UpdateHealthy, ++seq, bad});
        EF_CHECK(rep.has_value() && status_of(rep) == StatusState::Error && !c.stop_requested());
    }
    for (const char* v : {"0.3.0", "0.3.2", "0.3.1 ", "0.3", "v0.3.1"}) {
        EF_CHECK(status_of(c.on_frame(make_update_healthy(v, ++seq))) == StatusState::Error && !c.stop_requested());
    }
    EF_CHECK(r.host.phase() == StagePhase::Swapped && r.disk.files[kPaths.exe] == kNew);
    r.host.now += kServiceHealthWaitMs + 1;
    EF_CHECK(c.tick() == SupervisorAction::RollBack);
    EF_CHECK(r.disk.files[kPaths.exe] == kOld && !r.disk.files.count(kPaths.backup) && r.host.phase() == StagePhase::Idle);
    EF_CHECK(status_of(c.on_frame(make_update_healthy("0.3.1", ++seq))) == StatusState::Error && !c.stop_requested());
    // Eski EXE ile yeniden acilis: geri alinacak bir sey yok, dongu yok.
    auto c2 = r.make();
    EF_CHECK(c2.start() == SupervisorAction::None && r.disk.files[kPaths.exe] == kOld);
    // Yeni UI cokup duruyor ve hizmet Swapped iken kendisi de yeniden basladi: acilista geri alma (saglik beklenmez).
    Rig r2;
    {
        auto a = r2.make();
        (void)a.start();
        EF_CHECK(transfer(a) == StatusState::Swapped);
        r2.host.now += 1000;
        EF_CHECK(a.tick() == SupervisorAction::None);
    }
    auto b = r2.make();
    EF_CHECK(b.start() == SupervisorAction::RollBack && r2.disk.files[kPaths.exe] == kOld && r2.host.phase() == StagePhase::Idle);
}
