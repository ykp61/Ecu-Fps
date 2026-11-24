// CHK-01/02 (ekran) ve CHK-05 (Windows oyun ayarlari) kurallari. Saf: Win32 cagrisi yok.
//  - Yenileme hizi: ayni cozunurlukte desteklenen en yuksek degerin >= 1.5 Hz altindaysa Problem.
//    Dinamik yenileme (DRR) acikken masaustunde dusuk Hz normaldir -> yalniz bilgi. HDR acikken -> Oneri.
//  - HDR acik: sorun degil, bilgi (SDR icerik soluk gorunebilir; HDR desteklemeyen oyunda fark yok).
//  - VRR (G-SYNC/FreeSync): belgeli genel Windows API'si yok; bilinmiyorsa tek bir "dogrulanamadi".
//  - F-05 kare senkronu danismani (frame_sync_checks): VRR bilinen sistemde surucu V-Sync acik / oyun ici
//    kapali ve kare siniri = yenileme - 3 FPS onerir; VRR yoksa On/Kapali/Fast Sync seceneklerini aciklar.
//    Yalniz bilgi/oneri; yazma ve AutoFix yok. Durum bilinmiyorsa iddia yok.
//  - VBS/HVCI: guvenlik ozelligi; yalniz bilgi, kapatma onerilmez (urun kurali X05).

#include <cmath>
#include <cstdio>
#include <string>

#include "core/adapters/windows/display_process.h"
#include "core/readiness/readiness.h"

