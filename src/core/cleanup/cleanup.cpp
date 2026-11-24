#include "core/cleanup/cleanup.h"

#include <algorithm>
#include <chrono>
#include <set>

#include "core/cleanup/app_files.h"

namespace ef::core::cleanup {

using contracts::ErrorCode;

namespace {

// Kategori tablosu. path_documented=false olanlar varsayilan secili degildir.
//  - UserTemp / WindowsTemp: Windows gecici dosya klasorleri (Disk Temizleme "Temporary files").
//  - D3DSCache: Disk Temizleme "DirectX Shader Cache" isleyicisi; aciklamasi "grafik sistemi tarafindan
//    olusturulur, gerektiginde yeniden uretilir". Klasor adi resmi belgede yol olarak gecmez -> gozlem.
//  - NVIDIA DXCache/GLCache, AMD DxCache/DxcCache: surucu golgelendirici onbellekleri; surucu yeniden derler.
//    Ureticinin yol belgesi bu oturumda dogrulanmadi (bazi yeni NVIDIA surumleri LocalLow altini kullanir).
//  - thumbcache/iconcache: Disk Temizleme "Thumbnails"; Explorer yeniden olusturur, genelde Explorer kilitler.
//  - WER ReportArchive/ReportQueue: Disk Temizleme "Windows error reports"; yeniden olusturulmaz ama tani
//    verisidir, kullanici verisi degildir -> varsayilan secili degil.
//  - Teslim Iyilestirme: resmi yol Delete-DeliveryOptimizationCache; dogrudan silme broker'da yeniden
//    degerlendirilecek. Yonetici gerektirir.
//  - Geri Donusum Kutusu: kullanicinin sildigi dosyalar; YENIDEN OLUSTURULMAZ -> varsayilan secili degil.
constexpr std::array<CategoryInfo, kAllCategories.size()> kInfo = {{
    {CleanupCategory::UserTemp, "user_temp", "cleanup.user_temp", "cleanup.user_temp.desc", false, true, true, true,
     FileFilter::AllRecursive, 7},
    {CleanupCategory::WindowsTemp, "windows_temp", "cleanup.windows_temp", "cleanup.windows_temp.desc", true, false, true,
     true, FileFilter::AllRecursive},
    {CleanupCategory::ShaderCacheDirectX, "shader_dx", "cleanup.shader_dx", "cleanup.shader_dx.desc", false, true, true,
     true, FileFilter::AllRecursive},
    {CleanupCategory::ShaderCacheNvidia, "shader_nvidia", "cleanup.shader_nvidia", "cleanup.shader_nvidia.desc", false,
     false, true, false, FileFilter::AllRecursive},
    {CleanupCategory::ShaderCacheAmd, "shader_amd", "cleanup.shader_amd", "cleanup.shader_amd.desc", false, false, true,
     false, FileFilter::AllRecursive},
    {CleanupCategory::ThumbnailCache, "thumbnails", "cleanup.thumbnails", "cleanup.thumbnails.desc", false, true, true, true,
     FileFilter::ThumbcacheTopLevel},
    {CleanupCategory::WerReportsUser, "wer_user", "cleanup.wer_user", "cleanup.wer_user.desc", false, false, true, true,
     FileFilter::AllRecursive},
    {CleanupCategory::WerReportsSystem, "wer_system", "cleanup.wer_system", "cleanup.wer_system.desc", true, false, true,
     true, FileFilter::AllRecursive},
    {CleanupCategory::DeliveryOptimization, "delivery_optimization", "cleanup.delivery_optimization",
     "cleanup.delivery_optimization.desc", true, false, true, false, FileFilter::AllRecursive},
    {CleanupCategory::RecycleBin, "recycle_bin", "cleanup.recycle_bin", "cleanup.recycle_bin.desc", false, false, false,
     true, FileFilter::AllRecursive},
    // EXT-CLN: urunun kendi test klasorleri. Yol urun tarafindan olusturulur (belgeli sozlesme), varsayilan secili
    // degil (veri kaybi riski kullaniciya acik kalsin); 7 gunden yeni dosyalar korunur.
    {CleanupCategory::AppTestFiles, "app_test_files", "cleanup.app_test_files", "cleanup.app_test_files.desc", false, false,
     true, true, FileFilter::AllRecursive, 7},
    // T15: bekleme bellegi. Dosya degil; tek seferlik bakim isi (broker, kendi UAC'i). Varsayilan secili degil; fayda
    // kanitli degil (tartismali). Yol belgesi yok (PDH sayaclari + NtSetSystemInformation), bu yuzden path_documented=false.
    {CleanupCategory::StandbyMemory, "standby_memory", "cleanup.standby", "cleanup.standby.desc", false, false, true, false,
     FileFilter::AllRecursive},
}};

wchar_t fold(wchar_t c) noexcept { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c; }

std::wstring folded(std::wstring_view s) {
    std::wstring r(s);
    for (auto& c : r) c = fold(c);
    return r;
}

bool starts_with_ci(std::wstring_view s, std::wstring_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (fold(s[i]) != fold(prefix[i])) return false;
    return true;
}

bool equal_ci(std::wstring_view a, std::wstring_view b) { return a.size() == b.size() && starts_with_ci(a, b); }

std::wstring_view leaf(std::wstring_view p) {
    const auto k = p.find_last_of(L'\\');
    return k == std::wstring_view::npos ? p : p.substr(k + 1);
}

std::size_t depth_of(std::wstring_view normalized) {
    // "C:\" -> 0, "C:\a" -> 1, "C:\a\b" -> 2
    if (normalized.size() <= 3) return 0;
    return static_cast<std::size_t>(std::count(normalized.begin(), normalized.end(), L'\\'));
}

bool ends_with_ci(std::wstring_view s, std::wstring_view suffix) {
    return s.size() >= suffix.size() && equal_ci(s.substr(s.size() - suffix.size()), suffix);
}

// Kokun kendisi guvenli mi: en az iki bilesen, onemli klasorlerin atasi/esi degil, kategoriye ozgu ad kurali.
bool root_is_sane(CleanupCategory c, const std::wstring& root, const CleanupEnv& env) {
    if (depth_of(root) < 2) return false;
    for (const std::wstring* key : {&env.windir, &env.local_app_data, &env.program_data}) {
        if (key->empty()) continue;
        const auto k = normalize_path(*key);
        if (!k) continue;
        if (equal_ci(*k, root) || is_path_allowed(root, *k)) return false;  // kok, korunan klasorun kendisi/atasi
    }
    if (c == CleanupCategory::UserTemp) {
        const auto l = leaf(root);
        if (!equal_ci(l, L"temp") && !equal_ci(l, L"tmp")) return false;
    }
    return true;
}

void add_root(std::vector<std::wstring>& out, CleanupCategory c, const CleanupEnv& env, const std::wstring& base,
              std::wstring_view tail) {
    if (base.empty()) return;
    std::wstring raw = base;
    if (!tail.empty()) {
        if (raw.back() != L'\\' && raw.back() != L'/') raw += L'\\';
        raw += tail;
    }
    const auto n = normalize_path(raw);
    if (!n || !root_is_sane(c, *n, env)) return;
    for (const auto& e : out)
        if (equal_ci(e, *n)) return;
    out.push_back(*n);
}

// Yas kurali: min_age_days > 0 ve "simdi" biliniyorsa yalniz bu zamandan eski dosyalar adaydir.
// Bilinmeyen zaman (0) guvenli tarafta "yeni" sayilir (silinmez).
std::uint64_t min_filetime(const CleanupEnv& env, const CategoryInfo& ci) {
    if (ci.min_age_days == 0 || env.now_filetime == 0) return 0;
    const std::uint64_t age = static_cast<std::uint64_t>(ci.min_age_days) * 86400ull * 10000000ull;
    return env.now_filetime > age ? env.now_filetime - age : 0;
}

bool too_recent(std::uint64_t min_mtime, std::uint64_t last_write) {
    return min_mtime != 0 && (last_write == 0 || last_write >= min_mtime);
}

// Tarama butcesi: her girdiden once iptal, dosya sayisi ve sure kontrol edilir.
// Tek bir fs.list cagrisi bolunemez; bu yuzden sinir "girdiler arasi" olarak uygulanir.
struct ScanBudget {
    const PreviewOptions& opt;
    std::uint64_t start = 0;
    std::size_t files = 0;
    bool cancelled = false;
    bool stopped = false;

