// R-F2 (ADR-043): eski adli kurulumdan yeni markaya gozetimsiz yukseltme (saf durum makinesi).
#include "core/legacy/legacy_upgrade.h"

#include "core/legacy/legacy_install.h"
#include "core/legacy/legacy_names.h"
#include "core/recovery/journal.h"
#include "core/serialization/json.h"

namespace ef::core::legacy {

namespace {
constexpr int kRecordSchema = 1;
constexpr int kMaxRollbackTries = 3;  // geri alma artigi kalsa bile bu denemeden sonra eski EXE geri getirilir

std::string utf8(const std::filesystem::path& x) {
    const auto u = x.u8string();
    return std::string(u.begin(), u.end());
}

// Yol karsilastirmasi: '/' == '\\', ASCII buyuk/kucuk harf duyarsiz, sondaki ayirac yok sayilir.
std::string norm(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '/') c = '\\';
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        o += c;
    }
    while (o.size() > 3 && o.back() == '\\') o.pop_back();
    return o;
}
bool same(std::string_view a, std::string_view b) { return !a.empty() && !b.empty() && norm(a) == norm(b); }

std::string leaf(const std::string& path) {
    const auto i = path.find_last_of('\\');
    return i == std::string::npos ? path : path.substr(i + 1);
}

std::string parent(const std::string& path) {
    const auto i = path.find_last_of('\\');
    return i == std::string::npos ? std::string() : path.substr(0, i);
}

constexpr std::pair<UpgradePhase, std::string_view> kPhases[] = {
    {UpgradePhase::None, "none"},           {UpgradePhase::Begun, "begun"},
    {UpgradePhase::NewFiles, "new_files"},  {UpgradePhase::Journals, "journals"},
    {UpgradePhase::NewService, "new_service"}, {UpgradePhase::Registered, "registered"},
    {UpgradePhase::Healthy, "healthy"},     {UpgradePhase::Done, "done"},
    {UpgradePhase::RollingBack, "rolling_back"}, {UpgradePhase::RolledBack, "rolled_back"},
};

bool in_progress(UpgradePhase ph) {
    return ph == UpgradePhase::Begun || ph == UpgradePhase::NewFiles || ph == UpgradePhase::Journals || ph == UpgradePhase::NewService ||
           ph == UpgradePhase::Registered;
}

std::string err_text(const FsStatus& s, std::string_view what) {
    std::string e(what);
    e += ": ";
    e += to_string(s.code);
    if (!s.detail.empty()) e += " (" + recovery::clamp_utf8(s.detail, 120) + ")";
    return e;
}

class Upgrader {
public:
    Upgrader(MigrationFs& fs, UpgradeSystem& sys, const UpgradePaths& p, const UpgradeNames& n, const UpgradeEnv& env)
        : fs_(fs), sys_(sys), p_(p), n_(n), env_(env) {}

    UpgradeResult host();
    UpgradeResult new_service();

private:
    std::optional<UpgradeRecord> load() {
        std::string text;
        if (!fs_.read_file(p_.record_file(), 64 * 1024, text).ok()) return std::nullopt;
        return decode_record(text);
    }
    bool persist() {
        rec_.updated_unix_ms = env_.now_unix_ms;
        const auto text = encode_record(rec_);
        if (!fs_.make_dirs(parent(p_.record_file())).ok()) return false;
        if (!fs_.write_file(p_.record_file(), text).ok()) return false;
        std::string back;
        return fs_.read_file(p_.record_file(), 64 * 1024, back).ok() && back == text;
    }
    template <class Op>
    FsStatus retry(Op&& op) {
        FsStatus s;
        for (int attempt = 0;; ++attempt) {
            s = op();
            if (s.code != FsCode::Busy || attempt >= env_.busy_retries) return s;
            if (env_.sleep) env_.sleep(attempt);
        }
    }
    UpgradeResult finish(UpgradeOutcome o, std::uint32_t code) {
        res_.outcome = o;
        res_.exit_code = code;
        res_.record = rec_;
        return res_;
    }
    UpgradeResult rollback(std::string stage, std::string error);
    bool step_new_files(std::string& err);
    bool step_journals(std::string& err);
    bool step_new_service(std::string& err);
    bool step_register(std::string& err);
    bool remove_owned_file(const std::string& f, std::string& err);

