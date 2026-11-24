// R-F2 (ADR-043): eski makine journal'i gocu (kopyala -> dogrula -> tek adimda devret -> ACL geri oku; hata -> geri al).
#include "core/legacy/machine_journal_migration.h"

#include <algorithm>
#include <set>

#include "core/recovery/journal.h"
#include "core/serialization/json.h"

namespace ef::core::legacy {

namespace {
constexpr std::size_t kMarkerMax = 1024 * 1024;

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

template <class Op>
FsStatus with_retry(const MigrationEnv& env, Op&& op) {
    FsStatus s;
    for (int attempt = 0;; ++attempt) {
        s = op();
        if (s.code != FsCode::Busy || attempt >= env.busy_retries) return s;
        if (env.sleep) env.sleep(attempt);
    }
}

struct Planned {
    FileInfo info;
    FileDigest digest;
};

struct MarkerFile {
    std::string rel, sha256;
    std::uint64_t size = 0;
};

std::string marker_json(std::string_view result, const MigrationEnv& env, const std::vector<Planned>& files) {
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(kMigrationSchema));
    o.set("result", json::Value::string(std::string(result)));
    o.set("from", json::Value::string(env.from_name));
    o.set("to", json::Value::string(env.to_name));
    auto arr = json::Value::array();
    std::uint64_t total = 0;
    for (const auto& f : files) {
        auto e = json::Value::object();
        e.set("rel", json::Value::string(f.info.rel));
        e.set("size", json::Value::integer(static_cast<std::int64_t>(f.digest.size)));
        e.set("sha256", json::Value::string(f.digest.sha256));
        arr.push(std::move(e));
        total += f.digest.size;
    }
    o.set("files", std::move(arr));
    o.set("bytes", json::Value::integer(static_cast<std::int64_t>(total)));
    o.set("created_unix_ms", json::Value::integer(env.now_unix_ms));
    o.set("app_version", json::Value::string(env.app_version));
    return json::write(o);
}

// Gecerli isaret: sema 1, result migrated|no_legacy_data, dosya listesi okunur. Aksi halde false.
bool read_marker(MigrationFs& fs, const std::string& path, std::vector<MarkerFile>& out) {
    std::string text;
    if (!fs.read_file(path, kMarkerMax, text).ok()) return false;
    auto doc = json::parse(text);
    if (!doc.ok() || !doc.value().is(json::Value::Type::Object)) return false;
    const auto& o = doc.value();
    const auto* sv = o.find("schema_version");
    const auto* res = o.find("result");
    const auto* files = o.find("files");
    if (!sv || !sv->is(json::Value::Type::Int) || sv->as_int() != kMigrationSchema) return false;
    if (!res || !res->is(json::Value::Type::String) || (res->as_string() != "migrated" && res->as_string() != "no_legacy_data")) return false;
    if (!files || !files->is(json::Value::Type::Array)) return false;
    for (const auto& e : files->items()) {
        const auto* rel = e.find("rel");
        const auto* size = e.find("size");
        const auto* sha = e.find("sha256");
        if (!rel || !rel->is(json::Value::Type::String) || !size || !size->is(json::Value::Type::Int) || !sha ||
            !sha->is(json::Value::Type::String))
            return false;
        if (rel->as_string().empty() || rel->as_string().find("..") != std::string::npos) return false;
        out.push_back({rel->as_string(), sha->as_string(), static_cast<std::uint64_t>(size->as_int())});
    }
    return true;
}

class MachineMigrator {
public:
    MachineMigrator(MigrationFs& fs, MachineAcl& acl, const MachineMigrationPaths& p, const MigrationEnv& env)
        : fs_(fs),
          acl_(acl),
          old_journal_(join_path(p.old_root, kMachineJournalDir)),
          new_root_(p.new_root),
          new_journal_(join_path(p.new_root, kMachineJournalDir)),
          staging_(join_path(p.new_root, std::string(kMachineJournalDir) + std::string(kStagingSuffix))),
          marker_(join_path(p.new_root, kMachineMarkerFile)),
          old_root_(p.old_root),
          env_(env) {}

    MachineMigrationReport run();

private:
    MachineMigrationReport& fail(std::string stage, std::string error) {
        r_.outcome = MigrationOutcome::Failed;
        r_.stage = std::move(stage);
        r_.error = std::move(error);
        if (staging_created_) (void)fs_.remove_tree(staging_);  // yalniz bizim hazirlik klasorumuz
        return r_;
    }
    bool copy_one(Planned& f);
    bool verify_acl();

