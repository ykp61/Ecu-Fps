// WIN-08 ek: yonetici temizlik protokolu (ASCII JSON) birim testleri.
// Kapsam: izin listesi, her encode/decode ciftinin round-trip'i, kotu girdi reddi, rapor birlestirme.
#include "core/cleanup/admin_transfer.h"
#include "ef_test.h"

using namespace ef::core::cleanup;
using ef::contracts::ErrorCode;

EF_TEST(admin_transfer_allow_list_and_ids) {
    const auto& cats = admin_product_categories();
    EF_CHECK(cats.size() == 3);
    EF_CHECK(is_admin_product_category(CleanupCategory::WindowsTemp));
    EF_CHECK(is_admin_product_category(CleanupCategory::WerReportsSystem));
    EF_CHECK(is_admin_product_category(CleanupCategory::DeliveryOptimization));
    EF_CHECK(!is_admin_product_category(CleanupCategory::UserTemp));
    EF_CHECK(!is_admin_product_category(CleanupCategory::RecycleBin));
    EF_CHECK(!is_admin_product_category(CleanupCategory::AppTestFiles));
    const auto c = category_from_id("wer_system");
    EF_CHECK(c && *c == CleanupCategory::WerReportsSystem);
    EF_CHECK(category_from_id("windows_temp") == CleanupCategory::WindowsTemp);
    EF_CHECK(category_from_id("delivery_optimization") == CleanupCategory::DeliveryOptimization);
    EF_CHECK(!category_from_id("no_such_category"));
    EF_CHECK(!category_from_id(""));
}

EF_TEST(admin_transfer_scan_request_roundtrip_and_rejects) {
    AdminScanRequest r;
    r.categories = {CleanupCategory::WindowsTemp, CleanupCategory::DeliveryOptimization};
    r.volume = "C:\\";
    const auto back = decode_admin_scan_request(encode_admin_scan_request(r));
    EF_CHECK(back.ok());
    EF_CHECK(back.value().categories == r.categories);
    EF_CHECK(back.value().volume == "C:\\");
    // Bos birim de gecerli (birim filtresi yok)
    AdminScanRequest none;
    none.categories = {CleanupCategory::WerReportsSystem};
    const auto nb = decode_admin_scan_request(encode_admin_scan_request(none));
    EF_CHECK(nb.ok() && nb.value().volume.empty());
    // "D:/" -> "D:\" (kategori kimligi ve birim koku ASCII kalir)
    AdminScanRequest slash;
    slash.categories = {CleanupCategory::WindowsTemp};
    slash.volume = "D:/";
    const auto sb = decode_admin_scan_request(encode_admin_scan_request(slash));
    EF_CHECK(sb.ok() && sb.value().volume == "D:\\");
    // Izin listesi disi kategori reddedilir
    AdminScanRequest bad_cat;
    bad_cat.categories = {CleanupCategory::UserTemp};
    EF_CHECK(decode_admin_scan_request(encode_admin_scan_request(bad_cat)).code() == ErrorCode::InvalidEnum);
    // Bos liste reddedilir
    AdminScanRequest empty;
    EF_CHECK(decode_admin_scan_request(encode_admin_scan_request(empty)).code() == ErrorCode::OutOfRange);
    // Tekrarli kategori reddedilir
    AdminScanRequest dup;
    dup.categories = {CleanupCategory::WindowsTemp, CleanupCategory::WindowsTemp};
    EF_CHECK(decode_admin_scan_request(encode_admin_scan_request(dup)).code() == ErrorCode::InvalidEnum);
    // Birim yalniz "X:\" koku olabilir
    AdminScanRequest bad_vol;
    bad_vol.categories = {CleanupCategory::WindowsTemp};
    bad_vol.volume = "C:\\Win";
    EF_CHECK(decode_admin_scan_request(encode_admin_scan_request(bad_vol)).code() == ErrorCode::ParseError);
    // Bilinmeyen alan reddedilir
    EF_CHECK(!decode_admin_scan_request("{\"v\":1,\"categories\":[\"windows_temp\"],\"volume\":\"\",\"x\":1}").ok());
    // Bozuk JSON / eksik alan reddedilir
    EF_CHECK(!decode_admin_scan_request("{").ok());
    EF_CHECK(!decode_admin_scan_request("{\"v\":1,\"categories\":[\"windows_temp\"]}").ok());
    // Desteklenenden yeni surum tahmin edilmez
    EF_CHECK(decode_admin_scan_request("{\"v\":2,\"categories\":[\"windows_temp\"],\"volume\":\"\"}").code() ==
              ErrorCode::SchemaTooNew);
}

