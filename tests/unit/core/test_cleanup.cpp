// WIN-08: disk temizligi cekirdegi (sahte dosya sistemi; gercek dosya silinmez).
#include <atomic>

#include "benchmark/storage/storage_rules.h"
#include "core/cleanup/app_files.h"
#include "core/cleanup/cleanup.h"
#include "ef_test.h"

using namespace ef::core::cleanup;
using ef::contracts::ErrorCode;

namespace {
CleanupEnv test_env() {
    CleanupEnv env;
    env.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    env.temp = L"C:\\Users\\u\\AppData\\Local\\Temp";
    env.windir = L"C:\\Windows";
    env.program_data = L"C:\\ProgramData";
    return env;
}
const std::wstring kTemp = L"C:\\Users\\u\\AppData\\Local\\Temp";
}  // namespace

EF_TEST(cleanup_allow_list_basic_and_prefix_trick) {
    EF_CHECK(is_path_allowed(L"C:\\Temp", L"C:\\Temp\\a.txt"));
    EF_CHECK(is_path_allowed(L"C:\\Temp", L"C:\\Temp\\sub\\b.txt"));
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp2\\a.txt"));   // onek hilesi
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp"));           // kokun kendisi
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\"));
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"D:\\Temp\\a.txt"));
    EF_CHECK(!is_path_allowed(L"C:\\", L"C:\\Windows\\a.txt"));     // surucu koku izinli kok olamaz
}

EF_TEST(cleanup_allow_list_dotdot_and_case) {
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\..\\Windows\\system.ini"));
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\sub\\..\\..\\Windows\\x"));
    EF_CHECK(is_path_allowed(L"C:\\Temp", L"C:\\Temp\\sub\\..\\b.txt"));  // kok icinde kaliyor
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\..\\..\\Temp\\a"));
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\.. \\Windows\\x"));  // Win32 sondaki boslugu atar
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\x.\\a"));
    EF_CHECK(is_path_allowed(L"C:\\TEMP", L"c:\\temp\\A.TXT"));
    EF_CHECK(is_path_allowed(L"c:/temp/", L"C:\\Temp\\.\\a.txt"));
    EF_CHECK(is_path_allowed(L"C:\\Temp", L"\\\\?\\C:\\Temp\\a.txt"));
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"\\\\?\\UNC\\srv\\C$\\Temp\\a"));
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\a.txt:stream"));  // ADS
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"Temp\\a.txt"));             // goreli
    EF_CHECK(!is_path_allowed(L"C:\\Temp", L"C:\\Temp\\*"));
    // ASCII disi harfte katlama yok: fark varsa guvenli tarafta red.
    EF_CHECK(is_path_allowed(L"C:\\Gecici\\\u00C7", L"C:\\Gecici\\\u00C7\\a"));
    EF_CHECK(!is_path_allowed(L"C:\\Gecici\\\u00C7", L"C:\\Gecici\\\u00E7\\a"));
}

EF_TEST(cleanup_normalize_path) {
    EF_CHECK(normalize_path(L"c:\\a\\.\\b\\..\\c").value_or(L"") == L"C:\\a\\c");
    EF_CHECK(normalize_path(L"C:\\").value_or(L"x") == L"C:\\");
    EF_CHECK(!normalize_path(L"C:\\..").has_value());
    EF_CHECK(!normalize_path(L"\\\\server\\share\\x").has_value());
    EF_CHECK(!normalize_path(L"").has_value());
}

EF_TEST(cleanup_category_metadata) {
    for (auto c : kAllCategories) {
        const auto& i = info(c);
        EF_CHECK(i.category == c);
        EF_CHECK(i.title_key.rfind("cleanup.", 0) == 0);
        EF_CHECK(i.desc_key.size() > i.title_key.size());
        if (!i.path_documented) EF_CHECK(!i.default_selected);  // dogrulanmamis yol varsayilan secili olamaz
        if (i.needs_admin) EF_CHECK(!i.default_selected);
        if (c != CleanupCategory::RecycleBin) EF_CHECK(i.regenerable);
    }
    EF_CHECK(!info(CleanupCategory::RecycleBin).default_selected);
    EF_CHECK(!info(CleanupCategory::RecycleBin).regenerable);
    EF_CHECK(info(CleanupCategory::WindowsTemp).needs_admin);
    EF_CHECK(info(CleanupCategory::WerReportsSystem).needs_admin);
    EF_CHECK(info(CleanupCategory::DeliveryOptimization).needs_admin);
    EF_CHECK(!info(CleanupCategory::UserTemp).needs_admin);
    EF_CHECK(info(CleanupCategory::UserTemp).default_selected);
}