    std::uint64_t now() const {
        if (opt.now_ms) return opt.now_ms();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void begin() { start = now(); }
    bool tick() {
        if (cancelled || stopped) return false;
        if (opt.cancel && opt.cancel->load()) {
            cancelled = true;
            stopped = true;
            return false;
        }
        if (opt.max_files != 0 && files >= opt.max_files) {
            stopped = true;
            return false;
        }
        if (opt.time_budget_ms != 0 && now() - start >= opt.time_budget_ms) {
            stopped = true;
            return false;
        }
        return true;
    }
};

struct Walker {
    CategoryPlan& plan;
    CleanupFs& fs;
    FileFilter filter;
    ScanBudget& budget;
    std::uint64_t min_mtime = 0;

    void walk(std::size_t root_idx, const std::wstring& dir, std::size_t depth) {
        if (!budget.tick()) {
            plan.preview.partial = true;
            return;
        }
        auto entries = fs.list(dir);
        if (!entries.ok()) {
            ++plan.preview.skipped_unreadable;
            return;
        }
        const std::wstring& root = plan.roots[root_idx];
        for (const auto& e : entries.value()) {
            if (!budget.tick()) {
                plan.preview.partial = true;
                return;
            }
            if (e.name.empty() || e.name == L"." || e.name == L"..") continue;
            if (e.is_reparse) {
                ++plan.preview.skipped_reparse;  // symlink/junction: inilmez, silinmez
                continue;
            }
            const std::wstring path = dir + L"\\" + e.name;
            if (e.is_dir) {
                if (filter == FileFilter::ThumbcacheTopLevel) continue;
                if (depth + 1 > kMaxDepth || !is_path_allowed(root, path)) {
                    ++plan.preview.skipped_unreadable;
                    continue;
                }
                walk(root_idx, path, depth + 1);
                if (budget.stopped) return;  // yarim kalan dizin silinmez
                plan.dirs_post_order.push_back({root_idx, path});
                continue;
            }
            if (filter == FileFilter::ThumbcacheTopLevel && !is_thumbcache_name(e.name)) continue;
            if (too_recent(min_mtime, e.last_write)) {
                ++plan.preview.skipped_recent;
                continue;
            }
            if (!is_path_allowed(root, path)) {
                ++plan.preview.skipped_unreadable;
                continue;
            }
            plan.files.push_back(
                {root_idx, path, e.size, FileExpect{e.size, e.last_write, e.file_id, e.volume_serial, e.has_id}, e.allocation});
            ++plan.preview.files;
            plan.preview.bytes += e.size;
            plan.preview.allocated += e.allocation;
            ++budget.files;
        }
    }
};

bool same_volume(CleanupFs& fs, const std::wstring& path, const std::wstring& volume) {
    const auto v = fs.volume_of(path);
    return v && equal_ci(*v, volume);
}

std::optional<std::wstring> volume_parent(const std::wstring& raw) {
    const auto n = normalize_path(raw);
    if (!n || n->size() != 3 || (*n)[1] != L':' || (*n)[2] != L'\\') return std::nullopt;
    return n;
}

std::wstring child_path(const std::wstring& parent, std::wstring_view name) {
    std::wstring p = parent;
    if (p.empty() || p.back() != L'\\') p += L'\\';
    p += name;
    return p;
}

// EXT-CLN: urunun kendi test klasorleri. Yalniz birim kokunun DOGRUDAN altindaki
// "EcuFps-bench-<16 hex>" klasorleri; icerik yalniz bilinen uc dosya + gecerli isaret sihri.
// Boylece ayni adli kullanici klasoru veya yabanci icerik asla plana girmez.
CategoryPlan preview_app_tests(const CleanupEnv& env, CleanupFs& fs, const std::wstring& volume, ScanBudget& budget) {
    CategoryPlan plan;
    const auto& ci = info(CleanupCategory::AppTestFiles);
    plan.preview.category = CleanupCategory::AppTestFiles;
    plan.preview.needs_admin = false;
    const std::uint64_t min_mtime = min_filetime(env, ci);
    std::vector<std::wstring> parents;
    if (!volume.empty()) {
        if (auto v = volume_parent(volume)) parents.push_back(*v);
    } else {
        for (const auto& r : env.local_roots)
            if (auto v = volume_parent(r)) parents.push_back(*v);
    }
    if (parents.empty()) {
        plan.preview.status = PreviewStatus::NotPresent;
        plan.preview.detail = "no local volume";
        return plan;
    }
    bool any = false;
    std::set<std::wstring> seen;
    for (const auto& parent : parents) {
        if (!budget.tick()) {
            plan.preview.partial = true;
            break;
        }
        auto entries = fs.list(parent);
        if (!entries.ok()) {
            ++plan.preview.skipped_unreadable;
            continue;
        }
        for (const auto& e : entries.value()) {
            if (!budget.tick()) {
                plan.preview.partial = true;
                break;
            }
            if (e.name.empty() || !e.is_dir || e.is_reparse || !is_app_test_folder_name(e.name)) continue;
            const std::wstring folder = child_path(parent, e.name);
            if (!seen.insert(folder).second) continue;
            auto inner = fs.list(folder);
            if (!inner.ok()) {
                ++plan.preview.skipped_unreadable;
                continue;
            }
            bool known_only = true, has_marker = false, marker_ok = false;
            for (const auto& f : inner.value()) {
                if (f.is_dir || f.is_reparse) {
                    known_only = false;
                    break;
                }
                const bool marker = equal_ci(f.name, kAppTestMarkerName);
                if (!marker && !equal_ci(f.name, kAppTestDataName) && !equal_ci(f.name, kAppTestCacheName)) {
                    known_only = false;
                    break;
                }
                if (marker) {
                    has_marker = true;
                    auto c = fs.read_prefix(child_path(folder, f.name), kAppTestMarkerProbe);
                    marker_ok = c.ok() && c.value().rfind(std::string(kAppTestMarkerMagic), 0) == 0;
                }
            }
            if (!known_only || !has_marker || !marker_ok) {
                ++plan.preview.skipped_unverified;  // kullaniciya ait olabilir: dokunulmaz
                continue;
            }
            const std::size_t root_idx = plan.roots.size();
            plan.roots.push_back(folder);
            for (const auto& f : inner.value()) {
                if (too_recent(min_mtime, f.last_write)) {
                    ++plan.preview.skipped_recent;
                    continue;
                }
                const std::wstring path = child_path(folder, f.name);
                if (!is_path_allowed(folder, path)) {
                    ++plan.preview.skipped_unreadable;
                    continue;
                }
                plan.files.push_back(
                    {root_idx, path, f.size, FileExpect{f.size, f.last_write, f.file_id, f.volume_serial, f.has_id}, f.allocation});
                ++plan.preview.files;
                plan.preview.bytes += f.size;
                plan.preview.allocated += f.allocation;
                ++budget.files;
            }
            plan.remove_root_when_empty = true;
            any = true;
        }
        if (budget.stopped) break;
    }
    if (budget.stopped) plan.preview.partial = true;
    plan.preview.status = any ? PreviewStatus::Ready : PreviewStatus::NotPresent;
    if (!any) plan.preview.detail = "no app test folder";
    return plan;
}

CategoryPlan preview_one(CleanupCategory c, const CleanupEnv& env, CleanupFs& fs, const std::wstring& volume, ScanBudget& budget) {
    CategoryPlan plan;
    const auto& ci = info(c);
    plan.preview.category = c;
    plan.preview.needs_admin = ci.needs_admin;
    if (ci.needs_admin && !env.elevated) {
        plan.preview.status = PreviewStatus::NeedsAdmin;
        plan.preview.detail = "administrator required";
        return plan;
    }
    if (c == CleanupCategory::RecycleBin) {
        auto q = fs.query_recycle_bin(volume);
        if (!q.ok()) {
            plan.preview.status = PreviewStatus::Error;
            plan.preview.detail = q.error().detail;
            return plan;
        }
        plan.preview.status = PreviewStatus::Ready;
        plan.preview.files = static_cast<std::size_t>(q.value().items);
        plan.preview.bytes = q.value().bytes;
        return plan;
    }
    if (c == CleanupCategory::StandbyMemory) {
        // T15: boyut olculur; okunamazsa Error (tahmin yok). Birimden bagimsiz; dosya sayisi yok (files=0, bytes=bekleme boyutu).
        auto q = fs.standby_bytes();
        if (!q.ok()) {
            plan.preview.status = PreviewStatus::Error;
            plan.preview.detail = q.error().detail;
            return plan;
        }
        plan.preview.status = PreviewStatus::Ready;
        plan.preview.bytes = q.value();
        return plan;
    }
    if (c == CleanupCategory::AppTestFiles) return preview_app_tests(env, fs, volume, budget);
    plan.roots = category_roots(c, env);
    if (plan.roots.empty()) {
        plan.preview.status = PreviewStatus::NotPresent;
        plan.preview.detail = "no valid root";
        return plan;
    }
    if (!volume.empty()) {
        // EXT-CLN-01: yalniz secilen birimdeki kokler. Birim GetVolumePathName ile cozulur (baglama noktasi dahil).
        std::vector<std::wstring> on;
        for (auto& r : plan.roots)
            if (same_volume(fs, r, volume)) on.push_back(std::move(r));
        plan.roots = std::move(on);
        if (plan.roots.empty()) {
            plan.preview.status = PreviewStatus::OtherVolume;
            plan.preview.detail = "not on selected volume";
            return plan;
        }
    }
    bool any = false;
    Walker w{plan, fs, ci.filter, budget, min_filetime(env, ci)};
    for (std::size_t i = 0; i < plan.roots.size(); ++i) {
        if (!budget.tick()) {
            plan.preview.partial = true;
            break;
        }
        switch (fs.probe_dir(plan.roots[i])) {
            case DirState::Missing: break;
            case DirState::Reparse: ++plan.preview.skipped_reparse; break;  // kok bile baglanti ise inilmez
            case DirState::NotDirectory:
            case DirState::Denied: ++plan.preview.skipped_unreadable; break;
            case DirState::Directory:
                any = true;
                w.walk(i, plan.roots[i], 0);
                break;
        }
    }
    if (budget.stopped) plan.preview.partial = true;
    plan.preview.status = any ? PreviewStatus::Ready : PreviewStatus::NotPresent;
    if (!any) plan.preview.detail = "root not present";
    return plan;
}

std::string parent_volume_key(const std::wstring& v) {
    std::string s;
    for (wchar_t ch : v) s.push_back(static_cast<char>(fold(ch) & 0x7F));
    return s;
}

}  // namespace

const CategoryInfo& info(CleanupCategory c) noexcept { return kInfo[static_cast<std::size_t>(c)]; }

std::string_view to_string(CleanupCategory c) noexcept { return info(c).id; }

std::optional<std::wstring> normalize_path(std::wstring_view p) {
    std::wstring s(p);
    for (auto& ch : s)
        if (ch == L'/') ch = L'\\';
    if (s.rfind(L"\\\\?\\", 0) == 0) {
        s.erase(0, 4);
        if (starts_with_ci(s, L"UNC\\")) return std::nullopt;
    }
    if (s.size() < 3 || s[1] != L':' || s[2] != L'\\') return std::nullopt;
    const wchar_t d = fold(s[0]);
    if (d < L'a' || d > L'z') return std::nullopt;
    std::vector<std::wstring> parts;
    std::size_t i = 3;
    while (i < s.size()) {
        std::size_t j = s.find(L'\\', i);
        if (j == std::wstring::npos) j = s.size();
        std::wstring c = s.substr(i, j - i);
        i = j + 1;
        if (c.empty() || c == L".") continue;
        if (c == L"..") {
            if (parts.empty()) return std::nullopt;  // kokten yukari
            parts.pop_back();
            continue;
        }
        for (wchar_t ch : c)
            if (ch < 32 || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
                return std::nullopt;
        // Win32 sondaki nokta/boslugu atar (".. " -> ".."): belirsiz bilesen reddedilir.
        if (c.back() == L'.' || c.back() == L' ') return std::nullopt;
        parts.push_back(std::move(c));
    }
    std::wstring out;
    out.push_back(static_cast<wchar_t>(d - L'a' + L'A'));
    out += L":\\";
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k) out += L'\\';
        out += parts[k];
    }
    return out;
}

bool is_path_allowed(std::wstring_view root, std::wstring_view candidate) {
    const auto r = normalize_path(root);
    const auto c = normalize_path(candidate);
    if (!r || !c) return false;
    if (r->size() <= 3) return false;  // surucu koku asla izinli kok degildir
    const std::wstring prefix = folded(*r) + L"\\";
    const std::wstring cf = folded(*c);
    return cf.size() > prefix.size() && cf.compare(0, prefix.size(), prefix) == 0;
}

bool is_thumbcache_name(std::wstring_view name) {
    if (!ends_with_ci(name, L".db")) return false;
    return (starts_with_ci(name, L"thumbcache_") && name.size() > 14) || (starts_with_ci(name, L"iconcache_") && name.size() > 13);
}

bool is_app_test_folder_name(std::wstring_view name) noexcept {
    if (!starts_with_ci(name, kAppTestFolderPrefix)) return false;
    const std::size_t hex = name.size() - kAppTestFolderPrefix.size();
    if (hex != 16) return false;
    for (std::size_t i = kAppTestFolderPrefix.size(); i < name.size(); ++i) {
        const wchar_t c = fold(name[i]);
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    }
    return true;
}

std::vector<std::wstring> category_roots(CleanupCategory c, const CleanupEnv& env) {
    std::vector<std::wstring> out;
    if (const auto it = env.root_override.find(c); it != env.root_override.end()) {
        for (const auto& r : it->second) add_root(out, c, env, r, {});
        return out;
    }
    const auto& lad = env.local_app_data;
    switch (c) {
        case CleanupCategory::UserTemp: add_root(out, c, env, env.temp, {}); break;
        case CleanupCategory::WindowsTemp: add_root(out, c, env, env.windir, L"Temp"); break;
        case CleanupCategory::ShaderCacheDirectX: add_root(out, c, env, lad, L"D3DSCache"); break;
        case CleanupCategory::ShaderCacheNvidia:
            add_root(out, c, env, lad, L"NVIDIA\\DXCache");
            add_root(out, c, env, lad, L"NVIDIA\\GLCache");
            break;
        case CleanupCategory::ShaderCacheAmd:
            add_root(out, c, env, lad, L"AMD\\DxCache");
            add_root(out, c, env, lad, L"AMD\\DxcCache");
            break;
        case CleanupCategory::ThumbnailCache: add_root(out, c, env, lad, L"Microsoft\\Windows\\Explorer"); break;
        case CleanupCategory::WerReportsUser:
            add_root(out, c, env, lad, L"Microsoft\\Windows\\WER\\ReportArchive");
            add_root(out, c, env, lad, L"Microsoft\\Windows\\WER\\ReportQueue");
            break;
        case CleanupCategory::WerReportsSystem:
            add_root(out, c, env, env.program_data, L"Microsoft\\Windows\\WER\\ReportArchive");
            add_root(out, c, env, env.program_data, L"Microsoft\\Windows\\WER\\ReportQueue");
            break;
        case CleanupCategory::DeliveryOptimization:
            add_root(out, c, env, env.windir,
                     L"ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization\\Cache");
            break;
        case CleanupCategory::RecycleBin:
        case CleanupCategory::AppTestFiles:
        case CleanupCategory::StandbyMemory: break;  // kabuk API'si / birim koku taramasi / bellek sayaci preview_one'da
    }
    return out;
}

const CategoryPlan* CleanupPlan::find(CleanupCategory c) const noexcept {
    for (const auto& p : categories)
        if (p.preview.category == c) return &p;
    return nullptr;
}

CleanupPlan preview(const std::vector<CleanupCategory>& categories, const CleanupEnv& env, CleanupFs& fs,
                    const std::wstring& volume, const PreviewOptions& opt) {
    CleanupPlan plan;
    plan.elevated = env.elevated;
    plan.system_root = env.windir;
    if (!volume.empty()) {
        plan.volume.root = volume;
        if (auto id = fs.volume_id(volume)) {
            plan.volume = *id;
            plan.volume.root = volume;
        }
    }
    ScanBudget budget{opt};
    budget.begin();
    std::set<CleanupCategory> seen;
    for (auto c : categories) {
        if (!seen.insert(c).second) continue;
        if (budget.stopped || !budget.tick()) {
            // EXT-CLN: butce/iptal sonrasi kategori taranmadi; sayi UYDURULMAZ.
            CategoryPlan cp;
            cp.preview.category = c;
            cp.preview.needs_admin = info(c).needs_admin;
            cp.preview.status = PreviewStatus::NotScanned;
            cp.preview.detail = budget.cancelled ? "scan cancelled" : "scan budget reached";
            plan.categories.push_back(std::move(cp));
            continue;
        }
        if (!volume.empty() && !plan.volume.known) {
            // Kimligi okunamayan (ag/cikarilabilir/kaybolmus) birim: taranmaz, temizlenmez.
            CategoryPlan cp;
            cp.preview.category = c;
            cp.preview.needs_admin = info(c).needs_admin;
            cp.preview.status = PreviewStatus::Error;
            cp.preview.detail = "volume not verified";
            plan.categories.push_back(std::move(cp));
            continue;
        }
        plan.categories.push_back(preview_one(c, env, fs, volume, budget));
    }
    plan.scan_stopped = budget.stopped;
    plan.scan_cancelled = budget.cancelled;
    return plan;
}

std::vector<ListedFile> largest_files(const CategoryPlan& cp, std::size_t max_items) {
    std::vector<const PlannedFile*> v;
    v.reserve(cp.files.size());
    for (const auto& f : cp.files) v.push_back(&f);
    const std::size_t n = std::min(max_items, v.size());
    std::partial_sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n), v.end(), [](const PlannedFile* a, const PlannedFile* b) {
        return a->bytes != b->bytes ? a->bytes > b->bytes : a->path < b->path;
    });
    std::vector<ListedFile> out;
    for (std::size_t i = 0; i < n; ++i) {
        const auto* f = v[i];
        std::wstring rel = f->path;
        if (f->root < cp.roots.size()) {
            const auto& r = cp.roots[f->root];
            if (rel.size() > r.size() + 1 && starts_with_ci(rel, r)) rel = rel.substr(r.size() + 1);
        }
        out.push_back({std::move(rel), f->bytes});
    }
    return out;
}

