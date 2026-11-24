// F-81: depolama sagligi, oyun diski ve SSD/HDD bakim kurallari (saf; Win32 yok, birim testlenir).
// Kaynak: platform/windows/readiness/storage_reader (salt okuma; hicbir ayar yazilmaz).
// Ilkeler:
//  - Parcalanma orani belgelenmis hizli API ile okunamadigindan OLCULMEZ; metin bunu acikca soyler.
//  - SMART yalniz IOCTL basariyla okunup ariza ongorusu true ise Problem; okunamazsa iddia yok.
//  - HDD'de Windows "Surucuyu iyilestir" bakimi onerilir; SSD'de defrag ASLA onerilmez.
//  - Yukleme hizi ile FPS kilidi karistirilmaz; oyun HDD'deyse "aday" denir, kesin neden denmez.
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

constexpr double kSsdProblemFreeRatio = 0.10;
constexpr double kSsdAdviceFreeRatio = 0.15;

std::string num(double v) {
    char buf[32];
    if (!std::isfinite(v)) v = 0;
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

Check make(std::string id, Level level, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = Section::Hardware;
    c.level = level;
    c.text_key = std::move(key);
    return c;
}

const StorageVolumeInfo* find_volume(const StorageHealthInfo& s, const std::string& root) {
    for (const auto& v : s.volumes)
        if (v.root == root) return &v;
    return nullptr;
}

}  // namespace

std::optional<NvmeHealthLog> parse_nvme_health_log(const std::uint8_t* d, std::size_t n) {
    if (!d || n < 512) return std::nullopt;
    const auto u128 = [d](std::size_t at) {
        for (std::size_t i = 8; i < 16; ++i)
            if (d[at + i]) return UINT64_MAX;  // 64 biti asan sayac doygun
        std::uint64_t v = 0;
        for (std::size_t i = 8; i-- > 0;) v = (v << 8) | d[at + i];
        return v;
    };
    NvmeHealthLog h;
    h.critical_warning = d[0];
    const unsigned kelvin = static_cast<unsigned>(d[1]) | (static_cast<unsigned>(d[2]) << 8);
    if (kelvin != 0) h.temperature_c = static_cast<double>(kelvin) - 273.15;
    h.available_spare = d[3];
    h.spare_threshold = d[4];
    h.percent_used = d[5];
    h.power_on_hours = u128(128);
    h.unsafe_shutdowns = u128(144);
    h.media_errors = u128(160);
    return h;
}

std::vector<Check> disk_health_checks(const std::vector<DiskHealthInfo>& disks) {
    std::vector<Check> out;
    for (const auto& d : disks) {
        const std::string id = "storage.disk." + std::to_string(d.disk);
        const auto row = [&](Level l, const char* key) {
            Check c = make(id, l, key);
            c.args["disk"] = d.model.empty() ? "Disk " + std::to_string(d.disk) : d.model;
            return c;
        };
        if (d.support == DiskHealthSupport::NvmeReadFailed) {
            out.push_back(row(Level::Unknown, "chk.storage.nvme_unread"));
            continue;
        }
        if (d.support == DiskHealthSupport::Unsupported) {
            out.push_back(row(Level::Info, "chk.storage.health_unsupported"));
            continue;
        }
        if (d.support == DiskHealthSupport::Unknown) {  // USB/RAID: kopru destegi yoklanmadi; payda icinde "olculemedi"
            out.push_back(row(Level::Unknown, "chk.storage.health_unknown"));
            continue;
        }
        if (d.support == DiskHealthSupport::AtaPredictOnly || !d.nvme) continue;  // mevcut ariza ongorusu satiri kapsar
        const auto& h = *d.nvme;
        Check c = row(Level::Good, "chk.storage.nvme_ok");
        if (h.critical_warning != 0 || (h.spare_threshold > 0 && h.available_spare < h.spare_threshold)) {
            c.level = Level::Problem;
            c.text_key = "chk.storage.nvme_warning";
        } else if (h.media_errors > 0) {
            c.level = Level::Problem;
            c.text_key = "chk.storage.nvme_media";
            c.args["errors"] = std::to_string(h.media_errors);
        } else if (h.percent_used >= 90) {
            c.level = Level::Advice;
            c.text_key = "chk.storage.nvme_worn";
        }
        c.args["used"] = std::to_string(h.percent_used);
        c.args["spare"] = std::to_string(h.available_spare);
        c.args["temp"] = h.temperature_c ? num(*h.temperature_c) : std::string("-");
        out.push_back(std::move(c));
    }
    return out;
}

