#include "core/recovery/restore_report.h"

#include <algorithm>
#include <cctype>

#include "core/recovery/engine.h"

namespace ef::core::recovery {

using contracts::OperationId;
using contracts::TxState;

namespace {
bool contains_ci(std::string_view hay, std::string_view needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool eq = true;
        for (std::size_t k = 0; k < needle.size() && eq; ++k)
            eq = std::tolower(static_cast<unsigned char>(hay[i + k])) == std::tolower(static_cast<unsigned char>(needle[k]));
        if (eq) return true;
    }
    return false;
}

bool unreconciled(TxState s) noexcept { return s != TxState::Active && s != TxState::Restored && s != TxState::RecoveryRequired; }

// Degerleri ayar disi veri tasimayan islemler (GUID, 0/1, sayi, hizmet calisma durumu, surucu profili onaltilik degeri).
// Baslangic girdileri (komut satiri), uyumluluk katmani (EXE yolu) vb. icin hedef/deger destek paketine yazilmaz.
bool values_safe(OperationId op) noexcept {
    switch (op) {
        case OperationId::PowerModeSet:
        case OperationId::PowerSchemeSet:
        case OperationId::GameModeSet:
        case OperationId::VisualEffectsSet:
        case OperationId::GamingToggleSet:
        case OperationId::HagsSet:
        case OperationId::FastStartupSet:
        case OperationId::PowerSettingSet:
        case OperationId::VendorProfileValue:
        case OperationId::ServiceSessionStop: return true;
        default: return false;
    }
}

std::string shorten(std::string_view s, std::size_t n) { return clamp_utf8(s, n); }
}  // namespace

std::string_view to_string(RestoreFailReason r) noexcept {
    switch (r) {
        case RestoreFailReason::NotVerified: return "not_verified";
        case RestoreFailReason::Denied: return "denied";
        case RestoreFailReason::External: return "external";
        case RestoreFailReason::NeedsRestart: return "needs_restart";
    }
    return "not_verified";
}

RestoreFailReason classify_restore_failure(const JournalStep& s, bool pending_reboot) noexcept {
    if (s.state == StepState::Failed && s.error == kExternalChange) return RestoreFailReason::External;
    if (!s.error_code.empty())  // yapisal kod (bu surumden itibaren her basarisiz adimda yazilir)
        return s.error_code == contracts::to_string(contracts::ErrorCode::Denied) ? RestoreFailReason::Denied
               : pending_reboot                                                  ? RestoreFailReason::NeedsRestart
                                                                                 : RestoreFailReason::NotVerified;
    if (pending_reboot) return RestoreFailReason::NeedsRestart;
    // Yalniz eski (0.6.0) kayitlar: hata kodu yazilmamisti; Windows erisim hatasinin metni tek ipucu.
    if (contains_ci(s.error, "access is denied") || contains_ci(s.error, "access denied")) return RestoreFailReason::Denied;
    return RestoreFailReason::NotVerified;
}

std::string sanitize_support_text(std::string_view s, std::size_t max_bytes) {
    // Yol (C:\..., C:/..., \\sunucu\...) ve kullanici SID'i (S-1-5-21-...) bosluk/tirnak/noktali virgule kadar atilir.
    const auto stop = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' || c == ';' || c == ','; };
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const bool drive = std::isalpha(c) && i + 2 < s.size() && s[i + 1] == ':' &&
                           (s[i + 2] == '\\' || s[i + 2] == '/') && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])));
        const bool unc = c == '\\' && i + 1 < s.size() && s[i + 1] == '\\';
        const bool sid = (s.substr(i, 9) == "S-1-5-21-" || s.substr(i, 9) == "s-1-5-21-") &&
                         (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])));
        if (drive || unc || sid) {
            out += sid ? "<sid>" : "<path>";
            while (i < s.size() && !stop(s[i])) ++i;
            continue;
        }
        out += s[i++];
    }
    return clamp_utf8(out, max_bytes);
}

std::string restore_expected_text(const JournalStep& s) { return s.snapshot.existed ? s.snapshot.old_value : std::string("<absent>"); }

std::vector<FailedRestoreItem> failed_restore_items(std::string_view scope, const JournalStore::Listing& l) {
    std::vector<FailedRestoreItem> out;
    for (const auto& name : l.corrupt) {
        FailedRestoreItem i;
        i.scope = std::string(scope);
        i.target = name;  // "tx-<32 hex>.json"
        i.journal_unreadable = true;
        i.error = "journal file unreadable or failed integrity check";
        out.push_back(std::move(i));
    }
    for (const auto& tx : l.transactions) {
        const bool interrupted = unreconciled(tx.state);
        if (tx.state != TxState::RecoveryRequired && !interrupted) continue;
        for (const auto& s : tx.steps) {
            if (s.state == StepState::Planned || s.state == StepState::RolledBack || s.state == StepState::KeptExternal) continue;
            FailedRestoreItem i;
            i.scope = std::string(scope);
            i.tx_id = tx.id;
            i.op = s.op.id;
            i.target = s.op.target;
            i.expected = restore_expected_text(s);
            i.observed = s.observed;
            i.written = s.last_written;
            i.error = s.error;
            i.error_code = s.error_code;
            i.interrupted = interrupted;
            i.reason = classify_restore_failure(s, false);
            out.push_back(std::move(i));
        }
    }
    return out;
}

