// F-65/F-67: sistem sagligi hazirlik kurallari (saf; Win32 yok, birim testlenir).
// Kaynak: platform/windows/readiness/system_reader (salt okuma; hicbir ayar yazilmaz).
// Ilkeler:
//  - Okunamayan deger Unknown; sifir veya "sorun yok" varsayilmaz.
//  - Uzun uptime ve cekirdek havuzu icin oneri "tam yeniden baslat"tir; kapat-ac Hizli Baslatma
//    nedeniyle cekirdek durumunu sifirlamayabilir. Bu bir teshis degil, bakim onerisidir.
//  - Sayfa dosyasi yoklugu tek basina sorun degildir; commit basinci ile birlikte degerlendirilir.
//  - TRIM kapaliysa SSD omru/performansi icin oneri verilir; uygulama ayari DEGISTIRMEZ.
#include <cmath>
#include <cstdio>
#include <string>

#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

constexpr std::uint64_t kUptimeAdviceDays = 7;
constexpr double kCommitPressurePct = 85.0;
constexpr double kPagefileUsageAdvicePct = 80.0;
constexpr double kDiskProblemFreeRatio = 0.10;
constexpr double kDiskAdviceFreeRatio = 0.20;
constexpr double kNonpagedAdviceMib = 1024.0;
constexpr double kNonpagedProblemMib = 4096.0;
constexpr double kCompressedAdviceMib = 2048.0;
constexpr double kStandbyInfoMib = 8192.0;

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

}  // namespace

std::vector<Check> system_health_checks(const SystemHealthInfo& s) {
    std::vector<Check> out;

    if (s.uptime_s) {
        const std::uint64_t days = *s.uptime_s / 86400;
        if (days >= kUptimeAdviceDays) {
            Check c = make("system.uptime", Level::Advice, "chk.system.uptime_long");
            c.args["days"] = std::to_string(days);
            out.push_back(std::move(c));
        }
    }

    if (s.pagefile_configured && !*s.pagefile_configured && s.commit_used_pct &&
        *s.commit_used_pct >= kCommitPressurePct) {
        Check c = make("system.pagefile", Level::Problem, "chk.system.pagefile_missing");
        c.args["commit_pct"] = num(*s.commit_used_pct);
        out.push_back(std::move(c));
    } else if (s.pagefile_usage_pct && *s.pagefile_usage_pct >= kPagefileUsageAdvicePct &&
               (!s.pagefile_configured || *s.pagefile_configured)) {
        Check c = make("system.pagefile.usage", Level::Advice, "chk.system.pagefile_high");
        c.args["usage_pct"] = num(*s.pagefile_usage_pct);
        out.push_back(std::move(c));
    }

    if (s.system_drive_free_ratio) {
        const double free_pct = s.system_drive_free_ratio.value_or(0.0) * 100.0;
        if (*s.system_drive_free_ratio < kDiskProblemFreeRatio) {
            Check c = make("system.disk", Level::Problem, "chk.system.disk_low");
            c.args["free_pct"] = num(free_pct);
            out.push_back(std::move(c));
        } else if (*s.system_drive_free_ratio < kDiskAdviceFreeRatio) {
            Check c = make("system.disk", Level::Advice, "chk.system.disk_tight");
            c.args["free_pct"] = num(free_pct);
            out.push_back(std::move(c));
        }
    }

    if (s.trim_enabled) {
        if (!*s.trim_enabled) out.push_back(make("system.trim", Level::Advice, "chk.system.trim_off"));
    } else {
        out.push_back(make("system.trim", Level::Unknown, "chk.system.trim_unknown"));
    }

    if (s.nonpaged_pool_mib && *s.nonpaged_pool_mib >= kNonpagedAdviceMib) {
        Check c = make("system.pool",
                       *s.nonpaged_pool_mib >= kNonpagedProblemMib ? Level::Problem : Level::Advice,
                       "chk.system.pool_high");
        c.args["pool_gib"] = num(*s.nonpaged_pool_mib / 1024.0);
        out.push_back(std::move(c));
    }

    if (s.compressed_mib && *s.compressed_mib >= kCompressedAdviceMib && s.commit_used_pct &&
        *s.commit_used_pct >= kCommitPressurePct) {
        Check c = make("system.compression", Level::Advice, "chk.system.compressed_high");
        c.args["compressed_gib"] = num(*s.compressed_mib / 1024.0);
        out.push_back(std::move(c));
    }

    if (s.standby_mib && *s.standby_mib >= kStandbyInfoMib) {
        Check c = make("system.standby", Level::Info, "chk.system.standby_info");
        c.args["standby_gib"] = num(*s.standby_mib / 1024.0);
        out.push_back(std::move(c));
    }

    return out;
}

}  // namespace ef::core::readiness