    MigrationFs& fs_;
    MachineAcl& acl_;
    std::string old_journal_, new_root_, new_journal_, staging_, marker_, old_root_;
    const MigrationEnv& env_;
    MachineMigrationReport r_;
    std::vector<Planned> plan_;
    bool staging_created_ = false;
};

bool MachineMigrator::copy_one(Planned& f) {
    const auto src = join_path(old_journal_, f.info.rel);
    const auto dst = join_path(staging_, f.info.rel);
    for (int pass = 0; pass < 2; ++pass) {
        FileDigest before, after, copy;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(src, before); }); !s.ok()) return fail("copy", err_text(s, "read", f.info.rel)), false;
        (void)fs_.remove_file(dst);
        if (auto s = with_retry(env_, [&] { return fs_.copy_file(src, dst); }); !s.ok()) return fail("copy", err_text(s, "copy", f.info.rel)), false;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(dst, copy); }); !s.ok()) return fail("verify", err_text(s, "read copy", f.info.rel)), false;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(src, after); }); !s.ok()) return fail("copy", err_text(s, "reread", f.info.rel)), false;
        if (before.sha256 != after.sha256 || before.size != after.size) continue;  // eski surec yaziyor: bir kez daha
        if (copy.sha256 != before.sha256 || copy.size != before.size) return fail("verify", "mismatch " + f.info.rel), false;
        f.digest = before;
        return true;
    }
    return fail("copy", "source changing " + f.info.rel), false;
}

bool MachineMigrator::verify_acl() {
    // journal klasoru + kopyalanan her alt klasor (broker open_machine_journal her alt dizini ayrica dogrular).
    std::set<std::string> dirs{new_journal_};
    std::vector<FileInfo> now;
    if (fs_.list_files(new_journal_, now).ok())
        for (const auto& f : now)
            for (auto i = f.rel.find('/'); i != std::string::npos; i = f.rel.find('/', i + 1)) dirs.insert(join_path(new_journal_, f.rel.substr(0, i)));
    for (const auto& d : dirs)
        if (auto s = acl_.verify_machine_acl(d); !s.ok()) return fail("acl", err_text(s, "verify acl")), false;
    r_.acl_verified = true;
    return true;
}

MachineMigrationReport MachineMigrator::run() {
    using Kind = MigrationFs::Kind;
    r_.legacy = scan_legacy_journal(fs_, old_root_, env_.machine_id);
    const Kind rk = fs_.kind(new_root_);
    if (rk == Kind::File || rk == Kind::Other) return fail("new_root", "new machine path is not a plain directory");
    // 1. Isaret: is bitti; ACL yine geri okunur (yeniden calistirma ACL bozulmasini gizlemez).
    if (fs_.kind(marker_) == Kind::File) {
        std::vector<MarkerFile> mf;
        if (!read_marker(fs_, marker_, mf)) return fail("marker", "unreadable marker");
        r_.outcome = MigrationOutcome::AlreadyMigrated;
        if (fs_.kind(new_journal_) == Kind::Dir) (void)verify_acl();  // hata -> Failed (fail() ayarlar)
        return r_;
    }
    // 2. Eski envanter (salt okuma). Gecici artiklar (*.tmp) kopyalanmaz: commit edilmemis veri, kayit degil.
    std::vector<FileInfo> old_files;
    if (fs_.kind(old_journal_) == Kind::Dir)
        if (auto s = fs_.list_files(old_journal_, old_files); !s.ok()) return fail("scan", err_text(s, "list old"));
    std::vector<FileInfo> wanted;
    for (const auto& f : old_files)
        if (!(f.rel.size() > 4 && f.rel.compare(f.rel.size() - 4, 4, ".tmp") == 0)) wanted.push_back(f);
    std::sort(wanted.begin(), wanted.end(), [](const FileInfo& a, const FileInfo& b) { return a.rel < b.rel; });
    // 3. Yeni kok korumali ACL ile (journal yoksa da: isaret ayni korumali kokte).
    if (auto s = fs_.make_dirs(new_root_); !s.ok()) return fail("new_root", err_text(s, "create root"));
    if (auto s = acl_.apply_machine_acl(new_root_); !s.ok()) return fail("acl", err_text(s, "apply acl"));
    if (wanted.empty()) {
        r_.outcome = MigrationOutcome::NoLegacyData;
        if (auto s = fs_.write_file(marker_, marker_json("no_legacy_data", env_, plan_)); !s.ok()) return fail("marker", err_text(s, "marker"));
        return r_;
    }
    // 4. Onceki cokmeden kalan hazirlik: yalniz bizim isaretimizle silinir.
    if (const Kind sk = fs_.kind(staging_); sk != Kind::Missing) {
        if (sk != Kind::Dir || fs_.kind(join_path(staging_, kStagingSentinel)) != Kind::File) {
            r_.outcome = MigrationOutcome::Failed;
            r_.stage = "staging";
            r_.error = "unexpected staging directory (not ours); left untouched";
            return r_;
        }
        if (auto s = fs_.remove_tree(staging_); !s.ok()) return fail("staging", err_text(s, "remove leftover staging"));
    }
    std::uint64_t total = 0;
    for (const auto& f : wanted) total += f.size;
    if (const auto free = fs_.free_bytes(new_root_); free && *free < total + 4 * 1024 * 1024)
        return fail("preflight", "disk_full: need " + std::to_string(total) + " bytes");
    // 5. Hazirlik + kopya + dogrulama.
    if (auto s = fs_.make_dirs(staging_); !s.ok()) return fail("staging", err_text(s, "create staging"));
    staging_created_ = true;
    if (auto s = fs_.write_file(join_path(staging_, kStagingSentinel), "{\"schema_version\":1}"); !s.ok()) return fail("staging", err_text(s, "sentinel"));
    for (const auto& f : wanted) plan_.push_back({f, {}});
    for (auto& f : plan_)
        if (!copy_one(f)) return r_;
    // 6. Devir: yoksa tek yeniden adlandirma; varsa dosya dosya, uzerine yazmadan. Sentinel devirden SONRA silinir
    //    (arada cokme hazirlik klasorunu "bizim degil" gostermesin; .json olmadigi icin JournalStore onu okumaz).
    if (fs_.kind(new_journal_) == Kind::Missing) {
        if (auto s = with_retry(env_, [&] { return fs_.rename_dir(staging_, new_journal_); }); !s.ok()) return fail("commit", err_text(s, "rename"));
        staging_created_ = false;
        (void)fs_.remove_file(join_path(new_journal_, kStagingSentinel));
    } else {
        if (fs_.kind(new_journal_) != Kind::Dir) return fail("commit", "journal path is not a directory");
        for (const auto& f : plan_) {
            const auto from = join_path(staging_, f.info.rel);
            const auto to = join_path(new_journal_, f.info.rel);
            const Kind tk = fs_.kind(to);
            if (tk == Kind::File) {
                FileDigest d;
                if (auto s = with_retry(env_, [&] { return fs_.hash_file(to, d); }); !s.ok()) return fail("commit", err_text(s, "read existing", f.info.rel));
                if (d.sha256 == f.digest.sha256 && d.size == f.digest.size) continue;  // onceki denemede tasinmis
                return fail("commit", "journal conflict " + f.info.rel);
            }
            if (tk != Kind::Missing) return fail("commit", "path in the way " + f.info.rel);
            if (auto s = with_retry(env_, [&] { return fs_.move_file(from, to); }); !s.ok()) return fail("commit", err_text(s, "move", f.info.rel));
        }
        (void)fs_.remove_tree(staging_);
        staging_created_ = false;
        r_.merged = true;
    }
    // 7. Devredilen her dosya yeniden ozetlenir (bayt bayt ayni olmali).
    for (const auto& f : plan_) {
        FileDigest d;
        if (auto s = with_retry(env_, [&] { return fs_.hash_file(join_path(new_journal_, f.info.rel), d); }); !s.ok() || d.sha256 != f.digest.sha256)
            return fail("verify", "after commit " + f.info.rel);
    }
    // 8. Isaret (dosya listesiyle; geri alma icin) ve ACL geri okumasi.
    const auto marker = marker_json("migrated", env_, plan_);
    if (auto s = fs_.write_file(marker_, marker); !s.ok()) return fail("marker", err_text(s, "marker"));
    {
        std::string back;
        if (auto s = fs_.read_file(marker_, kMarkerMax, back); !s.ok() || back != marker) return fail("marker", "marker read-back");
    }
    if (!verify_acl()) return r_;
    r_.outcome = MigrationOutcome::Migrated;
    r_.files = plan_.size();
    for (const auto& f : plan_) r_.bytes += f.digest.size;
    return r_;
}
}  // namespace