std::uint64_t CleanupReport::bytes_deleted() const noexcept {
    std::uint64_t s = 0;
    for (const auto& c : categories) s += c.deleted.bytes;
    return s;
}

std::size_t CleanupReport::files_locked() const noexcept {
    std::size_t s = 0;
    for (const auto& c : categories) s += c.locked.files;
    return s;
}

contracts::Result<CleanupReport> run(const CleanupPlan& plan, const std::vector<CleanupCategory>& selected, CleanupFs& fs,
                                     const RunOptions& opt) {
    using R = contracts::Result<CleanupReport>;
    if (selected.empty()) return R::fail(ErrorCode::MissingValue, "no category selected");
    // Yetki kontrolu her seyden once: yonetici kategorisi secilmisse yukseltilmemis surecte hic baslanmaz.
    for (auto c : selected)
        if (info(c).needs_admin && !plan.elevated)
            return R::fail(ErrorCode::Denied, std::string("administrator required: ") + std::string(to_string(c)));
    std::vector<const CategoryPlan*> work;
    std::set<CleanupCategory> seen;
    for (auto c : selected) {
        if (!seen.insert(c).second) continue;
        if (c == CleanupCategory::StandbyMemory) continue;  // T15: dosya islemi degil; uygulama broker bakim isi olarak calistirir
        const auto* p = plan.find(c);
        if (!p) return R::fail(ErrorCode::InvalidTransition, std::string("category not previewed: ") + std::string(to_string(c)));
        if (p->preview.status != PreviewStatus::Ready)
            return R::fail(ErrorCode::InvalidTransition, std::string("category not ready: ") + std::string(to_string(c)));
        work.push_back(p);
    }

    // EXT-CLN-01: secilen birim hala ayni birim mi? (disk cikarildi/harf baska birime gecti -> hic baslama)
    if (!plan.volume.root.empty()) {
        const auto now = fs.volume_id(plan.volume.root);
        if (!plan.volume.known || !now || !now->known || now->serial != plan.volume.serial)
            return R::fail(ErrorCode::InvalidTransition, "volume changed");
    }

    CleanupReport rep;
    for (const auto* p : work) rep.total += (p->preview.category == CleanupCategory::RecycleBin) ? 1 : p->files.size();

    // Bos alan olcumu: secilen koklerin birimleri (Geri Donusum Kutusu icin sistem birimi).
    std::set<std::string> vol_keys;
    auto add_volume = [&](const std::wstring& path) {
        if (path.empty()) return;
        auto v = fs.volume_of(path);
        if (!v) return;
        if (!vol_keys.insert(parent_volume_key(*v)).second) return;
        rep.volumes.push_back({*v, fs.free_bytes(*v), std::nullopt});
    };
    for (const auto* p : work) {
        if (p->preview.category == CleanupCategory::RecycleBin) add_volume(plan.volume.root.empty() ? plan.system_root : plan.volume.root);
        for (const auto& r : p->roots) add_volume(r);
    }

    auto cancelled = [&] { return opt.cancel && opt.cancel->load(); };
    auto report = [&](CleanupCategory c) {
        if (opt.on_progress) opt.on_progress(CleanupProgress{c, rep.processed, rep.total});
    };

    for (const auto* p : work) {
        CategoryResult cr;
        cr.category = p->preview.category;
        report(cr.category);
        if (cancelled()) {
            rep.cancelled = true;
            rep.categories.push_back(cr);
            break;
        }
        if (cr.category == CleanupCategory::RecycleBin) {
            auto before = fs.query_recycle_bin(plan.volume.root);
            auto st = fs.empty_recycle_bin(plan.volume.root);
            auto after = fs.query_recycle_bin(plan.volume.root);
            if (st.ok() && before.ok() && after.ok()) {
                // Olculen fark (onizlemeden sonra degismis olabilir; tahmin yok).
                const auto& b = before.value();
                const auto& a = after.value();
                cr.deleted.files = static_cast<std::size_t>(b.items > a.items ? b.items - a.items : 0);
                cr.deleted.bytes = b.bytes > a.bytes ? b.bytes - a.bytes : 0;
                cr.locked.files = static_cast<std::size_t>(a.items);  // bosaltma sonrasi kalan
                cr.locked.bytes = a.bytes;
                cr.completed = true;
            } else {
                cr.other.files = 1;
            }
            ++rep.processed;
            report(cr.category);
            rep.categories.push_back(cr);
            continue;
        }
        bool stop = false;
        for (const auto& f : p->files) {
            if (cancelled()) {
                stop = true;
                break;
            }
            const std::wstring& root = p->roots[f.root];
            if (!is_path_allowed(root, f.path)) {
                cr.other.add(f.bytes);
            } else {
                const auto d = fs.delete_file(root, f.path, f.expect);
                switch (d.outcome) {
                    case DeleteOutcome::Deleted: cr.deleted.add(d.bytes); break;
                    case DeleteOutcome::Locked: cr.locked.add(f.bytes); break;
                    case DeleteOutcome::Denied: cr.denied.add(f.bytes); break;
                    case DeleteOutcome::Missing: cr.missing.add(f.bytes); break;
                    case DeleteOutcome::Reparse: cr.reparse.add(f.bytes); break;
                    case DeleteOutcome::Changed: cr.changed.add(f.bytes); break;
                    case DeleteOutcome::HardLink:
                    case DeleteOutcome::OutsideRoot:
                    case DeleteOutcome::Error: cr.other.add(f.bytes); break;
                }
            }
            ++rep.processed;
            report(cr.category);
        }
        if (stop) {
            rep.cancelled = true;
            rep.categories.push_back(cr);
            break;
        }
        // Bosalan alt dizinler (en derin once). Kok asla kaldirilmaz; bos olmayan dizin kalir.
        for (const auto& d : p->dirs_post_order) {
            const std::wstring& root = p->roots[d.root];
            if (is_path_allowed(root, d.path) && fs.remove_empty_dir(root, d.path)) ++cr.dirs_removed;
        }
        // AppTestFiles: kok zaten sahipligi dogrulanmis test klasorudur; bos kaldiysa kendisi de kaldirilir.
        if (p->remove_root_when_empty)
            for (const auto& root : p->roots)
                if (fs.remove_owned_empty_dir(root)) ++cr.dirs_removed;
        cr.completed = true;
        rep.categories.push_back(cr);
    }

    for (auto& v : rep.volumes) v.free_after = fs.free_bytes(v.volume);
    return rep;
}

