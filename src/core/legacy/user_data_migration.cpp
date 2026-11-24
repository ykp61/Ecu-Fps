// R-F1: marka degisikligi kullanici verisi gocu (kopyala -> dogrula -> tek adimda devret; hata -> geri al).
#include "core/legacy/user_data_migration.h"

#include <algorithm>

#include "core/recovery/journal.h"
#include "core/recovery/sha256.h"
#include "core/serialization/json.h"

namespace ef::core::legacy {

std::string_view to_string(FsCode c) noexcept {
    switch (c) {
        case FsCode::Ok: return "ok";
        case FsCode::NotFound: return "not_found";
        case FsCode::Exists: return "exists";
        case FsCode::Busy: return "busy";
        case FsCode::Denied: return "denied";
        case FsCode::DiskFull: return "disk_full";
        case FsCode::Io: return "io";
    }
    return "?";
}

std::string_view to_string(MigrationOutcome o) noexcept {
    switch (o) {
        case MigrationOutcome::AlreadyMigrated: return "already_migrated";
        case MigrationOutcome::NoLegacyData: return "no_legacy_data";
        case MigrationOutcome::NewDirInUse: return "new_dir_in_use";
        case MigrationOutcome::Migrated: return "migrated";
        case MigrationOutcome::Failed: return "failed";
        case MigrationOutcome::Skipped: return "skipped";
    }
    return "?";
}

std::string join_path(std::string_view dir, std::string_view rel) {
    std::string out(dir);
    if (!rel.empty()) out += '\\';
    for (char c : rel) out += c == '/' ? '\\' : c;
    return out;
}

bool excluded_from_migration(std::string_view rel) noexcept {
    // Yalniz ilk bilesen; buyuk/kucuk harf duyarsiz (NTFS). "drivers\" ve "setup\" indirme onbellekleridir.
    const auto slash = rel.find('/');
    if (slash == std::string_view::npos) return false;
    const auto first = rel.substr(0, slash);
    const auto ieq = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    };
    return ieq(first, "drivers") || ieq(first, "setup");
}

// ---- Eski journal durum taramasi (salt okuma) ----
namespace {
// MigrationFs alt klasorunu JournalFs olarak gosterir. YAZMA YOK: yazma/silme cagrilari reddedilir.
class ReadOnlyJournalView final : public recovery::JournalFs {
public:
    ReadOnlyJournalView(MigrationFs& fs, std::string dir) : fs_(fs), dir_(std::move(dir)) {}
    contracts::Status write_atomic(const std::string&, const std::string&) override {
        return contracts::Status::fail(contracts::ErrorCode::Denied, "read-only legacy journal");
    }
    contracts::Result<std::string> read(const std::string& name) override {
        std::string out;
        const auto s = fs_.read_file(join_path(dir_, name), 1024 * 1024, out);
        if (!s.ok()) return contracts::Result<std::string>::fail(contracts::ErrorCode::IoError, std::string(to_string(s.code)));
        return out;
    }
    contracts::Result<std::vector<std::string>> list() override {
        std::vector<std::string> out;
        std::vector<FileInfo> files;
        if (!fs_.list_files(dir_, files).ok()) return contracts::Result<std::vector<std::string>>::fail(contracts::ErrorCode::IoError, "list");
        for (const auto& f : files)
            if (f.rel.find('/') == std::string::npos && f.rel.size() > 5 && f.rel.compare(f.rel.size() - 5, 5, ".json") == 0)
                out.push_back(f.rel);
        return out;
    }
    contracts::Status remove(const std::string&) override {
        return contracts::Status::fail(contracts::ErrorCode::Denied, "read-only legacy journal");
    }

private:
    MigrationFs& fs_;
    std::string dir_;
};
}  // namespace

