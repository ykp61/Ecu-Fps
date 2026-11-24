#pragma once
// EF-X25 T07 (Koruma Modu): Performans Acik'ken Windows guncellemesi veya baska bir program yazdigimiz degerleri
// geri cevirdiyse bunu tespit eder (saf mantik; okuma disinda hicbir sey yapmaz). Sessiz yeniden uygulama yok:
// kullanici Evet/Hayir ile onaylar (ADR-035).
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "contracts/models.h"
#include "contracts/result.h"

namespace ef::core::perf {

// Journal'da etkin (Applied/Verified) bir adim: ne yazdigimiz ve hangi kullanici grubuna ait oldugu.
struct ActiveStep {
    contracts::SettingOperation op;
    std::string group_key;  // plan grubu ("plan.power_mode"); bos = bilinen grup yok (drift sayilmaz)
    std::string written;    // son yazilan deger (bos = op.new_value)
};

struct DriftItem {
    contracts::SettingOperation op;
    std::string group_key;
    std::optional<std::string> current;  // okunan deger; nullopt = deger yok
};

using DriftReadFn = std::function<contracts::Result<std::optional<std::string>>(const contracts::SettingOperation&)>;

// Oturum/yeniden baslatmayla zaten sona eren, surece bagli veya bakim niteligindeki adimlar drift degildir (sahte uyari yok).
bool drift_ignored(const contracts::SettingOperation& op) noexcept;
// Operasyondan Performans plan grubu; bilinmiyorsa bos.
std::string drift_group_key(const contracts::SettingOperation& op);

// Adim sapmistir: okuma basarili ve deger yazdigimizdan farkli. Okunamayan ve yoksayilan adimlar atlanir.
std::vector<DriftItem> find_drift(const std::vector<ActiveStep>& steps, const DriftReadFn& read_back);

}  // namespace ef::core::perf