// ---- FakeCleanupFs ----

void FakeCleanupFs::add_dir(const std::wstring& path, bool reparse) {
    for (std::size_t i = 3; i < path.size(); ++i)
        if (path[i] == L'\\') nodes_.try_emplace(path.substr(0, i), Node{true, false, false, false, 0});
    auto& n = nodes_[path];
    n.is_dir = true;
    if (reparse) n.is_reparse = true;  // sonradan eklenen alt dosya isareti silmez
}

void FakeCleanupFs::add_file(const std::wstring& path, std::uint64_t size, bool locked, std::string content) {
    const auto k = path.find_last_of(L'\\');
    if (k != std::wstring::npos && k > 2) add_dir(path.substr(0, k));
    nodes_[path] = Node{false, false, locked, false, size, ++next_id_, 1, std::move(content)};
}

void FakeCleanupFs::replace_file(const std::wstring& path, std::uint64_t size) {
    if (auto* n = node(path)) n->size = size, n->id = ++next_id_, ++n->mtime;
}

void FakeCleanupFs::touch(const std::wstring& path) {
    if (auto* n = node(path)) ++n->mtime;
}

FakeCleanupFs::Node* FakeCleanupFs::node(const std::wstring& path) {
    const auto it = nodes_.find(path);
    return it == nodes_.end() ? nullptr : &it->second;
}