    MigrationFs& fs_;
    UpgradeSystem& sys_;
    const UpgradePaths& p_;
    const UpgradeNames& n_;
    const UpgradeEnv& env_;
    UpgradeRecord rec_;
    UpgradeResult res_;
};

// ---- LegacyHost ----
bool Upgrader::step_new_files(std::string& err) {
    using Kind = MigrationFs::Kind;
    if (fs_.kind(p_.new_dir) == Kind::Missing) {
        rec_.created_dir = true;  // niyet ONCE kalici: cokmeden sonra geri alma bunu bilir
        if (!persist()) return err = "record", false;
    } else if (fs_.kind(p_.new_dir) != Kind::Dir) {
        return err = "new install path is not a directory", false;
    }
    if (auto s = fs_.make_dirs(p_.new_dir); !s.ok()) return err = err_text(s, "create install dir"), false;
    if (auto s = sys_.apply_install_acl(p_.new_dir); !s.ok()) return err = err_text(s, "install dir acl"), false;
    FileDigest self;
    if (auto s = retry([&] { return fs_.hash_file(p_.self_exe, self); }); !s.ok()) return err = err_text(s, "read own image"), false;
    if (fs_.kind(p_.new_exe) == Kind::File) {
        FileDigest cur;
        if (auto s = retry([&] { return fs_.hash_file(p_.new_exe, cur); }); !s.ok()) return err = err_text(s, "read new exe"), false;
        if (cur.sha256 == self.sha256 && cur.size == self.size) return true;  // onceki deneme kopyalamis
        // Yeni markanin ayri bir kurulumu (baska ikili) zaten var: uzerine yazilmaz, geri alinamazdi.
        if (!rec_.created_exe) return err = "a different new-brand exe is already installed", false;
    } else if (fs_.kind(p_.new_exe) != Kind::Missing) {
        return err = "new exe path in the way", false;
    }
    rec_.created_exe = true;
    if (!persist()) return err = "record", false;
    const auto staged = p_.new_exe + ".new";
    (void)fs_.remove_file(staged);
    (void)fs_.remove_file(p_.new_exe);  // yalniz created_exe: bizim yarim kopyamiz
    if (auto s = retry([&] { return fs_.copy_file(p_.self_exe, staged); }); !s.ok()) return err = err_text(s, "copy exe"), false;
    FileDigest copy;
    if (auto s = retry([&] { return fs_.hash_file(staged, copy); }); !s.ok() || copy.sha256 != self.sha256 || copy.size != self.size)
        return err = "exe copy verify mismatch", false;
    if (auto s = retry([&] { return fs_.move_file(staged, p_.new_exe); }); !s.ok()) return err = err_text(s, "commit exe"), false;
    if (auto s = retry([&] { return fs_.hash_file(p_.new_exe, copy); }); !s.ok() || copy.sha256 != self.sha256) return err = "exe verify after commit", false;
    return true;
}

bool Upgrader::step_journals(std::string& err) {
    rec_.migrated_journals = true;  // geri alma isarete dayanir: isaret yoksa hicbir sey silinmez
    if (!persist()) return err = "record", false;
    MigrationEnv me;
    me.now_unix_ms = env_.now_unix_ms;
    me.app_version = env_.version;
    me.to_name = leaf(p_.new_machine_root);
    me.from_name = leaf(p_.old_machine_root);
    me.machine_id = env_.machine_id;
    me.busy_retries = env_.busy_retries;
    me.sleep = env_.sleep;
    res_.journals = migrate_machine_journals(fs_, sys_, {p_.old_machine_root, p_.new_machine_root}, me);
    if (res_.journals.outcome == MigrationOutcome::Failed || res_.journals.outcome == MigrationOutcome::Skipped)
        return err = "machine journals: " + res_.journals.stage + " " + res_.journals.error, false;
    return true;
}

