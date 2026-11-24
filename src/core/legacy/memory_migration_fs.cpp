// R-F1: test icin bellek ici MigrationFs (hata enjeksiyonu: bozuk kopya, kilitli dosya, disk dolu, yeniden adlandirma).
#include "core/legacy/user_data_migration.h"

#include "core/recovery/sha256.h"

namespace ef::core::legacy {

namespace {
bool under(const std::string& p, const std::string& root) { return p.size() > root.size() && p.compare(0, root.size(), root) == 0 && p[root.size()] == '\\'; }
bool ends_with(const std::string& s, const std::string& suf) {
    return !suf.empty() && s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
std::string parent(const std::string& p) {
    const auto i = p.find_last_of('\\');
    return i == std::string::npos ? std::string() : p.substr(0, i);
}
}  // namespace

void MemoryMigrationFs::put(const std::string& path, std::string bytes, bool read_only) {
    for (auto d = parent(path); !d.empty() && d.find('\\') != std::string::npos; d = parent(d)) dirs.insert(d);
    files[path] = File{std::move(bytes), read_only};
}

FsStatus MemoryMigrationFs::check_busy(const std::string& path) {
    for (auto& [suf, n] : busy) {
        if (!ends_with(path, suf) || n == 0) continue;
        if (n > 0) --n;
        return FsStatus::fail(FsCode::Busy, "sharing violation");
    }
    return {};
}

FsStatus MemoryMigrationFs::account(std::uint64_t n, const std::string&) {
    if (capacity && written + n > *capacity) return FsStatus::fail(FsCode::DiskFull, "disk full");
    written += n;
    return {};
}

MigrationFs::Kind MemoryMigrationFs::kind(const std::string& path) {
    if (files.count(path)) return Kind::File;
    if (reparse.count(path)) return Kind::Other;
    if (dirs.count(path)) return Kind::Dir;
    for (const auto& [p, f] : files)
        if (under(p, path)) return Kind::Dir;
    return Kind::Missing;
}

FsStatus MemoryMigrationFs::list_files(const std::string& dir, std::vector<FileInfo>& out) {
    out.clear();
    for (const auto& r : reparse)
        if (under(r, dir)) return FsStatus::fail(FsCode::Denied, "reparse point");
    for (const auto& [p, f] : files) {
        if (!under(p, dir)) continue;
        std::string rel = p.substr(dir.size() + 1);
        for (auto& c : rel)
            if (c == '\\') c = '/';
        out.push_back({rel, f.bytes.size(), f.read_only});
    }
    return {};
}

FsStatus MemoryMigrationFs::hash_file(const std::string& path, FileDigest& out) {
    if (auto b = check_busy(path); !b.ok()) return b;
    const auto it = files.find(path);
    if (it == files.end()) return FsStatus::fail(FsCode::NotFound, "missing");
    out.size = it->second.bytes.size();
    out.sha256 = recovery::sha256_hex(it->second.bytes);
    return {};
}

FsStatus MemoryMigrationFs::copy_file(const std::string& from, const std::string& to) {
    note("copy", to);
    if (auto b = check_busy(from); !b.ok()) return b;
    const auto it = files.find(from);
    if (it == files.end()) return FsStatus::fail(FsCode::NotFound, "missing");
    if (files.count(to)) return FsStatus::fail(FsCode::Exists, "exists");
    if (ends_with(from, fail_copy_of)) return FsStatus::fail(FsCode::Io, "injected copy failure");
    std::string bytes = it->second.bytes;
    if (auto a = account(bytes.size(), to); !a.ok()) return a;
    if (ends_with(from, corrupt_copy_of)) {
        if (bytes.empty()) bytes = "!";
        else bytes[0] = static_cast<char>(bytes[0] ^ 0x5a);
    }
    put(to, std::move(bytes), false);
    for (auto m = mutate_source_during_copy.begin(); m != mutate_source_during_copy.end(); ++m) {
        if (!ends_with(from, m->first)) continue;
        it->second.bytes = m->second;  // kaynak (eski surum) kopya sirasinda degisti; bir kez
        mutate_source_during_copy.erase(m);
        break;
    }
    return {};
}

FsStatus MemoryMigrationFs::read_file(const std::string& path, std::size_t max_bytes, std::string& out) {
    if (auto b = check_busy(path); !b.ok()) return b;
    const auto it = files.find(path);
    if (it == files.end()) return FsStatus::fail(FsCode::NotFound, "missing");
    if (it->second.bytes.size() > max_bytes) return FsStatus::fail(FsCode::Io, "too large");
    out = it->second.bytes;
    return {};
}

FsStatus MemoryMigrationFs::write_file(const std::string& path, const std::string& bytes) {
    note("write", path);
    if (ends_with(path, fail_write_of)) return FsStatus::fail(FsCode::Io, "injected write failure");
    if (auto a = account(bytes.size(), path); !a.ok()) return a;
    put(path, bytes, false);
    return {};
}

FsStatus MemoryMigrationFs::make_dirs(const std::string& path) {
    note("mkdir", path);
    if (files.count(path)) return FsStatus::fail(FsCode::Exists, "file in the way");
    for (auto d = path; !d.empty() && d.find('\\') != std::string::npos; d = parent(d)) dirs.insert(d);
    return {};
}

FsStatus MemoryMigrationFs::rename_dir(const std::string& from, const std::string& to) {
    note("rename", from);
    note("rename", to);
    if (fail_rename) return FsStatus::fail(FsCode::Io, "injected rename failure");
    if (kind(to) != Kind::Missing) return FsStatus::fail(FsCode::Exists, "exists");
    if (kind(from) != Kind::Dir) return FsStatus::fail(FsCode::NotFound, "missing");
    std::map<std::string, File> moved;
    for (auto it = files.begin(); it != files.end();) {
        if (under(it->first, from)) {
            moved[to + it->first.substr(from.size())] = std::move(it->second);
            it = files.erase(it);
        } else {
            ++it;
        }
    }
    std::set<std::string> nd;
    for (auto it = dirs.begin(); it != dirs.end();) {
        if (*it == from || under(*it, from)) {
            nd.insert(to + it->substr(from.size()));
            it = dirs.erase(it);
        } else {
            ++it;
        }
    }
    nd.insert(to);
    for (auto& [p, f] : moved) files[p] = std::move(f);
    dirs.insert(nd.begin(), nd.end());
    return {};
}

FsStatus MemoryMigrationFs::move_file(const std::string& from, const std::string& to) {
    note("move", from);
    note("move", to);
    if (files.count(to)) return FsStatus::fail(FsCode::Exists, "exists");
    const auto it = files.find(from);
    if (it == files.end()) return FsStatus::fail(FsCode::NotFound, "missing");
    File f = std::move(it->second);
    files.erase(it);
    put(to, std::move(f.bytes), f.read_only);
    return {};
}

FsStatus MemoryMigrationFs::remove_file(const std::string& path) {
    note("remove", path);
    files.erase(path);
    return {};
}

FsStatus MemoryMigrationFs::remove_tree(const std::string& path) {
    note("remove_tree", path);
    for (auto it = files.begin(); it != files.end();) it = (it->first == path || under(it->first, path)) ? files.erase(it) : std::next(it);
    for (auto it = dirs.begin(); it != dirs.end();) it = (*it == path || under(*it, path)) ? dirs.erase(it) : std::next(it);
    return {};
}

}  // namespace ef::core::legacy