DirState FakeCleanupFs::probe_dir(const std::wstring& path) {
    const auto* n = node(path);
    if (!n) return DirState::Missing;
    if (n->is_reparse) return DirState::Reparse;
    if (!n->is_dir) return DirState::NotDirectory;
    if (n->denied) return DirState::Denied;
    return DirState::Directory;
}

contracts::Result<std::vector<FsEntry>> FakeCleanupFs::list(const std::wstring& dir) {
    using R = contracts::Result<std::vector<FsEntry>>;
    ++list_calls;
    if (on_list) on_list(dir);
    const auto* n = node(dir);
    if (!n) return R::fail(ErrorCode::MissingValue, "missing");
    if (n->is_reparse || n->denied) return R::fail(ErrorCode::Denied, "denied");
    if (!n->is_dir) return R::fail(ErrorCode::InvalidTransition, "not a directory");
    std::vector<FsEntry> out;
    std::wstring prefix = dir;
    if (prefix.empty() || prefix.back() != L'\\') prefix += L'\\';  // birim koku zaten '\' ile biter
    for (auto it = nodes_.lower_bound(prefix); it != nodes_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it) {
        const std::wstring rest = it->first.substr(prefix.size());
        if (rest.empty() || rest.find(L'\\') != std::wstring::npos) continue;
        const std::uint64_t alloc = it->second.is_dir ? 0 : (it->second.size + 4095) / 4096 * 4096;
        out.push_back({rest, it->second.is_dir, it->second.is_reparse, it->second.size, it->second.mtime, it->second.id, 7, true, alloc});
    }
    return out;
}