bool Upgrader::step_new_service(std::string& err) {
    const auto st = sys_.service_state(n_.new_service);
    if (st == SvcState::Missing) {
        rec_.created_service = true;
        if (!persist()) return err = "record", false;
        if (auto s = sys_.create_service(n_.new_service, p_.new_exe); !s.ok()) return err = err_text(s, "create service"), false;
    } else if (const auto exe = sys_.service_exe(n_.new_service); !exe || !same(*exe, p_.new_exe)) {
        return err = "new service name is registered for another image", false;
    }
    if (sys_.service_state(n_.new_service) != SvcState::Running)
        if (auto s = sys_.start_service(n_.new_service); !s.ok()) return err = err_text(s, "start service"), false;
    if (!sys_.service_healthy(n_.new_service, env_.health_timeout_ms)) return err = "new service not healthy", false;
    return true;
}

bool Upgrader::step_register(std::string& err) {
    if (const auto loc = sys_.arp_install_location(n_.new_arp_key); !loc) {
        rec_.created_arp = true;
        if (!persist()) return err = "record", false;
        if (auto s = sys_.write_arp(n_.new_arp_key); !s.ok()) return err = err_text(s, "write arp"), false;
    } else if (!same(*loc, p_.new_dir)) {
        return err = "new uninstall key points elsewhere", false;
    }
    if (!sys_.shortcut_present(p_.new_start_lnk)) {
        rec_.created_start_lnk = true;
        if (!persist()) return err = "record", false;
        if (auto s = sys_.write_shortcut(p_.new_start_lnk, p_.new_exe); !s.ok()) return err = err_text(s, "start menu shortcut"), false;
    }
    // Ortak masaustu kisayolu yalniz eskisi vardiysa ve BIZIMSE (hedef eski EXE) olusturulur.
    const auto old_desk = sys_.shortcut_present(p_.old_desktop_lnk) ? sys_.shortcut_target(p_.old_desktop_lnk) : std::nullopt;
    if (old_desk && same(*old_desk, p_.old_exe) && !sys_.shortcut_present(p_.new_desktop_lnk)) {
        rec_.created_desktop_lnk = true;
        if (!persist()) return err = "record", false;
        if (auto s = sys_.write_shortcut(p_.new_desktop_lnk, p_.new_exe); !s.ok()) return err = err_text(s, "desktop shortcut"), false;
    }
    return true;
}