EF_TEST(cleanup_roots_from_env_and_sanity) {
    auto env = test_env();
    auto r = category_roots(CleanupCategory::ShaderCacheNvidia, env);
    EF_CHECK(r.size() == 2);
    EF_CHECK(r[0] == L"C:\\Users\\u\\AppData\\Local\\NVIDIA\\DXCache");
    EF_CHECK(category_roots(CleanupCategory::WindowsTemp, env).at(0) == L"C:\\Windows\\Temp");
    EF_CHECK(category_roots(CleanupCategory::RecycleBin, env).empty());
    // Hatali TEMP (surucu koku / Temp adi olmayan klasor / korunan klasor) kok olamaz.
    env.temp = L"C:\\";
    EF_CHECK(category_roots(CleanupCategory::UserTemp, env).empty());
    env.temp = L"C:\\Users\\u\\Documents";
    EF_CHECK(category_roots(CleanupCategory::UserTemp, env).empty());
    env.temp = L"C:\\Users\\u\\AppData\\Local";
    EF_CHECK(category_roots(CleanupCategory::UserTemp, env).empty());
    env.temp = L"C:\\Users\\u\\AppData\\Local\\Temp\\..\\..\\Temp";  // C:\Users\u\AppData\Temp: ad kurali gecerli
    EF_CHECK(category_roots(CleanupCategory::UserTemp, env).size() == 1);
    env.temp.clear();
    EF_CHECK(category_roots(CleanupCategory::UserTemp, env).empty());
    env.root_override[CleanupCategory::UserTemp] = {L"D:\\probe\\Temp"};
    EF_CHECK(category_roots(CleanupCategory::UserTemp, env).at(0) == L"D:\\probe\\Temp");
}

EF_TEST(cleanup_preview_counts_and_reparse_refusal) {
    FakeCleanupFs fs;
    fs.add_file(kTemp + L"\\a.tmp", 100);
    fs.add_file(kTemp + L"\\sub\\b.tmp", 250);
    fs.add_file(kTemp + L"\\sub\\deep\\c.tmp", 7);
    fs.add_dir(kTemp + L"\\link", /*reparse=*/true);  // junction: inilmez
    fs.add_file(kTemp + L"\\link\\victim.doc", 999);    // baglanti hedefi icerigi sayilmaz
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    const auto* p = plan.find(CleanupCategory::UserTemp);
    EF_CHECK(p != nullptr);
    EF_CHECK(p->preview.status == PreviewStatus::Ready);
    EF_CHECK(p->preview.files == 3);
    EF_CHECK(p->preview.bytes == 357);
    EF_CHECK(p->preview.skipped_reparse == 1);
    for (const auto& f : p->files) EF_CHECK(f.path.find(L"victim") == std::wstring::npos);
    EF_CHECK(fs.delete_calls == 0);  // onizleme salt okumadir
    EF_CHECK(fs.exists(kTemp + L"\\a.tmp"));
}

EF_TEST(cleanup_root_reparse_is_refused) {
    FakeCleanupFs fs;
    fs.add_dir(kTemp, /*reparse=*/true);
    fs.add_file(kTemp + L"\\a.tmp", 5);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    const auto& pv = plan.find(CleanupCategory::UserTemp)->preview;
    EF_CHECK(pv.status == PreviewStatus::NotPresent);
    EF_CHECK(pv.skipped_reparse == 1);
    EF_CHECK(pv.files == 0);
}

EF_TEST(cleanup_run_skips_locked_and_measures) {
    FakeCleanupFs fs;
    fs.free_space[L"C:\\"] = 1000;
    fs.add_file(kTemp + L"\\a.tmp", 100);
    fs.add_file(kTemp + L"\\in_use.tmp", 40, /*locked=*/true);
    fs.add_file(kTemp + L"\\sub\\b.tmp", 250);
    fs.add_dir(kTemp + L"\\emptydir");
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(r.ok());
    const auto& rep = r.value();
    const auto& c = rep.categories.at(0);
    EF_CHECK(c.deleted.files == 2);
    EF_CHECK(c.deleted.bytes == 350);
    EF_CHECK(c.locked.files == 1);
    EF_CHECK(c.locked.bytes == 40);
    EF_CHECK(c.deleted.bytes + c.locked.bytes == plan.find(CleanupCategory::UserTemp)->preview.bytes);
    EF_CHECK(fs.exists(kTemp + L"\\in_use.tmp"));
    EF_CHECK(!fs.exists(kTemp + L"\\sub"));       // bosalan alt dizin kaldirildi
    EF_CHECK(!fs.exists(kTemp + L"\\emptydir"));
    EF_CHECK(fs.exists(kTemp));                   // kok korunur
    EF_CHECK(c.completed);
    EF_CHECK(rep.volumes.size() == 1);
    EF_CHECK(rep.volumes[0].free_before.value_or(0) == 1000);
    EF_CHECK(rep.volumes[0].free_after.value_or(0) == 1350);
    EF_CHECK(rep.bytes_deleted() == 350);
    EF_CHECK(rep.files_locked() == 1);
}