LegacyJournalState scan_legacy_journal(MigrationFs& fs, const std::string& data_dir, const std::string& machine_id) {
    LegacyJournalState st;
    st.scanned = true;
    for (const char* sub : {"journal", "journal/startup", "journal/fixes"}) {
        const auto dir = join_path(data_dir, sub);
        if (fs.kind(dir) != MigrationFs::Kind::Dir) continue;
        ReadOnlyJournalView view(fs, dir);
        recovery::JournalStore store(view, machine_id);
        const auto l = store.scan();
        st.corrupt += l.corrupt.size() + l.newer.size();
        st.transactions += l.transactions.size();
        for (const auto& tx : l.transactions)
            if (tx.state != contracts::TxState::Restored) ++st.unresolved;  // Prepared..Active..RecoveryRequired
    }
    return st;
}

// ---- Goc ----
namespace {
struct Planned {
    FileInfo info;
    FileDigest digest;
};

std::string err_text(const FsStatus& s, std::string_view what, std::string_view rel = {}) {
    std::string e(what);
    if (!rel.empty()) {
        e += " ";
        e += rel;
    }
    e += ": ";
    e += to_string(s.code);
    return e;
}

std::string marker_json(std::string_view result, const MigrationEnv& env, const std::vector<Planned>* files) {
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(kMigrationSchema));
    o.set("result", json::Value::string(std::string(result)));
    if (files) o.set("from", json::Value::string(env.from_name));
    o.set("to", json::Value::string(env.to_name));
    if (files) {
        std::uint64_t total = 0;
        std::string list;
        for (const auto& f : *files) {
            total += f.digest.size;
            list += f.info.rel + "\t" + std::to_string(f.digest.size) + "\t" + f.digest.sha256 + "\n";
        }
        o.set("files", json::Value::integer(static_cast<std::int64_t>(files->size())));
        o.set("bytes", json::Value::integer(static_cast<std::int64_t>(total)));
        o.set("files_sha256", json::Value::string(recovery::sha256_hex(list)));
    }
    o.set("created_unix_ms", json::Value::integer(env.now_unix_ms));
    o.set("app_version", json::Value::string(env.app_version));
    return json::write(o);
}

// Paylasim/kilit ihlalinde sinirli yeniden deneme. Basari ya da kalici hata doner.
template <class Op>
FsStatus with_retry(const MigrationEnv& env, Op&& op) {
    FsStatus s;
    for (int attempt = 0;; ++attempt) {
        s = op();
        if (s.code != FsCode::Busy || attempt >= env.busy_retries) return s;
        if (env.sleep) env.sleep(attempt);
    }
}

class Migrator {
public:
    Migrator(MigrationFs& fs, const MigrationPaths& p, const MigrationEnv& env)
        : fs_(fs), old_(p.old_dir), new_(p.new_dir), staging_(p.new_dir + std::string(kStagingSuffix)), env_(env) {}

    MigrationReport run();

private:
    MigrationReport& fail(std::string stage, std::string error);
    bool copy_one(Planned& f);
    bool commit_rename();
    bool commit_merge();

    MigrationFs& fs_;
    std::string old_, new_, staging_;
    const MigrationEnv& env_;
    MigrationReport r_;
    std::vector<Planned> plan_;
    bool staging_created_ = false;
};

MigrationReport& Migrator::fail(std::string stage, std::string error) {
    r_.outcome = MigrationOutcome::Failed;
    r_.stage = std::move(stage);
    r_.error = std::move(error);
    // Geri alma: yalniz BIZIM hazirlik klasorumuz silinir (eski klasor ve yeni klasordeki veri dokunulmaz).
    if (staging_created_) (void)fs_.remove_tree(staging_);
    // Kucuk hata kaydi (yeni klasorde). Yazilamazsa (disk dolu) yine de rapor doner; sonraki acilis yeniden dener.
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(kMigrationSchema));
    o.set("result", json::Value::string("failed"));
    o.set("stage", json::Value::string(r_.stage));
    o.set("error", json::Value::string(recovery::clamp_utf8(r_.error, 400)));
    o.set("legacy_unresolved", json::Value::integer(static_cast<std::int64_t>(r_.legacy.unresolved)));
    o.set("legacy_corrupt", json::Value::integer(static_cast<std::int64_t>(r_.legacy.corrupt)));
    o.set("created_unix_ms", json::Value::integer(env_.now_unix_ms));
    o.set("app_version", json::Value::string(env_.app_version));
    if (fs_.make_dirs(new_).ok()) (void)fs_.write_file(join_path(new_, kFailedFile), json::write(o));
    return r_;
}