UpgradeResult Upgrader::rollback(std::string stage, std::string error) {
    rec_.phase = UpgradePhase::RollingBack;
    if (rec_.stage.empty() || !stage.empty()) rec_.stage = std::move(stage);
    if (!error.empty()) rec_.error = recovery::clamp_utf8(error, 400);
    ++rec_.rollback_tries;
    (void)persist();  // yazilamasa da geri alma yapilir (en kotu: yeniden baslatmada kayit yok -> yeni deneme)
    res_.stage = rec_.stage;
    res_.error = rec_.error;
    bool clean = true;
    auto lnk = [&](bool& flag, const std::string& path) {
        if (!flag) return;
        const auto t = sys_.shortcut_present(path) ? sys_.shortcut_target(path) : std::nullopt;
        if (sys_.shortcut_present(path) && !(t && same(*t, p_.new_exe))) {
            flag = false;  // bizim degil (baskasi degistirdi): dokunulmaz
            return;
        }
        if (sys_.shortcut_present(path) && !sys_.delete_shortcut(path).ok()) return void(clean = false);
        flag = false;
    };
    lnk(rec_.created_desktop_lnk, p_.new_desktop_lnk);
    lnk(rec_.created_start_lnk, p_.new_start_lnk);
    if (rec_.created_arp) {
        if (sys_.arp_install_location(n_.new_arp_key) && !sys_.delete_arp(n_.new_arp_key).ok()) clean = false;
        else rec_.created_arp = false;
    }
    if (rec_.created_service) {
        if (sys_.service_state(n_.new_service) != SvcState::Missing) {
            (void)sys_.stop_service(n_.new_service);
            if (!sys_.delete_service(n_.new_service).ok()) clean = false;
            else rec_.created_service = false;
        } else {
            rec_.created_service = false;
        }
    }
    if (rec_.migrated_journals) {
        if (!undo_machine_migration(fs_, {p_.old_machine_root, p_.new_machine_root}).ok()) clean = false;
        else rec_.migrated_journals = false;
    }
    if (rec_.created_exe) {
        (void)fs_.remove_file(p_.new_exe + ".new");
        if (fs_.kind(p_.new_exe) == MigrationFs::Kind::File && !fs_.remove_file(p_.new_exe).ok()) clean = false;
        else rec_.created_exe = false;
    }
    if (rec_.created_dir) {
        std::vector<FileInfo> left;
        if (fs_.kind(p_.new_dir) == MigrationFs::Kind::Dir && fs_.list_files(p_.new_dir, left).ok() && left.empty()) (void)fs_.remove_tree(p_.new_dir);
        if (fs_.kind(p_.new_dir) == MigrationFs::Kind::Missing) rec_.created_dir = false;
        else clean = false;  // klasorde baska dosya var: silinmez
    }
    // Yeni artefakt kalirsa eski EXE henuz geri getirilmez (SCM eski ikiliyi baslatirsa artigi kimse kaldiramaz);
    // ust sinirdan sonra yine de getirilir: eski kurulumun calismasi artiktan onemlidir.
    if (!clean && rec_.rollback_tries < kMaxRollbackTries) {
        (void)persist();
        return finish(UpgradeOutcome::RollbackPending, kLegacyHostExitRetry);
    }
    if (auto s = sys_.restore_legacy_exe(); !s.ok()) {
        if (res_.error.empty()) res_.error = err_text(s, "restore old exe");
        (void)persist();
        return finish(UpgradeOutcome::RollbackPending, kLegacyHostExitRetry);
    }
    rec_.phase = UpgradePhase::RolledBack;
    (void)persist();
    return finish(UpgradeOutcome::RolledBack, kLegacyHostExitRolledBack);
}

UpgradeResult Upgrader::host() {
    if (!same(p_.self_exe, p_.old_exe)) return finish(UpgradeOutcome::NotApplicable, kLegacyHostExitWrongLocation);
    const auto loaded = load();
    if (loaded && (loaded->phase == UpgradePhase::Healthy || loaded->phase == UpgradePhase::Done)) {
        rec_ = *loaded;
        return finish(UpgradeOutcome::HandedOff, kLegacyHostExitHandedOff);  // yeni hizmet devraldi; sunum yok
    }
    if (loaded && loaded->phase == UpgradePhase::RollingBack) {
        rec_ = *loaded;
        return rollback({}, {});
    }
    if (loaded && in_progress(loaded->phase)) {
        rec_ = *loaded;  // cokme/guc kaybi: ayni deneme kaldigi yerden (her adim idempotent; olusturulanlar biliniyor)
    } else {
        rec_ = UpgradeRecord{};
        rec_.attempt = (loaded ? loaded->attempt : 0) + 1;
    }
    rec_.version = env_.version;
    rec_.phase = rec_.phase == UpgradePhase::None ? UpgradePhase::Begun : rec_.phase;
    rec_.stage.clear();
    rec_.error.clear();
    rec_.rollback_tries = 0;
    // Kayit yeni korumali makine kokunde: ACL kayittan ONCE.
    if (auto s = fs_.make_dirs(p_.new_machine_root); !s.ok()) return rollback("begin", err_text(s, "create machine root"));
    if (auto s = sys_.apply_machine_acl(p_.new_machine_root); !s.ok()) return rollback("begin", err_text(s, "machine root acl"));
    if (!persist()) return rollback("begin", "upgrade record not writable");
    std::string err;
    struct Step {
        UpgradePhase done;
        const char* stage;
        bool (Upgrader::*fn)(std::string&);
    };
    const Step steps[] = {
        {UpgradePhase::NewFiles, "new_files", &Upgrader::step_new_files},
        {UpgradePhase::Journals, "journals", &Upgrader::step_journals},
        {UpgradePhase::NewService, "new_service", &Upgrader::step_new_service},
        {UpgradePhase::Registered, "register", &Upgrader::step_register},
    };
    for (const auto& s : steps) {
        // Yeniden girisde tamamlanmis adimlar da yeniden dogrulanir (idempotent); faz geri gitmez.
        if (!(this->*s.fn)(err)) return rollback(s.stage, err);
        if (static_cast<int>(rec_.phase) < static_cast<int>(s.done)) rec_.phase = s.done;
        if (!persist()) return rollback(s.stage, "record");
    }
    // TEK silme kapisi: Healthy kalici yazilip geri okunmadan eski hicbir sey silinmez.
    rec_.phase = UpgradePhase::Healthy;
    if (!persist()) return rollback("healthy", "record");
    return finish(UpgradeOutcome::HandedOff, kLegacyHostExitHandedOff);
}

