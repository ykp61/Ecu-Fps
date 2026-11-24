#include "core/settings/preferences.h"

#include <algorithm>

#include "core/diagnostics/incident_engine.h"
#include "core/games/manual_games.h"
#include "core/i18n/catalog.h"
#include "core/net/server_ping.h"
#include "core/serialization/json.h"

namespace ef::core::settings {

using contracts::ErrorCode;
using R = contracts::Result<Preferences>;

std::string Preferences::serialize() const {
    auto o = json::Value::object();
    o.set("schema_version", json::Value::integer(1));
    o.set("welcome_seen", json::Value::boolean(welcome_seen));
    o.set("app_animations", json::Value::boolean(app_animations));
    o.set("strip_enabled", json::Value::boolean(strip_enabled));
    o.set("cleanup_hint_after_game", json::Value::boolean(cleanup_hint_after_game));
    if (!game_server_ipv4.empty() && net::normalize_server_ipv4(game_server_ipv4)) o.set("game_server_ipv4", json::Value::string(game_server_ipv4));
    o.set("auto_active", json::Value::boolean(auto_active));
    o.set("live_test_view", json::Value::boolean(live_test_view));
    // UI-14: tema tercihi artik yazilmaz; eski dosyadaki "theme" alani bilinmeyen alan olarak yok sayilir.
    auto items = json::Value::object();
    for (const auto& [k, v] : plan_item_selected) items.set(k, json::Value::boolean(v));
    o.set("plan_item_selected", std::move(items));
    // WIN-10: yalniz gecerli bicimli hizmet adlari yazilir (yetki/izin listesi dogrulamasi uygulama aninda).
    auto svcs = json::Value::array();
    for (const auto& s : session_stop_services)
        if (!s.empty() && s.size() <= 32) svcs.push(json::Value::string(s));
    o.set("session_stop_services", std::move(svcs));
    auto exes = json::Value::array();
    for (const auto& e : observed_game_exes)
        if (e.size() <= 512) exes.push(json::Value::string(e));
    o.set("observed_game_exes", std::move(exes));
    auto manual = json::Value::array();
    for (const auto& e : manual_game_exes)
        if (e.size() <= games::kMaxManualPathBytes) manual.push(json::Value::string(e));
    o.set("manual_game_exes", std::move(manual));
    o.set("incident_days", json::Value::integer(diagnostics::normalize_incident_days(incident_days)));
    if (ui_language != "system" && i18n::valid_ui_language(ui_language)) o.set("ui_language", json::Value::string(ui_language));
    if (!proof_no_effect_groups.empty()) {
        auto ne = json::Value::array();
        std::size_t n = 0;
        for (const auto& k : proof_no_effect_groups)
            if (!k.empty() && k.size() <= 128 && n < 64) ne.push(json::Value::string(k)), ++n;
        o.set("proof_no_effect_groups", std::move(ne));
    }
    if (last_os_build > 0) o.set("last_os_build", json::Value::integer(static_cast<std::int64_t>(last_os_build)));
    if (release_manifest_seen > 0 && release_manifest_seen <= 0x7FFFFFFFFFFFFFFFull)
        o.set("release_manifest_seen", json::Value::integer(static_cast<std::int64_t>(release_manifest_seen)));
    if (autostart_initialized) {
        o.set("autostart_initialized", json::Value::boolean(true));
        o.set("start_with_windows", json::Value::boolean(start_with_windows));
        o.set("autostart_notice_pending", json::Value::boolean(autostart_notice_pending));
    }
    return json::write(o);
}

R Preferences::parse(std::string_view text) {
    auto v = json::parse(text);
    if (!v.ok()) return v.error();
    const auto& o = v.value();
    const auto* ver = o.find("schema_version");
    if (!ver || ver->type() != json::Value::Type::Int) return R::fail(ErrorCode::ParseError, "schema_version");
    if (ver->as_int() != 1) return R::fail(ErrorCode::SchemaTooNew, "preferences schema");
    Preferences p;
    if (const auto* w = o.find("welcome_seen"); w && w->type() == json::Value::Type::Bool) p.welcome_seen = w->as_bool();
    if (const auto* a = o.find("app_animations"); a && a->type() == json::Value::Type::Bool) p.app_animations = a->as_bool();
    if (const auto* a = o.find("strip_enabled"); a && a->type() == json::Value::Type::Bool) p.strip_enabled = a->as_bool();
    if (const auto* a = o.find("cleanup_hint_after_game"); a && a->type() == json::Value::Type::Bool) p.cleanup_hint_after_game = a->as_bool();
    if (const auto* a = o.find("game_server_ipv4"); a && a->type() == json::Value::Type::String)
        if (const auto ip = net::normalize_server_ipv4(a->as_string())) p.game_server_ipv4 = *ip;
    if (const auto* a = o.find("auto_active"); a && a->type() == json::Value::Type::Bool) p.auto_active = a->as_bool();
    if (const auto* a = o.find("live_test_view"); a && a->type() == json::Value::Type::Bool) p.live_test_view = a->as_bool();
    // B10: hedef FPS ozelligi kaldirildi. Eski dosyadaki "target_fps" alani (her turde) bilinmeyen alan olarak yok
    // sayilir; dosya yine okunur ve bir sonraki kayitta alan yazilmaz.
    if (const auto* items = o.find("plan_item_selected"); items && items->type() == json::Value::Type::Object)
        for (const auto& k : items->keys())
            if (const auto* b = items->find(k); b && b->type() == json::Value::Type::Bool && k.size() <= 128 && p.plan_item_selected.size() < 256)
                p.plan_item_selected[k] = b->as_bool();
    // WIN-10: bozuk/uzun/tekrarli adlar yok sayilir; izin listesi denetimi plan olusturulurken yapilir.
    if (const auto* exes = o.find("observed_game_exes"); exes && exes->type() == json::Value::Type::Array)
        for (const auto& item : exes->items())
            if (item.type() == json::Value::Type::String && item.as_string().size() <= 512 && p.observed_game_exes.size() < 64)
                p.observed_game_exes.push_back(item.as_string());
    // EF-X01: yanlis tur / 7-30 disi deger yok sayilir (varsayilan 7); eski dosyada alan yoksa da 7.
    if (const auto* d = o.find("incident_days"); d && d->type() == json::Value::Type::Int &&
                                                 (d->as_int() == diagnostics::kDefaultDays || d->as_int() == diagnostics::kLongDays))
        p.incident_days = static_cast<int>(d->as_int());
    // Denetim 5.2: gecersiz dil degeri yok sayilir ("system").
    if (const auto* l = o.find("ui_language"); l && l->type() == json::Value::Type::String && i18n::valid_ui_language(l->as_string()))
        p.ui_language = l->as_string();
    // EF-X11: bozuk/tekrarli/gecersiz yol yok sayilir (Win32 baglami eklenirken uygulandi).
    if (const auto* m = o.find("manual_game_exes"); m && m->type() == json::Value::Type::Array)
        for (const auto& item : m->items())
            if (item.type() == json::Value::Type::String &&
                games::check_manual_exe(item.as_string(), {}, p.manual_game_exes) == games::ManualExeCheck::Ok)
                p.manual_game_exes.push_back(item.as_string());
    if (const auto* svcs = o.find("session_stop_services"); svcs && svcs->type() == json::Value::Type::Array)
        for (const auto& item : svcs->items()) {
            if (item.type() != json::Value::Type::String || p.session_stop_services.size() >= 8) continue;
            const auto& n = item.as_string();
            if (n.empty() || n.size() > 32) continue;
            if (std::find(p.session_stop_services.begin(), p.session_stop_services.end(), n) == p.session_stop_services.end())
                p.session_stop_services.push_back(n);
        }
    // P4: bozuk / uzun / tekrarli anahtar yok sayilir; sinir 64.
    if (const auto* ne = o.find("proof_no_effect_groups"); ne && ne->type() == json::Value::Type::Array)
        for (const auto& item : ne->items()) {
            if (item.type() != json::Value::Type::String || p.proof_no_effect_groups.size() >= 64) continue;
            const auto& k = item.as_string();
            if (k.empty() || k.size() > 128) continue;
            if (std::find(p.proof_no_effect_groups.begin(), p.proof_no_effect_groups.end(), k) == p.proof_no_effect_groups.end())
                p.proof_no_effect_groups.push_back(k);
        }
    if (const auto* b = o.find("last_os_build"); b && b->type() == json::Value::Type::Int && b->as_int() > 0 && b->as_int() <= 0x7FFFFFFF)
        p.last_os_build = static_cast<std::uint32_t>(b->as_int());
    if (const auto* r = o.find("release_manifest_seen"); r && r->type() == json::Value::Type::Int && r->as_int() > 0)
        p.release_manifest_seen = static_cast<std::uint64_t>(r->as_int());
    // ADR-030: alanlar yalniz baslatildiysa anlamlidir; yanlis tur yok sayilir (ilk calistirma kurali uygulanir).
    if (const auto* a = o.find("autostart_initialized"); a && a->type() == json::Value::Type::Bool && a->as_bool()) {
        p.autostart_initialized = true;
        if (const auto* s = o.find("start_with_windows"); s && s->type() == json::Value::Type::Bool) p.start_with_windows = s->as_bool();
        if (const auto* n = o.find("autostart_notice_pending"); n && n->type() == json::Value::Type::Bool)
            p.autostart_notice_pending = n->as_bool();
    }
    return p;
}

Preferences merge_setup_fields(Preferences on_disk, const Preferences& setup, bool with_autostart) {
    if (with_autostart) {
        on_disk.autostart_initialized = setup.autostart_initialized;
        on_disk.start_with_windows = setup.start_with_windows;
        on_disk.autostart_notice_pending = setup.autostart_notice_pending;
    }
    on_disk.release_manifest_seen = std::max(on_disk.release_manifest_seen, setup.release_manifest_seen);
    return on_disk;
}

Preferences merge_tray_fields(Preferences tray, const Preferences& on_disk, const Preferences& baseline) {
    // Baslatma secimi (baslatildi + acik/kapali) birlikte; ilk acilis bildirimi ayri (pencere acilinca tepsi kapatir).
    if (tray.autostart_initialized == baseline.autostart_initialized && tray.start_with_windows == baseline.start_with_windows) {
        tray.autostart_initialized = on_disk.autostart_initialized;
        tray.start_with_windows = on_disk.start_with_windows;
    }
    if (tray.autostart_notice_pending == baseline.autostart_notice_pending)
        tray.autostart_notice_pending = on_disk.autostart_notice_pending;
    tray.release_manifest_seen = std::max(tray.release_manifest_seen, on_disk.release_manifest_seen);
    return tray;
}

}  // namespace ef::core::settings