EF_TEST(cleanup_run_only_previewed_files) {
    FakeCleanupFs fs;
    fs.add_file(kTemp + L"\\a.tmp", 1);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    fs.add_file(kTemp + L"\\new_after_preview.tmp", 9);  // onaylanmamis dosya
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(r.ok());
    EF_CHECK(fs.exists(kTemp + L"\\new_after_preview.tmp"));
    EF_CHECK(r.value().categories.at(0).deleted.files == 1);
}

EF_TEST(cleanup_run_dir_swapped_to_junction_after_preview) {
    FakeCleanupFs fs;
    fs.add_file(kTemp + L"\\sub\\b.tmp", 3);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    fs.node(kTemp + L"\\sub")->is_reparse = true;  // saldirgan alt dizini baglantiya cevirdi
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(r.ok());
    EF_CHECK(r.value().categories.at(0).reparse.files == 1);
    EF_CHECK(r.value().categories.at(0).deleted.files == 0);
    EF_CHECK(fs.exists(kTemp + L"\\sub\\b.tmp"));
}

EF_TEST(cleanup_progress_real_counts) {
    FakeCleanupFs fs;
    for (int i = 0; i < 5; ++i) fs.add_file(kTemp + L"\\f" + std::to_wstring(i) + L".tmp", 10);
    auto env = test_env();
    env.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    fs.add_file(env.local_app_data + L"\\D3DSCache\\x\\s.bin", 20);
    auto plan = preview({CleanupCategory::UserTemp, CleanupCategory::ShaderCacheDirectX}, env, fs);
    std::vector<CleanupProgress> seen;
    RunOptions opt;
    opt.on_progress = [&](const CleanupProgress& p) { seen.push_back(p); };
    auto r = run(plan, {CleanupCategory::UserTemp, CleanupCategory::ShaderCacheDirectX}, fs, opt);
    EF_CHECK(r.ok());
    EF_CHECK(!seen.empty());
    for (const auto& p : seen) EF_CHECK(p.total == 6);
    std::size_t last = 0;
    for (const auto& p : seen) {
        EF_CHECK(p.done >= last);  // monoton
        last = p.done;
    }
    EF_CHECK(seen.back().done == 6);
    EF_CHECK(seen.back().percent() == 100);
    EF_CHECK(r.value().processed == 6);
}

EF_TEST(cleanup_cancel_mid_run) {
    FakeCleanupFs fs;
    for (int i = 0; i < 10; ++i) fs.add_file(kTemp + L"\\f" + std::to_wstring(i) + L".tmp", 1);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    std::atomic<bool> cancel{false};
    fs.on_delete = [&](const std::wstring&) {
        if (fs.delete_calls == 3) cancel = true;
    };
    RunOptions opt;
    opt.cancel = &cancel;
    auto r = run(plan, {CleanupCategory::UserTemp}, fs, opt);
    EF_CHECK(r.ok());
    EF_CHECK(r.value().cancelled);
    EF_CHECK(fs.delete_calls == 3);
    EF_CHECK(r.value().processed == 3);
    EF_CHECK(r.value().categories.at(0).deleted.files == 3);  // silinenler gercek sayi ile raporlanir
    EF_CHECK(!r.value().categories.at(0).completed);
}