// ---- NewService: eski artefakt temizligi ----
bool Upgrader::remove_owned_file(const std::string& f, std::string& err) {
    if (fs_.kind(f) != MigrationFs::Kind::File) return true;
    if (retry([&] { return fs_.remove_file(f); }).ok() && fs_.kind(f) == MigrationFs::Kind::Missing) return true;
    if (auto s = sys_.schedule_delete_on_reboot(f); !s.ok()) return err = err_text(s, "schedule reboot delete"), false;
    res_.scheduled_reboot_delete.push_back(f);
    rec_.reboot_pending = true;
    return true;
}

UpgradeResult Upgrader::new_service() {
    const auto loaded = load();
    if (!loaded) return finish(UpgradeOutcome::NotApplicable, 0);
    rec_ = *loaded;
    if (rec_.phase == UpgradePhase::Done) {
        res_.reboot_pending = rec_.reboot_pending;
        return finish(UpgradeOutcome::Completed, 0);
    }
    if (in_progress(rec_.phase)) return finish(UpgradeOutcome::WaitingForHost, 0);
    if (rec_.phase != UpgradePhase::Healthy) return finish(UpgradeOutcome::NotApplicable, 0);
    auto pending = [&](std::string stage, std::string error) {
        res_.stage = std::move(stage);
        res_.error = std::move(error);
        return finish(UpgradeOutcome::CleanupPending, 0);
    };
    // 1. Eski hizmet (ev sahibi yeni ikili; cikis 0 ile durmus olmali). Baska goruntulu ayni adli hizmete dokunulmaz.
    if (const auto st = sys_.service_state(n_.old_service); st != SvcState::Missing) {
        const auto exe = sys_.service_exe(n_.old_service);
        if (exe && same(*exe, p_.old_exe)) {
            if (st != SvcState::Stopped)
                if (auto s = sys_.stop_service(n_.old_service); !s.ok()) return pending("old_service", err_text(s, "stop old service"));
            if (auto s = sys_.delete_service(n_.old_service); !s.ok()) return pending("old_service", err_text(s, "delete old service"));
        }
    }
    // 2. Eski ARP: yalniz InstallLocation eski klasorse.
    if (const auto loc = sys_.arp_install_location(n_.old_arp_key); loc && same(*loc, p_.old_dir))
        if (auto s = sys_.delete_arp(n_.old_arp_key); !s.ok()) return pending("old_arp", err_text(s, "delete old arp"));
    // 3. Eski kisayollar: yalniz hedefi eski EXE olan .lnk.
    for (const auto* lnk : {&p_.old_start_lnk, &p_.old_desktop_lnk}) {
        if (!sys_.shortcut_present(*lnk)) continue;
        const auto t = sys_.shortcut_target(*lnk);
        if (!t || !same(*t, p_.old_exe)) continue;
        if (auto s = sys_.delete_shortcut(*lnk); !s.ok()) return pending("old_shortcuts", err_text(s, "delete old shortcut"));
    }
    // 4. Eski kurulum klasoru: yalniz bizim adlarimiz; kilitliyse yeniden baslatmada silinir.
    std::string err;
    for (const auto& f : p_.old_owned_files)
        if (!remove_owned_file(f, err)) return pending("old_files", err);
    if (fs_.kind(p_.old_dir) == MigrationFs::Kind::Dir) {
        std::vector<FileInfo> left;
        if (!fs_.list_files(p_.old_dir, left).ok()) return pending("old_dir", "list old dir");
        bool only_scheduled = true;
        for (const auto& f : left) {
            bool sched = false;
            for (const auto& s : res_.scheduled_reboot_delete) sched = sched || same(s, join_path(p_.old_dir, f.rel));
            only_scheduled = only_scheduled && sched;
        }
        if (left.empty()) {
            if (auto s = fs_.remove_tree(p_.old_dir); !s.ok()) return pending("old_dir", err_text(s, "remove old dir"));
        } else if (only_scheduled) {
            if (auto s = sys_.schedule_delete_on_reboot(p_.old_dir); !s.ok()) return pending("old_dir", err_text(s, "schedule old dir"));
            res_.scheduled_reboot_delete.push_back(p_.old_dir);
        }  // baska dosya var: klasor korunur
    }
    rec_.phase = UpgradePhase::Done;
    res_.reboot_pending = rec_.reboot_pending;
    if (!persist()) return pending("record", "record");  // sonraki acilis ayni adimlari (idempotent) yeniden dener
    return finish(UpgradeOutcome::Completed, 0);
}
}  // namespace