bool storage_write_stress_allowed(const std::vector<Check>& checks, std::optional<std::uint32_t> disk) noexcept {
    const auto hits = [&disk](const std::string& list) {
        if (!disk || list.empty()) return true;  // eslestirilemiyor: guvenli taraf
        const auto n = std::to_string(*disk);
        std::size_t p = 0;
        while (p <= list.size()) {
            const auto e = std::min(list.find(',', p), list.size());
            if (list.compare(p, e - p, n) == 0) return true;
            p = e + 1;
        }
        return false;
    };
    for (const auto& c : checks) {
        if (c.level != Level::Problem) continue;
        if (c.id.rfind("storage.disk.", 0) == 0) {
            if (hits(c.id.substr(13))) return false;
        } else if (c.id == "storage.smart" || c.id == "hardware.incidents.disk") {
            const auto it = c.args.find("disks");
            if (hits(it == c.args.end() ? std::string() : it->second)) return false;
        }
    }
    return true;
}

std::optional<std::uint32_t> disk_of_volume(const StorageHealthInfo& s, const std::string& root) noexcept {
    for (const auto& v : s.volumes)
        if (v.root == root) return v.disk_number;
    return std::nullopt;
}

std::vector<Check> storage_health_checks(const StorageHealthInfo& s) {
    std::vector<Check> out = disk_health_checks(s.disks);

    // SMART ariza ongorusu: yalniz gercekten okunduysa ve true ise. Veri guvenligi uyarisi.
    std::vector<std::uint32_t> warned;
    for (const auto& v : s.volumes) {
        if (!v.smart_failure || !*v.smart_failure) continue;
        if (v.disk_number) {  // iki bolum tek disk: iki kez uyarilmaz
            if (std::find(warned.begin(), warned.end(), *v.disk_number) != warned.end()) continue;
            warned.push_back(*v.disk_number);
        }
        Check c = make("storage.smart", Level::Problem, "chk.storage.smart_failure");
        c.args["root"] = v.root;
        if (v.disk_number) c.args["disks"] = std::to_string(*v.disk_number);
        out.push_back(std::move(c));
    }

    const StorageVolumeInfo* game = nullptr;
    if (s.game_volume_root) game = find_volume(s, *s.game_volume_root);

    bool hdd_covered = false;
    if (game && game->ssd) {
        if (!*game->ssd) {  // HDD
            Check c = make("storage.game_hdd", Level::Advice, "chk.storage.game_hdd");
            c.args["root"] = game->root;
            if (game->free_ratio) c.args["free_pct"] = num(*game->free_ratio * 100.0);
            out.push_back(std::move(c));
            hdd_covered = true;
        } else if (game->free_ratio) {  // SSD: bos alan
            const double pct = *game->free_ratio * 100.0;
            if (*game->free_ratio < kSsdProblemFreeRatio) {
                Check c = make("storage.game_ssd_low_space", Level::Problem, "chk.storage.game_ssd_low_space");
                c.args["root"] = game->root;
                c.args["free_pct"] = num(pct);
                out.push_back(std::move(c));
            } else if (*game->free_ratio < kSsdAdviceFreeRatio) {
                Check c = make("storage.game_ssd_low_space", Level::Advice, "chk.storage.game_ssd_low_space");
                c.args["root"] = game->root;
                c.args["free_pct"] = num(pct);
                out.push_back(std::move(c));
            }
        }
    }

    // Oyun birimi bilinmiyorsa da HDD varsa bakim bilgisi gosterilir (bir kez).
    if (!hdd_covered) {
        for (const auto& v : s.volumes) {
            if (!v.ssd || *v.ssd) continue;
            Check c = make("storage.hdd_maintenance", Level::Info, "chk.storage.hdd_maintenance");
            c.args["root"] = v.root;
            out.push_back(std::move(c));
            break;
        }
    }

    return out;
}

}  // namespace ef::core::readiness