bool Migrator::copy_one(Planned& f) {
    const auto src = join_path(old_, f.info.rel);
    const auto dst = join_path(staging_, f.info.rel);
    // Kaynak once ozetlenir, kopyalanir, kopya ozetlenir, kaynak yeniden ozetlenir: eski surum ayni anda yaziyorsa
    // (ozet degisti) dosya bir kez daha denenir; yine tutarsizsa goc basarisiz (yarim kopya devredilmez).
    for (int pass = 0; pass < 2; ++pass) {
        FileDigest before, after, copy;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(src, before); }); !s.ok()) return fail("copy", err_text(s, "read", f.info.rel)), false;
        (void)fs_.remove_file(dst);
        if (auto s = with_retry(env_, [&] { return fs_.copy_file(src, dst); }); !s.ok()) return fail("copy", err_text(s, "copy", f.info.rel)), false;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(dst, copy); }); !s.ok()) return fail("verify", err_text(s, "read copy", f.info.rel)), false;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(src, after); }); !s.ok()) return fail("copy", err_text(s, "reread", f.info.rel)), false;
        if (before.sha256 != after.sha256 || before.size != after.size) continue;  // kaynak degisti: yeniden dene
        if (copy.sha256 != before.sha256 || copy.size != before.size) return fail("verify", "mismatch " + f.info.rel), false;
        f.digest = before;
        return true;
    }
    return fail("copy", "source changing " + f.info.rel), false;
}

bool Migrator::commit_rename() {
    // Sentinel devirle birlikte gider ve SONRA silinir: devirden once silinseydi, arada cokme hazirlik klasorunu
    // "bizim degil" gosterir ve goc bir daha denenemezdi. Yeni klasorde kalan sentinel zararsizdir (isaret var).
    if (auto s = with_retry(env_, [&] { return fs_.rename_dir(staging_, new_); }); !s.ok()) return fail("commit", err_text(s, "rename")), false;
    staging_created_ = false;
    (void)fs_.remove_file(join_path(new_, kStagingSentinel));
    return true;
}

bool Migrator::commit_merge() {
    // Var olan yeni klasore (bos ya da onceki basarisiz denemeden sonra uygulamanin yazdiklariyla) UZERINE YAZMADAN
    // dosya dosya tasinir. Ayni icerik = zaten tasinmis (yeniden deneme). Farkli journal kaydi = cakisma (hata; islem
    // kimlikleri rastgele oldugu icin beklenmez). Journal disi farkli dosya: yeni klasordeki korunur.
    for (const auto& f : plan_) {
        const auto from = join_path(staging_, f.info.rel);
        const auto to = join_path(new_, f.info.rel);
        if (fs_.kind(to) == MigrationFs::Kind::File) {
            FileDigest d;
            if (auto s = with_retry(env_, [&] { return fs_.hash_file(to, d); }); !s.ok()) return fail("commit", err_text(s, "read existing", f.info.rel)), false;
            if (d.sha256 == f.digest.sha256 && d.size == f.digest.size) continue;
            if (f.info.rel.rfind("journal/", 0) == 0) return fail("commit", "journal conflict " + f.info.rel), false;
            ++r_.skipped_existing;
            continue;
        }
        if (fs_.kind(to) != MigrationFs::Kind::Missing) return fail("commit", "path in the way " + f.info.rel), false;
        if (auto s = with_retry(env_, [&] { return fs_.move_file(from, to); }); !s.ok()) return fail("commit", err_text(s, "move", f.info.rel)), false;
        FileDigest d;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(to, d); }); !s.ok() || d.sha256 != f.digest.sha256)
            return fail("verify", "after move " + f.info.rel), false;
    }
    const auto mfrom = join_path(staging_, kMarkerFile);
    if (auto s = fs_.move_file(mfrom, join_path(new_, kMarkerFile)); !s.ok()) return fail("commit", err_text(s, "marker")), false;
    (void)fs_.remove_tree(staging_);
    staging_created_ = false;
    r_.merged = true;
    return true;
}