std::string restore_item_name_key(const FailedRestoreItem& i) {
    if (i.journal_unreadable) return "restore.item.journal_file";
    const std::string_view t = i.target;
    switch (i.op) {
        case OperationId::GamingToggleSet:
            if (t == "gaming:game_mode") return "history.op.game_mode_set";
            if (t == "gaming:dvr_background") return "fix.name.game_dvr";
            if (t == "gaming:app_capture") return "restore.item.app_capture";
            if (t == "gaming:dvr_enabled") return "restore.item.dvr_enabled";
            if (t == "gaming:gamebar_startup") return "restore.item.gamebar_startup";
            if (t == "gaming:windowed_opt") return "fix.name.windowed_opt";
            if (t == "gaming:windows_vrr") return "fix.name.windows_vrr";
            break;
        case OperationId::PowerSettingSet:
            if (t == "power:boost:ac") return "restore.item.power_boost";
            if (t == "power:parking:ac") return "restore.item.power_parking";
            if (t == "power:aspm:ac") return "restore.item.power_aspm";
            if (t == "power:usbss:ac") return "restore.item.power_usb";
            break;
        case OperationId::VendorProfileValue: {
            // drs:<global|app:exe>:<onaltilik ayar kimligi> (core/adapters/nvidia/drs.h DrsSetting ile ayni)
            const auto c = t.rfind(':');
            const std::string_view id = c == std::string_view::npos ? std::string_view{} : t.substr(c + 1);
            if (id == "1057EB71") return "restore.item.nv_power";
            if (id == "007BA09E") return "restore.item.nv_queue";
            if (id == "00AC8497") return "restore.item.nv_shader";
            break;
        }
        default: break;
    }
    return "history.op." + std::string(contracts::to_string(i.op));
}

std::string_view restore_reason_key(RestoreFailReason r) noexcept {
    switch (r) {
        case RestoreFailReason::NotVerified: return "restore.reason.not_verified";
        case RestoreFailReason::Denied: return "restore.reason.denied";
        case RestoreFailReason::External: return "restore.reason.external";
        case RestoreFailReason::NeedsRestart: return "restore.reason.needs_restart";
    }
    return "restore.reason.not_verified";
}

FailedRestoreItem redact_for_support(FailedRestoreItem i) {
    if (!i.journal_unreadable && !values_safe(i.op)) {
        i.target.clear();
        i.expected.clear();
        i.observed.clear();
        i.written.clear();
    }
    i.target = sanitize_support_text(i.target, 96);
    i.expected = sanitize_support_text(i.expected, 64);
    i.observed = sanitize_support_text(i.observed, 64);
    i.written = sanitize_support_text(i.written, 64);
    i.error = sanitize_support_text(i.error, 160);
    i.error_code = shorten(i.error_code, 32);
    return i;
}

json::Value to_json(const std::vector<FailedRestoreItem>& items) {
    using json::Value;
    auto arr = Value::array();
    for (const auto& raw : items) {
        const auto i = redact_for_support(raw);
        auto o = Value::object();
        const auto str = [](const std::string& s) { return s.empty() ? Value::null() : Value::string(s); };
        o.set("scope", Value::string(i.scope));
        o.set("tx", str(i.tx_id.substr(0, 8)));  // kisa kimlik: restore.log satirlariyla eslestirmek icin yeterli
        o.set("operation", i.journal_unreadable ? Value::null() : Value::string(std::string(contracts::to_string(i.op))));
        o.set("setting", str(i.target));
        o.set("expected", str(i.expected));
        o.set("read", str(i.observed));
        o.set("written", str(i.written));
        o.set("reason", Value::string(std::string(to_string(i.reason))));
        o.set("error", str(i.error));
        o.set("error_code", str(i.error_code));
        o.set("interrupted", Value::boolean(i.interrupted));
        o.set("journal_unreadable", Value::boolean(i.journal_unreadable));
        arr.push(std::move(o));
    }
    return arr;
}

std::string restore_log_line(const FailedRestoreItem& raw) {
    const auto i = redact_for_support(raw);
    const auto v = [](const std::string& s) { return s.empty() ? std::string("-") : s; };
    std::string line = "failed scope=" + i.scope + " tx=" + v(i.tx_id.substr(0, 8));
    if (i.journal_unreadable) return line + " journal_unreadable=" + v(i.target);
    line += " op=" + std::string(contracts::to_string(i.op)) + " setting=" + v(i.target) + " expected=" + v(i.expected) +
            " read=" + v(i.observed) + " written=" + v(i.written) + " reason=" + std::string(to_string(i.reason)) +
            " code=" + v(i.error_code) + (i.interrupted ? " interrupted=1" : "") + " error=\"" + i.error + "\"";
    return line;
}

}  // namespace ef::core::recovery
