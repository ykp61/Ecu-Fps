// CHK-03/04: grafik (DirectX, surucu, G-SYNC) ve yazilim/BIOS kurallari. Saf; Win32 yok.
//  - Okunamayan alan Unknown (dogrulanamadi); tahmin yok.
//  - Surum/tarih yasina gore yalniz "bakmaya deger" onerisi; belirli bir yeni surumun varligi iddia edilmez
//    (latest_driver_version yalniz kullanici cevrimici kontrolu actiysa dolar).
//  - Baglantilar yalniz resmi https sayfalari (uretici / Microsoft).
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <string_view>

#include "core/adapters/windows/update_check.h"
#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

namespace aw = core::adapters::windows;

constexpr std::int64_t kDay = 86400;
constexpr std::int64_t kDriverOldDays = 180;
constexpr std::int64_t kBiosOldDays = 548;

constexpr std::string_view kDx9RuntimeUrl = "https://www.microsoft.com/en-us/download/details.aspx?id=35";
constexpr std::string_view kWindowsUpdateUri = "ms-settings:windowsupdate";
constexpr std::string_view kVcRedistUrl = "https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist";
constexpr std::string_view kDotnetUrl = "https://learn.microsoft.com/dotnet/core/install/windows";
constexpr std::string_view kDotnetFxUrl = "https://learn.microsoft.com/dotnet/framework/install/";

