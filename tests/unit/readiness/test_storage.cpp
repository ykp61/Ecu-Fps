// F-81: depolama sagligi, oyun diski ve SSD/HDD bakim kurallari (saf; Win32 cagrisi yok).
// Parcalanma olculmez; SMART yalniz okunup true ise bildirilir; SSD'de defrag onerilmez.
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "core/i18n/catalog.h"
#include "core/readiness/readiness.h"
#include "ef_test.h"

using namespace ef::core::readiness;

namespace {

const Check* find_ptr(const std::vector<Check>& v, const std::string& id) {
    for (const auto& c : v)
        if (c.id == id) return &c;
    return nullptr;
}

StorageVolumeInfo volume(std::string root, std::optional<bool> ssd, std::optional<double> free_ratio,
                         std::optional<bool> smart = std::nullopt) {
    StorageVolumeInfo v;
    v.root = std::move(root);
    v.ssd = ssd;
    v.free_ratio = free_ratio;
    v.smart_failure = smart;
    return v;
}

}  // namespace

EF_TEST(storage_empty_makes_no_claim) {
    EF_CHECK(storage_health_checks(StorageHealthInfo{}).empty());
}

EF_TEST(storage_smart_failure_is_problem) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.5, true));
    const auto v = storage_health_checks(s);
    const auto* c = find_ptr(v, "storage.smart");
    EF_CHECK(c && c->level == Level::Problem && c->section == Section::Hardware);
    EF_CHECK(c && c->text_key == "chk.storage.smart_failure" && c->args.at("root") == "C:\\");
    EF_CHECK(ef::core::i18n::id_of("chk.storage.smart_failure").has_value());
}

EF_TEST(storage_smart_ok_or_unknown_makes_no_claim) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.5, false));
    s.volumes.push_back(volume("D:\\", false, 0.5, std::nullopt));
    EF_CHECK(!find_ptr(storage_health_checks(s), "storage.smart"));
}

EF_TEST(storage_game_on_hdd_advises_move) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("D:\\", false, 0.40));
    s.game_volume_root = "D:\\";
    const auto v = storage_health_checks(s);
    const auto* c = find_ptr(v, "storage.game_hdd");
    EF_CHECK(c && c->level == Level::Advice && c->text_key == "chk.storage.game_hdd");
    EF_CHECK(c && c->args.at("root") == "D:\\" && c->args.at("free_pct") == "40.0");
    EF_CHECK(!find_ptr(v, "storage.hdd_maintenance"));  // ayni HDD iki kez anlatilmaz
    EF_CHECK(ef::core::i18n::id_of("chk.storage.game_hdd").has_value());
}

EF_TEST(storage_game_ssd_low_space_is_problem_below_ten) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.08));
    s.game_volume_root = "C:\\";
    const auto v = storage_health_checks(s);
    const auto* c = find_ptr(v, "storage.game_ssd_low_space");
    EF_CHECK(c && c->level == Level::Problem && c->text_key == "chk.storage.game_ssd_low_space");
    EF_CHECK(c && c->args.at("free_pct") == "8.0");
    EF_CHECK(ef::core::i18n::id_of("chk.storage.game_ssd_low_space").has_value());
}

EF_TEST(storage_game_ssd_between_ten_and_fifteen_is_advice) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.12));
    s.game_volume_root = "C:\\";
    const auto v = storage_health_checks(s);
    const auto* c = find_ptr(v, "storage.game_ssd_low_space");
    EF_CHECK(c && c->level == Level::Advice && c->args.at("free_pct") == "12.0");
}

EF_TEST(storage_game_ssd_above_fifteen_makes_no_claim) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.20));
    s.game_volume_root = "C:\\";
    EF_CHECK(!find_ptr(storage_health_checks(s), "storage.game_ssd_low_space"));
}

EF_TEST(storage_game_ssd_unknown_free_makes_no_claim) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, std::nullopt));
    s.game_volume_root = "C:\\";
    EF_CHECK(!find_ptr(storage_health_checks(s), "storage.game_ssd_low_space"));
}

EF_TEST(storage_unknown_game_media_makes_no_claim) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", std::nullopt, 0.05));
    s.game_volume_root = "C:\\";
    EF_CHECK(!find_ptr(storage_health_checks(s), "storage.game_hdd"));
    EF_CHECK(!find_ptr(storage_health_checks(s), "storage.game_ssd_low_space"));
}

EF_TEST(storage_hdd_maintenance_info_once_when_no_game_volume) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.5));
    s.volumes.push_back(volume("D:\\", false, 0.5));
    s.volumes.push_back(volume("E:\\", false, 0.5));
    const auto v = storage_health_checks(s);
    const auto* c = find_ptr(v, "storage.hdd_maintenance");
    EF_CHECK(c && c->level == Level::Info && c->args.at("root") == "D:\\");
    EF_CHECK(c && c->text_key == "chk.storage.hdd_maintenance");
    EF_CHECK(ef::core::i18n::id_of("chk.storage.hdd_maintenance").has_value());
}

EF_TEST(storage_no_hdd_makes_no_maintenance_claim) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.5));
    s.volumes.push_back(volume("D:\\", true, 0.5));
    EF_CHECK(!find_ptr(storage_health_checks(s), "storage.hdd_maintenance"));
}

EF_TEST(storage_never_produce_autofix) {
    StorageHealthInfo s;
    s.volumes.push_back(volume("C:\\", true, 0.05, true));
    s.volumes.push_back(volume("D:\\", false, 0.40));
    s.game_volume_root = "D:\\";
    const auto v = storage_health_checks(s);
    EF_CHECK(!v.empty());
    for (const auto& c : v) EF_CHECK(c.action != ActionKind::AutoFix);
}