EF_TEST(cleanup_admin_category_denied_when_not_elevated) {
    FakeCleanupFs fs;
    fs.add_file(L"C:\\Windows\\Temp\\x.tmp", 10);
    auto env = test_env();
    env.elevated = false;
    auto plan = preview({CleanupCategory::WindowsTemp, CleanupCategory::UserTemp}, env, fs);
    const auto& pv = plan.find(CleanupCategory::WindowsTemp)->preview;
    EF_CHECK(pv.status == PreviewStatus::NeedsAdmin);
    EF_CHECK(pv.needs_admin);
    EF_CHECK(pv.files == 0 && pv.bytes == 0);  // olculmeyen sayi gosterilmez
    auto r = run(plan, {CleanupCategory::UserTemp, CleanupCategory::WindowsTemp}, fs);
    EF_CHECK(!r.ok());
    EF_CHECK(r.code() == ErrorCode::Denied);
    EF_CHECK(fs.delete_calls == 0);  // hicbir sey silinmedi
    EF_CHECK(fs.exists(L"C:\\Windows\\Temp\\x.tmp"));

    env.elevated = true;
    auto plan2 = preview({CleanupCategory::WindowsTemp}, env, fs);
    EF_CHECK(plan2.find(CleanupCategory::WindowsTemp)->preview.files == 1);
    auto r2 = run(plan2, {CleanupCategory::WindowsTemp}, fs);
    EF_CHECK(r2.ok());
    EF_CHECK(!fs.exists(L"C:\\Windows\\Temp\\x.tmp"));
}

EF_TEST(cleanup_recycle_bin_explicit_only) {
    FakeCleanupFs fs;
    fs.recycle = {3, 3000};
    fs.add_file(kTemp + L"\\a.tmp", 1);
    auto plan = preview({CleanupCategory::UserTemp, CleanupCategory::RecycleBin}, test_env(), fs);
    EF_CHECK(plan.find(CleanupCategory::RecycleBin)->preview.files == 3);
    EF_CHECK(plan.find(CleanupCategory::RecycleBin)->preview.bytes == 3000);
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);  // kutu secilmedi
    EF_CHECK(r.ok());
    EF_CHECK(!fs.recycle_emptied);
    EF_CHECK(fs.recycle.items == 3);
    auto r2 = run(plan, {CleanupCategory::RecycleBin}, fs);
    EF_CHECK(r2.ok());
    EF_CHECK(fs.recycle_emptied);
    EF_CHECK(r2.value().categories.at(0).deleted.bytes == 3000);
    EF_CHECK(r2.value().categories.at(0).deleted.files == 3);
}

EF_TEST(cleanup_run_rejects_bad_selection) {
    FakeCleanupFs fs;
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);  // kok yok -> NotPresent
    auto r = run(plan, {}, fs);
    EF_CHECK(!r.ok() && r.code() == ErrorCode::MissingValue);
    auto r2 = run(plan, {CleanupCategory::ShaderCacheAmd}, fs);  // onizlenmemis
    EF_CHECK(!r2.ok() && r2.code() == ErrorCode::InvalidTransition);
    auto r3 = run(plan, {CleanupCategory::UserTemp}, fs);  // hazir degil
    EF_CHECK(!r3.ok() && r3.code() == ErrorCode::InvalidTransition);
}

EF_TEST(cleanup_thumbcache_filter) {
    EF_CHECK(is_thumbcache_name(L"thumbcache_256.db"));
    EF_CHECK(is_thumbcache_name(L"IconCache_idx.DB"));
    EF_CHECK(!is_thumbcache_name(L"thumbcache_.db"));
    EF_CHECK(!is_thumbcache_name(L"thumbcache_1.db.bak"));
    EF_CHECK(!is_thumbcache_name(L"ExplorerStartupLog.etl"));
    FakeCleanupFs fs;
    const std::wstring ex = L"C:\\Users\\u\\AppData\\Local\\Microsoft\\Windows\\Explorer";
    fs.add_file(ex + L"\\thumbcache_32.db", 10);
    fs.add_file(ex + L"\\iconcache_16.db", 20, /*locked=*/true);
    fs.add_file(ex + L"\\other.dat", 30);
    fs.add_file(ex + L"\\sub\\thumbcache_64.db", 40);  // alt klasor kapsam disi
    auto plan = preview({CleanupCategory::ThumbnailCache}, test_env(), fs);
    const auto* p = plan.find(CleanupCategory::ThumbnailCache);
    EF_CHECK(p->preview.files == 2);
    EF_CHECK(p->preview.bytes == 30);
    auto r = run(plan, {CleanupCategory::ThumbnailCache}, fs);
    EF_CHECK(r.ok());
    EF_CHECK(fs.exists(ex + L"\\other.dat"));
    EF_CHECK(fs.exists(ex + L"\\sub\\thumbcache_64.db"));
    EF_CHECK(fs.exists(ex + L"\\sub"));  // alt klasor dizin listesinde yok -> kaldirilmaz
    EF_CHECK(r.value().categories.at(0).locked.files == 1);
}