std::string_view to_string(UpgradePhase p) noexcept {
    for (const auto& [v, s] : kPhases)
        if (v == p) return s;
    return "?";
}

std::string_view to_string(UpgradeOutcome o) noexcept {
    switch (o) {
        case UpgradeOutcome::NotApplicable: return "not_applicable";
        case UpgradeOutcome::HandedOff: return "handed_off";
        case UpgradeOutcome::Completed: return "completed";
        case UpgradeOutcome::CleanupPending: return "cleanup_pending";
        case UpgradeOutcome::RolledBack: return "rolled_back";
        case UpgradeOutcome::RollbackPending: return "rollback_pending";
        case UpgradeOutcome::WaitingForHost: return "waiting_for_host";
    }
    return "?";
}

std::string encode_record(const UpgradeRecord& r) {
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(kRecordSchema));
    o.set("phase", json::Value::string(std::string(to_string(r.phase))));
    o.set("attempt", json::Value::integer(r.attempt));
    o.set("rollback_tries", json::Value::integer(r.rollback_tries));
    o.set("version", json::Value::string(r.version));
    auto c = json::Value::object();
    c.set("dir", json::Value::boolean(r.created_dir));
    c.set("exe", json::Value::boolean(r.created_exe));
    c.set("service", json::Value::boolean(r.created_service));
    c.set("arp", json::Value::boolean(r.created_arp));
    c.set("start_lnk", json::Value::boolean(r.created_start_lnk));
    c.set("desktop_lnk", json::Value::boolean(r.created_desktop_lnk));
    c.set("journals", json::Value::boolean(r.migrated_journals));
    o.set("created", std::move(c));
    o.set("reboot_pending", json::Value::boolean(r.reboot_pending));
    o.set("stage", json::Value::string(r.stage));
    o.set("error", json::Value::string(r.error));
    o.set("updated_unix_ms", json::Value::integer(r.updated_unix_ms));
    return json::write(o);
}

