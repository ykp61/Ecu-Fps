#pragma once
// WIN-08 ek: yonetici gerektiren temizlik kategorileri icin yukseltilmis yardimci protokolu (platformdan bagimsiz).
//  - Istek yalniz sabit kategori kimlikleri + surucu koku tasir; serbest yol/komut yok.
//  - Yardimci plani kendi surecinde tutar: calistirma, taranan planin AYNISI uzerinden yapilir (ADR-016).
//  - Aktarim yalniz onizleme ozeti / ilerleme / rapor; dosya yollari istemciye tasinmaz.
//  - Tum metinler ASCII (kategori kimligi ve "X:\\" koku); UTF-8 donusumu gerekmez.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/result.h"
#include "core/cleanup/cleanup.h"

namespace ef::core::cleanup {

inline constexpr int kAdminTransferVersion = 1;

// Yonetici gerektiren urun kategorileri (kProductCategories icinden). Baska kategori istenemez.
const std::vector<CleanupCategory>& admin_product_categories();
bool is_admin_product_category(CleanupCategory c) noexcept;

// Kategori kimligi ("windows_temp") -> enum. Bilinmeyen kimlik nullopt.
std::optional<CleanupCategory> category_from_id(std::string_view id) noexcept;

struct AdminScanRequest {
    std::vector<CleanupCategory> categories;
    std::string volume;  // "C:\\" veya bos (birim filtresi yok)
};
struct AdminRunRequest {
    std::vector<CleanupCategory> categories;
};
struct AdminProgress {
    CleanupCategory category = CleanupCategory::UserTemp;
    std::size_t done = 0;
    std::size_t total = 0;
};

std::string encode_admin_scan_request(const AdminScanRequest& r);
contracts::Result<AdminScanRequest> decode_admin_scan_request(std::string_view payload);
std::string encode_admin_scan_result(const std::vector<CleanupPreview>& previews);
contracts::Result<std::vector<CleanupPreview>> decode_admin_scan_result(std::string_view payload);
std::string encode_admin_run_request(const AdminRunRequest& r);
contracts::Result<AdminRunRequest> decode_admin_run_request(std::string_view payload);
std::string encode_admin_progress(const AdminProgress& p);
contracts::Result<AdminProgress> decode_admin_progress(std::string_view payload);
std::string encode_admin_report(const CleanupReport& r);
contracts::Result<CleanupReport> decode_admin_report(std::string_view payload);

// Iki ayrik kategori kumesinin raporlarini birlestirir (yonetici + kullanici yolu). Ayni birim icin
// ilk bos alan "once", sonuncu "sonra" olur; tahmin uretilmez.
CleanupReport merge_reports(CleanupReport first, const CleanupReport& second);

}  // namespace ef::core::cleanup