EF_TEST(cleanup_skips_files_changed_after_preview) {
    // Handoff v1 EXT-CLN-02: onaydan sonra silmeden hemen once kimlik/boyut/zaman tekrar kontrol edilir.
    FakeCleanupFs fs;
    fs.add_file(kTemp + L"\\same.tmp", 10);
    fs.add_file(kTemp + L"\\replaced.tmp", 10);
    fs.add_file(kTemp + L"\\touched.tmp", 10);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs);
    fs.replace_file(kTemp + L"\\replaced.tmp", 10);  // ayni ad + ayni boyut, farkli dosya
    fs.touch(kTemp + L"\\touched.tmp");             // icerik yeniden yazildi
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(r.ok());
    EF_CHECK(!fs.exists(kTemp + L"\\same.tmp"));
    EF_CHECK(fs.exists(kTemp + L"\\replaced.tmp") && fs.exists(kTemp + L"\\touched.tmp"));
    EF_CHECK(r.value().categories[0].changed.files == 2 && r.value().categories[0].deleted.files == 1);
}

// ---- Handoff v1 EXT-CLN-01/03: birim secimi, birim kimligi, ayrilan alan, dosya listesi ----

namespace {
VolumeId vol(const wchar_t* root, std::uint32_t serial) {
    VolumeId v;
    v.root = root;
    v.serial = serial;
    v.cluster = 4096;
    v.fs = L"NTFS";
    v.known = true;
    return v;
}
}  // namespace

EF_TEST(cleanup_volume_filter_other_volume_and_recycle_per_volume) {
    FakeCleanupFs fs;
    fs.volumes[L"C:\\"] = vol(L"C:\\", 0xC0C0);
    fs.volumes[L"D:\\"] = vol(L"D:\\", 0xD0D0);
    fs.recycle = {2, 500};
    fs.add_file(kTemp + L"\\a.tmp", 10);
    auto plan = preview({CleanupCategory::UserTemp, CleanupCategory::RecycleBin}, test_env(), fs, L"D:\\");
    EF_CHECK(plan.volume.known && plan.volume.serial == 0xD0D0 && plan.volume.root == L"D:\\");
    EF_CHECK(plan.find(CleanupCategory::UserTemp)->preview.status == PreviewStatus::OtherVolume);
    EF_CHECK(plan.find(CleanupCategory::UserTemp)->files.empty());
    EF_CHECK(fs.last_recycle_volume == L"D:\\");  // yalniz D: kutusu sorgulandi
    EF_CHECK(plan.find(CleanupCategory::RecycleBin)->preview.status == PreviewStatus::Ready);
    // Bu birimde olmayan kategori calistirilamaz
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(!r.ok() && fs.exists(kTemp + L"\\a.tmp"));
    auto r2 = run(plan, {CleanupCategory::RecycleBin}, fs);
    EF_CHECK(r2.ok() && fs.recycle_emptied && fs.last_recycle_volume == L"D:\\");
    EF_CHECK(r2.value().volumes.size() == 1 && r2.value().volumes[0].volume == L"D:\\");
    // C: secilirse kullanici Temp taranir
    auto plan_c = preview({CleanupCategory::UserTemp}, test_env(), fs, L"C:\\");
    EF_CHECK(plan_c.find(CleanupCategory::UserTemp)->preview.status == PreviewStatus::Ready);
    EF_CHECK(plan_c.find(CleanupCategory::UserTemp)->preview.files == 1);
}

EF_TEST(cleanup_volume_changed_after_scan_deletes_nothing) {
    FakeCleanupFs fs;
    fs.volumes[L"C:\\"] = vol(L"C:\\", 1);
    fs.add_file(kTemp + L"\\a.tmp", 10);
    fs.add_file(kTemp + L"\\b.tmp", 20);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs, L"C:\\");
    EF_CHECK(plan.find(CleanupCategory::UserTemp)->preview.files == 2);
    fs.volumes[L"C:\\"].serial = 2;  // ayni harf, baska birim
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(!r.ok() && r.error().detail == "volume changed");
    EF_CHECK(fs.delete_calls == 0 && fs.exists(kTemp + L"\\a.tmp") && fs.exists(kTemp + L"\\b.tmp"));
    fs.volumes.erase(L"C:\\");  // birim kayboldu
    auto r2 = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(!r2.ok() && fs.delete_calls == 0);
}