MachineMigrationReport migrate_machine_journals(MigrationFs& fs, MachineAcl& acl, const MachineMigrationPaths& p, const MigrationEnv& env) {
    if (p.old_root.empty() || p.new_root.empty() || p.old_root == p.new_root) {
        MachineMigrationReport r;
        r.error = "paths";
        return r;
    }
    return MachineMigrator(fs, acl, p, env).run();
}

FsStatus undo_machine_migration(MigrationFs& fs, const MachineMigrationPaths& p, std::size_t* kept) {
    if (kept) *kept = 0;
    const auto marker = join_path(p.new_root, kMachineMarkerFile);
    if (fs.kind(marker) == MigrationFs::Kind::Missing) return {};
    std::vector<MarkerFile> files;
    if (!read_marker(fs, marker, files)) return FsStatus::fail(FsCode::Io, "unreadable marker");
    const auto journal = join_path(p.new_root, kMachineJournalDir);
    for (const auto& f : files) {
        const auto path = join_path(journal, f.rel);
        if (fs.kind(path) != MigrationFs::Kind::File) continue;
        FileDigest d;
        if (!fs.hash_file(path, d).ok() || d.sha256 != f.sha256 || d.size != f.size) {
            if (kept) ++*kept;  // yeni surum bu kaydi degistirdi: veri korunur
            continue;
        }
        if (auto s = fs.remove_file(path); !s.ok()) return s;
    }
    return fs.remove_file(marker);
}

LegacyJournalState machine_legacy_after_upgrade(MigrationFs& fs, const MachineMigrationPaths& p, const std::string& machine_id) {
    std::vector<MarkerFile> files;
    if (!p.new_root.empty() && read_marker(fs, join_path(p.new_root, kMachineMarkerFile), files)) {
        LegacyJournalState st;
        st.scanned = true;  // aktarildi: bekleyen kayitlar yeni makine journal'inda gorunur
        return st;
    }
    return scan_legacy_journal(fs, p.old_root, machine_id);
}

}  // namespace ef::core::legacy