MigrationReport Migrator::run() {
    using Kind = MigrationFs::Kind;
    // 1. Yeni klasor: isaret varsa is bitti (hicbir sey yazilmaz).
    const Kind nk = fs_.kind(new_);
    if (nk == Kind::File || nk == Kind::Other) {
        r_.outcome = MigrationOutcome::Failed;  // dosya/junction: dokunulmaz, hata kaydi da yazilmaz
        r_.stage = "new_dir";
        r_.error = "new data path is not a plain directory";
        return r_;
    }
    if (nk == Kind::Dir && fs_.kind(join_path(new_, kMarkerFile)) == Kind::File) {
        r_.outcome = MigrationOutcome::AlreadyMigrated;
        return r_;
    }
    // 2. Eski klasor envanteri (salt okuma).
    std::vector<FileInfo> old_files;
    const Kind ok = fs_.kind(old_);
    if (ok == Kind::Dir) {
        if (auto s = fs_.list_files(old_, old_files); !s.ok()) return fail("scan", err_text(s, "list old"));
    }
    std::vector<FileInfo> wanted;
    for (const auto& f : old_files)
        if (!excluded_from_migration(f.rel)) wanted.push_back(f);
    if (wanted.empty()) {
        r_.outcome = MigrationOutcome::NoLegacyData;
        r_.marker = marker_json("no_legacy_data", env_, nullptr);
        r_.marker_written = fs_.make_dirs(new_).ok() && fs_.write_file(join_path(new_, kMarkerFile), r_.marker).ok();
        return r_;
    }
    std::sort(wanted.begin(), wanted.end(), [](const FileInfo& a, const FileInfo& b) { return a.rel < b.rel; });
    r_.legacy = scan_legacy_journal(fs_, old_, env_.machine_id);
    // 3. Yeni klasor kullaniliyor mu. Hata kaydi varsa (onceki deneme basarisiz) uzerine yazmadan birlestirilir.
    const bool retry = nk == Kind::Dir && fs_.kind(join_path(new_, kFailedFile)) == Kind::File;
    if (nk == Kind::Dir && !retry) {
        std::vector<FileInfo> nf;
        if (auto s = fs_.list_files(new_, nf); !s.ok()) {
            r_.outcome = MigrationOutcome::NewDirInUse;  // okunamayan yeni klasor: dokunulmaz
            r_.error = err_text(s, "list new");
            return r_;
        }
        if (!nf.empty()) {
            r_.outcome = MigrationOutcome::NewDirInUse;
            return r_;
        }
    }
    // 4. Eski surum aciksa kopya tutarsiz olabilir; bu acilista denenmez (eski veri korunur).
    if (env_.old_app_running) return fail("old_app_running", "previous version is running");
    // 5. Onceki cokmeden kalan hazirlik klasoru: yalniz bizim isaretimiz varsa silinir.
    if (const Kind sk = fs_.kind(staging_); sk != Kind::Missing) {
        if (sk != Kind::Dir || fs_.kind(join_path(staging_, kStagingSentinel)) != Kind::File) {
            r_.outcome = MigrationOutcome::Failed;
            r_.stage = "staging";
            r_.error = "unexpected staging directory (not ours); left untouched";
            return r_;
        }
        if (auto s = fs_.remove_tree(staging_); !s.ok()) return fail("staging", err_text(s, "remove leftover staging"));
    }
    // 6. On kontrol: bos alan (biliniyorsa).
    std::uint64_t total = 0;
    for (const auto& f : wanted) total += f.size;
    if (const auto free = fs_.free_bytes(new_); free && *free < total + 4 * 1024 * 1024)
        return fail("preflight", "disk_full: need " + std::to_string(total) + " bytes");
    // 7. Hazirlik klasoru + kopya + dogrulama.
    if (auto s = fs_.make_dirs(staging_); !s.ok()) return fail("staging", err_text(s, "create staging"));
    staging_created_ = true;
    if (auto s = fs_.write_file(join_path(staging_, kStagingSentinel), "{\"schema_version\":1}"); !s.ok())
        return fail("staging", err_text(s, "sentinel"));
    for (const auto& f : wanted) plan_.push_back({f, {}});
    for (auto& f : plan_)
        if (!copy_one(f)) return r_;
    // 8. Isaret hazirlik klasorune yazilir: yeniden adlandirma ile yeni klasor ISARETIYLE BIRLIKTE tek adimda belirir.
    r_.marker = marker_json("migrated", env_, &plan_);
    if (auto s = fs_.write_file(join_path(staging_, kMarkerFile), r_.marker); !s.ok()) return fail("marker", err_text(s, "marker"));
    {
        std::string back;
        if (auto s = fs_.read_file(join_path(staging_, kMarkerFile), 64 * 1024, back); !s.ok() || back != r_.marker)
            return fail("marker", "marker read-back");
    }
    // 9. Devir.
    if (fs_.kind(new_) == Kind::Missing) {
        if (!commit_rename()) return r_;
    } else if (!commit_merge()) {
        return r_;
    }
    // 10. Son geri okuma: isaret yeni klasorde.
    {
        std::string back;
        if (auto s = fs_.read_file(join_path(new_, kMarkerFile), 64 * 1024, back); !s.ok() || back != r_.marker) {
            // Veri yeni klasorde dogrulanmis kopya olarak duruyor; yalniz isaret okunamadi. Sonraki acilis birlestirir.
            return fail("verify", "marker missing after commit");
        }
    }
    (void)fs_.remove_file(join_path(new_, kFailedFile));
    r_.outcome = MigrationOutcome::Migrated;
    r_.marker_written = true;
    r_.files = plan_.size();
    for (const auto& f : plan_) r_.bytes += f.digest.size;
    return r_;
}
}  // namespace