DeleteResult FakeCleanupFs::delete_file(const std::wstring& root, const std::wstring& path, const FileExpect& expect) {
    ++delete_calls;
    if (on_delete) on_delete(path);
    if (!is_path_allowed(root, path)) return {DeleteOutcome::OutsideRoot, 0};
    // Ara dizinlerden biri sonradan baglantiya donusmusse (gercekte son yol kok disina cikar) reddedilir.
    for (std::size_t i = root.size() + 1; i < path.size(); ++i)
        if (path[i] == L'\\')
            if (const auto* a = node(path.substr(0, i)); a && a->is_reparse) return {DeleteOutcome::Reparse, 0};
    auto* n = node(path);
    if (!n) return {DeleteOutcome::Missing, 0};
    if (n->is_reparse) return {DeleteOutcome::Reparse, 0};
    if (n->is_dir) return {DeleteOutcome::Error, 0};
    if (n->locked) return {DeleteOutcome::Locked, 0};
    if (n->denied) return {DeleteOutcome::Denied, 0};
    if ((expect.has_id && expect.file_id != n->id) || expect.bytes != n->size || expect.last_write != n->mtime)
        return {DeleteOutcome::Changed, 0};  // taramadan sonra degisti: dokunulmaz
    const std::uint64_t sz = n->size;
    nodes_.erase(path);
    if (auto v = volume_of(path); v) {
        if (auto it = free_space.find(*v); it != free_space.end()) it->second += sz;
    }
    return {DeleteOutcome::Deleted, sz};
}