EF_TEST(cleanup_unverified_volume_is_not_scanned) {
    FakeCleanupFs fs;  // birim kimligi yok (ag/cikarilabilir benzetimi)
    fs.add_file(kTemp + L"\\a.tmp", 10);
    fs.recycle = {1, 1};
    auto plan = preview({CleanupCategory::UserTemp, CleanupCategory::RecycleBin}, test_env(), fs, L"C:\\");
    EF_CHECK(!plan.volume.known);
    for (const auto& c : plan.categories) EF_CHECK(c.preview.status == PreviewStatus::Error && c.files.empty());
    EF_CHECK(fs.last_recycle_volume.empty());  // kutu bile sorgulanmadi
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(!r.ok() && fs.delete_calls == 0);
}

EF_TEST(cleanup_allocation_and_largest_files_list) {
    FakeCleanupFs fs;
    fs.volumes[L"C:\\"] = vol(L"C:\\", 1);
    fs.add_file(kTemp + L"\\small.tmp", 10);
    fs.add_file(kTemp + L"\\sub\\big.tmp", 9000);
    fs.add_file(kTemp + L"\\mid.tmp", 5000);
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs, L"C:\\");
    const auto* cp = plan.find(CleanupCategory::UserTemp);
    EF_CHECK(cp->preview.bytes == 14010);
    EF_CHECK(cp->preview.allocated == 4096 + 12288 + 8192);  // kume yuvarlamasi (sahte: 4 KiB)
    auto top = largest_files(*cp, 2);
    EF_CHECK(top.size() == 2);
    EF_CHECK(top[0].relative == L"sub\\big.tmp" && top[0].bytes == 9000);  // kok goreli yol
    EF_CHECK(top[1].relative == L"mid.tmp");
    EF_CHECK(largest_files(*cp, 50).size() == 3);
    // Liste salt okumadir: hicbir dosya silinmedi
    EF_CHECK(fs.delete_calls == 0 && fs.exists(kTemp + L"\\small.tmp"));
}

// ---- EXT-CLN: tarama iptali, sure/dosya butcesi, yas kurali, uygulama test dosyalari ----

namespace {
constexpr std::uint64_t kDayFiletime = 86400ull * 10000000ull;  // FILETIME 100 ns
}  // namespace

EF_TEST(cleanup_temp_age_rule_skips_recent_and_unknown) {
    FakeCleanupFs fs;
    auto env = test_env();
    env.now_filetime = 1000ull * kDayFiletime;
    fs.add_file(kTemp + L"\\old.tmp", 100);
    fs.node(kTemp + L"\\old.tmp")->mtime = env.now_filetime - 8 * kDayFiletime;
    fs.add_file(kTemp + L"\\new.tmp", 200);
    fs.node(kTemp + L"\\new.tmp")->mtime = env.now_filetime - 1 * kDayFiletime;
    fs.add_file(kTemp + L"\\unknown.tmp", 300);
    fs.node(kTemp + L"\\unknown.tmp")->mtime = 0;  // zaman bilinmiyor: silinmez
    auto plan = preview({CleanupCategory::UserTemp}, env, fs);
    const auto& pv = plan.find(CleanupCategory::UserTemp)->preview;
    EF_CHECK(pv.status == PreviewStatus::Ready);
    EF_CHECK(pv.files == 1 && pv.bytes == 100);
    EF_CHECK(pv.skipped_recent == 2);
    auto r = run(plan, {CleanupCategory::UserTemp}, fs);
    EF_CHECK(r.ok());
    EF_CHECK(!fs.exists(kTemp + L"\\old.tmp"));
    EF_CHECK(fs.exists(kTemp + L"\\new.tmp") && fs.exists(kTemp + L"\\unknown.tmp"));
    // "simdi" bilinmiyorsa yas kurali uygulanmaz (eski davranis; test varsayilani)
    env.now_filetime = 0;
    auto plan2 = preview({CleanupCategory::UserTemp}, env, fs);
    EF_CHECK(plan2.find(CleanupCategory::UserTemp)->preview.files == 2);
}