EF_TEST(admin_transfer_scan_result_roundtrip_and_rejects) {
    CleanupPreview p;
    p.category = CleanupCategory::WindowsTemp;
    p.status = PreviewStatus::Ready;
    p.files = 12;
    p.bytes = 4096;
    p.allocated = 8192;
    p.skipped_reparse = 2;
    p.partial = true;
    CleanupPreview q;
    q.category = CleanupCategory::WerReportsSystem;
    q.status = PreviewStatus::NotPresent;
    const auto back = decode_admin_scan_result(encode_admin_scan_result({p, q}));
    EF_CHECK(back.ok() && back.value().size() == 2);
    const auto& a = back.value()[0];
    EF_CHECK(a.category == CleanupCategory::WindowsTemp && a.status == PreviewStatus::Ready);
    EF_CHECK(a.files == 12 && a.bytes == 4096 && a.allocated == 8192);
    EF_CHECK(a.skipped_reparse == 2 && a.partial);
    EF_CHECK(a.needs_admin);  // yardimcidan gelen sayilar yonetici kategorisine aittir
    EF_CHECK(back.value()[1].category == CleanupCategory::WerReportsSystem);
    EF_CHECK(back.value()[1].status == PreviewStatus::NotPresent);
    // Izin listesi disi kategori reddedilir
    CleanupPreview bad;
    bad.category = CleanupCategory::UserTemp;
    bad.status = PreviewStatus::Ready;
    EF_CHECK(decode_admin_scan_result(encode_admin_scan_result({bad})).code() == ErrorCode::InvalidEnum);
    // Yanlis kind (baska cerceve tipi) reddedilir
    EF_CHECK(!decode_admin_scan_result("{\"kind\":\"progress\",\"categories\":[]}").ok());
}

EF_TEST(admin_transfer_run_request_roundtrip_and_rejects) {
    AdminRunRequest r;
    r.categories = {CleanupCategory::WerReportsSystem};
    const auto back = decode_admin_run_request(encode_admin_run_request(r));
    EF_CHECK(back.ok() && back.value().categories == r.categories);
    AdminRunRequest bad;
    bad.categories = {CleanupCategory::UserTemp};
    EF_CHECK(decode_admin_run_request(encode_admin_run_request(bad)).code() == ErrorCode::InvalidEnum);
    EF_CHECK(!decode_admin_run_request("{\"v\":1}").ok());  // kategorisiz istek yok
}

EF_TEST(admin_transfer_progress_roundtrip_and_rejects) {
    AdminProgress p;
    p.category = CleanupCategory::DeliveryOptimization;
    p.done = 3;
    p.total = 10;
    const auto back = decode_admin_progress(encode_admin_progress(p));
    EF_CHECK(back.ok());
    EF_CHECK(back.value().category == p.category && back.value().done == 3 && back.value().total == 10);
    // Kullanici kategorisi ilerleme olarak bildirilemez
    EF_CHECK(!decode_admin_progress("{\"kind\":\"progress\",\"id\":\"user_temp\",\"done\":1,\"total\":2}").ok());
}