static std::vector<std::uint8_t> nvme_page() {
    std::vector<std::uint8_t> p(512, 0);
    p[1] = 0x37; p[2] = 0x01;  // 311 K
    p[3] = 100; p[4] = 10; p[5] = 7;
    p[128] = 0x10;  // 16 saat
    return p;
}

EF_TEST(x23_nvme_log_parses_counters_and_kelvin_bounds) {
    auto p = nvme_page();
    auto h = parse_nvme_health_log(p.data(), p.size());
    EF_CHECK(h && h->power_on_hours == 16 && h->percent_used == 7 && h->temperature_c && *h->temperature_c > 37.8 && *h->temperature_c < 37.9);
    p[1] = 0; p[2] = 0;  // 0 K gecersiz: 0 C/-273 uretilmez
    EF_CHECK(!parse_nvme_health_log(p.data(), p.size())->temperature_c);
    p[160 + 8] = 1;  // 2^64 ve uzeri medya hatasi doygun
    EF_CHECK(parse_nvme_health_log(p.data(), p.size())->media_errors == UINT64_MAX);
    EF_CHECK(!parse_nvme_health_log(p.data(), 511));
    EF_CHECK(!parse_nvme_health_log(nullptr, 512));
}

EF_TEST(x23_t01_each_bus_reports_its_own_state) {
    DiskHealthInfo usb; usb.disk = 1; usb.support = DiskHealthSupport::Unknown;
    DiskHealthInfo virt; virt.disk = 4; virt.support = DiskHealthSupport::Unsupported;
    DiskHealthInfo denied; denied.disk = 2; denied.support = DiskHealthSupport::NvmeReadFailed;
    DiskHealthInfo ata; ata.disk = 3; ata.support = DiskHealthSupport::AtaPredictOnly;
    const auto v = disk_health_checks({usb, denied, ata, virt});
    EF_CHECK(v.size() == 3 && v[0].level == Level::Unknown && v[0].text_key == "chk.storage.health_unknown");
    EF_CHECK(v[1].level == Level::Unknown && v[1].text_key == "chk.storage.nvme_unread");
    EF_CHECK(v[2].level == Level::Info && v[2].text_key == "chk.storage.health_unsupported");
}

EF_TEST(x23_t02_partitions_of_one_disk_warn_once) {
    StorageHealthInfo s;
    StorageVolumeInfo a; a.root = "C:\\"; a.smart_failure = true; a.disk_number = 0;
    StorageVolumeInfo b; b.root = "D:\\"; b.smart_failure = true; b.disk_number = 0;
    StorageVolumeInfo c; c.root = "E:\\"; c.smart_failure = true; c.disk_number = 1;
    s.volumes = {a, b, c};
    const auto v = storage_health_checks(s);
    EF_CHECK(std::count_if(v.begin(), v.end(), [](const Check& k) { return k.id == "storage.smart"; }) == 2);
}

EF_TEST(x23_t03_health_rules_and_clean_is_not_proof) {
    auto p = nvme_page();
    DiskHealthInfo d; d.model = "M"; d.support = DiskHealthSupport::Nvme; d.nvme = *parse_nvme_health_log(p.data(), p.size());
    EF_CHECK(disk_health_checks({d})[0].level == Level::Good && disk_health_checks({d})[0].text_key == "chk.storage.nvme_ok");
    d.nvme->available_spare = 5;
    EF_CHECK(disk_health_checks({d})[0].level == Level::Problem);
    d.nvme->available_spare = 100; d.nvme->critical_warning = 1;
    EF_CHECK(disk_health_checks({d})[0].text_key == "chk.storage.nvme_warning");
    d.nvme->critical_warning = 0; d.nvme->media_errors = 3;
    EF_CHECK(disk_health_checks({d})[0].text_key == "chk.storage.nvme_media" && disk_health_checks({d})[0].args.at("errors") == "3");
    d.nvme->media_errors = 0; d.nvme->percent_used = 95;
    EF_CHECK(disk_health_checks({d})[0].level == Level::Advice);
}

EF_TEST(x23_t04_write_stress_blocked_on_data_risk_only) {
    Check smart; smart.id = "storage.smart"; smart.level = Level::Problem;
    Check disk; disk.id = "storage.disk.0"; disk.level = Level::Problem;
    Check log; log.id = "hardware.incidents.disk"; log.level = Level::Problem;
    Check low; low.id = "storage.game_ssd_low_space"; low.level = Level::Problem;
    Check adv; adv.id = "storage.disk.1"; adv.level = Level::Advice;
    EF_CHECK(!storage_write_stress_allowed({smart}) && !storage_write_stress_allowed({disk}) && !storage_write_stress_allowed({log}));
    EF_CHECK(storage_write_stress_allowed({low, adv}) && storage_write_stress_allowed({}));
    // Baska diskin sorunu secilen diski engellemez; eslestirilemeyen kanit engeller.
    EF_CHECK(storage_write_stress_allowed({disk}, 3u) && !storage_write_stress_allowed({disk}, 0u));
    log.args["disks"] = "3,5";
    EF_CHECK(!storage_write_stress_allowed({log}, 5u) && storage_write_stress_allowed({log}, 2u));
    log.args["disks"] = "";
    EF_CHECK(!storage_write_stress_allowed({log}, 2u));
}