MigrationReport migrate_user_data(MigrationFs& fs, const MigrationPaths& paths, const MigrationEnv& env) {
    if (paths.old_dir.empty() || paths.new_dir.empty() || paths.old_dir == paths.new_dir) {
        MigrationReport r;
        r.outcome = MigrationOutcome::Skipped;
        r.error = "paths";
        return r;
    }
    return Migrator(fs, paths, env).run();
}

std::string migration_log_line(const MigrationReport& r) {
    std::string s = "migration outcome=" + std::string(to_string(r.outcome));
    if (!r.stage.empty()) s += " stage=" + r.stage;
    if (r.outcome == MigrationOutcome::Migrated) s += " files=" + std::to_string(r.files) + " bytes=" + std::to_string(r.bytes) + (r.merged ? " merged=1" : "");
    if (r.skipped_existing) s += " kept_new=" + std::to_string(r.skipped_existing);
    if (r.legacy.scanned)
        s += " legacy_tx=" + std::to_string(r.legacy.transactions) + " legacy_unresolved=" + std::to_string(r.legacy.unresolved) +
             " legacy_corrupt=" + std::to_string(r.legacy.corrupt);
    if (r.machine_legacy.scanned)
        s += " machine_legacy_unresolved=" + std::to_string(r.machine_legacy.unresolved) + " machine_legacy_corrupt=" + std::to_string(r.machine_legacy.corrupt);
    if (r.legacy_pending()) s += " legacy_pending=1";
    if (!r.error.empty()) s += " error=\"" + recovery::clamp_utf8(r.error, 200) + "\"";
    return s;
}

}  // namespace ef::core::legacy