EF_TEST(cleanup_scan_cancel_stops_and_marks_partial_without_fake_counts) {
    FakeCleanupFs fs;
    auto env = test_env();
    env.elevated = true;
    fs.add_file(kTemp + L"\\sub\\a.tmp", 10);
    fs.add_file(L"C:\\Windows\\Temp\\w.tmp", 20);
    std::atomic<bool> cancel{false};
    fs.on_list = [&](const std::wstring&) {
        if (fs.list_calls == 1) cancel = true;  // ilk list bitince kullanici iptal etti
    };
    PreviewOptions opt;
    opt.cancel = &cancel;
    auto plan = preview({CleanupCategory::UserTemp, CleanupCategory::WindowsTemp}, env, fs, {}, opt);
    EF_CHECK(plan.scan_cancelled && plan.scan_stopped);
    const auto* ut = plan.find(CleanupCategory::UserTemp);
    EF_CHECK(ut->preview.status == PreviewStatus::Ready && ut->preview.files == 0 && ut->preview.partial);
    const auto* wt = plan.find(CleanupCategory::WindowsTemp);
    EF_CHECK(wt->preview.status == PreviewStatus::NotScanned && wt->preview.files == 0);
    EF_CHECK(fs.delete_calls == 0 && fs.exists(kTemp + L"\\sub\\a.tmp"));
}

EF_TEST(cleanup_scan_file_budget_stops_and_leaves_rest_not_scanned) {
    FakeCleanupFs fs;
    for (int i = 0; i < 5; ++i) fs.add_file(kTemp + L"\\f" + std::to_wstring(i) + L".tmp", 1);
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Microsoft\\Windows\\Explorer\\thumbcache_32.db", 7);
    PreviewOptions opt;
    opt.max_files = 2;
    auto plan = preview({CleanupCategory::UserTemp, CleanupCategory::ThumbnailCache}, test_env(), fs, {}, opt);
    EF_CHECK(plan.scan_stopped && !plan.scan_cancelled);
    const auto* ut = plan.find(CleanupCategory::UserTemp);
    EF_CHECK(ut->preview.files == 2 && ut->preview.partial);
    const auto* th = plan.find(CleanupCategory::ThumbnailCache);
    EF_CHECK(th->preview.status == PreviewStatus::NotScanned && th->preview.files == 0);
    EF_CHECK(fs.delete_calls == 0);
}

EF_TEST(cleanup_scan_time_budget_uses_injected_clock) {
    FakeCleanupFs fs;
    fs.add_file(kTemp + L"\\a.tmp", 10);
    std::uint64_t clock = 0;
    PreviewOptions opt;
    opt.time_budget_ms = 100;
    opt.now_ms = [&] { return clock; };
    fs.on_list = [&](const std::wstring&) { clock += 200; };  // tek list butceyi asar
    auto plan = preview({CleanupCategory::UserTemp}, test_env(), fs, {}, opt);
    EF_CHECK(plan.scan_stopped && plan.scan_cancelled == false);
    const auto& pv = plan.find(CleanupCategory::UserTemp)->preview;
    EF_CHECK(pv.files == 0 && pv.partial);
    EF_CHECK(fs.delete_calls == 0 && fs.exists(kTemp + L"\\a.tmp"));
}