bool FakeCleanupFs::remove_empty_dir(const std::wstring& root, const std::wstring& path) {
    if (!is_path_allowed(root, path)) return false;
    const auto* n = node(path);
    if (!n || !n->is_dir || n->is_reparse) return false;
    const std::wstring prefix = path + L"\\";
    const auto it = nodes_.lower_bound(prefix);
    if (it != nodes_.end() && it->first.compare(0, prefix.size(), prefix) == 0) return false;
    nodes_.erase(path);
    return true;
}

bool FakeCleanupFs::remove_owned_empty_dir(const std::wstring& path) {
    const auto* n = node(path);
    if (!n || !n->is_dir || n->is_reparse) return false;
    const std::wstring prefix = path + L"\\";
    const auto it = nodes_.lower_bound(prefix);
    if (it != nodes_.end() && it->first.compare(0, prefix.size(), prefix) == 0) return false;
    nodes_.erase(path);
    return true;
}

contracts::Result<std::string> FakeCleanupFs::read_prefix(const std::wstring& path, std::size_t max_bytes) {
    using R = contracts::Result<std::string>;
    const auto* n = node(path);
    if (!n) return R::fail(ErrorCode::MissingValue, "missing");
    if (n->is_dir || n->is_reparse) return R::fail(ErrorCode::Denied, "not a plain file");
    return n->content.substr(0, max_bytes);
}