std::optional<UpgradeRecord> decode_record(std::string_view text) {
    auto doc = json::parse(text);
    if (!doc.ok() || !doc.value().is(json::Value::Type::Object)) return std::nullopt;
    const auto& o = doc.value();
    using T = json::Value::Type;
    auto get = [&](const json::Value& obj, std::string_view k, T t) -> const json::Value* {
        const auto* v = obj.find(k);
        return v && v->is(t) ? v : nullptr;
    };
    const auto* sv = get(o, "schema_version", T::Int);
    if (!sv || sv->as_int() != kRecordSchema) return std::nullopt;
    UpgradeRecord r;
    const auto* ph = get(o, "phase", T::String);
    if (!ph) return std::nullopt;
    bool found = false;
    for (const auto& [v, s] : kPhases)
        if (s == ph->as_string()) r.phase = v, found = true;
    if (!found) return std::nullopt;
    const auto* at = get(o, "attempt", T::Int);
    const auto* rt = get(o, "rollback_tries", T::Int);
    const auto* ver = get(o, "version", T::String);
    const auto* c = get(o, "created", T::Object);
    const auto* rp = get(o, "reboot_pending", T::Bool);
    const auto* st = get(o, "stage", T::String);
    const auto* er = get(o, "error", T::String);
    const auto* up = get(o, "updated_unix_ms", T::Int);
    if (!at || !rt || !ver || !c || !rp || !st || !er || !up) return std::nullopt;
    if (at->as_int() < 0 || at->as_int() > 1000000 || rt->as_int() < 0 || rt->as_int() > 1000000) return std::nullopt;
    r.attempt = static_cast<int>(at->as_int());
    r.rollback_tries = static_cast<int>(rt->as_int());
    r.version = ver->as_string();
    const std::pair<const char*, bool*> flags[] = {{"dir", &r.created_dir},         {"exe", &r.created_exe},
                                                   {"service", &r.created_service}, {"arp", &r.created_arp},
                                                   {"start_lnk", &r.created_start_lnk}, {"desktop_lnk", &r.created_desktop_lnk},
                                                   {"journals", &r.migrated_journals}};
    for (const auto& [k, dst] : flags) {
        const auto* b = get(*c, k, T::Bool);
        if (!b) return std::nullopt;
        *dst = b->as_bool();
    }
    r.reboot_pending = rp->as_bool();
    r.stage = st->as_string();
    r.error = er->as_string();
    r.updated_unix_ms = up->as_int();
    return r;
}

UpgradePaths make_upgrade_paths(const setup::KnownRoots& r, const std::filesystem::path& self_exe) {
    const auto old = legacy_layout(r);
    const auto cur = setup::make_layout(r);
    UpgradePaths p;
    p.self_exe = utf8(self_exe);
    p.old_dir = utf8(old.dir);
    p.old_exe = utf8(old.exe);
    p.old_machine_root = utf8(old.machine_dir);
    p.old_start_lnk = utf8(old.shortcut);
    p.old_desktop_lnk = utf8(old.desktop_shortcut);
    for (const auto& f : legacy_owned_files(old)) p.old_owned_files.push_back(utf8(f));
    p.new_dir = utf8(cur.dir);
    p.new_exe = utf8(cur.exe);
    p.new_machine_root = utf8(cur.machine_dir);
    p.new_start_lnk = utf8(cur.shortcut);
    p.new_desktop_lnk = utf8(cur.desktop_shortcut);
    return p;
}

UpgradeNames default_upgrade_names() {
    return {std::wstring(kOldServiceName), std::wstring(setup::kServiceName), legacy_arp_key(), std::wstring(setup::kArpKey)};
}

UpgradeResult run_legacy_upgrade(MigrationFs& fs, UpgradeSystem& sys, const UpgradePaths& p, const UpgradeNames& n, const UpgradeEnv& env) {
    Upgrader u(fs, sys, p, n, env);
    return env.role == UpgradeRole::LegacyHost ? u.host() : u.new_service();
}

std::string upgrade_log_line(const UpgradeResult& r) {
    std::string s = "legacy_upgrade outcome=" + std::string(to_string(r.outcome)) + " exit=" + std::to_string(r.exit_code) +
                    " phase=" + std::string(to_string(r.record.phase)) + " attempt=" + std::to_string(r.record.attempt);
    if (!r.stage.empty()) s += " stage=" + r.stage;
    if (r.journals.outcome != MigrationOutcome::Skipped || !r.journals.error.empty())
        s += " journals=" + std::string(to_string(r.journals.outcome)) + " journal_files=" + std::to_string(r.journals.files);
    if (r.reboot_pending) s += " reboot_pending=1 scheduled=" + std::to_string(r.scheduled_reboot_delete.size());
    if (!r.error.empty()) s += " error=\"" + recovery::clamp_utf8(r.error, 200) + "\"";
    return s;
}

}  // namespace ef::core::legacy