std::string lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool contains(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

// 1970-01-01'den gun -> "YYYY-MM-DD" (Howard Hinnant civil_from_days).
std::string iso_date(std::int64_t unix_s) {
    std::int64_t z = (unix_s >= 0 ? unix_s : unix_s - (kDay - 1)) / kDay + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y0 = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t y = y0 + (m <= 2 ? 1 : 0);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
    return buf;
}

// Yas (gun). Gelecek tarih 0 sayilir; now bilinmiyorsa nullopt.
std::optional<std::int64_t> age_days(std::optional<std::int64_t> when, std::int64_t now) {
    if (!when || now <= 0) return std::nullopt;
    return std::max<std::int64_t>(0, (now - *when) / kDay);
}

std::string months_of(std::int64_t days) { return std::to_string(days / 30); }

// "12.1" bicimi (D3D_FEATURE_LEVEL: 0xc100 -> 12.1).
std::string fl_string(std::uint32_t fl) {
    return std::to_string((fl >> 12) & 0xF) + "." + std::to_string((fl >> 8) & 0xF);
}
std::string sm_string(std::uint32_t sm) { return std::to_string((sm >> 4) & 0xF) + "." + std::to_string(sm & 0xF); }
std::string tier_string(std::uint32_t t) { return std::to_string(t / 10) + "." + std::to_string(t % 10); }

std::optional<std::vector<unsigned>> parse_version(std::string_view v) {
    std::vector<unsigned> out;
    std::size_t start = 0;
    while (start <= v.size()) {
        const auto dot = v.find('.', start);
        const auto part = v.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        unsigned x = 0;
        if (part.empty() || part.size() > 9) return std::nullopt;
        const auto r = std::from_chars(part.data(), part.data() + part.size(), x);
        if (r.ec != std::errc{} || r.ptr != part.data() + part.size()) return std::nullopt;
        out.push_back(x);
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    if (out.empty() || out.size() > 6) return std::nullopt;
    return out;
}

// Kurulu surumu "en son" bicimine getirir (NVIDIA: 32.0.15.9144 -> 591.44). Karsilastirilamazsa nullopt.
std::optional<std::string> comparable_installed(const GraphicsInfo& g, std::string_view latest) {
    const auto l = parse_version(latest);
    if (!l) return std::nullopt;
    std::string inst = g.driver_version;
    if (g.vendor == "nvidia" && l->size() == 2) {
        const auto nv = aw::nvidia_display_version(inst);
        if (!nv) return std::nullopt;
        inst = *nv;
    }
    const auto i = parse_version(inst);
    if (!i || i->size() != l->size()) return std::nullopt;
    return inst;
}

bool known_gpu_vendor(const std::string& vendor) {
    return vendor == "nvidia" || vendor == "amd" || vendor == "intel";
}

std::string_view driver_url(const std::string& vendor) {
    if (vendor == "nvidia") return aw::official_driver_url(aw::GpuVendor::Nvidia);
    if (vendor == "amd") return aw::official_driver_url(aw::GpuVendor::Amd);
    if (vendor == "intel") return aw::official_driver_url(aw::GpuVendor::Intel);
    return {};
}

Check make(std::string id, Section s, Level l, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = s;
    c.level = l;
    c.text_key = std::move(key);
    return c;
}

// Dogrulanamayan (Unknown) bulgu eylem tasimaz: durum bilinmeden kullanici bir yere yonlendirilmez.
void strip_unknown_actions(std::vector<Check>& v) {
    for (auto& c : v)
        if (c.level == Level::Unknown) {
            c.action = ActionKind::None;
            c.action_arg.clear();
        }
}

void set_url(Check& c, std::string_view url) {
    if (url.empty()) return;
    c.action = ActionKind::OpenUrl;
    c.action_arg = std::string(url);
}

std::string slug(std::string_view name) {
    std::string out;
    bool sep = false;
    for (char ch : name) {
        const auto u = static_cast<unsigned char>(ch);
        if (std::isalnum(u) && u < 0x80) {
            if (sep && !out.empty()) out.push_back('_');
            out.push_back(static_cast<char>(std::tolower(u)));
            sep = false;
        } else {
            sep = true;
        }
        if (out.size() >= 40) break;
    }
    return out.empty() ? "item" : out;
}

// Visual C++ v14 (2015-2022 / 2017-2026) yeniden dagitilabilir paketi mi; mimari "x64"/"x86".
std::optional<std::string> vcredist_arch(const SoftwareItem& it) {
    const auto n = lower(it.name);
    if (!contains(n, "visual c++") || !contains(n, "redistributable")) return std::nullopt;
    const bool v14 = it.version.rfind("14.", 0) == 0 || contains(n, "v14") || contains(n, "2015") ||
                     contains(n, "2017") || contains(n, "2019") || contains(n, "2022");
    if (!v14) return std::nullopt;
    if (contains(n, "x64")) return std::string("x64");
    if (contains(n, "x86")) return std::string("x86");
    return std::nullopt;
}

bool version_less(const std::string& a, const std::string& b) {
    const auto pa = parse_version(a), pb = parse_version(b);
    if (!pa || !pb) return a < b;
    return *pa < *pb;
}

// Anakart ureticisi -> resmi destek/indirme sayfasi (yalniz guvenilir esleme).
std::string_view board_support_url(const std::string& vendor) {
    const auto v = lower(vendor);
    if (contains(v, "asustek") || v.rfind("asus", 0) == 0) return "https://www.asus.com/support/download-center/";
    if (contains(v, "micro-star") || v.rfind("msi", 0) == 0) return "https://www.msi.com/support/download";
    if (contains(v, "gigabyte")) return "https://www.gigabyte.com/Support";
    if (contains(v, "asrock")) return "https://www.asrock.com/support/index.asp";
    return {};
}

}  // namespace

bool dx12_ultimate(const GraphicsInfo& g) {
    return g.d3d12_max_feature_level.value_or(0) >= 0xc200 && g.raytracing_tier.value_or(0) >= 11 &&
           g.mesh_shaders.value_or(false) && g.vrs_tier2.value_or(false) && g.sampler_feedback.value_or(false);
}

std::vector<Check> graphics_checks(const GraphicsInfo& g) {
    std::vector<Check> v;
    const auto S = Section::Graphics;

    // DirectX destegi (bilgi)
    {
        Check c = make("graphics.dx", S, Level::Info, "");
        c.args["name"] = g.gpu_name;
        if (!g.d3d12_max_feature_level && g.d3d11_feature_level) {
            // D3D12 cihazi olusmadi; D3D11 yedegi calisti.
            c.text_key = "chk.graphics.dx11_only";
            c.args["fl"] = fl_string(*g.d3d11_feature_level);
        } else if (!g.d3d12_max_feature_level) {
            c.level = Level::Unknown;
            c.text_key = "chk.graphics.dx_unknown";
        } else {
            const auto fl = *g.d3d12_max_feature_level;
            c.args["fl"] = fl_string(fl);
            if (g.shader_model) c.args["sm"] = sm_string(*g.shader_model);
            if (g.raytracing_tier && *g.raytracing_tier > 0) c.args["rt"] = tier_string(*g.raytracing_tier);
            if (dx12_ultimate(g))
                c.text_key = "chk.graphics.dx12u";
            else if (fl >= 0xc000)
                c.text_key = "chk.graphics.dx12";
            else
                c.text_key = "chk.graphics.dx11";
        }
        v.push_back(std::move(c));
    }

    // Eski oyunlar icin DirectX 9 ek calisma dosyalari (d3dx9_43.dll)
    {
        Check c = make("graphics.dx9", S, Level::Unknown, "chk.graphics.dx9_unknown");
        if (g.dx9_legacy_runtime) {
            if (*g.dx9_legacy_runtime) {
                c.level = Level::Good;
                c.text_key = "chk.graphics.dx9_ok";
            } else {
                c.level = Level::Info;
                c.text_key = "chk.graphics.dx9_missing";
                set_url(c, kDx9RuntimeUrl);
            }
        }
        v.push_back(std::move(c));
    }

    // Surucu yasi
    const auto url = driver_url(g.vendor);
    if (g.vendor == "basic_display") {
        // Okuyucu KANITLADI: ekran sinifindaki tum aygitlar Microsoft Temel Goruntu (BasicDisplay) hizmetinde.
        Check c = make("graphics.driver.missing", S, Level::Problem, "chk.graphics.no_driver");
        c.action = ActionKind::OpenSettings;
        c.action_arg = std::string(kWindowsUpdateUri);
        v.push_back(std::move(c));
    } else if (g.vendor == "microsoft") {
        // Yazilim olmayan Microsoft adaptoru (0x1414) ama surucusuzluk kanitlanmadi: Uzak Masaustu, Hyper-V,
        // dolayli ekran aygitlari olabilir. "Surucu yok" denmez; dogrulanamadi.
        Check c = make("graphics.driver.age", S, Level::Unknown, "chk.graphics.driver_unknown");
        if (!g.gpu_name.empty()) {
            c.text_key = "chk.graphics.driver_ms_adapter";
            c.args["name"] = g.gpu_name;
        }
        v.push_back(std::move(c));
    } else if (g.vendor == "other") {
        // Taninmayan uretici: yas esigi ve resmi sayfa bilinmiyor; tavsiye uretilmez.
        v.push_back(make("graphics.driver.age", S, Level::Unknown, "chk.graphics.driver_vendor_unknown"));
    } else {
        Check c = make("graphics.driver.age", S, Level::Unknown, "chk.graphics.driver_unknown");
        std::string shown = g.driver_version;
        if (g.vendor == "nvidia")
            if (auto nv = aw::nvidia_display_version(g.driver_version)) shown = *nv;
        const auto age = age_days(g.driver_date_unix, g.now_unix);
        if (!shown.empty()) c.args["version"] = shown;
        // Surum okunamadiysa ayri metin (_nover): yer tutucu "?" gosterilmez.
        const std::string sfx = shown.empty() ? "_nover" : "";
        if (shown.empty() && !age) {
            // ne surum ne yas: driver_unknown
        } else if (!age) {
            c.text_key = "chk.graphics.driver_date_unknown";
        } else {
            c.args["date"] = iso_date(*g.driver_date_unix);
            c.args["days"] = std::to_string(*age);
            c.args["months"] = months_of(*age);
            if (*age > kDriverOldDays) {
                c.level = Level::Advice;
                c.text_key = "chk.graphics.driver_old" + sfx;
                set_url(c, url);
            } else {
                c.level = Level::Good;
                c.text_key = "chk.graphics.driver_recent" + sfx;
            }
        }
        v.push_back(std::move(c));
    }

    // Cevrimici "en son surum" (yalniz kullanici kontrol ettiyse ve bicimler karsilastirilabiliyorsa)
    if (known_gpu_vendor(g.vendor) && g.latest_driver_version && !g.latest_driver_version->empty()) {
        if (const auto inst = comparable_installed(g, *g.latest_driver_version)) {
            const bool newer = version_less(*inst, *g.latest_driver_version);
            Check c = make("graphics.driver.latest", S, newer ? Level::Advice : Level::Good,
                           newer ? "chk.graphics.driver_update" : "chk.graphics.driver_latest");
            c.args["installed"] = *inst;
            c.args["latest"] = *g.latest_driver_version;
            if (newer) set_url(c, url);
            v.push_back(std::move(c));
        }
    }

    // G-SYNC (yalniz NVIDIA). Yonlendirmeli: NVIDIA Denetim Masasi; urun bu ayari yazmaz.
    // NOT: NVAPI NV_GET_VRR_INFO bitlerinin (bIsVRREnabled / bIsVRRPossible / bIsDisplayInVRRMode) masaustu
    // ve oyun sirasindaki anlami NVIDIA belgelerinde dogrulanmadi (tek cihazda gozlendi). Bu yuzden "kapali"
    // sonucu tavsiye degil, yalniz "Denetim Masasi'nda dogrulayin" bilgisidir.
    if (g.vendor == "nvidia") {
        Check c = make("graphics.gsync", S, Level::Unknown, "chk.graphics.gsync_unknown");
        if (g.gsync_enabled && *g.gsync_enabled) {
            c.level = Level::Good;
            c.text_key = "chk.graphics.gsync_on";
        } else if (g.gsync_possible && !*g.gsync_possible) {
            c.level = Level::Info;
            c.text_key = "chk.graphics.gsync_na";
        } else if (g.gsync_enabled && g.gsync_possible && *g.gsync_possible) {
            c.level = Level::Info;
            c.text_key = "chk.graphics.gsync_off";
        }
        v.push_back(std::move(c));
    }
    // Uyumluluk B8 (bosluk 17): AMD/Intel icin uretici API'si (ADLX / IGCL) baglanmadi. FreeSync/VRR durumu ve surucu
    // isi/guc siniri "bilinmiyor" degil "desteklenmiyor"dur: bilgi satiri, ayar onerisi yok.
    if (g.vendor == "amd" || g.vendor == "intel")
        v.push_back(make("graphics.vrr_vendor", S, Level::Info, "chk.graphics.vendor_api_unsupported"));
    strip_unknown_actions(v);
    return v;
}

std::vector<Check> software_checks(const SoftwareInfo& s) {
    std::vector<Check> v;
    const auto S = Section::Software;

    // Visual C++ v14 (x64 zorunluya yakin; x86 eski/32 bit oyunlar icin)
    std::optional<SoftwareItem> vc64, vc86;
    for (const auto& it : s.items) {
        const auto arch = vcredist_arch(it);
        if (!arch) continue;
        auto& slot = *arch == "x64" ? vc64 : vc86;
        if (!slot || version_less(slot->version, it.version)) slot = it;
    }
    for (const auto* arch : {"x64", "x86"}) {
        const auto& item = std::string_view(arch) == "x64" ? vc64 : vc86;
        Check c = make(std::string("software.vcredist.") + arch, S, Level::Good, "chk.software.vcredist_ok");
        c.args["arch"] = arch;
        if (item) {
            c.args["version"] = item->version;
        } else {
            c.level = std::string_view(arch) == "x64" ? Level::Advice : Level::Info;
            c.text_key = std::string("chk.software.vcredist_missing_") + arch;
            set_url(c, kVcRedistUrl);
        }
        v.push_back(std::move(c));
    }

    // .NET: bazi oyunlar ve baslaticilar ister; cogu oyun istemez, bu yuzden eksikligi yalniz bilgi (Info) sayilir.
    {
        std::string best;
        for (const auto& ver : s.dotnet_desktop_versions)
            if (best.empty() || version_less(best, ver)) best = ver;
        Check c = make("software.dotnet.desktop", S, Level::Good, "chk.software.dotnet_ok");
        if (!best.empty()) {
            c.args["version"] = best;
        } else {
            c.level = Level::Info;
            c.text_key = "chk.software.dotnet_missing";
            set_url(c, kDotnetUrl);
        }
        v.push_back(std::move(c));

        constexpr std::uint32_t kFx48 = 528040, kFx481 = 533320;
        Check f = make("software.dotnet.framework", S, Level::Good, "chk.software.dotnetfx_ok");
        const auto rel = s.dotnet_framework_release.value_or(0);
        if (rel >= kFx48) {
            f.args["version"] = rel >= kFx481 ? "4.8.1" : "4.8";
        } else {
            f.level = Level::Info;
            f.text_key = "chk.software.dotnetfx_old";
            set_url(f, kDotnetFxUrl);
        }
        v.push_back(std::move(f));
    }

    // Diger yazilimlar: surum ve kurulum tarihiyle bilgi
    std::vector<std::string> seen;
    for (const auto& it : s.items) {
        if (vcredist_arch(it) || it.name.empty()) continue;
        auto id = "software.item." + slug(it.name);
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);
        Check c = make(std::move(id), S, Level::Info, "chk.software.item_bare");
        c.args["name"] = it.name;
        if (!it.version.empty()) {
            c.args["version"] = it.version;
            c.text_key = "chk.software.item_nodate";
            if (it.date_unix) {
                c.args["date"] = iso_date(*it.date_unix);
                if (const auto age = age_days(it.date_unix, s.now_unix)) {
                    c.args["days"] = std::to_string(*age);
                    c.args["months"] = months_of(*age);
                }
                c.text_key = "chk.software.item";
            }
        }
        v.push_back(std::move(c));
    }

    // BIOS yasi: yeni surumun varligi iddia edilmez; yalniz "uretici sayfasina bakin".
    {
        Check c = make("software.bios", S, Level::Unknown, "chk.software.bios_unknown");
        if (!s.board_vendor.empty()) c.args["vendor"] = s.board_vendor;
        if (!s.board_model.empty()) c.args["model"] = s.board_model;
        if (const auto age = age_days(s.bios_date_unix, s.now_unix)) {
            c.args["date"] = iso_date(*s.bios_date_unix);
            c.args["days"] = std::to_string(*age);
            c.args["months"] = months_of(*age);
            if (*age > kBiosOldDays) {
                c.level = Level::Advice;
                c.text_key = "chk.software.bios_old";
                set_url(c, board_support_url(s.board_vendor));
            } else {
                c.level = Level::Info;  // tarih tek basina "guncel" kaniti degildir
                c.text_key = "chk.software.bios_ok";
            }
        }
        v.push_back(std::move(c));
    }
    strip_unknown_actions(v);
    return v;
}

}  // namespace ef::core::readiness