std::optional<std::wstring> FakeCleanupFs::volume_of(const std::wstring& path) {
    if (path.size() < 3 || path[1] != L':') return std::nullopt;
    return path.substr(0, 3);
}

std::optional<std::uint64_t> FakeCleanupFs::free_bytes(const std::wstring& volume) {
    const auto it = free_space.find(volume);
    if (it == free_space.end()) return std::nullopt;
    return it->second;
}

std::optional<VolumeId> FakeCleanupFs::volume_id(const std::wstring& volume) {
    const auto it = volumes.find(volume);
    if (it == volumes.end()) return std::nullopt;
    return it->second;
}

contracts::Result<RecycleBinInfo> FakeCleanupFs::query_recycle_bin(const std::wstring& volume) {
    last_recycle_volume = volume;
    return recycle;
}

contracts::Result<std::uint64_t> FakeCleanupFs::standby_bytes() {
    if (!standby_value) return contracts::Result<std::uint64_t>::fail(contracts::ErrorCode::IoError, "standby unreadable");
    return *standby_value;
}

contracts::Status FakeCleanupFs::empty_recycle_bin(const std::wstring& volume) {
    last_recycle_volume = volume;
    recycle = {};
    recycle_emptied = true;
    return contracts::ok_status();
}

}  // namespace ef::core::cleanup