namespace ef::core::readiness {

namespace {

// Hz metni: tam sayiya 0.25ten yakinsa tam sayi (143.98 / 480.17 -> "144" / "480"), degilse iki ondalik.
std::string hz_text(double hz) {
    const double r = std::round(hz);
    char b[32];
    if (std::fabs(hz - r) < 0.25)
        std::snprintf(b, sizeof b, "%.0f", r);
    else
        std::snprintf(b, sizeof b, "%.2f", hz);
    return b;
}

std::string display_name(const DisplayInfo& d) {
    if (!d.name.empty()) return d.name;
    return "#" + std::to_string(d.index + 1);
}

Check make(std::string id, Section s, Level l, std::string key) {
    Check c;
    c.id = std::move(id);
    c.section = s;
    c.level = l;
    c.text_key = std::move(key);
    return c;
}

constexpr double kRefreshGapHz = 1.5;
constexpr const char* kDisplayAdvanced = "ms-settings:display-advanced";
constexpr const char* kDisplayHdr = "ms-settings:display-hdr";
constexpr const char* kGraphicsSettings = "ms-settings:display-advancedgraphics";
constexpr const char* kGameMode = "ms-settings:gaming-gamemode";
constexpr const char* kCaptures = "ms-settings:gaming-gamedvr";
constexpr const char* kPower = "ms-settings:powersleep";

Check refresh_check(const DisplayInfo& d) {
    const std::string base = "display." + std::to_string(d.index) + ".";
    const std::string name = display_name(d);
    // args: "index" = DisplayInfo::index (onlu), "max" = ayarlanacak tam sayi Hz (AutoFix bunlari okur).
    auto with_args = [&](Check c) {
        c.args["name"] = name;
        c.args["index"] = std::to_string(d.index);
        if (d.width && d.height) c.args["res"] = std::to_string(d.width) + "x" + std::to_string(d.height);
        if (d.hz_current) c.args["hz"] = hz_text(*d.hz_current);
        if (d.hz_max_same_res) c.args["max"] = std::to_string(std::lround(*d.hz_max_same_res));
        // Uyumluluk B6 (bosluk 10): duzeltme hedefi GDI aygit adiyla tasinir.
        if (const auto tok = adapters::windows::gdi_display_token(d.gdi_device); !tok.empty()) c.args["device"] = tok;
        return c;
    };
    if (!d.hz_current) return with_args(make(base + "refresh", Section::Display, Level::Unknown, "chk.display.refresh_unknown"));
    if (d.dynamic_refresh) {
        auto c = with_args(make(base + "refresh", Section::Display, Level::Info, "chk.display.dynamic_refresh"));
        c.action = ActionKind::OpenSettings;
        c.action_arg = kDisplayAdvanced;
        return c;
    }
    if (!d.hz_max_same_res) {
        auto c = with_args(make(base + "refresh", Section::Display, Level::Unknown, "chk.display.max_unknown"));
        c.action = ActionKind::OpenSettings;
        c.action_arg = kDisplayAdvanced;
        return c;
    }
    if (*d.hz_max_same_res - *d.hz_current >= kRefreshGapHz) {
        // HDR acikken dusuk Hz bant genisligi (cozunurluk x renk derinligi) siniri yuzunden bilincli olabilir:
        // Problem yerine Oneri.
        const Level l = d.hdr_enabled.value_or(false) ? Level::Advice : Level::Problem;
        auto c = with_args(make(base + "refresh", Section::Display, l, "chk.display.below_max"));
        if (c.args.count("device")) {
            c.action = ActionKind::AutoFix;
            c.action_arg = "fix.refresh_rate";
        } else {  // aygit adi bilinmiyor: hangi ekranin degisecegi kesin degil -> Windows ayarlari
            c.action = ActionKind::OpenSettings;
            c.action_arg = kDisplayAdvanced;
        }
        return c;
    }
    return with_args(make(base + "refresh", Section::Display, Level::Good, "chk.display.at_max"));
}

}  // namespace

std::string display_summary(const std::vector<DisplayInfo>& displays) {
    if (displays.empty()) return {};
    const DisplayInfo* d = &displays.front();
    for (const auto& x : displays)
        if (x.primary) {
            d = &x;
            break;
        }
    std::string out;
    const auto add = [&](const std::string& part) {
        if (part.empty()) return;
        if (!out.empty()) out += " · ";
        out += part;
    };
    add(d->name);
    if (d->width > 0 && d->height > 0) add(std::to_string(d->width) + "×" + std::to_string(d->height));
    if (d->hz_current && std::isfinite(*d->hz_current) && *d->hz_current > 0) add(hz_text(*d->hz_current) + " Hz");
    return out;
}

std::vector<Check> display_checks(const std::vector<DisplayInfo>& displays) {
    std::vector<Check> out;
    if (displays.empty()) {
        out.push_back(make("display.none", Section::Display, Level::Unknown, "chk.display.none"));
        return out;
    }
    bool any_vrr_known = false;
    for (const auto& d : displays) {
        out.push_back(refresh_check(d));
        const std::string base = "display." + std::to_string(d.index) + ".";
        const std::string name = display_name(d);

        // F-44: etkin yolun GPU eslemesi. Yalniz bilinen esleme gosterilir; MUX varligi iddia edilmez,
        // coklu GPU'da yol yuksek performans adaptorunde degilse kopyalama yolu ve OEM/BIOS secenegi anlatilir.
        if (d.gpu_known && !d.gpu_name.empty()) {
            const bool hybrid = d.multi_gpu && !d.gpu_high_perf;
            auto c = make(base + "gpu", Section::Display, Level::Info,
                          hybrid ? "chk.display.gpu_hybrid_path" : "chk.display.gpu_path");
            c.args["name"] = name;
            c.args["gpu"] = d.gpu_name;
            out.push_back(std::move(c));
        }

        // HDR: acik -> bilgi; destekli ama kapali -> bilgi; okunamadi -> Unknown; desteklenmiyor -> bulgu yok.
        if (d.hdr_enabled.value_or(false)) {
            auto c = make(base + "hdr", Section::Display, Level::Info, "chk.display.hdr_on");
            c.args["name"] = name;
            c.action = ActionKind::OpenSettings;
            c.action_arg = kDisplayHdr;
            out.push_back(std::move(c));
        } else if (d.hdr_supported.value_or(false) && d.hdr_enabled.has_value()) {
            auto c = make(base + "hdr", Section::Display, Level::Info, "chk.display.hdr_available");
            c.args["name"] = name;
            c.action = ActionKind::OpenSettings;
            c.action_arg = kDisplayHdr;
            out.push_back(std::move(c));
        } else if (!d.hdr_supported.has_value() && !d.hdr_enabled.has_value()) {
            auto c = make(base + "hdr", Section::Display, Level::Unknown, "chk.display.hdr_unknown");
            c.args["name"] = name;
            out.push_back(std::move(c));
        }

        if (d.vrr_supported.has_value()) {
            any_vrr_known = true;
            auto c = make(base + "vrr", Section::Display, *d.vrr_supported ? Level::Good : Level::Info,
                          *d.vrr_supported ? "chk.display.vrr_supported" : "chk.display.vrr_not_reported");
            c.args["name"] = name;
            out.push_back(std::move(c));
        }
    }
    if (!any_vrr_known) {
        // Tek satir: her ekran icin tekrar etmez. G-SYNC durumu grafik bolumunde (NVAPI) ayrica gelir.
        // Uyumluluk B8 (bosluk 17): tum etkin ekranlari bilinen bir AMD/Intel kart suruyorsa durum "bilinmiyor" degil
        // "okunamiyor (bu uretici icin API yok)"dur. Yol GPU'su bilinmeyen ya da NVIDIA olan ekran varsa eski satir.
        // B8 inceleme: hibrit dizustunde (Optimus) paneli tumlesik Intel/AMD surer ama oyun baska kartta calisir; ekrani
        // surun kart cok GPU'lu sistemde yuksek performans karti degilse iddia edilmez (eski "bilinmiyor" satiri).
        bool vendor_unsupported = true;
        for (const auto& d : displays)
            if (!d.gpu_known || (d.gpu_vendor != "amd" && d.gpu_vendor != "intel") || (d.multi_gpu && !d.gpu_high_perf))
                vendor_unsupported = false;
        out.push_back(vendor_unsupported ? make("display.vrr", Section::Display, Level::Info, "chk.display.vrr_vendor_unsupported")
                                         : make("display.vrr", Section::Display, Level::Unknown, "chk.display.vrr_unknown"));
    }
    return out;
}

std::vector<Check> frame_sync_checks(const std::vector<DisplayInfo>& displays, const WindowsGamingInfo& w) {
    std::vector<Check> out;
    if (displays.empty()) return out;

    // Windows VRR anahtari yalniz VRR destekli sistemlerde gorunur; varligi destek isaretidir.
    bool vrr_capable = w.windows_vrr.has_value();
    bool all_displays_known = true;
    for (const auto& d : displays) {
        if (!d.vrr_supported.has_value()) {
            all_displays_known = false;
            continue;
        }
        if (*d.vrr_supported) vrr_capable = true;
    }

    if (!vrr_capable) {
        // Yalniz tum ekranlar acikca VRR bildirmiyorsa "VRR yok" denir. Karisik/bilinmeyen -> iddia yok.
        if (all_displays_known)
            out.push_back(make("display.vrr_sync", Section::Display, Level::Info, "chk.display.no_vrr_vsync"));
        return out;
    }

    // Kare siniri referansi: once VRR bildiren ekran; yoksa en yuksek bilinen yenileme.
    // VRR bilinen ekranin Hz'si oyunun calistigi ekran icin daha dogru referanstir.
    const DisplayInfo* ref = nullptr;
    std::optional<double> ref_hz;
    for (const bool vrr_only : {true, false}) {
        for (const auto& d : displays) {
            if (vrr_only && !d.vrr_supported.value_or(false)) continue;
            const auto hz = d.hz_max_same_res ? d.hz_max_same_res : d.hz_current;
            if (!hz) continue;
            if (!ref_hz || *hz > *ref_hz) {
                ref_hz = hz;
                ref = &d;
            }
        }
        if (ref) break;
    }

    const bool win_off = w.windows_vrr.has_value() && !*w.windows_vrr;
    const Level level = win_off ? Level::Advice : Level::Info;
    const char* key = nullptr;
    if (win_off)
        key = ref_hz ? "chk.display.vrr_sync_win_off" : "chk.display.vrr_sync_win_off_nohz";
    else
        key = ref_hz ? "chk.display.vrr_sync_advice" : "chk.display.vrr_sync_advice_nohz";

    Check c = make("display.vrr_sync", Section::Display, level, key);
    if (ref) c.args["name"] = display_name(*ref);
    if (ref_hz) {
        c.args["hz"] = hz_text(*ref_hz);
        c.args["cap"] = std::to_string(std::max(1L, std::lround(*ref_hz) - 3L));
    }
    out.push_back(std::move(c));
    return out;
}

std::vector<Check> windows_checks(const WindowsGamingInfo& w) {
    std::vector<Check> out;
    constexpr auto S = Section::Windows;

    {  // Oyun Modu
        Check c;
        if (!w.game_mode) {
            c = make("windows.game_mode", S, Level::Unknown, "chk.win.game_mode_unknown");
            c.action = ActionKind::OpenSettings;
            c.action_arg = kGameMode;
        } else if (*w.game_mode) {
            c = make("windows.game_mode", S, Level::Good, "chk.win.game_mode_on");
        } else {
            c = make("windows.game_mode", S, Level::Advice, "chk.win.game_mode_off");
            c.action = ActionKind::AutoFix;
            c.action_arg = "fix.game_mode";
        }
        out.push_back(std::move(c));
    }

    {  // Arka plan kaydi ("Olanlari kaydet")
        Check c;
        if (!w.game_dvr_background) {
            c = make("windows.game_dvr", S, Level::Unknown, "chk.win.game_dvr_unknown");
            c.action = ActionKind::OpenSettings;
            c.action_arg = kCaptures;
        } else if (*w.game_dvr_background) {
            c = make("windows.game_dvr", S, Level::Advice, "chk.win.game_dvr_on");
            c.action = ActionKind::AutoFix;
            c.action_arg = "fix.game_dvr";
        } else {
            c = make("windows.game_dvr", S, Level::Good, "chk.win.game_dvr_off");
        }
        out.push_back(std::move(c));
    }

    {  // Pencereli oyunlar icin iyilestirmeler
        Check c;
        if (!w.windowed_optimizations) {
            c = make("windows.windowed_opt", S, Level::Unknown, "chk.win.windowed_opt_unknown");
            c.action = ActionKind::OpenSettings;
            c.action_arg = kGraphicsSettings;
        } else if (*w.windowed_optimizations) {
            c = make("windows.windowed_opt", S, Level::Good, "chk.win.windowed_opt_on");
        } else {
            c = make("windows.windowed_opt", S, Level::Advice, "chk.win.windowed_opt_off");
            c.action = ActionKind::AutoFix;
            c.action_arg = "fix.windowed_opt";
        }
        out.push_back(std::move(c));
    }

    // Windows VRR anahtari: yalniz acikca kayitliysa (anahtar VRR destekli sistemlerde gorunur; yoksa gurultu).
    if (w.windows_vrr) {
        Check c = make("windows.vrr", S, *w.windows_vrr ? Level::Good : Level::Info,
                       *w.windows_vrr ? "chk.win.vrr_on" : "chk.win.vrr_off");
        if (!*w.windows_vrr) {
            c.action = ActionKind::OpenSettings;
            c.action_arg = kGraphicsSettings;
        }
        out.push_back(std::move(c));
    }

    {  // HAGS (donanim hizlandirmali GPU zamanlamasi)
        Check c;
        if (!w.hags_supported) {
            c = make("windows.hags", S, Level::Unknown, "chk.win.hags_unknown");
        } else if (!*w.hags_supported) {
            c = make("windows.hags", S, Level::Info, "chk.win.hags_unsupported");
        } else if (!w.hags_enabled) {
            c = make("windows.hags", S, Level::Unknown, "chk.win.hags_unknown");
            c.action = ActionKind::OpenSettings;
            c.action_arg = kGraphicsSettings;
        } else if (*w.hags_enabled) {
            c = make("windows.hags", S, Level::Good, "chk.win.hags_on");
        } else {
            c = make("windows.hags", S, Level::Advice, "chk.win.hags_off");
            c.args["restart"] = "1";
            c.action = ActionKind::AutoFix;
            c.action_arg = "fix.hags";
        }
        out.push_back(std::move(c));
    }

    // F-48: Hizli baslatma. Yalniz acikca kayitliysa gosterilir; bilinmiyorsa gurultu uretilmez.
    if (w.fast_startup) {
        Check c = make("windows.fast_startup", S, *w.fast_startup ? Level::Advice : Level::Good,
                       *w.fast_startup ? "chk.win.fast_startup_on" : "chk.win.fast_startup_off");
        if (*w.fast_startup) {
            c.action = ActionKind::AutoFix;
            c.action_arg = "fix.fast_startup";
        }
        out.push_back(std::move(c));
    }

    {  // VBS / Bellek Butunlugu: yalniz bilgi; eylem yok (kapatmaya yonlendiren ayar baglantisi da verilmez, X05).
        if (w.hvci_enabled.value_or(false)) {
            out.push_back(make("windows.vbs", S, Level::Info, "chk.win.hvci_on"));
        } else if (w.vbs_running.value_or(false)) {
            out.push_back(make("windows.vbs", S, Level::Info, "chk.win.vbs_on"));
        } else if (!w.hvci_enabled && !w.vbs_running) {
            out.push_back(make("windows.vbs", S, Level::Unknown, "chk.win.vbs_unknown"));
        }
        // Ikisi de kapali: guvenlik/performans takasi yok, bulgu uretilmez.
    }

    {  // Guc modu
        Check c;
        if (!w.power_mode) {
            c = make("windows.power", S, Level::Unknown, "chk.win.power_unknown");
            c.action = ActionKind::OpenSettings;
            c.action_arg = kPower;
        } else if (w.power_plan_kind == WindowsGamingInfo::PowerPlanKind::Custom) {
            // Ozel plan: adi bir dahili anahtarla ayni olsa bile ("balanced") ozel plan olarak gosterilir.
            c = make("windows.power", S, Level::Info, "chk.win.power_custom");
            c.args["plan"] = *w.power_mode;
            c.action = ActionKind::OpenSettings;
            c.action_arg = kPower;
        } else {
            const auto& m = *w.power_mode;
            // Otomatik duzeltme yalniz plan turu Builtin okunduysa; tur bilinmiyorsa Ayarlar'a yonlendirilir.
            const auto fix_or_settings = [&](Check& x) {
                if (w.power_plan_kind == WindowsGamingInfo::PowerPlanKind::Builtin) {
                    x.action = ActionKind::AutoFix;
                    x.action_arg = "fix.power";
                } else {
                    x.action = ActionKind::OpenSettings;
                    x.action_arg = kPower;
                }
            };
            if (m == "best_efficiency" || m == "power_saver") {
                c = make("windows.power", S, Level::Advice, m == "power_saver" ? "chk.win.power_saver" : "chk.win.power_efficiency");
                fix_or_settings(c);
            } else if (m == "best_performance" || m == "better_performance" || m == "high_performance" || m == "ultimate") {
                c = make("windows.power", S, Level::Good, "chk.win.power_performance");
            } else if (m == "balanced") {
                c = make("windows.power", S, Level::Info, "chk.win.power_balanced");
                fix_or_settings(c);
            } else {
                c = make("windows.power", S, Level::Info, "chk.win.power_custom");
                c.args["plan"] = m;
                c.action = ActionKind::OpenSettings;
                c.action_arg = kPower;
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace ef::core::readiness