EF_TEST(admin_transfer_report_roundtrip_and_rejects) {
    CleanupReport r;
    CategoryResult cr;
    cr.category = CleanupCategory::WindowsTemp;
    cr.deleted.files = 2;
    cr.deleted.bytes = 700;
    cr.locked.files = 1;
    cr.locked.bytes = 100;
    cr.changed.files = 1;
    cr.dirs_removed = 3;
    cr.completed = true;
    r.categories.push_back(cr);
    VolumeSpace v;
    v.volume = L"C:\\";
    v.free_before = std::uint64_t{1000};
    v.free_after = std::uint64_t{1700};
    r.volumes.push_back(v);
    VolumeSpace v2;
    v2.volume = L"D:\\";
    v2.free_before = std::uint64_t{50};  // after yok: null olarak tasinir (tahmin uretilmez)
    r.volumes.push_back(v2);
    r.cancelled = true;
    r.processed = 2;
    r.total = 5;
    const auto back = decode_admin_report(encode_admin_report(r));
    EF_CHECK(back.ok());
    const auto& b = back.value();
    EF_CHECK(b.categories.size() == 1 && b.categories[0].category == CleanupCategory::WindowsTemp);
    EF_CHECK(b.categories[0].deleted.files == 2 && b.categories[0].deleted.bytes == 700);
    EF_CHECK(b.categories[0].locked.files == 1 && b.categories[0].locked.bytes == 100);
    EF_CHECK(b.categories[0].changed.files == 1);
    EF_CHECK(b.categories[0].dirs_removed == 3 && b.categories[0].completed);
    EF_CHECK(b.volumes.size() == 2);
    EF_CHECK(b.volumes[0].volume == L"C:\\" && b.volumes[0].free_before && *b.volumes[0].free_before == 1000);
    EF_CHECK(b.volumes[0].free_after && *b.volumes[0].free_after == 1700);
    EF_CHECK(b.volumes[1].volume == L"D:\\" && !b.volumes[1].free_after);
    EF_CHECK(b.cancelled && b.processed == 2 && b.total == 5);
    // Birim koku olmayan rapor reddedilir ("C:\Windows" 8 karakter sinirini de asar)
    CleanupReport bad;
    VolumeSpace bv;
    bv.volume = L"C:\\Windows";
    bad.volumes.push_back(bv);
    EF_CHECK(!decode_admin_report(encode_admin_report(bad)).ok());
    // Izin listesi disi kategori reddedilir
    CleanupReport bad2;
    CategoryResult bc;
    bc.category = CleanupCategory::UserTemp;
    bad2.categories.push_back(bc);
    EF_CHECK(decode_admin_report(encode_admin_report(bad2)).code() == ErrorCode::InvalidEnum);
}

EF_TEST(admin_transfer_merge_reports) {
    CleanupReport first;
    CategoryResult a;
    a.category = CleanupCategory::WindowsTemp;
    first.categories.push_back(a);
    VolumeSpace ca;
    ca.volume = L"C:\\";
    ca.free_before = std::uint64_t{100};
    ca.free_after = std::uint64_t{150};
    first.volumes.push_back(ca);
    first.processed = 1;

    CleanupReport second;
    CategoryResult b;
    b.category = CleanupCategory::DeliveryOptimization;
    second.categories.push_back(b);
    VolumeSpace cb;
    cb.volume = L"C:\\";
    cb.free_before = std::uint64_t{999};  // ilk "once" degeri korunur
    cb.free_after = std::uint64_t{175};   // son "sonra" degeri kazanir
    VolumeSpace db;
    db.volume = L"D:\\";
    db.free_before = std::uint64_t{7};
    second.volumes = {cb, db};
    second.cancelled = true;
    second.processed = 3;
    second.total = 4;

    const auto m = merge_reports(first, second);
    EF_CHECK(m.categories.size() == 2);
    EF_CHECK(m.categories[0].category == CleanupCategory::WindowsTemp);
    EF_CHECK(m.categories[1].category == CleanupCategory::DeliveryOptimization);
    EF_CHECK(m.volumes.size() == 2);
    EF_CHECK(m.volumes[0].volume == L"C:\\");
    EF_CHECK(m.volumes[0].free_before && *m.volumes[0].free_before == 100);
    EF_CHECK(m.volumes[0].free_after && *m.volumes[0].free_after == 175);
    EF_CHECK(m.volumes[1].volume == L"D:\\" && m.volumes[1].free_before && *m.volumes[1].free_before == 7);
    EF_CHECK(m.cancelled);
    EF_CHECK(m.processed == 4 && m.total == 4);
}
