#include "core/cleanup/session.h"
#include "ef_test.h"

using namespace ef::core::cleanup;

namespace {
CleanupEnv env_for(FakeCleanupFs&) {
    CleanupEnv e;
    e.local_app_data = L"C:\\Users\\u\\AppData\\Local";
    e.temp = L"C:\\Users\\u\\AppData\\Local\\Temp";
    e.windir = L"C:\\Windows";
    e.program_data = L"C:\\ProgramData";
    return e;
}
}  // namespace

EF_TEST(cleanup_session_requires_preview_and_confirm) {
    FakeCleanupFs fs;
    fs.add_dir(L"C:\\Users\\u\\AppData\\Local\\Temp");
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp", 1000);
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Temp\\b.tmp", 3000);
    const auto env = env_for(fs);
    std::vector<std::function<void()>> q;
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const PreviewOptions& o) { return preview(c, env, fs, v, o); };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    auto drain = [&] { while (!q.empty()) { auto f = std::move(q.front()); q.erase(q.begin()); f(); } };
    CleanupSession s(d);
    s.request_run();
    EF_CHECK(s.phase() == SessionPhase::Idle);  // onizlemesiz calistirma yok
    s.scan();
    EF_CHECK(s.phase() == SessionPhase::Scanning);
    drain();
    EF_CHECK(s.phase() == SessionPhase::Ready && s.selected(CleanupCategory::UserTemp) && s.selected_bytes() == 4000);
    EF_CHECK(!s.selected(CleanupCategory::RecycleBin) && !s.selectable(CleanupCategory::WindowsTemp));  // yonetici gerekir
    s.request_run();
    EF_CHECK(s.phase() == SessionPhase::Confirming && fs.exists(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp"));
    s.answer(false);
    EF_CHECK(s.phase() == SessionPhase::Ready && fs.exists(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp"));  // Hayir: silinmedi
    s.request_run();
    s.answer(true);
    drain();
    EF_CHECK(s.phase() == SessionPhase::Done && s.report() && s.report()->bytes_deleted() == 4000);
    EF_CHECK(!fs.exists(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp") && fs.exists(L"C:\\Users\\u\\AppData\\Local\\Temp"));
    EF_CHECK(s.progress().done == s.progress().total && s.progress().total == 2);
}

EF_TEST(cleanup_session_nothing_selected_does_not_confirm) {
    FakeCleanupFs fs;
    const auto env = env_for(fs);
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const PreviewOptions& o) { return preview(c, env, fs, v, o); };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [](std::function<void()> f) { f(); };
    d.post_ui = [](std::function<void()> f) { f(); };
    CleanupSession s(d);
    s.scan();
    s.set_selected(CleanupCategory::UserTemp, false);
    s.request_run();
    EF_CHECK(s.phase() == SessionPhase::Ready);
}

EF_TEST(cleanup_session_never_scans_shader_caches_in_v1) {
    // Handoff v1 06_DISK_TEMIZLIGI: shader cache ilk surumde haric; oturum onu taramaz, secilemez.
    FakeCleanupFs fs;
    fs.add_dir(L"C:\\Users\\u\\AppData\\Local\\D3DSCache");
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\D3DSCache\\x.bin", 5000);
    const auto env = env_for(fs);
    std::vector<std::function<void()>> q;
    std::vector<CleanupCategory> asked;
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const PreviewOptions& o) {
        asked = c;
        return preview(c, env, fs, v, o);
    };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    CleanupSession s(d);
    s.scan();
    while (!q.empty()) {
        auto f = std::move(q.front());
        q.erase(q.begin());
        f();
    }
    EF_CHECK(!asked.empty());
    for (auto c : asked)
        EF_CHECK(c != CleanupCategory::ShaderCacheDirectX && c != CleanupCategory::ShaderCacheNvidia && c != CleanupCategory::ShaderCacheAmd);
    s.set_selected(CleanupCategory::ShaderCacheDirectX, true);
    EF_CHECK(!s.selected(CleanupCategory::ShaderCacheDirectX) && s.selected_bytes() == 0);
}

EF_TEST(cleanup_session_volume_change_rescans_and_file_list_toggles) {
    FakeCleanupFs fs;
    VolumeId c;
    c.root = L"C:\\", c.serial = 1, c.known = true;
    VolumeId dd = c;
    dd.root = L"D:\\", dd.serial = 2;
    fs.volumes[L"C:\\"] = c;
    fs.volumes[L"D:\\"] = dd;
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp", 1000);
    const auto env = env_for(fs);
    std::vector<std::wstring> scanned;
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& cats, const std::wstring& v, const PreviewOptions& o) {
        scanned.push_back(v);
        return preview(cats, env, fs, v, o);
    };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [](std::function<void()> f) { f(); };
    d.post_ui = [](std::function<void()> f) { f(); };
    CleanupSession s(d);
    s.set_volume(L"C:\\");
    EF_CHECK(s.phase() == SessionPhase::Idle && scanned.empty());  // Idle: secim tarama baslatmaz
    s.scan();
    EF_CHECK(scanned.size() == 1 && scanned[0] == L"C:\\" && s.selected_bytes() == 1000);
    s.toggle_files(CleanupCategory::UserTemp);
    EF_CHECK(s.files_open() && *s.files_open() == CleanupCategory::UserTemp);
    s.toggle_files(CleanupCategory::UserTemp);
    EF_CHECK(!s.files_open());
    s.toggle_files(CleanupCategory::UserTemp);
    s.set_volume(L"D:\\");  // plan gecersiz: yeniden taranir, liste kapanir
    EF_CHECK(scanned.size() == 2 && scanned[1] == L"D:\\" && !s.files_open());
    EF_CHECK(s.plan().find(CleanupCategory::UserTemp)->preview.status == PreviewStatus::OtherVolume);
    EF_CHECK(!s.selectable(CleanupCategory::UserTemp) && s.selected_bytes() == 0);
    // Birim degisti -> calistirma basarisiz, neden kaydedilir, dosya yerinde
    s.set_volume(L"C:\\");
    fs.volumes[L"C:\\"].serial = 9;
    s.request_run();
    s.answer(true);
    EF_CHECK(s.phase() == SessionPhase::Failed && s.failure() == "volume changed");
    EF_CHECK(fs.exists(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp"));
}

// ---- WIN-08 ek: yonetici kategori onizlemeleri (yukseltilmis yardimci) ----

EF_TEST(cleanup_session_adopts_admin_previews_and_scan_invalidates_them) {
    FakeCleanupFs fs;
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp", 1000);
    const auto env = env_for(fs);
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const PreviewOptions& o) { return preview(c, env, fs, v, o); };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [](std::function<void()> f) { f(); };
    d.post_ui = [](std::function<void()> f) { f(); };
    CleanupSession s(d);
    s.scan();
    EF_CHECK(s.phase() == SessionPhase::Ready);
    EF_CHECK(!s.selectable(CleanupCategory::WindowsTemp));  // yukseltilmemis tarama: yetki yok
    EF_CHECK(!s.admin_previewed(CleanupCategory::WindowsTemp));

    // Yardimcinin olctugu onizleme plana islenir; sayilar yardimcidan (uydurma yok).
    CleanupPreview wp;
    wp.category = CleanupCategory::WindowsTemp;
    wp.status = PreviewStatus::Ready;
    wp.files = 5;
    wp.bytes = 5000;
    wp.needs_admin = true;
    CleanupPreview dop;
    dop.category = CleanupCategory::DeliveryOptimization;
    dop.status = PreviewStatus::NotPresent;  // bos kategori: yetki verilmez, secilemez
    // Bos ama Ready: yetki alinir yine de secilemez (dosya yok)
    CleanupPreview er;
    er.category = CleanupCategory::WerReportsSystem;
    er.status = PreviewStatus::Ready;
    er.files = 0;
    er.bytes = 0;
    s.adopt_admin_previews({wp, dop, er});
    EF_CHECK(s.admin_previewed(CleanupCategory::WindowsTemp));
    EF_CHECK(s.selectable(CleanupCategory::WindowsTemp));
    EF_CHECK(!s.admin_previewed(CleanupCategory::DeliveryOptimization));
    EF_CHECK(!s.selectable(CleanupCategory::DeliveryOptimization));
    EF_CHECK(s.admin_previewed(CleanupCategory::WerReportsSystem));
    EF_CHECK(!s.selectable(CleanupCategory::WerReportsSystem));
    s.set_selected(CleanupCategory::WindowsTemp, true);
    EF_CHECK(s.selected(CleanupCategory::WindowsTemp));
    EF_CHECK(s.selected_bytes() == 6000);  // kullanici 1000 + yonetici 5000

    // Izin listesi disi kategori adopt edilmez (plan bozulmaz).
    CleanupPreview bad;
    bad.category = CleanupCategory::UserTemp;
    bad.status = PreviewStatus::Ready;
    bad.files = 1;
    bad.bytes = 1;
    s.adopt_admin_previews({bad});
    EF_CHECK(s.plan().find(CleanupCategory::UserTemp)->preview.bytes == 1000);

    // Yetki sifirlanirsa NeedsAdmin'e doner; secim artik sayilmaz (silme yolu kapali).
    s.clear_admin_previews();
    EF_CHECK(!s.admin_previewed(CleanupCategory::WindowsTemp));
    EF_CHECK(!s.selectable(CleanupCategory::WindowsTemp));
    EF_CHECK(s.selected_bytes() == 1000);

    // Yeni tarama eski yonetici yetkisini kullanmaz.
    s.adopt_admin_previews({wp});
    EF_CHECK(s.selectable(CleanupCategory::WindowsTemp));
    s.scan();
    EF_CHECK(s.phase() == SessionPhase::Ready);
    EF_CHECK(!s.admin_previewed(CleanupCategory::WindowsTemp));
    EF_CHECK(!s.selectable(CleanupCategory::WindowsTemp));
}

// ---- EXT-CLN: tarama iptali + sure/dosya butcesi ----

EF_TEST(cleanup_session_scan_cancel_ignores_stale_result_and_never_deletes) {
    FakeCleanupFs fs;
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp", 1000);
    const auto env = env_for(fs);
    std::vector<std::function<void()>> q;
    const std::atomic<bool>* seen_cancel = nullptr;
    std::size_t seen_max_files = 0;
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const PreviewOptions& o) {
        seen_cancel = o.cancel;
        seen_max_files = o.max_files;
        auto p = preview(c, env, fs, v, o);
        p.scan_stopped = true;
        p.scan_cancelled = true;
        return p;
    };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    d.post_ui = [&](std::function<void()> f) { q.push_back(std::move(f)); };
    auto drain = [&] {
        while (!q.empty()) {
            auto f = std::move(q.front());
            q.erase(q.begin());
            f();
        }
    };
    CleanupSession s(d);
    s.scan();
    EF_CHECK(s.phase() == SessionPhase::Scanning);
    EF_CHECK(seen_cancel == nullptr);  // is parcacigi daha kosmadi (sirada)
    s.cancel_scan();
    EF_CHECK(s.phase() == SessionPhase::Idle && s.scan_cancelled());
    EF_CHECK(s.plan().categories.empty());  // iptal edilen plan gosterilmez
    drain();                                  // gecikmis is parcacigi isi + sonuc postasi
    EF_CHECK(s.phase() == SessionPhase::Idle);  // eski sonuc yok sayildi
    EF_CHECK(s.plan().categories.empty() && !s.selected_bytes());
    EF_CHECK(fs.delete_calls == 0 && fs.exists(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp"));  // silme yok
    // Yeniden tarama temiz baslar.
    s.scan();
    EF_CHECK(s.phase() == SessionPhase::Scanning && seen_cancel != nullptr && seen_max_files == kDefaultScanMaxFiles);
    EF_CHECK(!s.scan_cancelled());
}

EF_TEST(cleanup_session_scan_budget_reports_partial_without_fake_counts) {
    FakeCleanupFs fs;
    fs.add_file(L"C:\\Users\\u\\AppData\\Local\\Temp\\a.tmp", 1000);
    const auto env = env_for(fs);
    std::size_t seen_max_files = 0;
    std::uint64_t seen_budget_ms = 0;
    SessionDeps d;
    d.preview = [&](const std::vector<CleanupCategory>& c, const std::wstring& v, const PreviewOptions& o) {
        seen_max_files = o.max_files;
        seen_budget_ms = o.time_budget_ms;
        CleanupPlan p = preview(c, env, fs, v, o);
        p.scan_stopped = true;
        p.scan_cancelled = false;
        // Butce nedeniyle kategoriye hic girilmedi: sayi yok, durum NotScanned.
        CategoryPlan ns;
        ns.preview.category = CleanupCategory::RecycleBin;
        ns.preview.status = PreviewStatus::NotScanned;
        ns.preview.detail = "scan budget reached";
        for (auto& cp : p.categories)
            if (cp.preview.category == CleanupCategory::RecycleBin) cp = ns;
        return p;
    };
    d.run = [&](const CleanupPlan& p, const std::vector<CleanupCategory>& s, const RunOptions& o) { return run(p, s, fs, o); };
    d.run_background = [](std::function<void()> f) { f(); };
    d.post_ui = [](std::function<void()> f) { f(); };
    CleanupSession s(d);
    s.scan();
    EF_CHECK(s.phase() == SessionPhase::Ready);
    EF_CHECK(seen_max_files == kDefaultScanMaxFiles && seen_budget_ms == kDefaultScanBudgetMs);
    EF_CHECK(s.plan().scan_stopped && !s.plan().scan_cancelled);
    const auto* rb = s.plan().find(CleanupCategory::RecycleBin);
    EF_CHECK(rb && rb->preview.status == PreviewStatus::NotScanned && rb->preview.files == 0);
}