EF_TEST(cleanup_app_test_files_only_verified_folders_and_age) {
    FakeCleanupFs fs;
    auto env = test_env();
    env.local_roots = {L"D:\\"};
    fs.add_dir(L"D:\\");  // gercek dosya sisteminde birim koku vardir
    env.now_filetime = 1000ull * kDayFiletime;
    const auto old = env.now_filetime - 30 * kDayFiletime;
    const auto recent = env.now_filetime - 1 * kDayFiletime;
    const std::wstring root = L"D:\\";
    const std::wstring good = root + L"EcuFps-bench-0123456789abcdef";
    fs.add_file(good + L"\\itx-bench.marker", 20, false, "EcuFps-bench-v1 pid=42");
    fs.add_file(good + L"\\test.bin", 4096, false, "test");
    fs.add_file(good + L"\\cache.bin", 2048, false, "cache");
    for (const wchar_t* n : {L"itx-bench.marker", L"test.bin", L"cache.bin"}) fs.node(good + L"\\" + n)->mtime = old;
    // Yabanci icerik: sahiplenilmez.
    const std::wstring foreign = root + L"EcuFps-bench-aaaaaaaaaaaaaaaa";
    fs.add_file(foreign + L"\\itx-bench.marker", 20, false, "EcuFps-bench-v1");
    fs.add_file(foreign + L"\\user.dat", 10);
    // Yanlis isaret icerigi: sahiplenilmez.
    const std::wstring bad_marker = root + L"EcuFps-bench-bbbbbbbbbbbbbbbb";
    fs.add_file(bad_marker + L"\\itx-bench.marker", 20, false, "not-ours");
    fs.add_file(bad_marker + L"\\test.bin", 10);
    // Alt klasor: sahiplenilmez.
    const std::wstring nested = root + L"EcuFps-bench-cccccccccccccccc";
    fs.add_file(nested + L"\\itx-bench.marker", 20, false, "EcuFps-bench-v1");
    fs.add_file(nested + L"\\sub\\test.bin", 10);
    // 7 gunden yeni dosyalar korunur (klasor kalir).
    const std::wstring fresh = root + L"EcuFps-bench-dddddddddddddddd";
    fs.add_file(fresh + L"\\itx-bench.marker", 20, false, "EcuFps-bench-v1");
    fs.add_file(fresh + L"\\test.bin", 10, false, "test");
    fs.node(fresh + L"\\itx-bench.marker")->mtime = recent;
    fs.node(fresh + L"\\test.bin")->mtime = recent;
    auto plan = preview({CleanupCategory::AppTestFiles}, env, fs);
    const auto* cp = plan.find(CleanupCategory::AppTestFiles);
    EF_CHECK(cp->preview.status == PreviewStatus::Ready);
    EF_CHECK(cp->preview.files == 3 && cp->preview.bytes == 20 + 4096 + 2048);
    EF_CHECK(cp->preview.skipped_unverified == 3);
    EF_CHECK(cp->preview.skipped_recent == 2);
    EF_CHECK(cp->roots.size() == 2 && cp->roots[0] == good && cp->roots[1] == fresh);
    auto r = run(plan, {CleanupCategory::AppTestFiles}, fs);
    EF_CHECK(r.ok());
    EF_CHECK(!fs.exists(good));  // bosalan sahipli klasor kaldirildi
    EF_CHECK(fs.exists(foreign + L"\\user.dat") && fs.exists(bad_marker + L"\\test.bin"));
    EF_CHECK(fs.exists(nested + L"\\sub\\test.bin"));
    EF_CHECK(fs.exists(fresh + L"\\test.bin"));  // yeni dosya silinmedi
    EF_CHECK(r.value().categories.at(0).deleted.files == 3 && r.value().categories.at(0).dirs_removed == 1);
    EF_CHECK(fs.delete_calls == 3);  // yalniz dogrulanmis 3 dosya
}

EF_TEST(cleanup_app_test_names_match_benchmark_contract) {
    namespace bs = ef::benchmark::storage;
    EF_CHECK(kAppTestFolderPrefix == bs::kFolderPrefix);
    EF_CHECK(kAppTestMarkerName == bs::kMarkerName);
    EF_CHECK(kAppTestDataName == bs::kTestFileName);
    EF_CHECK(kAppTestCacheName == bs::kCacheFileName);
    EF_CHECK(kAppTestMarkerMagic == bs::kMarkerMagic);
    for (std::uint64_t id : {0ull, 1ull, 0xDEADBEEFCAFEull}) EF_CHECK(is_app_test_folder_name(bs::bench_folder_name(id)));
    EF_CHECK(!is_app_test_folder_name(L"EcuFps-bench-"));
    EF_CHECK(!is_app_test_folder_name(L"EcuFps-bench-0123456789abcde"));   // 15 hex
    EF_CHECK(!is_app_test_folder_name(L"EcuFps-bench-0123456789abcdef0"));  // 17 hex
    EF_CHECK(!is_app_test_folder_name(L"EcuFps-bench-0123456789abcdeg"));
    EF_CHECK(is_app_test_folder_name(L"EcuFps-bench-0123456789ABCDEF"));  // buyuk harf katlanir: gecerli
}

EF_TEST(cleanup_shader_caches_stay_off_in_product_v1) {
    EF_CHECK(!kShaderCacheEnabledInV1);
    bool has_app = false;
    for (auto c : kProductCategories) {
        EF_CHECK(c != CleanupCategory::ShaderCacheDirectX && c != CleanupCategory::ShaderCacheNvidia &&
                  c != CleanupCategory::ShaderCacheAmd);
        has_app |= c == CleanupCategory::AppTestFiles;
    }
    EF_CHECK(has_app);
    EF_CHECK(info(CleanupCategory::UserTemp).min_age_days == 7);
    EF_CHECK(info(CleanupCategory::AppTestFiles).min_age_days == 7);
    EF_CHECK(!info(CleanupCategory::AppTestFiles).default_selected);
}
